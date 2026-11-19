# AI helper (doc/ai_coding): asks the parent's AI helper gem (asterism-tutor)
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
# Uses: question (the editor's document goes with it when the editor runs
# and the document has something in it, unless the child turned "send the
# document" off; with no editor, an empty document or an editor that cannot
# hand it over, the question goes alone and says so), hint (AI comments
# "# AI: " above lines, the code unchanged), make (the
# whole document, after asking whether to back it up), fix (after a run that
# failed: a hint or the fixed document, the child chooses each time),
# erase (the AI comment lines, after showing how many) and restore (the
# document back to one of its backups, no gem needed).
#
# The screen: a menu-bar band on top (the three uses, the selected one
# inverted; Fix; the Edit pull-down with erase and restore), the answers,
# the status line, the input line, and the bottom row (Send, Abort, the
# document switch, Reset talk on the right). The window can be resized down
# to its first size; the answers are kept as whole messages and wrapped
# again for the new width.
#
# The class is in three files: this one (the gem and the requests),
# ai_helper/editor.rb (the editor, the document, backups) and
# ai_helper/screen.rb (the drawing and the input), required at the end.
# One file of 47 KB put the compile peak at 76% of the VM pool on the sim;
# the compile peak follows the largest file.
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
  MENU_H = 15            # the menu-bar band
  ITEM_H = 15            # one row of the Edit pull-down
  # The editor cuts the app name in ai_doc ("rn") to this many bytes on a
  # character boundary (editor/remote.rb AI_RUN_NAME_MAX); run_error cuts the
  # last error's name the same way.
  RUN_NAME_MAX = 24

  MODE_LABEL = { "ask" => "質問", "hint" => "ヒント", "make" => "作成" }
  PLACEHOLDER = {
    "ask" => "質問を打って Enter",
    "hint" => "どこが分からないか (空でもよい)",
    "make" => "作りたいものを書いて Enter",
  }
  # The menu bar: the uses, a separator (:sep), Fix and the Edit pull-down.
  MENU_BAR = [[:m_ask, "質問"], [:m_hint, "ヒント"], [:m_make, "作成"], [:sep, ""],
              [:fix, "修正"], [:edit, "編集"]]
  EDIT_ITEMS = [[:erase, "AIコメント削除"], [:restore, "バックアップから戻す"]]
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
  # Why a question went without the document, when the editor would not
  # hand it over (the question is not stopped for it).
  ASK_WHY = {
    "busy" => "エディタでダイアログやメニューが開いているので",
    "help" => "エディタでヘルプ (F1) が開いているので",
  }

  # ------------------------------------------------------------ lifecycle

  def on_create
    @gfx.set_font(:ja, 12)
    @hits = FmrbUI::HitAreas.new
    # The answers: whole messages (wrapped again when the width changes) and
    # the lines they make, as (message number, byte offset, byte length).
    @msgs = []
    @msg_colors = []
    @msg_base = 0         # the number of @msgs[0]
    @l_msg = []
    @l_off = []
    @l_len = []
    @wrap_w = 0
    @top = 0
    # the menu bar's colours: the editor's menu row (a step off the title
    # bar, never the key 0x01), its text, and a muted text for a use that
    # is turned off
    @bar_bg = FmrbColors.shade(FmrbConst::THEME_MENU_BG, theme_bg)
    @bar_fg = theme_fg_light
    @bar_dim = FmrbColors.shade(@bar_fg, @bar_bg)
    @menu_open = false
    @edit_x = 0
    @input = ""
    @u8_buf = ""
    @u8_need = 0
    # the input line's cursor, and the last one told to a PC's IME
    @in_cx = 4
    @tc = -2
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
    @attach = true        # a question takes the editor's document along
    @ask_note = ""
    @asked_doc = ""
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
    @wrap_w = @area_w - 8
    add_text("AI", "AI ヘルパーです。答えるのは AI (人ではありません) です。上で使い方を選び、下の行に打って Enter か「送信」を押してください。「質問」「ヒント」「作成」は、エディタで開いている文書を使います (質問は、下の切り替えで文書を送らないこともできます)。")
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

  # The kernel resizes on the release of the corner (an outline follows
  # the pointer meanwhile), so this runs once per resize: the answers are
  # wrapped again only when the width changed.
  def on_resize(w, h)
    @menu_open = false
    @tc = -2              # the area's origin may have moved: tell it again
    layout
    rewrap if @area_w - 8 != @wrap_w
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

  # ------------------------------------------------------------ asking

  def send_input
    text = @input.strip
    return set_status("前の頼みがまだ終わっていません。") if @phase != :idle
    return set_status("AI の係につながっていません。") unless @hello
    if @mode == "ask"
      return set_status("質問を打ってください。") if text.empty?
    elsif @mode == "make"
      return set_status("「作成」は、親の設定で止められています。") unless mode_on?("make")
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
    label = mode == "fix" || !error.empty? ? "修正" : MODE_LABEL[mode]
    @req_shown = text.empty? ? "(#{label})" : "(#{label}) #{text}"
    if mode == "ask"
      @doc = nil
      if !@attach
        @ask_note = ""
        ask_now(nil, nil)
      elsif editor_pid < 0
        ask_without_doc("エディタが動いていないので")
      else
        read_editor(:ask) || ask_without_doc("エディタが動いていないので")
      end
    elsif mode == "fix" || !error.empty?
      send_request(mode)   # the document was read already (offer_fix)
    else
      read_editor(mode == "ask" ? :ask : mode.to_sym) || (@phase = :idle)
    end
  end

  # The request (the document, if one was read for it, goes first).
  def send_request(mode)
    if mode == "ask"
      return ask_without_doc("エディタが動いていないので") if @doc.nil?
      return ask_without_doc("エディタの文書が空なので") if doc_bytes == 0
      return begin_upload(:ask)
    end
    if doc_bytes == 0 && mode != "make"
      @phase = :idle
      return set_status("エディタの文書が空です。")
    elsif doc_bytes > 0
      return begin_upload(:ask)
    end
    ask_now(nil, nil)
  end

  # A question that goes alone although the document was to go with it; the
  # reason is shown with it.
  def ask_without_doc(reason)
    @doc = nil
    @ask_note = reason
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
    # shown (and the input line emptied) once it really goes; a question
    # says whether the document went with it
    @asked_doc = doc_id || doc_text ? doc_label : ""
    add_text("あなた", ask_shown(doc_id || doc_text))
    add_text("AI", "(#{@ask_note}、文書は付けずに送りました)") if @req_mode == "ask" && !(doc_id || doc_text) && !@ask_note.empty?
    @input = ""
    draw_input
    @phase = :asking
    reset_poll_stats
    @cancelled = false
    @asked_at = Machine.board_millis
    start_call(:ask, req)
    set_status("送っています…")
  end

  def ask_shown(with_doc)
    return @req_shown unless @req_mode == "ask"
    head = with_doc ? "(質問 + #{doc_label})" : "(質問、文書なし)"
    @req_text.empty? ? head : "#{head} #{@req_text}"
  end

  def doc_label
    n = base_name(doc_file)
    n.empty? ? "名前のない文書" : "文書 #{n}"
  end

  # The end of the status line after a question was taken.
  def ask_status_tail
    return "" unless @req_mode == "ask"
    @asked_doc.empty? ? "文書は付けていません。" : "#{@asked_doc}#{@asked_doc.end_with?("文書") ? "" : " "}を付けました。"
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
    set_status("受け付けました。#{ask_status_tail}")
    Log.info("ai_helper: ticket #{@ticket}")
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
    set_status(@cancelled ? "中断しています… (#{secs} 秒)" : "#{word}… (#{secs} 秒)")
  end

  def took_result(v)
    secs = (Machine.board_millis - @asked_at) / 1000
    Log.info("ai_helper: result #{@ticket} after #{secs} s, status #{@poll_n} calls avg #{@poll_n > 0 ? @poll_sum / @poll_n : 0} ms max #{@poll_max} ms pool #{FmrbApp.pool_used}")
    if @cancelled
      set_status("中断しました。")
      @phase = :ack
      return
    end
    if error_of(v)
      if error_of(v) == "not_ready"
        @phase = :poll
        return
      end
      add_text("AI", message_of(v))
      set_status(error_of(v) == "cancelled" ? "中断しました。" : "うまくいきませんでした。")
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

  # ------------------------------------------------------------ other buttons

  def new_talk
    return set_status("AI の係につながっていません。") unless @hello && @call.nil? && @phase == :idle
    start_call(:new_talk)
  end

  def took_new_talk(v)
    @msgs = []
    @msg_colors = []
    @msg_base = 0
    @l_msg = []
    @l_off = []
    @l_len = []
    @top = 0
    add_text("AI", "会話をリセットしました。前のやり取りは AI に送りません。")
    set_status("会話をリセットしました。")
  end

  def cancel_request
    return choose(@choices[@choices.size - 1][0]) if @phase == :choice
    return set_status("今は頼んでいません。") if @ticket.nil? || !net_phase?
    return if @cancelled
    @cancelled = true
    set_status("中断しています…")
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
    return set_status("「作成」は、親の設定で止められています。") if m == "make" && @hello && !mode_on?("make")
    @mode = m
    draw_buttons(false)
    draw_input(false)
    draw_status
  end
end

# the editor half and the screen (one class, three files)
require "/app/modern/ai_helper/editor"
require "/app/modern/ai_helper/screen"
