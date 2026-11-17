# AI helper (doc/ai_coding): asks the parent's AI helper gem (fmrb-tutor)
# over Asterism, and puts what comes back into the editor's document. The
# device never talks to an LLM provider and holds no key: its only partner
# is <node>/tutor/helper, found on the network.
#
# Calls to the gem (report a1.md 7.1): hello, then pending (answers that
# came while the app was away). A request is ask -> status every second ->
# result -> result_piece (the document, in pieces) -> ack; a document over
# one piece goes first with doc_begin / doc_piece. Every call is async (a
# Future checked in on_update), one at a time, so the window never waits on
# the network.
#
# The editor (doc/editor_multi_file report m3.md): ai_read makes it write
# its document to /tmp/ai_doc.rb and answer ai_doc (version, cursor, file);
# ai_replace loads /tmp/ai_new.rb in its place when the version is still the
# one read, and answers ai_done. This app writes only /tmp/ai_*.rb and the
# backups <file>.bakN beside the document. The document is streamed through
# those files, never held whole in the VM.
#
# Uses: question (the editor is not touched, unless "send the document" is
# on), hint (AI comments "# AI: " above lines, the code unchanged), make (the
# whole document, after asking whether to back it up), fix (after a run that
# failed: a hint or the fixed document, the child chooses each time), and
# erase (the AI comment lines, after showing how many).
class AiHelperApp < FmrbApp
  APP_NAME = "ai_helper"
  HELPER = "*/tutor/helper"
  EDITOR = "FM-Editor"
  DOC = "/tmp/ai_doc.rb"
  NEW = "/tmp/ai_new.rb"
  MARK = "# AI: "
  BAK_KEEP = 5
  BAK_SCAN = 9
  LINE_H = 13
  MAX_LINES = 400
  INPUT_MAX = 200        # characters
  POLL_MS = 1000
  SCAN_MS = 2000
  HELLO_RETRY_MS = 10000
  EDITOR_WAIT_MS = 3000
  CALL_TIMEOUT = 6.0
  LINK_RETRY_MS = 8000
  PIECE_MAX = 8192       # what this app sends in one call at most
  REBASE_MAX = 3
  CHUNK = 4096
  BTN_H = 15

  MODE_LABEL = { "ask" => "質問", "hint" => "ヒント", "make" => "作る" }
  PLACEHOLDER = {
    "ask" => "質問を打って Enter",
    "hint" => "どこが分からないか (空でもよい)",
    "make" => "作りたいものを書いて Enter",
  }
  TOP_BUTTONS = [[:m_ask, "質問", 36], [:m_hint, "ヒント", 44], [:m_make, "作る", 36],
                 [:fix, "直して", 46], [:erase, "AIの行を消す", 80]]
  STATE_TEXT = {
    "queued" => "順番を待っています",
    "thinking" => "考えています",
    "checking" => "検査しています",
  }
  EDITOR_WHY = {
    "busy" => "エディタでダイアログやメニューが開いています。閉じてから、もう一度押してください。",
    "help" => "エディタでヘルプ (F1) が開いています。閉じてから、もう一度押してください。",
    "io" => "エディタがファイルを読み書きできませんでした。",
    "nomem" => "エディタのメモリが足りません。",
  }

  # ------------------------------------------------------------ lifecycle

  def on_create
    @gfx.set_font(:ja, 12)
    @hits = FmrbUI::HitAreas.new
    @lines = []
    @colors = []
    @top = 0
    @input = ""
    @u8_buf = ""
    @u8_need = 0
    @status = ""
    @ai = nil
    @ai_path = nil
    @hello = nil
    @hello_at = 0
    @modes = []
    @piece = PIECE_MAX
    @call = nil
    @call_kind = nil
    @ticket = nil
    @phase = :idle
    @cancelled = false
    @want_cancel = false
    @pending = []
    @need_pending = false
    @next_poll = 0
    @next_scan = 0
    @asked_at = 0
    reset_poll_stats
    @link_state = nil
    @link_failed = false
    @mode = "hint"
    @attach = false
    # the editor
    @msg_id = 0
    @ed_wait = nil
    @ed_deadline = 0
    @after_read = nil
    @doc = nil            # the last ai_doc (a Hash)
    @req_mode = nil
    @req_text = ""
    @req_error = ""
    @req_shown = ""
    # sending the document
    @up_then = nil
    @up_doc = nil
    @up_text = nil
    @up_f = nil
    @up_left = 0
    @up_carry = ""
    @up_i = 0
    # the result
    @res = nil
    @res_i = 0
    @res_f = nil
    @rebases = 0
    @apply = nil
    @new_bak = nil
    @erase_n = 0
    # a question to the child
    @question = nil
    @choices = nil
    layout
    add_text("AI", "AI ヘルパーです。答えるのは AI (人ではありません) です。上で使い方を選び、下の行に打って Enter か「送る」を押してください。「ヒント」と「作る」は、エディタで開いている文書を使います。")
    set_status("つないでいます…")
    # connect blocks the app for a few seconds when the router is not
    # there: try less often than the Link's default
    @link = FmrbAsterism::Link.new(self, APP_NAME, tag: "ai_helper", retry_ms: LINK_RETRY_MS)
    draw_all
  end

  def on_destroy
    close_files
    @link.close if @link
  end

  def on_resize
    layout
    draw_all
  end

  # ------------------------------------------------------------ the link

  def link_up(link)
    @ai = nil
    @hello = nil
    @next_scan = 0
    set_status("AI の係を探しています…")
  end

  def link_down(link)
    forget_ai
  end

  def forget_ai
    @ai = nil
    @ai_path = nil
    @hello = nil
    @call = nil
    @call_kind = nil
  end

  # What the Link's state means for the child (shown when it changes).
  def link_text(st)
    case st
    when :waiting_wifi then "Wi-Fi につながるのを待っています…"
    when :connecting
      @link_failed ? "ネットワーク (ルータ) につながりません。もう一度つないでいます…" : "つないでいます…"
    when :waiting_screen then "つないでいます…"
    when :still_there then "前のつながりが残っています。少し待ちます…"
    when :retrying, :lost, :failed
      @link_failed = true
      "ネットワーク (ルータ) につながりません。少し待ってからもう一度つなぎます。"
    else "つながっていません。"
    end
  end

  def on_update
    editor_timeout
    up = @link.update
    unless up
      st = @link.state
      if st != @link_state
        @link_state = st
        forget_ai
        network_lost if net_phase?
        set_status(link_text(st))
        Log.info("ai_helper: link #{st} #{@link.reason}")
      end
      return 100
    end
    @link_state = :connected
    @link_failed = false
    collect_call
    next_step if @call.nil?
    @call ? 50 : 100
  end

  # The phases that wait for the gem (not for the editor or the child).
  def net_phase?
    p = @phase
    p == :asking || p == :poll || p == :result || p == :pieces || p == :ack ||
      p == :doc_begin || p == :doc_piece || p == :rebasing
  end

  # The next call, when none is on its way.
  def next_step
    now = Machine.board_millis
    if @ai.nil?
      scan(now)
    elsif @hello.nil?
      return if now < @hello_at
      start_call(:hello, { "fw" => FmrbConst::OS_VERSION, "lang" => "ja", "host" => FmrbAsterism.host })
    elsif @want_cancel && @ticket
      @want_cancel = false
      start_call(:cancel, @ticket)
    elsif @need_pending
      @need_pending = false
      start_call(:pending)
    elsif @phase == :poll
      return if now < @next_poll
      @next_poll = now + POLL_MS
      start_call(:status, @ticket)
    elsif @phase == :result
      start_call(:result, @ticket)
    elsif @phase == :pieces
      start_call(:result_piece, @ticket, @res_i)
    elsif @phase == :doc_begin
      start_call(:doc_begin, @up_left)
    elsif @phase == :doc_piece
      send_doc_piece
    elsif @phase == :ack
      start_call(:ack, @ticket)
    elsif @phase == :idle && !@pending.empty?
      take_pending
    elsif @phase == :idle && now >= @next_scan
      @next_scan = now + SCAN_MS
      still_there?
    end
  end

  # Looks for the helper (the liveliness the connection follows: nothing is
  # sent).
  def scan(now)
    return if now < @next_scan
    @next_scan = now + SCAN_MS
    found = @link.conn.each(HELPER)
    if found.empty?
      set_status("AI の係が見つかりません。親の PC で AI の係が動いているか確かめてください。")
      return
    end
    @ai_path = found[0].asterism_path
    @ai = @link.conn[@ai_path, timeout: CALL_TIMEOUT]
    @hello = nil
    @hello_at = 0
    Log.info("ai_helper: found #{@ai_path}")
  rescue ::Asterism::Error => e
    Log.info("ai_helper: scan: #{e.message}")
  end

  # The helper is still on the network (between requests).
  def still_there?
    found = @link.conn.each(HELPER)
    i = 0
    while i < found.size
      return true if found[i].asterism_path == @ai_path
      i += 1
    end
    Log.info("ai_helper: #{@ai_path} left")
    forget_ai
    set_status("AI の係が見つかりません。親の PC で AI の係が動いているか確かめてください。")
    false
  rescue ::Asterism::Error
    false
  end

  # ------------------------------------------------------------ calls

  def start_call(kind, *args)
    @call = @ai.async.asterism_call(kind.to_s, *args)
    @call_kind = kind
  rescue ::Asterism::Error => e
    @call = nil
    call_failed(kind, e)
  end

  def collect_call
    f = @call
    return unless f && f.done?
    kind = @call_kind
    @call = nil
    @call_kind = nil
    v = nil
    begin
      v = f.value
    rescue ::Asterism::Error => e
      call_failed(kind, e)
      return
    end
    took = f.took_ms.to_i
    if kind == :status
      @poll_n += 1
      @poll_sum += took
      @poll_max = took if took > @poll_max
    elsif kind == :doc_piece || kind == :result_piece
      Log.info("ai_helper: #{kind} took #{took} ms")
    end
    take(kind, v)
  end

  # A call that got no answer: the helper is gone or the network is. What
  # was asked is not lost: the helper keeps it, and pending (after the next
  # hello) brings it back.
  def call_failed(kind, e)
    Log.info("ai_helper: #{kind}: #{e.class}: #{e.message}")
    had = net_phase?
    forget_ai
    network_lost if had
    @next_scan = Machine.board_millis + SCAN_MS
    if had
      set_status("AI の係から返事がありません。つながったら答えを受け取りに行きます。")
    else
      set_status("AI の係から返事がありません。親の PC で AI の係が動いているか確かめてください。")
    end
  end

  # The network went away in the middle of a request: what was being sent
  # is dropped (sent again by the child), what was asked waits in the gem.
  def network_lost
    close_files
    if @ticket
      @phase = :idle      # pending after the next hello resumes it
    else
      @phase = :idle
      add_text("AI", "送っている途中で切れました。つながったら、もう一度送ってください。") if @up_then
      @up_then = nil
    end
  end

  def take(kind, v)
    case kind
    when :hello then took_hello(v)
    when :pending then took_pending(v)
    when :doc_begin then took_doc_begin(v)
    when :doc_piece then took_doc_piece(v)
    when :ask then took_ask(v)
    when :rebase then took_rebase(v)
    when :status then took_status(v)
    when :result then took_result(v)
    when :result_piece then took_result_piece(v)
    when :ack then took_ack(v)
    when :new_talk then took_new_talk(v)
    when :cancel then took_cancel(v)
    end
  end

  def error_of(v)
    v.is_a?(Hash) ? v["error"] : nil
  end

  def message_of(v)
    m = v.is_a?(Hash) ? v["message"].to_s : ""
    m.empty? ? "AI の係が断りました (#{error_of(v)})。" : m
  end

  def took_hello(v)
    if error_of(v) || !v.is_a?(Hash)
      @hello = nil
      @hello_at = Machine.board_millis + HELLO_RETRY_MS
      m = message_of(v)
      m = "#{m} (この端末の名前: #{@link.node})" if error_of(v) == "not_allowed"
      set_status(m)
      Log.info("ai_helper: hello refused: #{error_of(v)}")
      return
    end
    @hello = v
    @modes = v["modes"].is_a?(Array) ? v["modes"] : []
    p = v["piece"].to_i
    @piece = p > 0 && p < PIECE_MAX ? p : PIECE_MAX
    name = v["name"].to_s
    used = v["used"].to_i
    max = v["max"].to_i
    Log.info("ai_helper: hello #{@ai_path} modes=#{@modes.join(',')} piece=#{v['piece']} used=#{used}/#{max}")
    @need_pending = true
    @mode = "hint" if @mode == "make" && !mode_on?("make")
    draw_buttons
    set_status("つながりました#{name.empty? ? '' : " (#{name})"}。今日 #{used} / #{max} 回。") if @phase == :idle
  end

  def mode_on?(m)
    @modes.index(m) ? true : false
  end

  # [[ticket, state], ...]: answers to this machine not taken yet.
  def took_pending(v)
    @pending = []
    return unless v.is_a?(Array)
    i = 0
    while i < v.size
      t = v[i]
      @pending << t[0].to_s if t.is_a?(Array) && t.size > 0
      i += 1
    end
    Log.info("ai_helper: pending #{@pending.size}")
    # a request this run was already waiting for goes on as it was
    if @ticket && @pending.index(@ticket)
      @pending.delete(@ticket)
      if @phase == :idle
        @phase = :poll
        @next_poll = 0
      end
    elsif @ticket && @phase == :idle
      add_text("AI", "前の頼みの答えは、AI の係が持っていませんでした。もう一度送ってください。")
      @ticket = nil
    end
  end

  def take_pending
    reset_poll_stats
    @ticket = @pending.shift
    @cancelled = false
    @rebases = 0
    @asked_at = Machine.board_millis
    @phase = :poll
    @next_poll = 0
    add_text("AI", "前に頼んだ答えを受け取ります。")
  end

  def reset_poll_stats
    @poll_n = 0
    @poll_sum = 0
    @poll_max = 0
  end

  # ------------------------------------------------------------ the editor

  def editor_pid
    procs = FmrbApp.ps
    return -1 unless procs
    i = procs.size - 1
    while i >= 0
      pr = procs[i]
      return pr[:id] if pr[:name] == EDITOR
      i -= 1
    end
    -1
  end

  # Sends cmd to the editor (false when no editor runs; the reason is shown).
  def ed_send(cmd, extra = nil)
    pid = editor_pid
    if pid < 0
      set_status("エディタが動いていません。エディタで文書を開いてから、もう一度押してください。")
      return false
    end
    @msg_id += 1
    h = { "cmd" => cmd, "re" => FmrbApp.pid, "id" => @msg_id }
    if extra
      ks = extra.keys
      i = 0
      while i < ks.size
        h[ks[i]] = extra[ks[i]]
        i += 1
      end
    end
    send_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL, h)
    @ed_wait = cmd
    @ed_deadline = Machine.board_millis + EDITOR_WAIT_MS
    true
  end

  # Asks the editor for its document; then after_read(what) runs.
  def read_editor(what)
    return false unless ed_send("ai_read")
    @after_read = what
    @phase = :ed_read
    set_status("エディタの文書を読んでいます…")
    true
  end

  def editor_timeout
    return unless @ed_wait
    return if Machine.board_millis < @ed_deadline
    Log.info("ai_helper: no #{@ed_wait} answer from the editor")
    @ed_wait = nil
    editor_gave_up("エディタから返事がありません。エディタが動いているか確かめてください。")
  end

  def on_control(msg)
    cmd = msg["cmd"]
    return unless cmd == "ai_doc" || cmd == "ai_done"
    return unless msg["id"] == @msg_id && @ed_wait
    @ed_wait = nil
    if cmd == "ai_doc"
      took_doc(msg)
    else
      took_done(msg)
    end
  end

  # The editor would not do it (busy, help, io, nomem, no answer): what was
  # being done stops; a result waiting to go in can be put in again.
  def editor_gave_up(text)
    if @apply
      ask_child(text, [[:retry_apply, "もう一度"], [:drop, "やめる"]])
    else
      @phase = :idle
      @after_read = nil
      set_status(text)
    end
  end

  def why_text(why)
    EDITOR_WHY[why.to_s] || "エディタが断りました (#{why})。"
  end

  def took_doc(msg)
    if msg["ok"] != 1
      Log.info("ai_helper: ai_doc refused #{msg['why']}")
      editor_gave_up(why_text(msg["why"]))
      return
    end
    @doc = msg
    Log.info("ai_helper: ai_doc ver=#{msg['ver']} n=#{msg['n']} b=#{msg['b']} y=#{msg['y']} m=#{msg['m']} f=#{msg['f']} rp=#{msg['rp']}")
    what = @after_read
    @after_read = nil
    case what
    when :ask then send_request("ask")
    when :hint then send_request("hint")
    when :make then send_request("make")
    when :fix then offer_fix
    when :erase then count_ai_lines
    when :apply then apply_result
    end
  end

  def doc_bytes
    @doc ? @doc["b"].to_i : 0
  end

  def doc_file
    @doc ? @doc["f"].to_s : ""
  end

  # The document's name only (the place is not sent to the gem).
  def base_name(path)
    i = path.bytesize - 1
    i -= 1 while i >= 0 && path.getbyte(i) != 47
    path.byteslice(i + 1, path.bytesize - i - 1).to_s
  end

  # ------------------------------------------------------------ asking

  def send_input
    text = @input.strip
    return set_status("前の頼みがまだ終わっていません。") if @phase != :idle
    return set_status("AI の係につながっていません。") unless @hello
    if @mode == "ask"
      return set_status("質問を打ってください。") if text.empty?
    elsif @mode == "make"
      return set_status("「作る」は、親の設定で止められています。") unless mode_on?("make")
      return set_status("作りたいものを打ってください。") if text.empty?
    end
    start_request(@mode, text, "")
  end

  # mode: ask / hint / make / fix (the gem's names); fix_hint is a hint
  # with the error.
  def start_request(mode, text, error)
    @req_mode = mode
    @req_text = text
    @req_error = error
    @rebases = 0
    label = mode == "fix" || !error.empty? ? "直して" : MODE_LABEL[mode]
    @req_shown = text.empty? ? "(#{label})" : "(#{label}) #{text}"
    if mode == "ask" && (!@attach || editor_pid < 0)
      add_text("AI", "(エディタが動いていないので、文書は送りません)") if @attach
      @doc = nil
      send_request("ask")
    elsif mode == "fix" || !error.empty?
      send_request(mode)   # the document was read already (offer_fix)
    else
      read_editor(mode == "ask" ? :ask : mode.to_sym) || (@phase = :idle)
    end
  end

  # The request (the document, if one was read for it, goes first).
  def send_request(mode)
    if mode != "ask" || @attach
      if mode == "ask" && @doc.nil?
        # no editor: the question goes without it
      elsif doc_bytes == 0 && mode != "make" && mode != "ask"
        @phase = :idle
        return set_status("エディタの文書が空です。")
      elsif doc_bytes > 0
        return begin_upload(:ask)
      end
    end
    ask_now(nil, nil)
  end

  def ask_now(doc_id, doc_text)
    req = { "mode" => @req_mode, "text" => @req_text, "fw" => FmrbConst::OS_VERSION, "lang" => "ja" }
    if doc_id || doc_text
      req["doc"] = doc_id if doc_id
      req["doc_text"] = doc_text if doc_text
      req["file"] = base_name(doc_file)
      req["ver"] = @doc["ver"].to_i
      req["cy"] = @doc["y"].to_i
      req["cx"] = @doc["x"].to_i
    end
    req["error"] = @req_error unless @req_error.empty?
    # shown (and the input line emptied) once it really goes
    add_text("あなた", @req_shown)
    @input = ""
    draw_input
    @phase = :asking
    reset_poll_stats
    @cancelled = false
    @asked_at = Machine.board_millis
    start_call(:ask, req)
    set_status("送っています…")
  end

  def took_ask(v)
    @up_then = nil
    if error_of(v)
      @phase = :idle
      add_text("AI", message_of(v))
      set_status(message_of(v))
      return
    end
    @ticket = v["ticket"].to_s
    @phase = :poll
    @next_poll = Machine.board_millis + 300
    set_status("受け付けました。")
    Log.info("ai_helper: ticket #{@ticket}")
  end

  # ------------------------------------------------------------ sending the document

  # The document in DOC goes to the gem: in the request itself when it fits
  # one piece, else doc_begin and doc_piece first. then_: :ask or :rebase.
  def begin_upload(then_)
    @up_then = then_
    size = File.exist?(DOC) ? File.size(DOC) : 0
    if size <= @piece
      text = File.open(DOC, "r") { |f| f.read }.to_s
      return after_upload(nil, text)
    end
    @up_left = size
    @up_doc = nil
    @up_i = 0
    @up_carry = ""
    @phase = :doc_begin
    set_status("文書を送っています…")
  end

  def took_doc_begin(v)
    if error_of(v)
      @phase = :idle
      @up_then = nil
      add_text("AI", message_of(v))
      return set_status(message_of(v))
    end
    @up_doc = v["doc"].to_s
    @up_f = File.open(DOC, "r")
    @phase = :doc_piece
  end

  # The next piece of the document: cut after a newline (or between two
  # characters), so a piece is whole UTF-8.
  def next_up_piece
    want = @piece - @up_carry.bytesize
    chunk = ""
    if want > 0 && @up_left > 0
      chunk = @up_f.read(want < @up_left ? want : @up_left).to_s
      @up_left -= chunk.bytesize
      @up_left = 0 if chunk.bytesize == 0
    end
    buf = @up_carry + chunk
    @up_carry = ""
    return buf if @up_left == 0 || buf.bytesize == 0
    # after the last newline; with none, before the last character that
    # starts in the buffer
    j = buf.bytesize - 1
    j -= 1 while j > 0 && buf.getbyte(j) != 10
    if j > 0
      cut = j + 1
    else
      cut = buf.bytesize - 1
      cut -= 1 while cut > 0 && (buf.getbyte(cut) & 0xC0) == 0x80
      cut = buf.bytesize if cut <= 0
    end
    @up_carry = buf.byteslice(cut, buf.bytesize - cut).to_s
    buf.byteslice(0, cut).to_s
  end

  def send_doc_piece
    text = next_up_piece
    if text.bytesize == 0
      close_files
      after_upload(@up_doc, nil)
      return
    end
    set_status("文書を送っています… (#{@up_i + 1})")
    start_call(:doc_piece, @up_doc, @up_i, text)
  end

  def took_doc_piece(v)
    if error_of(v)
      close_files
      @phase = :idle
      @up_then = nil
      add_text("AI", message_of(v))
      return set_status(message_of(v))
    end
    Log.info("ai_helper: piece #{@up_i} sent, the gem has #{v['have']} bytes")
    @up_i += 1
  end

  def after_upload(doc_id, text)
    th = @up_then
    if th == :rebase
      d = { "ver" => @doc["ver"].to_i }
      d["doc"] = doc_id if doc_id
      d["doc_text"] = text if text
      @phase = :rebasing
      start_call(:rebase, @ticket, d)
      set_status("ヒントを今の文書に合わせ直しています…")
    else
      ask_now(doc_id, text)
    end
  end

  def close_files
    if @up_f
      @up_f.close
      @up_f = nil
    end
    if @res_f
      @res_f.close
      @res_f = nil
    end
  end

  # ------------------------------------------------------------ waiting and the result

  def took_status(v)
    if error_of(v)
      # the helper no longer has it (restarted)
      add_text("AI", "前の頼みの答えは、AI の係が持っていませんでした。もう一度送ってください。")
      finish_request
      return
    end
    st = v["state"].to_s
    if st == "done" || st == "failed" || st == "cancelled"
      @phase = :result
      return
    end
    secs = (Machine.board_millis - @asked_at) / 1000
    word = STATE_TEXT[st] || st
    set_status(@cancelled ? "やめています… (#{secs} 秒)" : "#{word}… (#{secs} 秒)")
  end

  def took_result(v)
    secs = (Machine.board_millis - @asked_at) / 1000
    Log.info("ai_helper: result #{@ticket} after #{secs} s, status #{@poll_n} calls avg #{@poll_n > 0 ? @poll_sum / @poll_n : 0} ms max #{@poll_max} ms pool #{FmrbApp.pool_used}")
    if @cancelled
      set_status("やめました。")
      @phase = :ack
      return
    end
    if error_of(v)
      if error_of(v) == "not_ready"
        @phase = :poll
        return
      end
      add_text("AI", message_of(v))
      set_status(error_of(v) == "cancelled" ? "やめました。" : "うまくいきませんでした。")
      @phase = :ack
      return
    end
    kind = v["kind"].to_s
    if kind == "answer"
      add_text("AI", v["explain"].to_s)
      set_status("答えが届きました (#{secs} 秒)。")
      @phase = :ack
      return
    end
    # a hint or a document: the text comes in pieces into NEW
    @res = v
    add_text("AI", v["explain"].to_s) if @rebases == 0
    @res_i = 0
    if v["pieces"].to_i <= 0
      add_text("AI", "入れるものがありませんでした。")
      @phase = :ack
      return
    end
    @res_f = File.open(NEW, "w")
    @phase = :pieces
    set_status("受け取っています…")
  end

  def took_result_piece(v)
    if error_of(v) || !v.is_a?(Hash)
      close_files
      add_text("AI", "答えを受け取れませんでした。もう一度頼んでください。")
      @phase = :ack
      return
    end
    t = v["text"].to_s
    @res_f.write(t)
    Log.info("ai_helper: result piece #{@res_i}: #{t.bytesize} bytes")
    @res_i += 1
    return if @res_i < @res["pieces"].to_i
    close_files
    @apply = @res["kind"].to_s == "hint" ? :hint : :make
    # what the editor has now decides how it goes in
    read_editor(:apply) || editor_gave_up("エディタが動いていません。エディタで文書を開いてから「もう一度」を押してください。")
  end

  def took_ack(v)
    finish_request
  end

  def finish_request
    close_files
    @ticket = nil
    @phase = :idle
    @cancelled = false
    @res = nil
    @apply = nil
    @question = nil
    @choices = nil
    @up_then = nil
    draw_buttons
  end

  # ------------------------------------------------------------ putting the result in

  # After the editor was read again (@doc is now): a hint goes in when the
  # document is the one sent, else it is placed again on the new one
  # (rebase); a whole document asks first.
  def apply_result
    base = @res["base_ver"]
    same = base.is_a?(Integer) && base == @doc["ver"].to_i
    if @apply == :hint
      if same
        put_hint
      elsif @rebases >= REBASE_MAX
        add_text("AI", "文書が書きかわり続けたので、ヒントを入れられませんでした。")
        give_up_apply
      else
        @rebases += 1
        Log.info("ai_helper: rebase #{@rebases} (sent ver #{base}, now #{@doc['ver']})")
        begin_upload(:rebase)
      end
    else
      ask_overwrite(same)
    end
  end

  def put_hint
    y = @doc["y"].to_i
    x = @doc["x"].to_i
    y += hint_lines_above(y)
    set_status("エディタに入れています…")
    @phase = :ed_replace
    ed_send("ai_replace", { "ver" => @doc["ver"].to_i, "p" => NEW, "y" => y, "x" => x }) ||
      editor_gave_up("エディタが動いていません。")
  end

  # The comment lines the gem put above row y (0-based) of the sent
  # document: the cursor moves down by as many.
  def hint_lines_above(y)
    hs = @res["hints"]
    return 0 unless hs.is_a?(Array)
    n = 0
    i = 0
    while i < hs.size
      h = hs[i]
      if h.is_a?(Array) && h[0].to_i <= y + 1
        n += comment_line_count(h[2].to_s)
      end
      i += 1
    end
    n
  end

  # How many lines the gem makes of a comment (non-empty lines, two at most).
  def comment_line_count(c)
    n = 0
    i = 0
    start = 0
    len = c.bytesize
    while i <= len
      if i == len || c.getbyte(i) == 10
        line = c.byteslice(start, i - start).to_s.strip
        n += 1 unless line.empty?
        start = i + 1
      end
      i += 1
    end
    n = 1 if n == 0
    n > 2 ? 2 : n
  end

  def took_rebase(v)
    if error_of(v)
      add_text("AI", message_of(v))
      give_up_apply
      return
    end
    # the old ticket is taken by the gem; the new one is done already
    @ticket = v["ticket"].to_s
    @phase = :poll
    @next_poll = 0
  end

  # make / fix: the document is replaced whole, after the child says how.
  def ask_overwrite(same)
    head = same ? "" : "待っている間に書いた分があります。"
    f = doc_file
    if doc_bytes == 0 && f.empty?
      replace_whole(false)
    elsif f.empty? || f.getbyte(0) != 47
      ask_child("#{head}今の文書には名前が無いので、退避できません。上書きしますか?",
                [[:over, "上書きする"], [:drop, "やめる"]])
    else
      ask_child("#{head}今の中身を残しますか? (#{base_name(f)}.bak に退避します)",
                [[:bak, "はい"], [:over, "いいえ"], [:drop, "やめる"]])
    end
  end

  def replace_whole(backup)
    @new_bak = nil
    if backup
      @new_bak = make_backup(doc_file)
      return editor_gave_up("退避のファイルを書けませんでした。") unless @new_bak
    end
    set_status("エディタに入れています…")
    @phase = :ed_replace
    ed_send("ai_replace", { "ver" => @doc["ver"].to_i, "p" => NEW, "y" => 0, "x" => 0 }) ||
      editor_gave_up("エディタが動いていません。")
  end

  def took_done(msg)
    ok = msg["ok"] == 1
    why = msg["why"].to_s
    Log.info("ai_helper: ai_done ok=#{ok ? 1 : 0} why=#{why} ver=#{msg['ver']}")
    if !ok && @new_bak
      File.delete(@new_bak) if File.exist?(@new_bak)
      @new_bak = nil
    end
    if ok
      rotate_backups(doc_file) if @new_bak
      done_text
      return
    end
    if why == "ver"
      # written meanwhile: read again and go on from there
      if @apply == :hint || @apply == :erase
        read_editor(@apply == :erase ? :erase : :apply)
      else
        read_editor(:apply)
      end
      return
    end
    editor_gave_up(why_text(why))
  end

  def done_text
    case @apply
    when :hint
      n = @res["hints"].is_a?(Array) ? @res["hints"].size : 0
      left = @res["left_out"].to_i
      set_status(left > 0 ? "ヒントを #{n} 個入れました (#{left} 個は入れられませんでした)。" : "ヒントを #{n} 個入れました。")
    when :make
      set_status(@new_bak ? "入れかえました。前の中身は #{base_name(@new_bak)} に残しました。" : "入れかえました。")
    when :erase
      set_status("AI のコメントを #{@erase_n} 行消しました。")
      @apply = nil
      @phase = :idle
      return
    end
    @new_bak = nil
    @phase = :ack
  end

  def give_up_apply
    if @apply == :erase || @ticket.nil?
      @apply = nil
      @phase = :idle
      @question = nil
      @choices = nil
      draw_buttons
      return
    end
    @phase = :ack
  end

  # ------------------------------------------------------------ backups

  # <file>.bakN: the next number after the ones there (the copy is the file
  # on disk when the document is saved, else what the editor has).
  def make_backup(f)
    last = 0
    n = 1
    while n <= BAK_SCAN
      last = n if File.exist?("#{f}.bak#{n}")
      n += 1
    end
    return nil if last >= BAK_SCAN
    path = "#{f}.bak#{last + 1}"
    src = @doc["m"].to_i == 0 && File.exist?(f) ? f : DOC
    copy_file(src, path) ? path : nil
  end

  def copy_file(from, to)
    i = File.open(from, "r")
    o = File.open(to, "w")
    while true
      s = i.read(CHUNK).to_s
      break if s.bytesize == 0
      o.write(s)
    end
    i.close
    o.close
    Log.info("ai_helper: backup #{from} -> #{to} (#{File.size(to)} bytes)")
    true
  rescue => e
    Log.info("ai_helper: backup failed: #{e.message}")
    false
  end

  # At most BAK_KEEP: the first (the child's own code) stays, the second
  # oldest goes, and the rest move down so the numbers have no gaps.
  def rotate_backups(f)
    nums = []
    n = 1
    while n <= BAK_SCAN
      nums << n if File.exist?("#{f}.bak#{n}")
      n += 1
    end
    while nums.size > BAK_KEEP
      File.delete("#{f}.bak#{nums[1]}")
      Log.info("ai_helper: removed #{f}.bak#{nums[1]}")
      nums.delete_at(1)
    end
    i = 0
    while i < nums.size
      if nums[i] != i + 1
        File.rename("#{f}.bak#{nums[i]}", "#{f}.bak#{i + 1}")
        @new_bak = "#{f}.bak#{i + 1}" if @new_bak == "#{f}.bak#{nums[i]}"
      end
      i += 1
    end
  end

  # ------------------------------------------------------------ fix and erase

  def press_fix
    return set_status("前の頼みがまだ終わっていません。") if @phase != :idle
    return set_status("AI の係につながっていません。") unless @hello
    read_editor(:fix)
  end

  # The last error is the editor's run when its app name is the document's.
  def run_error
    e = FmrbApp._get_last_error
    return nil unless e && @doc && @doc["rp"].to_i >= 0
    name = e[:name].to_s
    f = base_name(doc_file)
    return nil if f.empty?
    stem = f
    k = stem.index(".")
    stem = stem[0, k] if k
    return nil unless name == f || name == stem || name.downcase == stem.downcase
    e[:error].to_s
  end

  def offer_fix
    @phase = :idle
    err = run_error
    unless err
      return set_status("エディタで実行 (F5) したアプリのエラーが見つかりません。F5 で実行して落ちた後に押してください。")
    end
    @fix_error = err
    add_text("AI", "このエラーについて頼めます:\n#{first_lines(err, 3)}")
    choices = [[:fix_hint, "ヒントをもらう"]]
    choices << [:fix_make, "直してもらう"] if mode_on?("fix")
    choices << [:drop_local, "やめる"]
    ask_child(mode_on?("fix") ? "ヒントをもらいますか、直してもらいますか?" : "ヒントをもらいますか? (「直してもらう」は親の設定で止められています)", choices)
  end

  def first_lines(s, n)
    out = ""
    i = 0
    k = 0
    while i < s.bytesize && k < n
      j = i
      j += 1 while j < s.bytesize && s.getbyte(j) != 10
      out += "\n" unless out.empty?
      out += s.byteslice(i, j - i).to_s
      i = j + 1
      k += 1
    end
    out
  end

  def press_erase
    return set_status("前の頼みがまだ終わっていません。") if @phase != :idle
    read_editor(:erase)
  end

  # Is this line an AI comment (MARK after spaces and tabs)?
  def ai_line?(line)
    i = 0
    n = line.bytesize
    i += 1 while i < n && (line.getbyte(i) == 32 || line.getbyte(i) == 9)
    rest = line.byteslice(i, n - i).to_s
    rest.byteslice(0, 6) == MARK || rest == "# AI:" || rest == "# AI:\r"
  end

  # A reader of DOC line by line (without the newline), CHUNK at a time.
  def lr_open
    @lr_f = File.open(DOC, "r")
    @lr_buf = ""
    @lr_pos = 0
    @lr_eof = false
  end

  def lr_next
    while true
      n = @lr_buf.bytesize
      j = @lr_pos
      j += 1 while j < n && @lr_buf.getbyte(j) != 10
      if j < n
        line = @lr_buf.byteslice(@lr_pos, j - @lr_pos).to_s
        @lr_pos = j + 1
        return line
      end
      if @lr_eof
        return nil if @lr_pos >= n
        line = @lr_buf.byteslice(@lr_pos, n - @lr_pos).to_s
        @lr_pos = n
        return line
      end
      s = @lr_f.read(CHUNK).to_s
      @lr_eof = true if s.bytesize == 0
      @lr_buf = @lr_buf.byteslice(@lr_pos, n - @lr_pos).to_s + s
      @lr_pos = 0
    end
  end

  def lr_close
    @lr_f.close if @lr_f
    @lr_f = nil
    @lr_buf = ""
  end

  def count_ai_lines
    @phase = :idle
    n = 0
    lr_open
    while (l = lr_next)
      n += 1 if ai_line?(l)
    end
    lr_close
    return set_status("AI のコメント (# AI: で始まる行) はありません。") if n == 0
    @erase_n = n
    @apply = :erase
    ask_child("AI のコメント (# AI: で始まる行) を #{n} 行消します。自分で同じ形に書いた行も消えます。",
              [[:erase_go, "消す"], [:drop_local, "やめる"]])
  end

  def erase_go
    y = @doc["y"].to_i
    o = File.open(NEW, "w")
    lr_open
    row = 0
    above = 0
    gone = 0
    first = true
    while (l = lr_next)
      if ai_line?(l)
        above += 1 if row < y
        gone += 1
      else
        o.write("\n") unless first
        o.write(l)
        first = false
      end
      row += 1
    end
    lr_close
    o.close
    @erase_n = gone
    set_status("エディタに入れています…")
    @phase = :ed_replace
    ed_send("ai_replace", { "ver" => @doc["ver"].to_i, "p" => NEW, "y" => y - above, "x" => 0 }) ||
      editor_gave_up("エディタが動いていません。")
  end

  # ------------------------------------------------------------ questions to the child

  def ask_child(text, choices)
    @question = text
    @choices = choices
    @phase = :choice
    set_status(text)
    draw_buttons
  end

  def choose(id)
    @question = nil
    @choices = nil
    draw_buttons
    case id
    when :bak then replace_whole(true)
    when :over then replace_whole(false)
    when :retry_apply
      if @apply == :erase
        read_editor(:erase) || editor_gave_up("エディタが動いていません。")
      else
        read_editor(:apply) || editor_gave_up("エディタが動いていません。")
      end
    when :erase_go then erase_go
    when :fix_hint then start_request("hint", "", @fix_error.to_s)
    when :fix_make
      return set_status("「直してもらう」は、親の設定で止められています。") unless mode_on?("fix")
      start_request("fix", "", @fix_error.to_s)
    when :drop
      set_status("やめました。エディタの文書はそのままです。")
      give_up_apply
    when :drop_local
      @apply = nil
      @phase = :idle
      set_status("やめました。")
    end
  end

  # ------------------------------------------------------------ other buttons

  def new_talk
    return set_status("AI の係につながっていません。") unless @hello && @call.nil? && @phase == :idle
    start_call(:new_talk)
  end

  def took_new_talk(v)
    @lines = []
    @colors = []
    @top = 0
    add_text("AI", "新しい話を始めます。前のやり取りは AI に送りません。")
    set_status("新しい話にしました。")
  end

  def cancel_request
    return choose(@choices[@choices.size - 1][0]) if @phase == :choice
    return set_status("今は頼んでいません。") if @ticket.nil? || !net_phase?
    return if @cancelled
    @cancelled = true
    set_status("やめています…")
    # the cancel goes between two status calls
    if @call.nil?
      start_call(:cancel, @ticket)
    else
      @want_cancel = true
    end
  end

  def took_cancel(v)
    @want_cancel = false
  end

  def pick_mode(m)
    return set_status("「作る」は、親の設定で止められています。") if m == "make" && @hello && !mode_on?("make")
    @mode = m
    draw_buttons(false)
    draw_input(false)
    draw_status
  end

  # ------------------------------------------------------------ the text

  # Adds str (may hold newlines) under the label, wrapped to the area, and
  # shows its first line.
  def add_text(who, str)
    color = who == "あなた" ? theme_accent : theme_fg
    first = @lines.size
    ls = wrap("#{who}: #{str}", @area_w - 8)
    i = 0
    while i < ls.size
      push_line(ls[i], color)
      i += 1
    end
    push_line("", color)
    while @lines.size > MAX_LINES
      @lines.shift
      @colors.shift
      first -= 1
    end
    first = 0 if first < 0
    rows = answer_rows
    # the start of the new text, or the end when it is short
    @top = @lines.size - rows
    @top = first if @top > first
    @top = 0 if @top < 0
    draw_answers if @ready
  end

  def push_line(s, color)
    @lines << s
    @colors << color
  end

  # s cut into lines of w pixels at its newlines and where it gets too
  # wide. Fixed-pitch widths of the 12px Japanese font: ASCII 6, the rest 12.
  def wrap(s, w)
    out = []
    n = s.bytesize
    i = 0
    start = 0
    used = 0
    while i < n
      b = s.getbyte(i)
      if b == 10
        out << s.byteslice(start, i - start).to_s
        i += 1
        start = i
        used = 0
        next
      end
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : (b >= 0xC0 ? 2 : 1)))
      cw = b < 0x80 ? 6 : 12
      if used + cw > w && i > start
        out << s.byteslice(start, i - start).to_s
        start = i
        used = 0
      end
      used += cw
      i += len
    end
    out << s.byteslice(start, n - start).to_s if start < n
    out
  end

  def text_px(s)
    n = s.bytesize
    i = 0
    px = 0
    while i < n
      b = s.getbyte(i)
      if b < 0x80
        px += 6
      elsif b >= 0xC0
        px += 12
      end
      i += 1
    end
    px
  end

  # The end of s that fits in w pixels.
  def tail_fit(s, w)
    return s if text_px(s) <= w
    i = s.bytesize
    px = 0
    while i > 0
      j = i - 1
      j -= 1 while j > 0 && (s.getbyte(j) & 0xC0) == 0x80
      cw = s.getbyte(j) < 0x80 ? 6 : 12
      break if px + cw > w
      px += cw
      i = j
    end
    s.byteslice(i, s.bytesize - i).to_s
  end

  # The start of s that fits (the status line keeps its beginning).
  def head_fit(s, w)
    return s if text_px(s) <= w
    n = s.bytesize
    i = 0
    px = 0
    while i < n
      b = s.getbyte(i)
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2))
      cw = b < 0x80 ? 6 : 12
      break if px + cw > w - 12
      px += cw
      i += len
    end
    s.byteslice(0, i).to_s + ".."
  end

  def set_status(s)
    return if s == @status
    @status = s
    draw_status if @ready
  end

  # ------------------------------------------------------------ drawing

  def layout
    @area_w = @user_area_width
    @area_h = @user_area_height
    @top_y = 1
    @ans_y = @top_y + BTN_H + 3
    @btn_y = @area_h - BTN_H - 1
    @in_y = @btn_y - BTN_H - 3
    @st_y = @in_y - LINE_H * 2 - 1
    @ans_h = @st_y - @ans_y - 2
    @ready = true
  end

  def answer_rows
    r = @ans_h / LINE_H
    r < 1 ? 1 : r
  end

  def draw_all
    clear_user_area
    draw_answers(false)
    draw_status(false)
    draw_input(false)
    draw_buttons(false)
    @gfx.present
  end

  def draw_answers(show = true)
    @gfx.fill_rect(0, @ans_y, @area_w, @ans_h, theme_bg)
    rows = answer_rows
    i = 0
    while i < rows && @top + i < @lines.size
      @gfx.draw_text(3, @ans_y + i * LINE_H, @lines[@top + i], @colors[@top + i])
      i += 1
    end
    # where the view is in the whole
    if @lines.size > rows
      bar_h = @ans_h * rows / @lines.size
      bar_h = 6 if bar_h < 6
      bar_y = @ans_y + (@ans_h - bar_h) * @top / (@lines.size - rows)
      @gfx.fill_rect(@area_w - 3, @ans_y, 2, @ans_h, theme_bg)
      @gfx.fill_rect(@area_w - 3, bar_y, 2, bar_h, theme_border)
    end
    @gfx.present if show
  end

  def draw_status(show = true)
    @gfx.fill_rect(0, @st_y - 1, @area_w, LINE_H * 2 + 1, theme_bg)
    @gfx.draw_line(0, @st_y - 2, @area_w - 1, @st_y - 2, theme_border)
    color = @phase == :choice ? theme_fg : theme_border
    ls = wrap(@status, @area_w - 6)
    @gfx.draw_text(3, @st_y, ls[0].to_s, color)
    if ls.size > 1
      l2 = ls.size > 2 ? head_fit(ls[1] + ls[2], @area_w - 6) : ls[1]
      @gfx.draw_text(3, @st_y + LINE_H, l2, color)
    end
    @gfx.present if show
  end

  def draw_input(show = true)
    h = BTN_H
    @gfx.fill_rect(1, @in_y, @area_w - 2, h, theme_bg)
    @gfx.draw_rect(1, @in_y, @area_w - 2, h, theme_border)
    if @input.empty?
      @gfx.draw_text(4, @in_y + 2, "#{MODE_LABEL[@mode]}: #{PLACEHOLDER[@mode]}", theme_border)
      @gfx.draw_line(4, @in_y + 2, 4, @in_y + h - 3, theme_fg)
    else
      t = tail_fit(@input, @area_w - 14)
      @gfx.draw_text(4, @in_y + 2, t, theme_fg)
      cx = 4 + text_px(t)
      @gfx.draw_line(cx, @in_y + 2, cx, @in_y + h - 3, theme_fg)
    end
    @gfx.present if show
  end

  def button(id, x, y, w, label, on, enabled = true)
    fill = on ? theme_accent : theme_bg
    fg = on ? theme_fg_light : (enabled ? theme_fg : theme_border)
    @gfx.draw_button(x, y, w, BTN_H, label, fill, fg)
    @gfx.draw_rect(x, y, w, BTN_H, theme_border) unless on
    @hits.add(id, x, y, w, BTN_H)
  end

  def draw_buttons(show = true)
    @hits.clear
    # the uses and the two actions
    @gfx.fill_rect(0, @top_y, @area_w, BTN_H, theme_bg)
    x = 1
    i = 0
    while i < TOP_BUTTONS.size
      b = TOP_BUTTONS[i]
      id = b[0]
      on = (id == :m_ask && @mode == "ask") || (id == :m_hint && @mode == "hint") || (id == :m_make && @mode == "make")
      enabled = id != :m_make || @hello.nil? || mode_on?("make")
      x += 6 if id == :fix
      button(id, x, @top_y, b[2], b[1], on, enabled)
      x += b[2] + 2
      i += 1
    end
    # the bottom row: the choices of a question, or the usual buttons
    @gfx.fill_rect(0, @btn_y, @area_w, BTN_H, theme_bg)
    x = 1
    if @choices
      i = 0
      while i < @choices.size
        c = @choices[i]
        w = text_px(c[1]) + 14
        button(c[0], x, @btn_y, w, c[1], true)
        x += w + 4
        i += 1
      end
    else
      button(:send, x, @btn_y, 40, "送る", true)
      x += 43
      button(:stop, x, @btn_y, 46, "やめる", true)
      x += 49
      button(:talk, x, @btn_y, 58, "新しい話", true)
      x += 61
      if @mode == "ask"
        w = @area_w - x - 2
        button(:attach, x, @btn_y, w, @attach ? "文書も送る" : "文書は送らない", @attach)
      end
    end
    @gfx.present if show
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    if t == :mouse_down
      press(@hits.id_at(ev[:x], ev[:y]))
    elsif t == :mouse_wheel
      rows = wheel_rows(ev)
      scroll(-rows) if rows
    elsif t == :key_down
      key(ev)
    end
  end

  def press(id)
    return if id.nil?
    if @choices
      i = 0
      while i < @choices.size
        return choose(id) if @choices[i][0] == id
        i += 1
      end
    end
    case id
    when :send then send_input
    when :stop then cancel_request
    when :talk then new_talk
    when :m_ask then pick_mode("ask")
    when :m_hint then pick_mode("hint")
    when :m_make then pick_mode("make")
    when :fix then press_fix
    when :erase then press_erase
    when :attach
      @attach = !@attach
      draw_buttons
    end
  end

  def scroll(d)
    rows = answer_rows
    top = @top + d
    max = @lines.size - rows
    top = max if top > max
    top = 0 if top < 0
    return if top == @top
    @top = top
    draw_answers
  end

  def key(ev)
    return if ev_ctrl?(ev)
    case ev[:scancode].to_i
    when 0x52 then return scroll(-1)
    when 0x51 then return scroll(1)
    when 0x4B then return scroll(-answer_rows)
    when 0x4E then return scroll(answer_rows)
    end
    ch = ev[:character].to_i
    return if ch == 0
    if ch >= 0x80
      s = utf8_feed(ch)
      type_char(s) if s
      return
    end
    @u8_need = 0
    if ch == 10 || ch == 13
      send_input unless @choices
    elsif ch == 8
      n = @input.length
      @input = @input[0, n - 1].to_s if n > 0
      draw_input
    elsif ch == 27
      cancel_request
    elsif ch >= 32 && ch <= 126
      type_char(ch.chr)
    end
  end

  def type_char(s)
    return if @input.length >= INPUT_MAX
    @input = @input + s
    draw_input
  end

  # One byte of a UTF-8 character (kana input sends them one per event).
  def utf8_feed(byte)
    if @u8_need == 0
      if byte >= 0xF0
        @u8_need = 3
      elsif byte >= 0xE0
        @u8_need = 2
      elsif byte >= 0xC0
        @u8_need = 1
      else
        return nil
      end
      @u8_buf = one_byte(byte)
      return nil
    end
    if byte < 0x80 || byte >= 0xC0
      @u8_need = 0
      return utf8_feed(byte)
    end
    @u8_buf += one_byte(byte)
    @u8_need -= 1
    return nil if @u8_need > 0
    @u8_buf
  end

  def one_byte(b)
    s = " ".dup
    s.setbyte(0, b)
    s
  end
end
