# Text Editor Application
# MS-DOS style text editor with menu bar and status line

class EditorApp < FmrbApp
  # Shared constants (colors, layout, scancodes, ...) live in editor/const.rb.
  # Every mixin that uses them includes EditorConst too, so all of them reach
  # the constants by their bare names.
  include EditorConst
  # The on-device debugger lives in editor/debug_pane.rb (mruby build) or in a
  # no-op stub of the same name (Spinel build). Everything below talks to it
  # through the dbg_* hooks only.
  include EditorDebugPane
  # Type-inference UI (completion / signature help / F1 help / hover /
  # diagnostics) lives in editor/ti_ui.rb. Reached through its method names;
  # it owns the COMP_/HELP_/ET_ constants.
  include EditorTiUi
  # Selection and clipboard live in editor/clipboard.rb.
  include EditorClipboard
  # Find / Find-next dialog lives in editor/search.rb.
  include EditorSearch
  # The Colors dialog lives in editor/palette_ui.rb.
  include EditorPalette
  # Drawing (menu bar, status line, edit rows, cursor), render-latency
  # instrumentation and the quit dialog live in editor/render.rb.
  include EditorRender
  # Key entry / UTF-8 / navigation live in editor/input.rb.
  include EditorInput
  # The key list (menu bar Keys, Alt-K) lives in editor/keys.rb.
  include EditorKeys
  # File/Edit dropdowns and the template list live in editor/menu.rb.
  include EditorMenu
  # File load/save, Run (F5) and the status-line helpers live in
  # editor/file_run.rb.
  include EditorFileRun

  def initialize
    super()
    # The document lives in EditorCore (C, POOL_ID_EDITOR_DOC arena): the editor
    # holds only cursor / selection / view state. Nothing here keeps line text.
    EditorCore.reset
    @cx = 0             # Cursor column in current line
    @cy = 0             # Cursor line index
    @scroll_y = 0       # Anchor: logical line of the first visible screen row
    # Anchor: which wrapped segment of @scroll_y that row shows. Always 0 when
    # wrapping is off, which is what makes the unwrapped paths below identical
    # to what they were.
    @anchor_seg = 0
    # Long lines fold into the window instead of scrolling sideways. On by
    # default: reading a whole line without moving is the point for a child.
    # Per buffer, like the highlight toggle -- nothing is persisted.
    @wrap_on = true
    # Segments the cursor's line occupied when it was last drawn. An edit that
    # changes that number reflows everything below, so it has to repaint from
    # there; an edit that does not touches one screen row.
    @cur_line_segs = 1
    @scroll_x = 0       # Horizontal scroll offset (columns)
    # Redraw bookkeeping. @need_redraw asks for the whole screen (menus,
    # dialogs, scroll, layout); the finer flags below repaint only what an edit
    # or a cursor move actually touched.
    @need_redraw = true
    @dirty_lines = []   # document line indices to repaint
    @dirty_from = nil   # repaint this line and everything below (line shift)
    @dirty_status = false
    @frame_ms = 33
    @modified = false
    @current_file = nil
    @active_menu = nil   # :file, :edit, :template, or nil when none is open
    @menu_idx = 0
    @pending_file_op = nil  # :open or :save
    # File > Template: filled in from TEMPLATE_DIR each time the menu opens, so
    # a template dropped in while the editor runs shows up without a restart.
    @template_names = []
    @template_labels = []
    # Selection (anchor side; cursor side is the moving end). nil = no selection.
    @sel_anchor_x = nil
    @sel_anchor_y = nil
    # Clipboard lives in EditorCore too (copy_range / paste_at), so a copied
    # block never becomes a String on this VM's heap.
    # Syntax highlight: default comes from the file type (Ruby only -- the
    # tokenizer is a Ruby lexer), and a manual toggle wins until another file is
    # opened. A new unnamed buffer starts on: this machine is a Ruby machine.
    @hl_enabled = true
    @hl_manual = false
    # Key repeat state
    @held_keycode = nil
    @hold_frames = 0
    # Transient message in the status line's left zone (see draw_status_line).
    # Set only through flash_status; counts down per on_update tick.
    @status_msg = nil
    @status_msg_ok = false
    @status_msg_frames = 0
    @status_msg_fresh = false
    # ---- Run (F5) ----
    # pid of the app the last RUN started, so the next RUN replaces it. The
    # kernel reports it back in "run_result"; nil when nothing is running.
    @run_pid = nil
    # Set when RUN had to ask for a file name first: run once the save lands.
    @run_after_save = false
    # Modal "save before quit?" dialog raised by Ctrl-X when @modified.
    @quit_dialog_open = false
    @keys_open = false
    # ---- Completion (Tab) ----
    # Candidates from the type inference engine, asked for only when Tab is
    # pressed after an identifier or a dot. Modal while the list is up.
    @comp_open = false
    @comp_labels = []
    @comp_details = []
    @comp_docs = []
    @comp_idx = 0      # selected candidate
    @comp_top = 0      # first candidate on screen (the list scrolls)
    @comp_prefix = 0   # characters before the cursor the choice replaces
    # ---- Diagnostics (Ctrl+E, and after a save) ----
    # nil until a run has happened; the lines and messages are parallel arrays.
    # Any edit drops them, since their positions age the moment the text moves.
    @diag_count = nil
    @diag_lines = []
    @diag_msgs = []
    @diag_idx = -1
    # ---- Help (F1) ----
    # While a help page is open the buffer is read-only, and the one being
    # edited waits in /tmp (see open_help).
    @help_open = false
    @help_stashed = false
    @help_return_file = nil
    @help_return_y = 0
    @help_return_x = 0
    @help_return_modified = false
    @help_return_hl = true
    @help_return_hl_manual = false
    # Modal Find dialog (Alt-S / Search menu / F3 for find next).
    @search_open = false
    @palette_open = false
    @search_query = ""
    @search_last = ""
    @search_status = ""
    @gutter_w = 0             # breakpoint gutter width (set by recompute_layout)
    # ---- Kana input ----
    # A composed kana arrives as its UTF-8 bytes, one key event per byte
    # (host_task's composition layer; the HID payload has one byte of
    # character). These collect a whole character before it reaches the
    # document, so a half character can never be inserted.
    @u8_buf = ""
    @u8_need = 0
    # Kana input mode as last reported by the host. Shown from the start, at
    # "A": the badge is clickable, so it is the way into kana input on a
    # keyboard that has no half/full-width key. It also agrees with the
    # desktop's menu bar, which shows the same state from boot.
    @kana_mode = 0
    @kana_badge_x = nil
    @kana_badge_w = 0
    dbg_init
    # ---- Latency instrumentation ----
    @lat_t0 = nil     # uptime_us of the oldest key not yet shown on screen
    @lat_n = 0
    @lat_sum = 0
    @lat_max = 0
    @lat_slow = 0
    @lat_hist = []
    i = 0
    while i <= LAT_BUCKETS
      @lat_hist << 0
      i += 1
    end
    @draw_n = 0
    @draw_sum = 0
    @draw_max = 0
  end

  def on_create
    apply_hl_enabled
    recompute_layout
    @need_redraw = true
  end

  # Coming back from a fullscreen park (Ctrl+Tab) or from another app's
  # fullscreen: the canvas was hidden, so repaint everything once.
  def on_resume
    @need_redraw = true
  end

  # Ctrl+Q. Same question Ctrl-X asks, so an unsaved buffer is never dropped.
  def on_quit_request
    if @modified
      @quit_dialog_open = true
      @need_redraw = true
      redraw_if_dirty
    else
      stop
    end
  end

  def on_resize(new_width, new_height)
    Log.info("Editor resize: #{new_width}x#{new_height}")
    recompute_layout
    # Shrinking can leave the cursor off-screen; re-clamp scroll position.
    ensure_cursor_visible
    @need_redraw = true
  end

  def recompute_layout
    @menu_y = @user_area_y0
    @edit_y = @menu_y + CHAR_H + 1
    @status_y = @user_area_y0 + @user_area_height - CHAR_H
    @dbg_pane_h = dbg_pane_h
    if @dbg_pane_h > 0
      @dbg_pane_y = @status_y - @dbg_pane_h - 1
      @edit_height = @dbg_pane_y - @edit_y
    else
      @dbg_pane_y = @status_y
      @edit_height = @status_y - @edit_y
    end
    # Left gutter for breakpoint dots is only present during a debug session, so
    # normal editing keeps the full width.
    @gutter_w = dbg_gutter_w
    # Columns are cells, not characters: a full-width character occupies two.
    @edit_cols = (@user_area_width - 2 - @gutter_w) / CELL_W
    @edit_rows = @edit_height / LINE_H
  end

  # ---- Wrapped layout ----
  #
  # A logical line occupies one screen row when wrapping is off, and
  # wrap_count(y) of them when it is on. Nothing here ever measures the whole
  # document: the walk starts at the anchor and stops at the bottom of the
  # window, so a 200KB file costs the same as a short one.

  def line_segments(y)
    return 1 unless @wrap_on
    EditorCore.wrap_count(y, @edit_cols)
  end

  # First character index of segment +seg+ of line +y+.
  def segment_start(y, seg)
    return @scroll_x unless @wrap_on
    EditorCore.wrap_start(y, @edit_cols, seg)
  end

  # Characters in segment +seg+ (to the end of the line for the last one).
  def segment_chars(y, seg)
    unless @wrap_on
      return EditorCore.line_length(y) - @scroll_x
    end
    EditorCore.wrap_start(y, @edit_cols, seg + 1) -
      EditorCore.wrap_start(y, @edit_cols, seg)
  end

  # Which segment of line +y+ holds character +cx+.
  def segment_of(y, cx)
    return 0 unless @wrap_on
    n = line_segments(y)
    seg = 0
    while seg + 1 < n
      break if cx < EditorCore.wrap_start(y, @edit_cols, seg + 1)
      seg += 1
    end
    seg
  end

  # Screen row of (line, seg) counting from the anchor, or -1 when it is off
  # screen. Walks segments, so it costs the visible lines and no more.
  def row_of(y, seg)
    return -1 if y < @scroll_y
    return -1 if y == @scroll_y && seg < @anchor_seg
    row = 0
    ly = @scroll_y
    ls = @anchor_seg
    while row < @edit_rows
      return row if ly == y && ls == seg
      ls += 1
      if ls >= line_segments(ly)
        ly += 1
        ls = 0
        return -1 if ly >= EditorCore.line_count
      end
      row += 1
    end
    -1
  end

  # Move the anchor +delta+ screen rows (either direction), clamped to the
  # document. Returns the number of rows actually moved.
  def move_anchor(delta)
    moved = 0
    if delta > 0
      while moved < delta
        segs = line_segments(@scroll_y)
        if @anchor_seg + 1 < segs
          @anchor_seg += 1
        else
          break if @scroll_y + 1 >= EditorCore.line_count
          @scroll_y += 1
          @anchor_seg = 0
        end
        moved += 1
      end
    else
      while moved > delta
        if @anchor_seg > 0
          @anchor_seg -= 1
        else
          break if @scroll_y == 0
          @scroll_y -= 1
          @anchor_seg = line_segments(@scroll_y) - 1
        end
        moved -= 1
      end
    end
    moved
  end

  # ---- Dirty tracking ----
  #
  # Typing used to repaint the whole editor area (200-400 gfx commands at full
  # screen). These mark what actually changed so a keystroke repaints one row.
  # Anything that shifts rows around (line insert/delete) marks "from here
  # down"; anything structural (scroll, menus, dialogs, layout) falls back to
  # @need_redraw, i.e. the full repaint.

  def mark_dirty_line(idx)
    @dirty_lines << idx unless @dirty_lines.include?(idx)
  end

  # Inclusive range of document lines, in either order.
  def mark_dirty_range(a, b)
    lo = a < b ? a : b
    hi = a < b ? b : a
    i = lo
    while i <= hi
      mark_dirty_line(i)
      i += 1
    end
  end

  def mark_dirty_from(idx)
    @dirty_from = idx if @dirty_from.nil? || idx < @dirty_from
  end

  def dirty_line?(idx)
    return true if @dirty_from && idx >= @dirty_from
    @dirty_lines.include?(idx)
  end

  def dirty?
    @need_redraw || @dirty_from || @dirty_status || @dirty_lines.length > 0
  end

  def clear_dirty
    @need_redraw = false
    @dirty_from = nil
    @dirty_status = false
    @dirty_lines.clear
  end

  # ---- Scrolling ----

  # True when the cursor's cell column is past the right edge of the window.
  # Its own cell counts: a full-width character has to fit whole.
  def cursor_cell_overflow?
    return false if @cx <= @scroll_x
    # More characters than there are cells cannot fit, whatever their widths --
    # and asking for that many would run past the width map's own limit.
    return true if @cx - @scroll_x >= @edit_cols
    widths = EditorCore.render_width(@cy, @scroll_x, @cx - @scroll_x + 1)
    idx = @cx - @scroll_x
    cell = cell_offset(widths, idx)
    w = idx < widths.bytesize ? widths.getbyte(idx) : 1
    cell + w > @edit_cols
  end

  def ensure_cursor_visible
    old_scroll_y = @scroll_y
    old_scroll_x = @scroll_x
    old_anchor_seg = @anchor_seg
    # Vertical. With wrapping on the unit is a segment, not a line, so the
    # answer comes from walking the anchor rather than from arithmetic on line
    # numbers -- and the walk is bounded by the window height either way.
    if @wrap_on
      seg = segment_of(@cy, @cx)
      if @cy < @scroll_y || (@cy == @scroll_y && seg < @anchor_seg)
        @scroll_y = @cy
        @anchor_seg = seg
      elsif row_of(@cy, seg) < 0
        # Below the window: put the cursor on the last row.
        @scroll_y = @cy
        @anchor_seg = seg
        move_anchor(-(@edit_rows - 1))
      end
    elsif @cy < @scroll_y
      @scroll_y = @cy
      @anchor_seg = 0
    elsif @cy >= @scroll_y + @edit_rows
      @scroll_y = @cy - @edit_rows + 1
      @anchor_seg = 0
    end
    # Horizontal. @scroll_x stays a character index -- it is what EditorCore
    # slices by -- but whether the cursor is inside the window is a question
    # about cells, and a full-width character is two of them. Walk the width map
    # forward until the cursor fits rather than guess a character count.
    if @wrap_on
      @scroll_x = 0    # nothing scrolls sideways when the line folds instead
    elsif @cx < @scroll_x
      @scroll_x = @cx
    else
      guard = 0
      while cursor_cell_overflow? && guard < @edit_cols * 2
        @scroll_x += 1
        guard += 1
      end
    end
    # The baseline mark_edited compares against is the cursor line's segment
    # count as of the last time the cursor settled here.
    @cur_line_segs = line_segments(@cy) if @wrap_on
    # A scroll moves every row, so the fine-grained marks are useless here.
    if @scroll_y != old_scroll_y || @scroll_x != old_scroll_x ||
       @anchor_seg != old_anchor_seg
      @need_redraw = true
    end
  end

  # ---- Pointer ----

  # Move the cursor to the character under (mx, my). The screen row gives the
  # (line, segment) by walking down from the anchor -- the same walk the drawing
  # does -- and the cell offset inside the row gives the character, by running
  # the width map forward until the pointer is passed. Clicking either half of a
  # full-width character lands on that character.
  def place_cursor_at(mx, my)
    row = (my - @edit_y) / LINE_H
    return if row < 0 || row >= @edit_rows

    ly = @scroll_y
    ls = @anchor_seg
    i = 0
    while i < row
      ls += 1
      if ls >= line_segments(ly)
        ly += 1
        ls = 0
      end
      break if ly >= EditorCore.line_count
      i += 1
    end
    if ly >= EditorCore.line_count
      ly = EditorCore.line_count - 1
      ls = line_segments(ly) - 1
    end

    col0 = segment_start(ly, ls)
    limit = segment_chars(ly, ls)
    want = (mx - (@user_area_x0 + 1 + @gutter_w)) / CELL_W
    want = 0 if want < 0

    widths = EditorCore.render_width(ly, col0, @edit_cols)
    n = widths.bytesize
    n = limit if limit < n
    cells = 0
    ci = 0
    while ci < n
      w = widths.getbyte(ci)
      # Past the middle of a wide character counts as that character, not the
      # next one, so its right half does not select its neighbour.
      break if want < cells + w
      cells += w
      ci += 1
    end

    prev_cy = @cy
    clear_selection if has_selection?
    @cy = ly
    @cx = col0 + ci
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  # ---- Events ----

  def on_control(msg)
    if msg["cmd"] == "file_selected" && msg["path"]
      if msg["mode"] == "save" || @pending_file_op == :save
        save_file_as(msg["path"])
        if @run_after_save
          @run_after_save = false
          run_current_file
        end
      else
        load_file(msg["path"])
      end
      @pending_file_op = nil
    elsif msg["cmd"] == "run_result"
      @run_pid = msg["pid"]
      if @run_pid
        flash_status("#{FmrbI18n.t(:b_run_pid).to_s} #{@run_pid}")
        Log.info("Run started: #{msg["path"]} pid=#{@run_pid}")
      else
        flash_status(FmrbI18n.t(:b_run_failed).to_s)
        Log.error("Run failed: #{msg["path"]}")
      end
    end
  end

  def on_event(ev)
    super(ev)
    handle_editor_event(ev)
    # A status message survives the key that raised it and goes on the next
    # one (see flash_status). Ageing it here, after the key was handled, is
    # what draws that line.
    @status_msg_fresh = false if ev[:type] == :key_down
    # Draw here rather than leaving it to the next on_update. Going through
    # on_update cost up to a whole 33ms frame per key on top of the drawing
    # itself, which was most of the measured key-to-present time.
    redraw_if_dirty
  end

  def handle_editor_event(ev)
    # Kana input turned on/off or switched script. Only the status badge
    # cares; the characters themselves arrive as ordinary key events.
    if ev[:type] == :kana_mode
      @kana_mode = ev[:mode]
      utf8_reset
      @dirty_status = true
      return
    end

    # A half-typed character cannot survive a different kind of event.
    utf8_reset if ev[:type] != :key_down && @u8_need > 0

    # The wheel moves the view, not the cursor -- the same thing the scrollbar
    # of any other window does, and the reason move_anchor exists (it counts
    # screen rows, so wrapped lines are stepped a segment at a time). A modal
    # is not scrolled: it owns the window while it is up.
    rows = wheel_rows(ev)
    if rows
      return if @keys_open || @quit_dialog_open || @palette_open ||
                @search_open || @active_menu
      @need_redraw = true if move_anchor(-rows) != 0
      return
    end

    if ev[:type] == :mouse_up
      if @keys_open
        keys_advance
        return
      end

      # Open dropdown click handling
      if @active_menu
        handle_menu_click(ev[:x], ev[:y])
        return
      end

      # Menu bar click. Hit boxes come from the widths draw_menu_bar measured,
      # so they follow the translated labels without a second layout rule.
      if ev[:y] >= @menu_y && ev[:y] < @menu_y + CHAR_H
        hit = menu_bar_hit(ev[:x])
        if hit >= 0
          activate_menu_bar(@menu_ids[hit])
          return
        end
        return
      end

      # Kana mode badge in the status line: click to change input mode.
      if hit_kana_badge?(ev[:x], ev[:y])
        cycle_kana_mode
        return
      end

      # Edit area click: put the cursor where the pointer is. The gutter (debug
      # breakpoints) and the status line keep their own meaning, so the test is
      # the text rectangle, not "not the menu bar".
      text_x0 = @user_area_x0 + 1 + @gutter_w
      if ev[:y] >= @edit_y && ev[:y] < @edit_y + @edit_rows * LINE_H &&
         ev[:x] >= text_x0 && ev[:y] < @dbg_pane_y
        place_cursor_at(ev[:x], ev[:y])
        return
      end
    end

    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0

      # Latency clock starts here, except for bare modifier presses (they draw
      # nothing, so stamping them would charge their idle time to the next key).
      lat_key_arrived unless keycode >= 224 && keycode <= 231

      # A message that has already survived one key gives the line back now.
      # Modifiers alone do not count: Ctrl arrives as its own event, and it
      # would take the message down before its own shortcut ran.
      unless keycode >= 224 && keycode <= 231
        clear_status_message if @status_msg && !@status_msg_fresh
      end

      # The key list is there to be read: a key turns the page, and takes the
      # panel down after the last one. A click does the same.
      if @keys_open
        keys_advance
        return
      end

      # Modal quit-confirm dialog steals all keys until dismissed.
      if @quit_dialog_open
        handle_quit_dialog_key(ev)
        return
      end

      # Modal owned by the debugger (attach-target picker).
      return if dbg_handle_modal_key(ev)

      # Open dropdown is modal: arrows / Enter / hotkey letters / ESC.
      if @active_menu
        handle_menu_key(ev)
        return
      end

      # Find dialog is modal: typed chars build the query, Enter searches.
      if @palette_open
        handle_palette_key(ev)
        return
      end
      if @search_open
        handle_search_dialog_key(ev)
        return
      end

      # F1: the long help for the method under the cursor, or for the
      # candidate being looked at. Checked before the completion list, which
      # is where it is most useful.
      if (ev[:scancode] || 0) == SC_F1
        @help_open ? close_help : open_help
        return
      end

      # A help page is read-only: navigation works, Esc closes it, and
      # anything that would change the text says so instead of doing it.
      if @help_open
        if (ev[:scancode] || 0) == SC_ESC
          close_help
          return
        end
        if help_edit_key?(ev)
          flash_status(FmrbI18n.t(:b_readonly).to_s)
          return
        end
      end

      # Completion list is modal for the keys it uses; anything else closes it
      # and carries on below, so typing never gets stuck behind the popup.
      if @comp_open
        return if handle_completion_key(ev)
      end

      # Tab: completion when the cursor is just after a name or a dot, the
      # indent it has always been anywhere else. Answered by scancode -- kana
      # input withholds the character, and this has to work in every mode.
      if (ev[:scancode] || 0) == SC_TAB
        if comp_trigger?
          open_completion
        else
          insert_indent
        end
        return
      end

      # F3 -> Find Next (uses last query). Available without modifiers.
      if (ev[:scancode] || 0) == 0x3C
        find_next
        return
      end

      scancode = ev[:scancode] || 0
      # F5 runs the file. During a debug session it means Continue instead
      # (handled by the debugger below), which is the usual meaning of that key.
      if scancode == SC_F5 && !dbg_active?
        run_current_file
        return
      end
      # F11 switches between window and fullscreen without restarting the app,
      # so the buffer stays. During a debug session F11 keeps its step meaning
      # (handled below).
      if scancode == SC_F11 && !dbg_active?
        toggle_fullscreen
        return
      end
      # F9 (breakpoint) and the run-control keys, when the debugger is present.
      return if dbg_handle_key(ev)

      # Ctrl shortcuts. ev_ctrl? + scancode (USB HID Usage ID) is uniform
      # across ESP32 and Linux/SDL2; ev[:keycode] for letters differs by
      # platform. 0x04=A, 0x06=C, 0x16=S, 0x19=V, 0x1B=X.
      if ev_ctrl?(ev)
        case ev[:scancode] || 0
        when 0x16  # Ctrl-S -> save
          save_file
          return
        when 0x1B  # Ctrl-X -> quit (prompt if unsaved)
          if @modified
            @quit_dialog_open = true
            @need_redraw = true
          else
            stop
          end
          return
        when 0x06  # Ctrl-C -> Copy
          copy_selection
          return
        when 0x19  # Ctrl-V -> Paste
          paste_clipboard
          return
        when 0x04  # Ctrl-A -> Select All
          select_all
          return
        when 0x07  # Ctrl-D -> open Debug menu (Alt-D on real HW; the Linux sim
          open_menu(:debug) if dbg_menu_visible?  # cannot carry Alt, so Ctrl-D
          return
        when SC_T  # Ctrl-T -> type under the cursor
          show_hover
          return
        when SC_E  # Ctrl-E -> type errors, then walk them
          diagnostics_key
          return
        end
      end

      # Alt shortcuts mirror the menu bar hotkey letters (yellow chars).
      # 0x09=F, 0x08=E, 0x16=S, 0x0B=H.
      if ev_alt?(ev)
        case ev[:scancode] || 0
        when 0x09  # Alt-F -> open File menu
          open_menu(:file)
          return
        when 0x08  # Alt-E -> open Edit menu
          open_menu(:edit)
          return
        when 0x16  # Alt-S -> open Search (Find) dialog
          open_search_dialog
          return
        when 0x15  # Alt-R -> Run
          run_current_file
          return
        when 0x0B  # Alt-H -> toggle Highlight
          toggle_highlight
          return
        when 0x1A  # Alt-W -> toggle line wrapping
          toggle_wrap
          return
        when 0x07  # Alt-D -> open Debug menu
          open_menu(:debug) if dbg_menu_visible?
          return
        when 0x19  # Alt-V -> open View menu (the display toggles)
          open_menu(:view)
          return
        when 0x0E  # Alt-K -> the key list
          @keys_open ? close_keys_list : open_keys_list
          return
        end
      end

      # Navigation keys (USB HID keycodes) - handle immediately + start repeat
      case keycode
      when 79, 80, 81, 82, 75, 78, 74, 77, 76
        # Right, Left, Down, Up, PageUp, PageDown, Home, End, Delete
        # Shift+Arrow extends a selection; bare arrow drops it.
        # Forward-Delete (76) ignores Shift (no selection extend semantic).
        if ev_shift?(ev) && keycode != 76
          begin_selection_if_needed
        else
          clear_selection
        end
        execute_key_action(keycode)
        @held_keycode = keycode
        @hold_frames = 0
        return
      end

      # Modifier keys - ignore
      return if keycode >= 224 && keycode <= 231

      # Printable / control characters. Handled inline: the old path queued the
      # character for a separate editor Task that polled with sleep_ms, which
      # added up to one 33ms sleep before the edit even happened.
      handle_key(character) if character > 0
    end

    if ev[:type] == :key_up
      keycode = ev[:keycode] || 0
      # Stop key repeat when key is released
      if keycode == @held_keycode
        @held_keycode = nil
        @hold_frames = 0
      end
    end
  end

  def on_update
    # Poll the debugger for stopped/resumed/exited events (non-blocking).
    dbg_poll

    # Key repeat: if a navigation key is held down, repeat the action
    if @held_keycode
      @hold_frames += 1
      if @hold_frames >= KEY_REPEAT_DELAY
        if (@hold_frames - KEY_REPEAT_DELAY) % KEY_REPEAT_RATE == 0
          execute_key_action(@held_keycode)
        end
      end
    end

    # Tick down the status message and give the line back when it expires.
    if @status_msg_frames > 0
      @status_msg_frames -= 1
      clear_status_message if @status_msg_frames == 0
    end

    redraw_if_dirty
    @frame_ms
  end

  def on_destroy
    # Release the debug session so a crash/exit never leaves a target parked.
    dbg_shutdown
    Log.info("Editor destroyed")
  end
end

Log.info("EditorApp.new")
begin
  app = EditorApp.new
  Log.info("EditorApp created successfully")
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
