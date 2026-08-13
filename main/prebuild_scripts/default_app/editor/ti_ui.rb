# Type-inference UI for the editor (picoruby-ti): completion, signature help,
# F1 help pages, hover (Ctrl+T) and diagnostics (Ctrl+E).
#
# Split out of editor.app.rb (doc/editor_refactor). The editor body reaches
# these through the method names below; the state is EditorApp instance
# variables, shared as usual. This is the same mixin arrangement as
# EditorDebugPane. This module owns the COMP_/HELP_/ET_ constants; the
# layout/keys it shares with the rest of the editor (SC_*, DROPDOWN_*, LINE_H,
# CHAR_W) come from EditorConst, which it includes below.
module EditorTiUi
  include EditorConst

  # ---- Completion (type inference) ----
  #
  # Tab asks EditorCore.suggest what can follow the cursor and puts the answers
  # in a list under it. The engine is only asked when Tab is pressed -- never on
  # every keystroke -- because one request re-parses the whole document
  # (doc/editor_ti/report/p2.md).

  COMP_MAX_ROWS     = 8
  COMP_ITEM_H       = 10    # matches DROPDOWN_ITEM_H (dropdown cell height)
  COMP_FIELD_LABEL  = 0
  COMP_FIELD_DETAIL = 1
  COMP_FIELD_DOC    = 2
  COMP_TOO_LARGE    = -4    # ET_ERR_TOO_LARGE: document above the engine's cap
  COMP_DETAIL_COLOR = FmrbGfx.rgb_to_332(110, 110, 110)

  # ---- Two languages in one doc comment ----
  #
  # A doc comment in sig/ carries Japanese and English at once, split by a
  # single "<<en>>" marker (sig/README.md): inline on the summary line, and on
  # a line of its own between the two long forms. Nothing between here and the
  # signature files knows about it -- the type database and the generated help
  # pages carry the marker as ordinary text -- so this is the only place that
  # decides which half a reader sees.
  #
  # A comment without the marker is shown as it is, in either language. That is
  # what keeps the Japanese-only signatures working while they are translated.

  TI_LANG_MARK = "<<en>>"

  def ti_pick_lang(text)
    s = text.to_s
    i = s.index(TI_LANG_MARK)
    return s if i.nil?
    if FmrbI18n.lang == "en"
      s[i + TI_LANG_MARK.length, s.length].to_s.strip
    else
      s[0, i].to_s.strip
    end
  end

  # Letters, digits and underscore: what a name being completed is made of.
  def comp_name_byte?(b)
    (b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122) ||
      b == 95
  end

  # Is the character before the cursor one a completion could follow? A name,
  # its ? / ! ending, the dot of a method call, or the colon of a constant
  # path (FmrbGfx::B). Anywhere else -- line start, after a space -- Tab keeps
  # its old meaning.
  def comp_trigger?
    return false if @cx <= 0
    c = EditorCore.char_at(@cy, @cx - 1)
    return false if c.bytesize != 1
    b = c.getbyte(0)
    return true if b == 46 || b == 63 || b == 33 || b == 58   # . ? ! :
    comp_name_byte?(b)
  end

  # Characters before the cursor that the chosen candidate replaces. Zero right
  # after a dot, which is the "show me everything" case.
  def comp_prefix_len
    n = 0
    x = @cx
    while x > 0
      c = EditorCore.char_at(@cy, x - 1)
      break if c.bytesize != 1
      break unless comp_name_byte?(c.getbyte(0))
      n += 1
      x -= 1
    end
    n
  end

  def comp_rows
    n = @comp_labels.size
    n < COMP_MAX_ROWS ? n : COMP_MAX_ROWS
  end

  # Object-common methods (every receiver has them, flattened in from Object and
  # Kernel by the type database) that completion sinks below the receiver's own
  # methods, so the specific names lead. Operators are sunk too, by the shape of
  # the name. The console applies the same rule (tool/web/js/ti-editor.js) --
  # keep the two lists in step.
  COMP_COMMON_METHODS = {
    "attr_accessor" => true, "attr_reader" => true, "block_given?" => true,
    "class" => true, "dup" => true, "exit" => true, "extend" => true,
    "include" => true, "inspect" => true, "is_a?" => true, "lambda" => true,
    "loop" => true, "nil?" => true, "p" => true, "print" => true,
    "private" => true, "proc" => true, "public" => true, "puts" => true,
    "raise" => true, "relinquish" => true, "self" => true, "sleep" => true,
    "sleep_ms" => true, "sprintf" => true, "to_s" => true, "yield" => true,
  }

  # A candidate goes to the bottom tier if it is object-common or an operator.
  def comp_demoted?(label)
    return true if COMP_COMMON_METHODS[label]
    b = label.getbyte(0)
    return true unless b
    # Operator methods start with something other than a letter or underscore.
    return true unless (b >= 65 && b <= 90) || (b >= 97 && b <= 122) || b == 95
    false
  end

  # Split the parallel candidate arrays into two tiers, keeping each tier in the
  # order it arrived (EditorCore.suggest already returns them alphabetically):
  # the receiver's own methods first, then object-common methods and operators.
  # A stable partition rather than a sort -- no sort_by/Array-compare, so it
  # compiles the same under Spinel and mruby.
  def comp_sink_common_and_operators
    n = @comp_labels.size
    return if n < 2
    keep_labels = []
    keep_details = []
    keep_docs = []
    sink_labels = []
    sink_details = []
    sink_docs = []
    i = 0
    while i < n
      if comp_demoted?(@comp_labels[i])
        sink_labels << @comp_labels[i]
        sink_details << @comp_details[i]
        sink_docs << @comp_docs[i]
      else
        keep_labels << @comp_labels[i]
        keep_details << @comp_details[i]
        keep_docs << @comp_docs[i]
      end
      i += 1
    end
    @comp_labels = keep_labels + sink_labels
    @comp_details = keep_details + sink_details
    @comp_docs = keep_docs + sink_docs
  end

  def open_completion
    t0 = Machine.uptime_us
    n = EditorCore.suggest(@cy, @cx)
    ms = (Machine.uptime_us - t0) / 1000
    bytes = EditorCore.doc_bytesize
    # info, not debug: debug is filtered out of the build, and this number is
    # the one that says whether completion still feels instant (same reason
    # edit_lat reports at info).
    Log.info("ti_lat: #{ms} ms (#{n} candidates, #{bytes} bytes)")

    if n == COMP_TOO_LARGE
      flash_status(FmrbI18n.t(:b_comp_too_large).to_s)
      return
    end
    if n <= 0
      flash_status(FmrbI18n.t(:b_comp_none).to_s)
      return
    end

    @comp_labels = []
    @comp_details = []
    @comp_docs = []
    i = 0
    while i < n
      @comp_labels << EditorCore.suggestion(i, COMP_FIELD_LABEL)
      @comp_details << EditorCore.suggestion(i, COMP_FIELD_DETAIL)
      @comp_docs << EditorCore.suggestion(i, COMP_FIELD_DOC)
      i += 1
    end
    comp_sink_common_and_operators
    @comp_prefix = comp_prefix_len
    @comp_idx = 0
    @comp_top = 0
    @comp_open = true
    comp_explain_selected
    @need_redraw = true
  end

  # The part of an API a list of names cannot show: the doc comment of the
  # selected candidate, or its signature when it has none. Goes through the
  # status line's message zone like everything else, so the badges stay put.
  def comp_explain_selected
    about = ti_pick_lang(@comp_docs[@comp_idx])
    # The signature is the same in every language, so it needs no picking.
    about = @comp_details[@comp_idx].to_s if about.length == 0
    flash_status(about)
  end

  def close_completion
    return unless @comp_open
    @comp_open = false
    @comp_labels = []
    @comp_details = []
    @comp_docs = []
    clear_status_message
    @need_redraw = true
  end

  # Replace the name being typed with the chosen candidate. After a dot there
  # is nothing to replace, so it is a plain insert.
  def accept_completion
    label = @comp_labels[@comp_idx].to_s
    prefix = @comp_prefix
    close_completion
    return if label.bytesize == 0

    if prefix > 0
      EditorCore.delete_range(@cy, @cx - prefix, @cy, @cx)
      @cx -= prefix
    end
    nx = EditorCore.insert_text(@cy, @cx, label)
    if nx < 0
      doc_full
      return
    end
    @cx = nx
    mark_edited
    ensure_cursor_visible
    mark_dirty_line(@cy)
  end

  def comp_scroll_into_view
    rows = comp_rows
    @comp_top = @comp_idx if @comp_idx < @comp_top
    @comp_top = @comp_idx - rows + 1 if @comp_idx >= @comp_top + rows
    @comp_top = 0 if @comp_top < 0
  end

  # Modal while the list is up. Answers true when the key was the list's; any
  # other key closes it and is then handled the way it normally would be.
  def handle_completion_key(ev)
    case ev[:scancode] || 0
    when SC_UP
      @comp_idx -= 1 if @comp_idx > 0
      comp_scroll_into_view
      comp_explain_selected
      @need_redraw = true
      true
    when SC_DOWN
      @comp_idx += 1 if @comp_idx < @comp_labels.size - 1
      comp_scroll_into_view
      comp_explain_selected
      @need_redraw = true
      true
    when SC_ENTER, SC_KP_ENTER, SC_TAB
      accept_completion
      true
    when SC_ESC
      close_completion
      true
    else
      close_completion
      false
    end
  end

  def comp_width
    longest = 0
    i = 0
    while i < @comp_labels.size
      n = @comp_labels[i].to_s.length
      longest = n if n > longest
      i += 1
    end
    w = (longest + 2) * CHAR_W + 8
    room = @user_area_width - 4
    w > room ? room : w
  end

  # Under the cursor, or above it when the bottom of the edit area is too
  # close; pushed left when it would run off the right edge.
  def comp_origin(w, h)
    box = cursor_cell_box
    cx = box.nil? ? @user_area_x0 + 1 : box[0]
    cy = box.nil? ? @edit_y : box[1]

    x = cx
    right = @user_area_x0 + @user_area_width - 2
    x = right - w if x + w > right
    x = @user_area_x0 + 1 if x < @user_area_x0 + 1

    y = cy + LINE_H
    y = cy - h if y + h > @status_y
    y = @edit_y if y < @edit_y
    [x, y]
  end

  def draw_completion
    rows = comp_rows
    return if rows <= 0
    w = comp_width
    h = COMP_ITEM_H * rows + 2
    x, y = comp_origin(w, h)

    @gfx.fill_rect(x, y, w, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, w, h, 0x60)

    i = 0
    while i < rows
      idx = @comp_top + i
      item_y = y + 1 + i * COMP_ITEM_H
      label = @comp_labels[idx].to_s
      if idx == @comp_idx
        @gfx.fill_rect(x + 1, item_y, w - 2, COMP_ITEM_H, DROPDOWN_SEL_BG)
        @gfx.draw_text(x + 4, item_y + 1, label, DROPDOWN_SEL_TEXT, DROPDOWN_SEL_BG)
      else
        @gfx.draw_text(x + 4, item_y + 1, label, DROPDOWN_TEXT, DROPDOWN_BG)
      end
      i += 1
    end
  end

  # ---- Signature help (while the arguments are being typed) ----
  #
  # Asked for after a "(" or a "," -- the two moments where the answer changes
  # -- and never on other keystrokes: a request re-parses the document.

  ET_CALL_SIGNATURE = 0
  ET_CALL_ARG_NAME  = 1
  ET_CALL_ARG_TYPE  = 2

  def show_signature_help
    t0 = Machine.uptime_us
    found = EditorCore.call_context(@cy, @cx)
    ms = (Machine.uptime_us - t0) / 1000
    return if found <= 0

    sig = EditorCore.call_field(ET_CALL_SIGNATURE)
    idx = EditorCore.call_argument_index
    name = EditorCore.call_field(ET_CALL_ARG_NAME)
    type = EditorCore.call_field(ET_CALL_ARG_TYPE)
    Log.info("sig_lat: #{ms} ms (arg #{idx} of #{sig})")

    flash_status(signature_hint(sig, idx, type))
  end

  # "draw_text: (Integer x, Integer y, ...) -> FmrbGfx" and argument 1 become
  # "draw_text(x, >>y: Integer<<, str, color)": the names alone, with the one
  # being typed marked and carrying its type. The marked argument sits early
  # in the line on purpose -- a narrow window trims the tail.
  def signature_hint(sig, idx, type = "")
    open_at = sig.index("(")
    close_at = open_at ? sig.index(")", open_at) : nil
    return sig if open_at.nil? || close_at.nil?

    head = sig[0, open_at].to_s.strip
    head = head[0, head.length - 1] if head.end_with?(":")
    body = sig[open_at + 1, close_at - open_at - 1].to_s

    names = []
    body.split(",").each do |part|
      words = part.strip.split(" ")
      names << (words.length > 0 ? words[words.length - 1] : "")
    end

    out = ""
    i = 0
    while i < names.length
      out += ", " if i > 0
      if i == idx
        out += (type.length > 0) ? ">>#{names[i]}: #{type}<<" : ">>#{names[i]}<<"
      else
        out += names[i]
      end
      i += 1
    end
    "#{head}(#{out})"
  end

  # ---- Help (F1) ----
  #
  # The long half of the doc comments in sig/, generated into /help at build
  # time. index.txt maps a method name to a file; the file is opened in the
  # same buffer, read-only, and the buffer being edited is put aside in /tmp
  # so nothing is lost -- including changes that were never saved.

  HELP_INDEX = "/help/index.txt"
  HELP_DIR   = "/help/"
  HELP_STASH = "/tmp/editor_stash.txt"

  def help_path_for(name)
    return nil if name.nil? || name.length == 0
    begin
      f = File.open(HELP_INDEX, "r")
      text = f.read
      f.close
    rescue => e
      Log.info("help index unavailable: #{e.message}")
      return nil
    end
    return nil if text.nil?

    hits = 0
    found = nil
    text.split("\n").each do |line|
      next if line.length == 0
      parts = line.split("\t")
      next unless parts.length >= 2
      next unless parts[0] == name
      hits += 1
      found = parts[1] if found.nil?
    end
    Log.info("help: #{name} -> #{found} (#{hits} entr#{hits == 1 ? 'y' : 'ies'})") if found
    found
  end

  # The word the help is asked for: the selected candidate when the list is
  # open, otherwise the name under the cursor.
  def help_topic
    return @comp_labels[@comp_idx].to_s if @comp_open
    x0 = @cx
    x0 -= 1 while x0 > 0 && comp_name_byte_at?(@cy, x0 - 1)
    x1 = @cx
    len = EditorCore.line_length(@cy)
    x1 += 1 while x1 < len && comp_name_byte_at?(@cy, x1)
    return "" if x1 <= x0
    word = ""
    x = x0
    while x < x1
      word += EditorCore.char_at(@cy, x)
      x += 1
    end
    word
  end

  def comp_name_byte_at?(y, x)
    c = EditorCore.char_at(y, x)
    return false if c.bytesize != 1
    comp_name_byte?(c.getbyte(0))
  end

  def open_help
    topic = help_topic
    path = help_path_for(topic)
    if path.nil?
      flash_status(FmrbI18n.t(:b_no_help).to_s)
      return
    end
    close_completion
    return unless stash_buffer
    @help_open = true
    @help_return_file = @current_file
    @help_return_y = @cy
    @help_return_x = @cx
    @help_return_modified = @modified
    # Loading a .md turns highlighting off (it is not Ruby); the buffer that
    # comes back should look the way it did.
    @help_return_hl = @hl_enabled
    @help_return_hl_manual = @hl_manual
    full = HELP_DIR + path
    load_file(full)
    # Only touch the buffer when the page really was read: load_file leaves the
    # previous document in place (and says so) when it fails.
    help_filter_language(path) if @current_file == full
    @modified = false
    @need_redraw = true
  end

  # ---- Help pages: keeping one language ----
  #
  # A page holds both languages, the way the doc comment it came from does.
  # gen_help writes a method page as
  #
  #   "# Class#method" / "" / signature / "" / summary / "" / long text
  #
  # and a class page as "# Class" / "" / long text. The summary line has the
  # marker inline; the long text has a line that is nothing but the marker,
  # between the Japanese half and the English one. Both are dealt with here,
  # right after the read, so what is scrolled through is one language.
  #
  # A page without a marker line is left alone -- the one language it has is
  # what both readers get.

  HELP_BODY_LINE_METHOD = 6   # "# Class#method", "", signature, "", summary, ""
  HELP_BODY_LINE_CLASS  = 2   # "# Class", ""

  def help_filter_language(path)
    body = path.to_s.end_with?("index.md") ? HELP_BODY_LINE_CLASS : HELP_BODY_LINE_METHOD
    return if EditorCore.line_count <= body
    help_pick_lang_in_head(body)

    marker = help_marker_line(body)
    return if marker.nil?
    if FmrbI18n.lang == "en"
      # The Japanese half is everything from the first line of the long text
      # down to the marker.
      help_delete_lines(body, marker)
    else
      help_delete_lines(marker, EditorCore.line_count - 1)
    end
  end

  # The summary is a single line with both languages on it, so it is rewritten
  # instead of removed. Only the lines above the long text are looked at: below
  # them the marker stands on a line of its own.
  def help_pick_lang_in_head(body)
    y = 0
    while y < body
      text = help_line_text(y)
      if text.index(TI_LANG_MARK)
        picked = ti_pick_lang(text)
        EditorCore.delete_range(y, 0, y, EditorCore.line_length(y))
        EditorCore.insert_text(y, 0, picked) if picked.length > 0
      end
      y += 1
    end
  end

  def help_line_text(y)
    n = EditorCore.line_length(y)
    return "" if n <= 0
    EditorCore.render_text(y, 0, n).to_s
  end

  # The line that is nothing but the marker, or nil when the page has only one
  # language. Lines of the wrong length are skipped without being read.
  def help_marker_line(from)
    y = from
    n = EditorCore.line_count
    while y < n
      len = EditorCore.line_length(y)
      if len >= TI_LANG_MARK.length && len <= TI_LANG_MARK.length + 2 &&
         help_line_text(y).strip == TI_LANG_MARK
        return y
      end
      y += 1
    end
    nil
  end

  # Remove lines a..b. Deleting as far as the start of the line after b leaves
  # that line whole; at the end of the document there is no line after, so the
  # deletion runs from the end of the line before a instead.
  def help_delete_lines(a, b)
    last = EditorCore.line_count - 1
    return if a < 0 || a > b || b > last
    if b < last
      EditorCore.delete_range(a, 0, b + 1, 0)
    elsif a > 0
      EditorCore.delete_range(a - 1, EditorCore.line_length(a - 1), b, EditorCore.line_length(b))
    else
      EditorCore.delete_range(a, 0, b, EditorCore.line_length(b))
    end
  end

  def close_help
    return unless @help_open
    @help_open = false
    # The stash is the truth whenever it exists: a named file that had unsaved
    # edits is on disk in its old state, and reloading that would throw the
    # edits away.
    if @help_stashed
      load_file(HELP_STASH)
      @current_file = @help_return_file
      @help_stashed = false
    else
      load_file(@help_return_file)
    end
    @modified = @help_return_modified
    @hl_enabled = @help_return_hl
    @hl_manual = @help_return_hl_manual
    apply_hl_enabled
    @cy = @help_return_y
    @cx = @help_return_x
    clamp_cy
    clamp_cx
    ensure_cursor_visible
    @need_redraw = true
  end

  # Put the buffer aside before help takes the document over. A named file is
  # already on disk unless it was edited, so only unsaved work has to be
  # written out, and /tmp is RAM -- no flash wear for reading a help page.
  def stash_buffer
    return true if @current_file && !@modified
    written = EditorCore.save_file(HELP_STASH)
    if written < 0
      flash_status(FmrbI18n.t(:b_save_failed).to_s)
      return false
    end
    @help_stashed = true
    true
  end

  def clamp_cy
    last = EditorCore.line_count - 1
    @cy = last if @cy > last
    @cy = 0 if @cy < 0
  end

  # Would this key change the text? Typing, deleting, pasting and saving are
  # the ones a help page has to refuse; moving around is fine.
  def help_edit_key?(ev)
    sc = ev[:scancode] || 0
    return true if sc == SC_TAB || sc == SC_ENTER || sc == SC_KP_ENTER
    return true if sc == 0x2A || sc == 0x4C   # Backspace / Delete
    if ev_ctrl?(ev)
      # Save, paste, cut and quit-with-save all write; copy and select-all
      # are harmless.
      return true if sc == 0x16 || sc == 0x19 || sc == 0x1B
      return false
    end
    ch = ev[:character] || 0
    ch >= 32 && ch <= 126 || ch >= 0x80
  end

  # ---- Type information: hover (Ctrl+T) and diagnostics (Ctrl+E) ----
  #
  # Both ask the same engine as completion and answer in the status line's
  # message zone. Neither is modal and neither runs on its own: hover is one
  # key, diagnostics run on a successful save and on Ctrl+E.

  ET_HOVER_NAME      = 0
  ET_HOVER_TYPE      = 1
  ET_HOVER_SIGNATURE = 2
  ET_HOVER_DOC       = 3

  ET_DIAG_START_Y = 0

  def show_hover
    found = EditorCore.hover(@cy, @cx)
    if found == COMP_TOO_LARGE
      flash_status(FmrbI18n.t(:b_comp_too_large).to_s)
      return
    end
    if found <= 0
      flash_status(FmrbI18n.t(:b_no_type).to_s)
      return
    end

    if EditorCore.hover_method?
      # The signature already begins with the method's name, so it reads as a
      # sentence on its own; the doc comment follows when there is room.
      text = EditorCore.hover_field(ET_HOVER_SIGNATURE)
      doc = ti_pick_lang(EditorCore.hover_field(ET_HOVER_DOC))
      text += " -- " + doc if doc.length > 0
    else
      text = "#{EditorCore.hover_field(ET_HOVER_NAME)} : #{EditorCore.hover_field(ET_HOVER_TYPE)}"
    end
    flash_status(text)
  end

  # Diagnostics found by the last run. Kept as parallel arrays of line numbers
  # and messages: the count is at most 64 and this avoids a Hash per problem.
  def clear_diagnostics
    return if @diag_count.nil?
    had_marks = @diag_lines.length > 0
    @diag_count = nil
    @diag_lines = []
    @diag_msgs = []
    @diag_idx = -1
    # The tint was part of the rows, so they have to be repainted.
    @need_redraw = true if had_marks
    @dirty_status = true
  end

  def problem_on_line?(line_idx)
    return false if @diag_lines.nil?
    @diag_lines.include?(line_idx)
  end

  # Run the engine over the whole document. The answer always reaches the
  # status line, including after a save: "no problems" is the reassurance the
  # save was worth waiting for, and the file name losing its "*" already says
  # the save itself went through.
  def run_diagnostics
    n = EditorCore.diagnose
    if n == COMP_TOO_LARGE
      # Distinct from "no problems": nothing was checked at all.
      clear_diagnostics
      flash_status(FmrbI18n.t(:b_comp_too_large).to_s)
      return
    end
    if n < 0
      clear_diagnostics
      flash_status(FmrbI18n.t(:b_diag_failed).to_s)
      return
    end

    @diag_lines = []
    @diag_msgs = []
    i = 0
    while i < n
      @diag_lines << EditorCore.diagnostic_pos(i, ET_DIAG_START_Y)
      @diag_msgs << EditorCore.diagnostic_message(i)
      i += 1
    end
    @diag_count = n
    @diag_idx = -1
    @need_redraw = true

    if n == 0
      flash_status(FmrbI18n.t(:b_no_problems).to_s)
    else
      word = (n == 1) ? FmrbI18n.t(:b_problem) : FmrbI18n.t(:b_problems)
      flash_status("#{n} #{word.to_s}: #{@diag_msgs[0]}")
    end
  end

  # Ctrl+E: run the diagnostics, and on every press after that walk to the next
  # problem line, wrapping at the end.
  def diagnostics_key
    # Nothing to walk yet, or nothing found last time: check again. (An edit
    # drops the previous answer, so this is also the path after typing.)
    if @diag_count.nil? || @diag_lines.length == 0
      run_diagnostics
      return
    end
    @diag_idx += 1
    @diag_idx = 0 if @diag_idx >= @diag_lines.length
    goto_problem(@diag_idx)
  end

  def goto_problem(i)
    line = @diag_lines[i]
    return if line.nil?
    line = EditorCore.line_count - 1 if line >= EditorCore.line_count
    prev_cy = @cy
    @cy = line
    @cx = 0
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
    flash_status("#{line + 1}: #{@diag_msgs[i]}")
  end

  # Only Ruby gets checked, by the same rule that decides highlighting: the
  # engine is a Ruby type checker, and a .bas file would be nothing but noise.
  def diagnose_after_save
    return unless hl_default_for(@current_file)
    run_diagnostics
  end

end
