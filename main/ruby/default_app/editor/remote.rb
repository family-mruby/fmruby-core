# Requests from outside the editor (doc/editor_multi_file, plan 4.4 and 4.5).
#
# Two kinds arrive through on_control:
#
# - A file to open. The kernel sends "open_file" when something asks for the
#   editor while one is running (the file manager, the shell, the desktop), and
#   a "file_selected" the editor did not ask for is the same request. It is
#   opened the way File > Open opens a file (editor/docs.rb). While a question
#   or a menu is up it waits, and is opened when that has gone (one is kept;
#   the latest wins).
#
# - Reading and replacing the current document, for an app on the machine (the
#   AI helper, doc/ai_coding). "ai_read" writes the document to AI_DOC_PATH and
#   answers "ai_doc" with its version and where the cursor is; "ai_replace"
#   loads a file in its place only when the version it names is still the
#   current one, and answers "ai_done". Only the editor changes its text: the
#   other app asks. The text crosses through files written and read by
#   EditorCore (C), so it never becomes a String on this VM. The requests are
#   refused, never kept waiting, while a question or a menu is up ("busy") or a
#   help page stands in for the document ("help").
#
# Every message is a flat Hash (no nesting: the Spinel build reads values one
# key at a time, narrowed with is_a?) and stays under the 176-byte message cap.
module EditorRemote
  include EditorConst

  # Where ai_read writes the current document. /tmp is flat (fmrb_tmpfs.h:
  # no subdirectories), so the name carries the "ai_" instead of a folder.
  AI_DOC_PATH = "/tmp/ai_doc.rb"
  # Longer paths go out as the name alone, to keep "ai_doc" under the cap.
  AI_PATH_MAX = 60

  def remote_init
    # A file handed over while the editor was busy ("" when none is waiting).
    @open_waiting = ""
  end

  # Something is on screen that a request would pull the document from under:
  # the save-before question, a file dialog the editor is waiting on, a menu,
  # Find, Colors, the key list, the completion list, the debugger's picker.
  def remote_busy?
    return true if @quit_dialog_open || @after_save != 0 || !@pending_file_op.nil?
    return true if @active_menu || @search_open || @palette_open || @keys_open
    return true if @comp_open
    dbg_modal?
  end

  # ---- Opening a file handed over from outside ----

  def remote_open(path)
    return if path.length == 0
    if remote_busy?
      @open_waiting = path
      Log.info("open_file: #{path} waits for the dialog")
      return
    end
    Log.info("open_file: #{path}")
    docs_open_path(path)
  end

  # Called every frame (on_update): a waiting file is opened once nothing is in
  # its way any more.
  def remote_flush_open
    return if @open_waiting.length == 0
    return if remote_busy?
    path = @open_waiting
    @open_waiting = ""
    Log.info("open_file: #{path} (after the dialog)")
    docs_open_path(path)
  end

  # ---- Reading and replacing the current document ----

  # An Integer field of a request, or +dflt+ when it is missing or another type.
  def remote_int(msg, key, dflt)
    v = msg[key]
    return v if v.is_a?(Integer)
    dflt
  end

  # Why a request cannot be served now, or "" when it can.
  def remote_refusal
    return "help" if @help_open
    return "busy" if remote_busy?
    ""
  end

  def remote_refuse(re, cmd, id, why)
    Log.info("#{cmd} id=#{id}: refused (#{why}) ver=#{EditorCore.doc_version}")
    send_message(re, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => cmd, "id" => id, "ok" => 0, "why" => why,
                   "ver" => EditorCore.doc_version })
  end

  # The file name for "ai_doc": "" for a document with no name, the name alone
  # when the whole path would not fit.
  def remote_file_field
    f = @current_file
    return "" if f.nil?
    s = f.to_s
    return s if s.length <= AI_PATH_MAX
    tab_base_name(s)
  end

  def ai_read(msg)
    re = remote_int(msg, "re", -1)
    id = remote_int(msg, "id", 0)
    if re < 0
      Log.error("ai_read without a pid to answer (re)")
      return
    end
    why = remote_refusal
    return remote_refuse(re, "ai_doc", id, why) if why.length > 0
    written = EditorCore.save_file(AI_DOC_PATH)
    if written < 0
      return remote_refuse(re, "ai_doc", id, written == -2 ? "nomem" : "io")
    end
    ver = EditorCore.doc_version
    sx = @sel_anchor_x
    sy = @sel_anchor_y
    rp = @run_pid
    Log.info("ai_read id=#{id}: ver=#{ver} lines=#{EditorCore.line_count} bytes=#{written}")
    send_message(re, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => "ai_doc", "id" => id, "ok" => 1, "ver" => ver,
                   "n" => EditorCore.line_count, "b" => written,
                   "y" => @cy, "x" => @cx,
                   "sy" => sy.nil? ? -1 : sy, "sx" => sx.nil? ? -1 : sx,
                   "m" => @modified ? 1 : 0, "f" => remote_file_field,
                   "rp" => rp.nil? ? -1 : rp })
  end

  def ai_replace(msg)
    re = remote_int(msg, "re", -1)
    id = remote_int(msg, "id", 0)
    if re < 0
      Log.error("ai_replace without a pid to answer (re)")
      return
    end
    why = remote_refusal
    return remote_refuse(re, "ai_done", id, why) if why.length > 0
    if remote_int(msg, "ver", -1) != EditorCore.doc_version
      return remote_refuse(re, "ai_done", id, "ver")
    end
    src = msg["p"]
    path = src.is_a?(String) ? src : ""
    return remote_refuse(re, "ai_done", id, "io") if path.length == 0
    n = EditorCore.replace_file(path)
    if n < 0
      return remote_refuse(re, "ai_done", id, n == -2 ? "nomem" : "io")
    end
    remote_after_replace(remote_int(msg, "y", 0), remote_int(msg, "x", 0))
    ver = EditorCore.doc_version
    Log.info("ai_replace id=#{id}: lines=#{n} ver=#{ver}")
    send_message(re, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => "ai_done", "id" => id, "ok" => 1, "ver" => ver })
  end

  # The document has new text under the same name: it has unsaved edits, the
  # problem markers named the old lines, and the cursor goes where the asking
  # app said (kept inside the text).
  def remote_after_replace(y, x)
    utf8_reset
    @sel_anchor_x = nil
    @sel_anchor_y = nil
    @modified = true
    clear_diagnostics
    @cy = y < 0 ? 0 : y
    clamp_cy
    @cx = x < 0 ? 0 : x
    clamp_cx
    last = EditorCore.line_count - 1
    @scroll_y = last if @scroll_y > last
    @anchor_seg = 0
    @scroll_x = 0
    ensure_cursor_visible
    @cur_line_segs = line_segments(@cy)
    @dirty_status = true
    @need_redraw = true
  end
end
