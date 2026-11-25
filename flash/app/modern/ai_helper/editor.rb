# AI helper (doc/ai_coding): the part that works with the editor, required
# by ai_helper.app.rb after its class body (a smaller main file keeps the
# compile peak in the VM pool low, doc/reference/app_memory.md).
#
# The editor (doc/editor_multi_file report m3.md): ai_read makes it write
# its document to /tmp/ai_doc.rb and answer ai_doc; ai_replace loads
# /tmp/ai_new.rb in its place when the version is still the one read, and
# answers ai_done. Also here: sending the document in pieces, putting a
# result in (hint, whole document, rebase), backups and restoring one, fix,
# erase and the questions to the child.
class AiHelperApp < FmrbApp
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
    if @after_read == :ask
      # a question still goes, without the document
      @after_read = nil
      return ask_without_doc("エディタから文書を読めなかったので")
    end
    if @apply
      ask_child(text, [[:retry_apply, "もう一度"], [:drop, "キャンセル"]])
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
      if @after_read == :ask
        @after_read = nil
        return ask_without_doc(ASK_WHY[msg["why"].to_s] || "エディタから文書を読めなかったので")
      end
      editor_gave_up(why_text(msg["why"]))
      return
    end
    @doc = msg
    Log.info("ai_helper: ai_doc ver=#{msg['ver']} n=#{msg['n']} b=#{msg['b']} y=#{msg['y']} m=#{msg['m']} f=#{msg['f']} rp=#{msg['rp']} rn=#{msg['rn']}")
    what = @after_read
    @after_read = nil
    case what
    when :ask then send_request("ask")
    when :hint then send_request("hint")
    when :make then send_request("make")
    when :fix then offer_fix
    when :erase then count_ai_lines
    when :restore then list_backups
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

  # ------------------------------------------------------------ putting the result in

  # After the editor was read again (@doc is now): a hint goes in when the
  # document is the one sent, else it is placed again on the new one
  # (rebase); a whole document asks first.
  def apply_result
    return restore_again if @apply == :restore
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
                [[:over, "上書きする"], [:drop, "キャンセル"]])
    else
      ask_child("#{head}今の中身を残しますか? (#{base_name(f)}.bak に退避します)",
                [[:bak, "はい"], [:over, "いいえ"], [:drop, "キャンセル"]])
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
    when :restore
      back = "#{base_name(doc_file)}.bak#{@restore_k} の中身に戻しました。"
      set_status(@new_bak ? "#{back}前の中身は #{base_name(@new_bak)} に残しました。" : back)
      @apply = nil
      @new_bak = nil
      @phase = :idle
      return
    end
    @new_bak = nil
    @phase = :ack
  end

  def give_up_apply
    if @apply == :erase || @apply == :restore || @ticket.nil?
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
    Log.info("ai_helper: copied #{from} -> #{to} (#{File.size(to)} bytes)")
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

  # ------------------------------------------------------------ restoring a backup

  # Edit > Restore from backup: the editor's document back to one of its
  # backups. No gem is needed (nothing goes to the AI).
  def press_restore
    return set_status("前の頼みがまだ終わっていません。") if @phase != :idle
    read_editor(:restore)
  end

  def backup_nums(f)
    nums = []
    n = 1
    while n <= BAK_SCAN
      nums << n if File.exist?("#{f}.bak#{n}")
      n += 1
    end
    nums
  end

  # After ai_read: the backups of the document, listed in the answers, and
  # one button for each.
  def list_backups
    @phase = :idle
    f = doc_file
    if f.empty? || f.getbyte(0) != 47
      return set_status("今の文書には名前が無いので、バックアップはありません。")
    end
    nums = backup_nums(f)
    name = base_name(f)
    if nums.empty?
      return set_status("#{name} のバックアップはありません。")
    end
    text = "#{name} のバックアップ (1 が最初の中身、番号が大きいほど新しい):"
    choices = []
    i = 0
    while i < nums.size
      k = nums[i]
      tag = k == 1 ? " (最初の中身)" : (i == nums.size - 1 ? " (いちばん新しい)" : "")
      text += "\n#{k}#{tag}: #{bak_hint("#{f}.bak#{k}")}"
      choices << [k, k.to_s]
      i += 1
    end
    choices << [:drop_local, "キャンセル"]
    @restore_file = f
    add_text("AI", text)
    ask_child("どのバックアップに戻しますか? 番号を選んでください。", choices)
  end

  # Its size and its first line of code (a comment line when it has none).
  def bak_hint(path)
    size = File.size(path)
    head = File.open(path, "r") { |io| io.read(512) }.to_s
    first = ""
    code = ""
    i = 0
    n = head.bytesize
    while i < n && code.empty?
      j = i
      j += 1 while j < n && head.getbyte(j) != 10
      line = head.byteslice(i, j - i).to_s.strip
      if !line.empty?
        first = line if first.empty?
        code = line if line.getbyte(0) != 35
      end
      i = j + 1
    end
    line = code.empty? ? first : code
    line.empty? ? "#{size} バイト" : "#{size} バイト「#{head_fit(line, 150)}」"
  rescue => e
    Log.info("ai_helper: bak_hint #{path}: #{e.message}")
    "読めません"
  end

  # The chosen backup is copied to NEW first: backing up the document now
  # may remove or renumber it (BAK_KEEP, the second oldest goes, the
  # numbers close up), and what goes in must be what was chosen.
  def restore_from(k)
    @phase = :idle
    src = "#{@restore_file}.bak#{k}"
    return set_status("バックアップ #{base_name(src)} が見つかりません。") unless File.exist?(src)
    return set_status("バックアップ #{base_name(src)} を読めませんでした。") unless copy_file(src, NEW)
    @apply = :restore
    @restore_k = k
    ask_overwrite(true)
  end

  # The editor was read again (written meanwhile, or "again" after busy):
  # the same question once more, unless it now shows another document.
  def restore_again
    if doc_file != @restore_file
      add_text("AI", "エディタの文書が切りかわったので、戻しませんでした。")
      set_status("戻しませんでした。")
      give_up_apply
      return
    end
    ask_overwrite(false)
  end

  # ------------------------------------------------------------ fix and erase

  def press_fix
    return set_status("前の頼みがまだ終わっていません。") if @phase != :idle
    return set_status("AI ホストにつながっていません。") unless @hello
    read_editor(:fix)
  end

  # The last error is this document's when it is filed under the name the
  # editor says the document last ran as with F5 ("rn" in ai_doc; "" before
  # its first run). The editor cuts a long name to RUN_NAME_MAX bytes, so the
  # error's name is cut the same way before the two are compared.
  def run_error
    e = FmrbApp.last_error
    return nil unless e && @doc
    rn = @doc["rn"].to_s
    return nil if rn.empty?
    return nil unless cut_name(e[:name].to_s) == rn
    e[:error].to_s
  end

  def cut_name(s)
    return s if s.bytesize <= RUN_NAME_MAX
    k = RUN_NAME_MAX
    k -= 1 while k > 0 && (s.getbyte(k) & 0xC0) == 0x80
    s.byteslice(0, k).to_s
  end

  def offer_fix
    @phase = :idle
    err = run_error
    unless err
      return set_status("エディタで実行 (F5) したアプリのエラーが見つかりません。F5 で実行して落ちた後に押してください。")
    end
    @fix_error = err
    add_text("AI", "このエラーについて頼めます:\n#{first_lines(err, 3)}")
    choices = [[:fix_hint, "ヒント"]]
    choices << [:fix_make, "修正してもらう"] if mode_on?("fix")
    choices << [:drop_local, "キャンセル"]
    ask_child(mode_on?("fix") ? "「ヒント」か「修正してもらう」を選んでください。" : "「ヒント」を選んでください (「修正してもらう」は親の設定で止められています)。", choices)
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
              [[:erase_go, "消す"], [:drop_local, "キャンセル"]])
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
    return restore_from(id) if id.is_a?(Integer)
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
      return set_status("「修正してもらう」は、親の設定で止められています。") unless mode_on?("fix")
      start_request("fix", "", @fix_error.to_s)
    when :drop
      set_status("キャンセルしました。エディタの文書はそのままです。")
      give_up_apply
    when :drop_local
      @apply = nil
      @phase = :idle
      set_status("キャンセルしました。")
    end
  end
end
