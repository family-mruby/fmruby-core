# Text Editor Application
# MS-DOS style text editor with menu bar and status line

class EditorApp < FmrbApp
  # Colors - Light pink theme
  BG_COLOR      = FmrbGfx.rgb_to_332(255, 230, 240)  # Nearly white pink
  TEXT_COLOR     = FmrbGfx.rgb_to_332(0, 0, 0)        # Black text
  MENU_BG       = FmrbGfx.rgb_to_332(100, 60, 100)    # Dark purple menu bar
  MENU_TEXT     = FmrbGfx.rgb_to_332(255, 255, 255)   # White menu text
  MENU_KEY      = FmrbGfx.rgb_to_332(255, 255, 0)     # Yellow hotkey
  STATUS_BG     = FmrbGfx.rgb_to_332(40, 40, 60)      # Dark gray status line
  STATUS_TEXT   = FmrbGfx.rgb_to_332(255, 255, 255)   # White status text
  STATUS_OK_BG  = FmrbGfx.rgb_to_332(0, 160, 0)       # Green flash for save success
  STATUS_OK_TEXT = FmrbGfx.rgb_to_332(255, 255, 255)
  CURSOR_COLOR  = FmrbGfx.rgb_to_332(0, 0, 200)       # Blue cursor

  # Syntax highlight colors (for light background)
  HL_COLORS = [
    FmrbGfx.rgb_to_332(0, 0, 0),        # 0: default    - black
    FmrbGfx.rgb_to_332(180, 0, 0),      # 1: keyword    - dark red
    FmrbGfx.rgb_to_332(0, 120, 0),      # 2: string     - dark green
    FmrbGfx.rgb_to_332(120, 120, 120),  # 3: comment    - gray
    FmrbGfx.rgb_to_332(160, 100, 0),    # 4: number     - brown
    FmrbGfx.rgb_to_332(140, 0, 140),    # 5: symbol     - dark magenta
    FmrbGfx.rgb_to_332(0, 100, 140),    # 6: constant   - dark cyan
    FmrbGfx.rgb_to_332(180, 0, 60),     # 7: variable   - crimson
    FmrbGfx.rgb_to_332(0, 0, 180),      # 8: method     - dark blue
  ]

  CHAR_W = 6
  CHAR_H = 8
  TAB_SIZE = 2

  # Menu dropdown common style
  DROPDOWN_BG = FmrbGfx.rgb_to_332(255, 255, 255)
  DROPDOWN_TEXT = FmrbGfx.rgb_to_332(0, 0, 0)
  DROPDOWN_SEL_BG = FmrbGfx.rgb_to_332(100, 60, 100)
  DROPDOWN_SEL_TEXT = FmrbGfx.rgb_to_332(255, 255, 255)
  DROPDOWN_ITEM_H = 10

  # Per-menu config: items + scancode hotkeys + dropdown pixel width.
  # Hotkey scancodes pick a distinguishing letter per item (DOS-Edit style).
  MENU_FILE_ITEMS    = ["Open", "Save", "Save as", "Exit"]
  MENU_FILE_HOTKEYS  = [0x12, 0x16, 0x04, 0x1B]  # O, S, A, X
  MENU_FILE_W        = 54

  MENU_EDIT_ITEMS    = ["Cut", "Copy", "Paste", "Select All"]
  MENU_EDIT_HOTKEYS  = [0x17, 0x06, 0x13, 0x04]  # T (cuT), C, P, A
  MENU_EDIT_W        = 72

  # Selection / clipboard colors
  SEL_BG = FmrbGfx.rgb_to_332(180, 200, 255)  # Light blue selection

  # Key repeat timing (in frames, ~33ms each)
  KEY_REPEAT_DELAY = 12  # ~400ms before repeat starts
  KEY_REPEAT_RATE = 3    # ~100ms between repeats

  # Re-tokenize for syntax highlight after this many idle frames (~1s)
  HL_DEBOUNCE_FRAMES = 30
  SAVE_OK_FRAMES = 60     # ~2s flash of "Saved" on the status line

  # Auto-disable syntax highlight when loaded file exceeds this size
  HL_AUTO_LIMIT_BYTES = 1024

  # ---- Input latency instrumentation (doc/editor_serious_mode/plan.md) ----
  # Time from a key event to the present that shows its effect. Always on: one
  # Machine.uptime_us per key and per redraw, integer accumulators, and a single
  # log line every LAT_REPORT_N samples -- so the measurement itself neither
  # allocates in the hot path nor changes what it measures.
  LAT_REPORT_N = 100
  LAT_SLOW_US = 25_000   # "over 25ms" bucket, the target says zero of these
  LAT_BUCKET_US = 5_000  # histogram resolution
  LAT_BUCKETS = 10       # last bucket is the >= 50ms overflow

  # Search dialog
  SEARCH_QUERY_MAX = 32
  SEARCH_NOT_FOUND = FmrbGfx.rgb_to_332(180, 0, 0)

  # ---- On-device debugger (Phase E2, design doc sec 4.5) ----
  # Bottom split pane shown only during a debug session; edit area shrinks.
  DBG_PANE_ROWS = 8
  DBG_PANE_BG   = FmrbGfx.rgb_to_332(30, 30, 45)
  DBG_HDR_BG    = FmrbGfx.rgb_to_332(60, 60, 90)
  DBG_TEXT      = FmrbGfx.rgb_to_332(220, 220, 220)
  DBG_HDR_TEXT  = FmrbGfx.rgb_to_332(255, 255, 120)
  DBG_SEL_BG    = FmrbGfx.rgb_to_332(80, 80, 130)
  # Breakpoint / current-stop indicators. In debug mode a left gutter column
  # holds a red dot per breakpoint line (VSCode style); the current-stop line
  # keeps a full-row highlight.
  BP_MARK   = FmrbGfx.rgb_to_332(220, 0, 0)     # red breakpoint dot
  STOP_BG   = FmrbGfx.rgb_to_332(250, 240, 140) # current-line row highlight
  STOP_MARK = FmrbGfx.rgb_to_332(230, 160, 0)   # current-line gutter marker
  GUTTER_W  = 8                                 # gutter width in px (debug mode)
  GUTTER_BG = FmrbGfx.rgb_to_332(210, 195, 205) # gutter column background
  # ps filter values (fmrb_app.h): general mruby apps that are running.
  APP_TYPE_USER  = 2   # APP_TYPE_USER_APP
  VM_TYPE_MRUBY  = 0   # FMRB_VM_TYPE_MRUBY
  PROC_RUNNING   = 2   # PROC_STATE_RUNNING
  # Debug function-key scancodes (USB HID Usage IDs).
  SC_F4 = 0x3D; SC_F5 = 0x3E; SC_F6 = 0x3F; SC_F7 = 0x40
  SC_F8 = 0x41; SC_F9 = 0x42; SC_F10 = 0x43; SC_F11 = 0x44

  def initialize
    super()
    @lines = [""]       # Document lines
    @cx = 0             # Cursor column in current line
    @cy = 0             # Cursor line index
    @scroll_y = 0       # First visible line index
    @scroll_x = 0       # Horizontal scroll offset (columns)
    @need_redraw = true
    @input_buffer = []
    @frame_ms = 33
    @modified = false
    @current_file = nil
    @active_menu = nil   # :file, :edit, or nil when no dropdown is open
    @menu_idx = 0
    @pending_file_op = nil  # :open or :save
    # Selection (anchor side; cursor side is the moving end). nil = no selection.
    @sel_anchor_x = nil
    @sel_anchor_y = nil
    # Clipboard for Cut/Copy/Paste. May contain newlines.
    @clipboard = ""
    @highlight_map = nil
    @highlight_dirty = true
    @hl_idle_frames = HL_DEBOUNCE_FRAMES  # Allow immediate tokenize on first draw
    @hl_enabled = true
    @line_offsets = nil
    # Key repeat state
    @held_keycode = nil
    @hold_frames = 0
    # Transient status badge: counts down per on_update tick. @status_label
    # picks the text so Run can share the indicator with Save.
    @save_ok_frames = 0
    @status_label = nil
    # ---- Run (F5) ----
    # pid of the app the last RUN started, so the next RUN replaces it. The
    # kernel reports it back in "run_result"; nil when nothing is running.
    @run_pid = nil
    # Set when RUN had to ask for a file name first: run once the save lands.
    @run_after_save = false
    # Modal "save before quit?" dialog raised by Ctrl-X when @modified.
    @quit_dialog_open = false
    # Modal Find dialog (Alt-S / Search menu / F3 for find next).
    @search_open = false
    @search_query = ""
    @search_last = ""
    @search_status = ""
    # ---- Debugger session state (Phase E2) ----
    @dbg_active = false       # session held (acquired + attached)
    @dbg_pid = nil            # attached target pid
    @dbg_stopped = false      # target currently parked
    @dbg_stop_file = nil      # file/line of the current stop
    @dbg_stop_line = nil
    @dbg_frames = []          # last stack_trace
    @dbg_vars = []            # last frame_vars for @dbg_frame_idx
    @dbg_frame_idx = 0
    @dbg_pane = :stack        # :stack or :vars
    @dbg_msg = ""             # transient status string
    @gutter_w = 0             # breakpoint gutter width (set by recompute_layout)
    # Breakpoints: { path => { line(1-based) => bp_id_or_nil } }.
    @bp = {}
    # Modal attach-target picker.
    @target_picker_open = false
    @target_list = []
    @target_idx = 0
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
    recompute_layout
    @need_redraw = true

    app_self = self
    @editor_task = Task.new(name: "editor_task", priority: 100) do
      app_self.editor_loop
    end
  end

  # Coming back from a fullscreen park (Ctrl+Tab) or from another app's
  # fullscreen: the canvas was hidden, so repaint everything once.
  def on_resume
    @need_redraw = true
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
    if @dbg_active
      @dbg_pane_h = DBG_PANE_ROWS * CHAR_H
      @dbg_pane_y = @status_y - @dbg_pane_h - 1
      @edit_height = @dbg_pane_y - @edit_y
    else
      @dbg_pane_h = 0
      @dbg_pane_y = @status_y
      @edit_height = @status_y - @edit_y
    end
    # Left gutter for breakpoint dots is only present during a debug session, so
    # normal editing keeps the full width.
    @gutter_w = @dbg_active ? GUTTER_W : 0
    @edit_cols = (@user_area_width - 2 - @gutter_w) / CHAR_W
    @edit_rows = @edit_height / CHAR_H
  end

  def editor_loop
    while @running
      ch = getch
      break if ch.nil?
      handle_key(ch)
    end
  end

  def getch
    while @input_buffer.empty? && @running
      sleep_ms @frame_ms
    end
    return nil unless @running
    @input_buffer.shift
  end

  # ---- Drawing ----

  def draw_menu_bar
    y = @menu_y
    # +1 covers the 1px separator row between menu bar and edit area
    @gfx.fill_rect(@user_area_x0, y, @user_area_width, CHAR_H + 1, MENU_BG)

    x = @user_area_x0 + 2
    @menu_file_x = x
    draw_menu_item(x, y, "F", "ile")
    x += 6 * CHAR_W
    @menu_edit_x = x
    draw_menu_item(x, y, "E", "dit")
    x += 6 * CHAR_W
    @menu_search_x = x
    draw_menu_item(x, y, "S", "earch")
    x += 7 * CHAR_W  # "Search" is 6 chars, leave 1-char gap
    @menu_run_x = x
    draw_menu_item(x, y, "R", "un")
    x += 4 * CHAR_W  # "Run" is 3 chars + 1-char gap
    @menu_hilight_x = x
    # Trailing "*" marks enabled state, space marks disabled
    draw_menu_item(x, y, "H", @hl_enabled ? "ilight*" : "ilight ")
    x += 9 * CHAR_W  # past "Hilight*" (8 chars) + 1-char gap
    @menu_debug_x = x
    # Trailing "*" marks an active debug session.
    draw_menu_item(x, y, "D", @dbg_active ? "ebug*" : "ebug ")
  end

  def draw_menu_item(x, y, key_char, rest)
    @gfx.draw_text(x, y, key_char, MENU_KEY, MENU_BG)
    @gfx.draw_text(x + CHAR_W, y, rest, MENU_TEXT, MENU_BG)
  end

  def draw_status_line
    y = @status_y
    @gfx.fill_rect(@user_area_x0, y, @user_area_width, CHAR_H, STATUS_BG)

    line_num = @cy + 1
    col_num = @cx + 1

    fname = @current_file ? @current_file.split("/").last : "[New]"
    status = " #{fname}  Ln #{line_num}, Col #{col_num}"
    status += " *" if @modified
    if !@hl_enabled
      status += "  [HL off]"
    elsif @highlight_map.nil? && @lines.any? { |l| !l.empty? }
      status += "  [No HL]"
    end

    @gfx.draw_text(@user_area_x0 + 2, y, status, STATUS_TEXT, STATUS_BG)

    # Right-aligned green "Saved" badge that fades after SAVE_OK_FRAMES ticks.
    if @save_ok_frames > 0
      label = @status_label || " Saved "
      bw = label.length * CHAR_W
      bx = @user_area_x0 + @user_area_width - bw - 2
      @gfx.fill_rect(bx, y, bw, CHAR_H, STATUS_OK_BG)
      @gfx.draw_text(bx, y, label, STATUS_OK_TEXT, STATUS_OK_BG)
    end
  end

  def update_highlight
    return unless @highlight_dirty
    # Debounce: skip tokenize while user is actively editing
    return if @hl_idle_frames < HL_DEBOUNCE_FRAMES
    @highlight_dirty = false

    unless @hl_enabled
      @highlight_map = nil
      @line_offsets = nil
      return
    end

    source = @lines.join("\n")
    begin
      @highlight_map = SyntaxHighlight.tokenize(source)
    rescue => e
      Log.warn("SyntaxHighlight.tokenize failed: #{e.message}")
      @highlight_map = nil
    end

    @line_offsets = []
    offset = 0
    li = 0
    ln = @lines.length
    while li < ln
      @line_offsets << offset
      offset += @lines[li].length + 1  # +1 for newline
      li += 1
    end
  end

  def toggle_highlight
    @hl_enabled = !@hl_enabled
    @highlight_dirty = true
    @hl_idle_frames = HL_DEBOUNCE_FRAMES  # Tokenize immediately
    @need_redraw = true
    Log.info("Highlight #{@hl_enabled ? 'ON' : 'OFF'}")
  end

  # Mark buffer as edited; restarts the highlight debounce timer so we
  # re-tokenize only after the user stops typing.
  def mark_edited
    @modified = true
    @highlight_dirty = true
    @hl_idle_frames = 0
  end

  def draw_edit_area
    @gfx.fill_rect(@user_area_x0, @edit_y,
                    @user_area_width, @edit_height, BG_COLOR)

    # Breakpoint gutter column (debug mode only).
    if @gutter_w > 0
      @gfx.fill_rect(@user_area_x0, @edit_y, @gutter_w, @edit_height, GUTTER_BG)
    end

    update_highlight

    sel_range = selection_range  # [sx, sy, ex, ey] or nil

    row = -1
    while (row += 1) < @edit_rows
      line_idx = @scroll_y + row
      break if line_idx >= @lines.length

      full_line = @lines[line_idx] || ""
      # Apply horizontal scroll
      text = @scroll_x > 0 ? (full_line[@scroll_x..-1] || "") : full_line
      visible_len = text.length > @edit_cols ? @edit_cols : text.length

      x = @user_area_x0 + 1 + @gutter_w
      y = @edit_y + row * CHAR_H

      # Current-stop line gets a full-row highlight (text area only, so the
      # gutter dot stays visible). Breakpoints are shown as a gutter dot.
      line_bg = line_background(line_idx)
      if line_bg != BG_COLOR
        @gfx.fill_rect(@user_area_x0 + @gutter_w, y,
                       @user_area_width - @gutter_w, CHAR_H, line_bg)
      end
      draw_gutter_marker(line_idx, y) if @gutter_w > 0

      # Selection background goes down before the text so we can overpaint
      # the selected glyphs with SEL_BG as their background.
      sel_vstart = nil
      sel_vend = nil
      if sel_range
        sel = line_selection_cols(line_idx, full_line.length)
        if sel
          vstart = sel[0] - @scroll_x
          vend   = sel[1] - @scroll_x
          vstart = 0 if vstart < 0
          vend = visible_len if vend > visible_len
          if vstart < vend
            @gfx.fill_rect(x + vstart * CHAR_W, y,
                           (vend - vstart) * CHAR_W, CHAR_H, SEL_BG)
            sel_vstart = vstart
            sel_vend = vend
          end
        end
        # Multi-line selection: fill from end of visible text to the right
        # edit margin so the wrapped newline is visible.
        if line_idx >= sel_range[1] && line_idx < sel_range[3]
          fill_x0 = x + visible_len * CHAR_W
          fill_x1 = x + @edit_cols * CHAR_W
          if fill_x0 < fill_x1
            @gfx.fill_rect(fill_x0, y, fill_x1 - fill_x0, CHAR_H, SEL_BG)
          end
        end
      end

      next if visible_len == 0

      # Use plain render while highlight is stale (debouncing during edit)
      # to avoid color misalignment against the changed source. Tinted
      # (breakpoint/stop) lines always render plain over their background.
      if line_bg == BG_COLOR && !@highlight_dirty && @highlight_map && @line_offsets
        draw_highlighted_line(x, y, text, visible_len, @line_offsets[line_idx] + @scroll_x)
      else
        @gfx.draw_text(x, y, text[0, visible_len], TEXT_COLOR, line_bg)
      end

      # Overpaint the selected substring so its glyph background matches.
      if sel_vstart
        sel_text = text[sel_vstart, sel_vend - sel_vstart]
        @gfx.draw_text(x + sel_vstart * CHAR_W, y, sel_text,
                       TEXT_COLOR, SEL_BG)
      end
    end

    draw_cursor
  end

  def draw_highlighted_line(x, y, text, visible_len, offset)
    i = 0
    while i < visible_len
      cat = @highlight_map.getbyte(offset + i) || 0
      color = HL_COLORS[cat] || TEXT_COLOR

      # Gather consecutive characters with the same category
      j = i + 1
      while j < visible_len
        next_cat = @highlight_map.getbyte(offset + j) || 0
        break if next_cat != cat
        j += 1
      end

      chunk = text[i, j - i]
      @gfx.draw_text(x + i * CHAR_W, y, chunk, color, BG_COLOR)
      i = j
    end
  end

  def draw_cursor
    # Cursor position relative to scroll
    screen_row = @cy - @scroll_y
    return if screen_row < 0 || screen_row >= @edit_rows

    screen_col = @cx - @scroll_x
    return if screen_col < 0 || screen_col >= @edit_cols

    x = @user_area_x0 + 1 + @gutter_w + screen_col * CHAR_W
    y = @edit_y + screen_row * CHAR_H

    # Draw block cursor
    @gfx.fill_rect(x, y, CHAR_W, CHAR_H, CURSOR_COLOR)

    # Draw character under cursor in contrasting color
    line = @lines[@cy] || ""
    if @cx < line.length
      @gfx.draw_text(x, y, line[@cx], BG_COLOR, CURSOR_COLOR)
    end
  end

  # ---- Latency instrumentation ----

  # Stamp the arrival of a key. Only the first key of a burst is stamped, so a
  # sample is "oldest key not yet on screen -> present": the number that says
  # whether the editor kept up, not the flattering last-key-only one.
  def lat_key_arrived
    @lat_t0 = Machine.uptime_us if @lat_t0.nil?
  end

  # One sample, taken right after redraw_all. +t_start+ is the timestamp from
  # just before it, so the draw cost is measured with the same two clock reads.
  # A key that produces no redraw at all leaves its stamp armed and lands in the
  # next sample; those are rare and always on the pessimistic side.
  def lat_sample(t_start)
    t_end = Machine.uptime_us
    draw_us = t_end - t_start
    @draw_n += 1
    @draw_sum += draw_us
    @draw_max = draw_us if draw_us > @draw_max

    t0 = @lat_t0
    return if t0.nil?
    @lat_t0 = nil
    lat_us = t_end - t0
    @lat_n += 1
    @lat_sum += lat_us
    @lat_max = lat_us if lat_us > @lat_max
    @lat_slow += 1 if lat_us > LAT_SLOW_US
    b = lat_us / LAT_BUCKET_US
    b = LAT_BUCKETS if b > LAT_BUCKETS
    @lat_hist[b] += 1
    return if @lat_n < LAT_REPORT_N
    lat_report
  end

  # p99 estimate: the upper edge (in ms) of the bucket where the cumulative
  # count reaches 99%. Coarse by design (5ms buckets, no per-sample storage).
  def lat_p99_ms
    need = (@lat_n * 99 + 99) / 100
    acc = 0
    b = 0
    while b <= LAT_BUCKETS
      acc += @lat_hist[b]
      return (b + 1) * LAT_BUCKET_US / 1000 if acc >= need
      b += 1
    end
    (LAT_BUCKETS + 1) * LAT_BUCKET_US / 1000
  end

  def lat_report
    hist = ""
    b = 0
    while b <= LAT_BUCKETS
      hist += "," if b > 0
      hist += @lat_hist[b].to_s
      @lat_hist[b] = 0
      b += 1
    end
    msg = "edit_lat: n=#{@lat_n} mean=#{@lat_sum / @lat_n}us max=#{@lat_max}us"
    msg += " p99<=#{lat_p99_ms}ms over25ms=#{@lat_slow}"
    msg += " draw_mean=#{@draw_sum / @draw_n}us draw_max=#{@draw_max}us"
    msg += " hist5ms=#{hist} rows=#{@edit_rows} hl=#{@hl_enabled ? 1 : 0}"
    Log.info(msg)
    @lat_n = 0
    @lat_sum = 0
    @lat_max = 0
    @lat_slow = 0
    @draw_n = 0
    @draw_sum = 0
    @draw_max = 0
  end

  def redraw_all
    draw_menu_bar
    draw_edit_area
    draw_debug_pane if @dbg_active
    draw_status_line
    draw_active_menu if @active_menu
    draw_target_picker if @target_picker_open
    draw_search_dialog if @search_open
    draw_quit_dialog if @quit_dialog_open
    # Re-issue the title-bar / border GfxBlock with current window size so the
    # frame survives canvas resize (the block is bound to @window_width / @window_height kwargs).
    draw_window_frame
    @gfx.present
  end

  # ---- Quit-confirm dialog ----

  QUIT_DLG_BG     = FmrbGfx.rgb_to_332(255, 255, 255)
  QUIT_DLG_BORDER = FmrbGfx.rgb_to_332(0, 0, 0)
  QUIT_DLG_TEXT   = FmrbGfx.rgb_to_332(0, 0, 0)
  QUIT_DLG_KEY    = FmrbGfx.rgb_to_332(180, 0, 0)

  def quit_dialog_rect
    w = 26 * CHAR_W + 8
    h = 4 * CHAR_H + 10
    x = @user_area_x0 + (@user_area_width  - w) / 2
    y = @user_area_y0 + (@user_area_height - h) / 2
    [x, y, w, h]
  end

  def draw_quit_dialog
    x, y, w, h = quit_dialog_rect
    @gfx.fill_rect(x, y, w, h, QUIT_DLG_BG)
    @gfx.draw_rect(x, y, w, h, QUIT_DLG_BORDER)
    @gfx.draw_rect(x + 1, y + 1, w - 2, h - 2, QUIT_DLG_BORDER)

    tx = x + 4
    ty = y + 4
    @gfx.draw_text(tx, ty, "Unsaved changes",            QUIT_DLG_TEXT, QUIT_DLG_BG)
    @gfx.draw_text(tx, ty + CHAR_H + 2, "Save before exit?", QUIT_DLG_TEXT, QUIT_DLG_BG)

    by = ty + (CHAR_H + 2) * 2 + 2
    draw_quit_choice(tx,                  by, "Y", "es")
    draw_quit_choice(tx + 8  * CHAR_W,    by, "N", "o")
    draw_quit_choice(tx + 14 * CHAR_W,    by, "C", "ancel/Esc")
  end

  def draw_quit_choice(x, y, key_char, rest)
    @gfx.draw_text(x,            y, "[",       QUIT_DLG_TEXT, QUIT_DLG_BG)
    @gfx.draw_text(x + CHAR_W,   y, key_char,  QUIT_DLG_KEY,  QUIT_DLG_BG)
    @gfx.draw_text(x + 2*CHAR_W, y, "]" + rest, QUIT_DLG_TEXT, QUIT_DLG_BG)
  end

  def handle_quit_dialog_key(character)
    case character
    when 121, 89  # 'y' / 'Y'
      save_file
      stop unless @modified  # Save failed (e.g. no current_file) -> stay open
      @need_redraw = true
    when 110, 78  # 'n' / 'N'
      stop
    when 99, 67, 27  # 'c' / 'C' / Esc
      @quit_dialog_open = false
      @need_redraw = true
    end
  end

  # ---- Search (Find / Find-next) dialog ----

  def search_dialog_rect
    w = (SEARCH_QUERY_MAX + 2) * CHAR_W + 8
    h = 5 * CHAR_H + 14
    x = @user_area_x0 + (@user_area_width  - w) / 2
    y = @user_area_y0 + (@user_area_height - h) / 2
    [x, y, w, h]
  end

  def draw_search_dialog
    x, y, w, h = search_dialog_rect
    @gfx.fill_rect(x, y, w, h, QUIT_DLG_BG)
    @gfx.draw_rect(x, y, w, h, QUIT_DLG_BORDER)
    @gfx.draw_rect(x + 1, y + 1, w - 2, h - 2, QUIT_DLG_BORDER)

    tx = x + 4
    ty = y + 4
    @gfx.draw_text(tx, ty, "Find:", QUIT_DLG_TEXT, QUIT_DLG_BG)

    iy = ty + CHAR_H + 4
    iw = SEARCH_QUERY_MAX * CHAR_W + 2
    @gfx.fill_rect(tx, iy, iw, CHAR_H + 2, BG_COLOR)
    @gfx.draw_rect(tx, iy, iw, CHAR_H + 2, QUIT_DLG_BORDER)
    @gfx.draw_text(tx + 1, iy + 1, @search_query, TEXT_COLOR, BG_COLOR)
    cur_x = tx + 1 + @search_query.length * CHAR_W
    @gfx.fill_rect(cur_x, iy + 1, CHAR_W, CHAR_H, CURSOR_COLOR)

    sy = iy + CHAR_H + 4
    if @search_status && @search_status.length > 0
      @gfx.draw_text(tx, sy, @search_status, SEARCH_NOT_FOUND, QUIT_DLG_BG)
    end

    hy = sy + CHAR_H + 2
    @gfx.draw_text(tx, hy, "[Enter]Find  [F3]Next  [Esc]Cancel",
                   QUIT_DLG_TEXT, QUIT_DLG_BG)
  end

  def open_search_dialog
    @search_open = true
    # Pre-fill with the previous query so the user can re-search quickly.
    @search_query = @search_last.dup
    # Pre-filled query: Enter acts as Find-Next.
    # User-modified query: Enter searches from the current cursor position.
    @search_query_dirty = false
    @search_status = ""
    @need_redraw = true
  end

  def close_search_dialog
    @search_open = false
    @search_status = ""
    @need_redraw = true
  end

  def handle_search_dialog_key(ev)
    keycode = ev[:keycode] || 0
    character = ev[:character] || 0

    case keycode
    when 40, 88  # Enter / Keypad-Enter
      if @search_query.length == 0
        close_search_dialog
      else
        @search_last = @search_query
        # Unchanged pre-filled query advances; edited query searches from cursor.
        after_cursor = !@search_query_dirty
        if find_from_cursor(@search_query, after_cursor)
          close_search_dialog
        else
          @search_status = "Not found"
          @need_redraw = true
        end
      end
      return
    when 41  # ESC
      close_search_dialog
      return
    end

    case character
    when 8, 127  # Backspace / Delete
      if @search_query.length > 0
        @search_query = @search_query[0, @search_query.length - 1]
        @search_query_dirty = true
        @search_status = ""
        @need_redraw = true
      end
    when 32..126  # Printable
      if @search_query.length < SEARCH_QUERY_MAX
        @search_query += character.chr
        @search_query_dirty = true
        @search_status = ""
        @need_redraw = true
      end
    end
  end

  # Find +query+ starting from the cursor, wrapping to the top once.
  # When +after_cursor+ is true (F3 / Find Next), skip the character at the
  # cursor so we advance past the previous hit. Returns true on match.
  def find_from_cursor(query, after_cursor)
    return false if query.nil? || query.length == 0

    abs_cursor = 0
    i = 0
    while i < @cy
      abs_cursor += (@lines[i] || "").length + 1  # +1 for '\n'
      i += 1
    end
    abs_cursor += @cx

    full = @lines.join("\n")
    start_at = after_cursor ? abs_cursor + 1 : abs_cursor
    start_at = full.length if start_at > full.length

    idx = full.index(query, start_at)
    if idx.nil?
      # Wrap: only accept matches strictly before the cursor.
      idx = full.index(query)
      return false if idx.nil? || idx >= abs_cursor
    end

    pos = 0
    ly = 0
    while ly < @lines.length
      line_len = (@lines[ly] || "").length
      if idx <= pos + line_len
        @cy = ly
        @cx = idx - pos
        ensure_cursor_visible
        @need_redraw = true
        return true
      end
      pos += line_len + 1
      ly += 1
    end
    false
  end

  def find_next
    return false if @search_last.length == 0
    found = find_from_cursor(@search_last, true)
    Log.info("Find: not found '#{@search_last}'") unless found
    found
  end

  # ---- Scrolling ----

  def ensure_cursor_visible
    # Vertical
    if @cy < @scroll_y
      @scroll_y = @cy
    elsif @cy >= @scroll_y + @edit_rows
      @scroll_y = @cy - @edit_rows + 1
    end
    # Horizontal
    if @cx < @scroll_x
      @scroll_x = @cx
    elsif @cx >= @scroll_x + @edit_cols
      @scroll_x = @cx - @edit_cols + 1
    end
  end

  # ---- Key handling ----

  def handle_key(ch)
    case ch
    when 10, 13  # Enter
      handle_enter
    when 8       # Backspace
      handle_backspace
    when 127     # Delete (some terminals)
      handle_delete
    when 9       # Tab
      ti = 0
      while ti < TAB_SIZE
        insert_char(' ')
        ti += 1
      end
    when 32..126 # Printable
      insert_char(ch.chr)
    end
  end

  def insert_char(c)
    delete_selection if has_selection?
    line = @lines[@cy] || ""
    @lines[@cy] = line[0, @cx].to_s + c + line[@cx..-1].to_s
    @cx += 1
    mark_edited
    ensure_cursor_visible
    @need_redraw = true
  end

  def handle_enter
    delete_selection if has_selection?
    line = @lines[@cy] || ""
    # Split line at cursor
    left = line[0, @cx].to_s
    right = line[@cx..-1].to_s
    @lines[@cy] = left
    @lines.insert(@cy + 1, right)
    @cy += 1
    @cx = 0
    mark_edited
    ensure_cursor_visible
    @need_redraw = true
  end

  def handle_backspace
    return if delete_selection
    if @cx > 0
      line = @lines[@cy] || ""
      @lines[@cy] = line[0, @cx - 1].to_s + line[@cx..-1].to_s
      @cx -= 1
      mark_edited
      ensure_cursor_visible
      @need_redraw = true
    elsif @cy > 0
      # Merge with previous line
      prev_len = @lines[@cy - 1].length
      @lines[@cy - 1] += @lines[@cy]
      @lines.delete_at(@cy)
      @cy -= 1
      @cx = prev_len
      mark_edited
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def handle_delete
    return if delete_selection
    line = @lines[@cy] || ""
    if @cx < line.length
      @lines[@cy] = line[0, @cx].to_s + line[@cx + 1..-1].to_s
      mark_edited
      @need_redraw = true
    elsif @cy < @lines.length - 1
      # Merge next line
      @lines[@cy] += @lines[@cy + 1]
      @lines.delete_at(@cy + 1)
      mark_edited
      @need_redraw = true
    end
  end

  # ---- Navigation (arrow keys, page up/down, home/end) ----

  def execute_key_action(keycode)
    case keycode
    when 79 then move_right
    when 80 then move_left
    when 81 then move_down
    when 82 then move_up
    when 75 then page_up
    when 78 then page_down
    when 74 then move_home
    when 77 then move_end
    when 76 then handle_delete  # Delete forward key
    end
  end

  def move_up
    if @cy > 0
      @cy -= 1
      clamp_cx
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def move_down
    if @cy < @lines.length - 1
      @cy += 1
      clamp_cx
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def move_left
    if @cx > 0
      @cx -= 1
      ensure_cursor_visible
      @need_redraw = true
    elsif @cy > 0
      @cy -= 1
      @cx = (@lines[@cy] || "").length
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def move_right
    line = @lines[@cy] || ""
    if @cx < line.length
      @cx += 1
      ensure_cursor_visible
      @need_redraw = true
    elsif @cy < @lines.length - 1
      @cy += 1
      @cx = 0
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def page_up
    @cy -= @edit_rows
    @cy = 0 if @cy < 0
    clamp_cx
    ensure_cursor_visible
    @need_redraw = true
  end

  def page_down
    @cy += @edit_rows
    max = @lines.length - 1
    @cy = max if @cy > max
    clamp_cx
    ensure_cursor_visible
    @need_redraw = true
  end

  def move_home
    @cx = 0
    ensure_cursor_visible
    @need_redraw = true
  end

  def move_end
    @cx = (@lines[@cy] || "").length
    ensure_cursor_visible
    @need_redraw = true
  end

  def clamp_cx
    line = @lines[@cy] || ""
    @cx = line.length if @cx > line.length
  end

  # ---- Menu dropdown (File / Edit) ----

  def menu_items
    case @active_menu
    when :file then MENU_FILE_ITEMS
    when :edit then MENU_EDIT_ITEMS
    when :debug then debug_menu_items
    end
  end

  # Debug dropdown adapts to whether a session is active.
  def debug_menu_items
    if @dbg_active
      ["Continue", "Step Over", "Step In", "Step Out", "Pause", "Toggle BP", "Detach"]
    else
      ["Attach...", "Toggle BP"]
    end
  end

  def menu_hotkeys
    case @active_menu
    when :file then MENU_FILE_HOTKEYS
    when :edit then MENU_EDIT_HOTKEYS
    end
  end

  def menu_width
    case @active_menu
    when :file then MENU_FILE_W
    when :edit then MENU_EDIT_W
    when :debug then 66
    end
  end

  # Top-left of the dropdown panel for the active menu (just below its label).
  # The Debug label sits at the right end of the menu bar, so its dropdown is
  # right-anchored to stay inside the (narrow) editor window.
  def menu_origin
    case @active_menu
    when :file then [@menu_file_x, @menu_y + CHAR_H]
    when :edit then [@menu_edit_x, @menu_y + CHAR_H]
    when :debug
      dx = @user_area_x0 + @user_area_width - menu_width - 2
      dx = @menu_debug_x if dx > @menu_debug_x
      [dx, @menu_y + CHAR_H]
    else            [0, 0]
    end
  end

  def draw_active_menu
    return unless @active_menu
    items = menu_items
    w = menu_width
    x, y = menu_origin
    h = DROPDOWN_ITEM_H * items.size + 2

    @gfx.fill_rect(x, y, w, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, w, h, 0x60)

    items.each_with_index do |item, i|
      item_y = y + 1 + i * DROPDOWN_ITEM_H
      if i == @menu_idx
        @gfx.fill_rect(x + 1, item_y, w - 2, DROPDOWN_ITEM_H, DROPDOWN_SEL_BG)
        @gfx.draw_text(x + 4, item_y + 1, item,
                       DROPDOWN_SEL_TEXT, DROPDOWN_SEL_BG)
      else
        @gfx.draw_text(x + 4, item_y + 1, item, DROPDOWN_TEXT, DROPDOWN_BG)
      end
    end
  end

  def open_menu(kind)
    @active_menu = kind
    @menu_idx = 0
    render_with_dropdown
  end

  # Re-render the screen with the dropdown overlay (used on open / nav move).
  def render_with_dropdown
    redraw_all
    draw_active_menu
    @gfx.present
  end

  def close_menu
    @active_menu = nil
    @need_redraw = true
  end

  # Modal key handling while a menu dropdown is open.
  # keycode/scancode are USB HID Usage IDs (uniform across ESP32 / SDL2).
  def handle_menu_key(ev)
    keycode = ev[:keycode] || 0
    scancode = ev[:scancode] || 0
    items = menu_items

    # Enter / ESC by scancode (HID Usage ID) so this works on the Linux sim too,
    # where ev[:keycode] carries the SDL keysym (13/27) instead of 40/41.
    if scancode == 40 || scancode == 88  # Enter / Keypad-Enter
      activate_menu_item(@menu_idx)
      return
    elsif scancode == 41  # ESC
      close_menu
      return
    end

    case keycode
    when 82  # Up
      n = items.size
      @menu_idx = (@menu_idx + n - 1) % n
      render_with_dropdown
      return
    when 81  # Down
      @menu_idx = (@menu_idx + 1) % items.size
      render_with_dropdown
      return
    end

    # Per-item letter hotkey (scancode-based; case-insensitive by nature).
    # The Debug menu has no letter hotkeys (menu_hotkeys is nil there).
    hk = menu_hotkeys
    if hk
      idx = hk.index(scancode)
      activate_menu_item(idx) if idx
    end
  end

  def handle_menu_click(x, y)
    dx, dy = menu_origin
    w = menu_width
    items = menu_items
    if x >= dx && x < dx + w && y >= dy
      idx = (y - dy - 1) / DROPDOWN_ITEM_H
      if idx >= 0 && idx < items.size
        activate_menu_item(idx)
        return
      end
    end
    close_menu
  end

  def activate_menu_item(idx)
    kind = @active_menu
    close_menu
    case kind
    when :file then activate_file_item(idx)
    when :edit then activate_edit_item(idx)
    when :debug then activate_debug_item(idx)
    end
  end

  def activate_debug_item(idx)
    if @dbg_active
      case idx
      when 0 then dbg_continue
      when 1 then dbg_step(:over)
      when 2 then dbg_step(:in)
      when 3 then dbg_step(:out)
      when 4 then dbg_pause
      when 5 then toggle_breakpoint
      when 6 then dbg_detach
      end
    else
      case idx
      when 0 then open_target_picker
      when 1 then toggle_breakpoint
      end
    end
  end

  def activate_file_item(idx)
    case idx
    when 0  # Open
      @pending_file_op = :open
      request_file_select("open")
    when 1  # Save
      save_file
    when 2  # Save as
      @pending_file_op = :save
      request_file_select("save")
    when 3  # Exit
      stop
    end
  end

  def activate_edit_item(idx)
    case idx
    when 0 then cut_selection
    when 1 then copy_selection
    when 2 then paste_clipboard
    when 3 then select_all
    end
  end

  # ---- Selection ----

  def has_selection?
    !@sel_anchor_y.nil?
  end

  # Returns [sx, sy, ex, ey] with start <= end in document order, or nil.
  def selection_range
    return nil unless has_selection?
    if @sel_anchor_y < @cy || (@sel_anchor_y == @cy && @sel_anchor_x <= @cx)
      [@sel_anchor_x, @sel_anchor_y, @cx, @cy]
    else
      [@cx, @cy, @sel_anchor_x, @sel_anchor_y]
    end
  end

  def clear_selection
    return unless has_selection?
    @sel_anchor_x = nil
    @sel_anchor_y = nil
    @need_redraw = true
  end

  def begin_selection_if_needed
    return if has_selection?
    @sel_anchor_x = @cx
    @sel_anchor_y = @cy
  end

  def select_all
    return if @lines.empty?
    @sel_anchor_x = 0
    @sel_anchor_y = 0
    @cy = @lines.length - 1
    @cx = (@lines[@cy] || "").length
    ensure_cursor_visible
    @need_redraw = true
  end

  # Pixel column range of the selection on +line_idx+, accounting for
  # multi-line spans. Returns [start_col, end_col_exclusive] or nil.
  def line_selection_cols(line_idx, line_len)
    range = selection_range
    return nil unless range
    sx, sy, ex, ey = range
    return nil if line_idx < sy || line_idx > ey
    start_col = (line_idx == sy) ? sx : 0
    end_col   = (line_idx == ey) ? ex : line_len
    end_col = line_len if end_col > line_len
    return nil if start_col >= end_col
    [start_col, end_col]
  end

  def selected_text
    range = selection_range
    return "" unless range
    sx, sy, ex, ey = range
    if sy == ey
      (@lines[sy] || "")[sx, ex - sx].to_s
    else
      out = (@lines[sy] || "")[sx..-1].to_s
      ly = sy + 1
      while ly < ey
        out += "\n" + (@lines[ly] || "")
        ly += 1
      end
      out += "\n" + (@lines[ey] || "")[0, ex].to_s
      out
    end
  end

  def delete_selection
    range = selection_range
    return false unless range
    sx, sy, ex, ey = range
    if sy == ey
      line = @lines[sy] || ""
      @lines[sy] = line[0, sx].to_s + line[ex..-1].to_s
    else
      head = (@lines[sy] || "")[0, sx].to_s
      tail = (@lines[ey] || "")[ex..-1].to_s
      @lines[sy] = head + tail
      # Drop intermediate + end lines (delete from sy+1 .. ey inclusive).
      del_count = ey - sy
      del_count.times { @lines.delete_at(sy + 1) }
    end
    @cy = sy
    @cx = sx
    clear_selection
    mark_edited
    ensure_cursor_visible
    @need_redraw = true
    true
  end

  # ---- Clipboard ops ----

  def copy_selection
    return unless has_selection?
    @clipboard = selected_text
    Log.info("Copied #{@clipboard.length} bytes")
  end

  def cut_selection
    return unless has_selection?
    copy_selection
    delete_selection
  end

  def paste_clipboard
    return if @clipboard.nil? || @clipboard.length == 0
    delete_selection if has_selection?
    text = @clipboard
    parts = text.split("\n")
    # mruby's String#split drops trailing empty fields. Restore them so each
    # "\n" becomes its own line break.
    trailing = 0
    ti = text.length - 1
    while ti >= 0 && text[ti] == "\n"
      trailing += 1
      ti -= 1
    end
    trailing.times { parts << "" }
    return if parts.length == 0

    line = @lines[@cy] || ""
    if parts.length == 1
      @lines[@cy] = line[0, @cx].to_s + parts[0] + line[@cx..-1].to_s
      @cx += parts[0].length
    else
      head = line[0, @cx].to_s
      tail = line[@cx..-1].to_s
      @lines[@cy] = head + parts[0]
      i = 1
      while i < parts.length - 1
        @lines.insert(@cy + i, parts[i])
        i += 1
      end
      @lines.insert(@cy + parts.length - 1, parts[-1] + tail)
      @cy += parts.length - 1
      @cx = parts[-1].length
    end
    mark_edited
    ensure_cursor_visible
    @need_redraw = true
  end

  # ---- File operations ----

  def load_file(path)
    begin
      file = File.open(path, "r")
      content = file.read
      file.close

      @lines = content.split("\n")
      @lines = [""] if @lines.empty?
      @cx = 0
      @cy = 0
      @scroll_y = 0
      @scroll_x = 0
      @modified = false
      @highlight_dirty = true
      @hl_idle_frames = HL_DEBOUNCE_FRAMES  # Tokenize immediately on file open
      @hl_enabled = content.bytesize <= HL_AUTO_LIMIT_BYTES
      @current_file = path
      @need_redraw = true
      Log.info("Loaded file: #{path} (#{@lines.length} lines)")
    rescue => e
      Log.error("Failed to load file '#{path}': #{e.class}: #{e.message}")
    end
  end

  def save_file
    unless @current_file
      Log.warn("No file to save (use Save as)")
      return
    end

    begin
      content = @lines.join("\n")
      expected = content.bytesize
      file = File.open(@current_file, "w")
      written = file.write(content)
      file.flush
      file.close
      actual = File.size(@current_file) rescue -1
      if written != expected || actual != expected
        Log.error("Save mismatch for #{@current_file}: expected=#{expected}, written=#{written}, on_disk=#{actual}")
      else
        @modified = false
        flash_status("Saved")
        @need_redraw = true
        Log.info("Saved file: #{@current_file} (#{expected} bytes)")
      end
    rescue => e
      Log.error("Failed to save file: #{e.message}")
    end
  end

  # ---- Debugger (Phase E2, uses ::FMRB::Debug) ----

  def base(p)
    p ? p.split("/").last : ""
  end

  # Only the current-stop line gets a row highlight; breakpoints are shown by
  # the red gutter dot (draw_gutter_marker), not a row tint.
  def line_background(line_idx)
    (@dbg_stopped && stop_on_line?(line_idx)) ? STOP_BG : BG_COLOR
  end

  def stop_on_line?(line_idx)
    return false unless @dbg_stopped && @dbg_stop_file && @dbg_stop_line && @current_file
    base(@dbg_stop_file) == base(@current_file) && @dbg_stop_line == line_idx + 1
  end

  def bp_on_line?(line_idx)
    m = @current_file && @bp[@current_file]
    m && m.has_key?(line_idx + 1)
  end

  # Gutter dot: red circle for a breakpoint line; a yellow ring/dot marks the
  # current-stop line (ring over the red dot when a breakpoint sits there).
  def draw_gutter_marker(line_idx, y)
    cx = @user_area_x0 + @gutter_w / 2
    cyc = y + CHAR_H / 2
    bp = bp_on_line?(line_idx)
    stop = @dbg_stopped && stop_on_line?(line_idx)
    if bp
      @gfx.fill_circle(cx, cyc, 3, BP_MARK)
      @gfx.draw_circle(cx, cyc, 3, STOP_MARK) if stop
    elsif stop
      @gfx.fill_circle(cx, cyc, 2, STOP_MARK)
    end
  end

  def draw_debug_pane
    y0 = @dbg_pane_y
    @gfx.fill_rect(@user_area_x0, y0, @user_area_width, @dbg_pane_h, DBG_PANE_BG)
    # Header row.
    @gfx.fill_rect(@user_area_x0, y0, @user_area_width, CHAR_H, DBG_HDR_BG)
    state = @dbg_stopped ? "stop ln#{@dbg_stop_line}" : "run"
    view = (@dbg_pane == :stack) ? "Stack" : "Vars f#{@dbg_frame_idx}"
    hdr = " #{view} pid=#{@dbg_pid} #{state}"
    hdr += " #{@dbg_msg}" if @dbg_msg.length > 0
    @gfx.draw_text(@user_area_x0 + 2, y0, hdr[0, @edit_cols], DBG_HDR_TEXT, DBG_HDR_BG)

    cy = y0 + CHAR_H
    rows = DBG_PANE_ROWS - 1
    if @dbg_pane == :stack
      if @dbg_frames.empty?
        note = @dbg_stopped ? "(no frames)" : "(running - F9 set BP / F5 continue)"
        @gfx.draw_text(@user_area_x0 + 2, cy, note, DBG_TEXT, DBG_PANE_BG)
      else
        i = 0
        while i < rows && i < @dbg_frames.size
          f = @dbg_frames[i]
          txt = "##{f['idx']} #{f['func']} #{base(f['file'])}:#{f['line']}"
          bg = (i == @dbg_frame_idx) ? DBG_SEL_BG : DBG_PANE_BG
          @gfx.fill_rect(@user_area_x0, cy, @user_area_width, CHAR_H, bg) if bg != DBG_PANE_BG
          @gfx.draw_text(@user_area_x0 + 2, cy, txt[0, @edit_cols], DBG_TEXT, bg)
          cy += CHAR_H
          i += 1
        end
      end
    else
      if @dbg_vars.empty?
        note = @dbg_stopped ? "(no vars)" : "(not stopped)"
        @gfx.draw_text(@user_area_x0 + 2, cy, note, DBG_TEXT, DBG_PANE_BG)
      else
        i = 0
        while i < rows && i < @dbg_vars.size
          v = @dbg_vars[i]
          txt = "#{v['name']} = #{v['value']}"
          txt += " >" if v['ref'] && v['ref'] > 0
          @gfx.draw_text(@user_area_x0 + 2, cy, txt[0, @edit_cols], DBG_TEXT, DBG_PANE_BG)
          cy += CHAR_H
          i += 1
        end
      end
    end
  end

  # ---- Attach-target picker (modal) ----

  def open_target_picker
    # General mruby apps that are running (exclude kernel/system/self).
    @target_list = []
    FmrbApp.ps.each do |a|
      if a[:type] == APP_TYPE_USER && a[:vm_type] == VM_TYPE_MRUBY &&
         a[:state] == PROC_RUNNING && a[:name] != @name
        @target_list << a
      end
    end
    if @target_list.empty?
      @dbg_msg = "no attachable app"
      @need_redraw = true
      return
    end
    @target_idx = 0
    @target_picker_open = true
    @need_redraw = true
  end

  def draw_target_picker
    items = @target_list
    n = items.size
    w = 32 * CHAR_W
    h = (n + 2) * CHAR_H + 8
    x = @user_area_x0 + (@user_area_width - w) / 2
    y = @user_area_y0 + (@user_area_height - h) / 2
    @gfx.fill_rect(x, y, w, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, w, h, 0x60)
    @gfx.draw_text(x + 4, y + 3, "Attach to app:", DROPDOWN_TEXT, DROPDOWN_BG)
    iy = y + 3 + CHAR_H + 2
    items.each_with_index do |a, i|
      label = " #{a[:name]} (pid #{a[:id]})"
      if i == @target_idx
        @gfx.fill_rect(x + 1, iy, w - 2, CHAR_H, DROPDOWN_SEL_BG)
        @gfx.draw_text(x + 4, iy, label, DROPDOWN_SEL_TEXT, DROPDOWN_SEL_BG)
      else
        @gfx.draw_text(x + 4, iy, label, DROPDOWN_TEXT, DROPDOWN_BG)
      end
      iy += CHAR_H
    end
    @gfx.draw_text(x + 4, iy + 2, "[Enter]Attach [Esc]Cancel", DROPDOWN_TEXT, DROPDOWN_BG)
  end

  def handle_target_picker_key(ev)
    n = @target_list.size
    # Navigate by scancode (HID Usage ID); consistent on ESP32 and Linux sim.
    case ev[:scancode] || 0
    when 82  # Up
      @target_idx = (@target_idx + n - 1) % n
      @need_redraw = true
    when 81  # Down
      @target_idx = (@target_idx + 1) % n
      @need_redraw = true
    when 40, 88  # Enter
      a = @target_list[@target_idx]
      @target_picker_open = false
      dbg_attach(a[:id]) if a
    when 41  # ESC
      @target_picker_open = false
      @need_redraw = true
    end
  end

  # ---- Breakpoints ----

  def toggle_breakpoint
    unless @current_file
      @dbg_msg = "open a file first"
      @need_redraw = true
      return
    end
    line = @cy + 1
    path = @current_file
    @bp[path] ||= {}
    if @bp[path].has_key?(line)
      bp_id = @bp[path][line]
      ::FMRB::Debug.bp_clear(@dbg_pid, bp_id) if @dbg_active && @dbg_pid && bp_id
      @bp[path].delete(line)
    else
      bp_id = nil
      bp_id = ::FMRB::Debug.bp_set(@dbg_pid, path, line) if @dbg_active && @dbg_pid
      @bp[path][line] = bp_id
    end
    @need_redraw = true
  end

  # ---- Session control ----

  def dbg_attach(pid)
    unless ::FMRB::Debug.acquire
      @dbg_msg = "busy (remote in use)"
      @need_redraw = true
      return
    end
    unless ::FMRB::Debug.attach(pid)
      ::FMRB::Debug.release
      @dbg_msg = "attach failed"
      @need_redraw = true
      return
    end
    @dbg_pid = pid
    @dbg_active = true
    @dbg_stopped = false
    @dbg_frames = []
    @dbg_vars = []
    @dbg_msg = "attached #{pid}"
    # Open the target's source so breakpoints can be placed before it stops.
    # Skip if the buffer has unsaved edits (don't clobber the user's work).
    src = ::FMRB::Debug.source_file(pid)
    if src && !@modified && (@current_file.nil? || base(@current_file) != base(src))
      load_file(src)
    end
    # Arm any breakpoints already placed on the open file.
    if @current_file && @bp[@current_file]
      @bp[@current_file].keys.each do |line|
        @bp[@current_file][line] = ::FMRB::Debug.bp_set(pid, @current_file, line)
      end
    end
    recompute_layout
    ensure_cursor_visible
    @need_redraw = true
    Log.info("Editor debug: attached pid=#{pid}")
  end

  def dbg_detach
    end_debug_session(true)
    @dbg_msg = "detached"
    @need_redraw = true
  end

  def end_debug_session(do_detach)
    return unless @dbg_active
    ::FMRB::Debug.detach(@dbg_pid) if do_detach && @dbg_pid
    ::FMRB::Debug.release
    @dbg_active = false
    @dbg_stopped = false
    @dbg_pid = nil
    @dbg_frames = []
    @dbg_vars = []
    @dbg_frame_idx = 0
    # Keep breakpoint lines for redisplay but drop the (now invalid) bp_ids.
    @bp.keys.each { |path| @bp[path].keys.each { |line| @bp[path][line] = nil } }
    recompute_layout
    ensure_cursor_visible
    @need_redraw = true
  end

  def dbg_continue
    return unless @dbg_active
    ::FMRB::Debug.continue(@dbg_pid)
  end

  def dbg_pause
    return unless @dbg_active
    ::FMRB::Debug.pause(@dbg_pid)
  end

  def dbg_step(mode)
    return unless @dbg_active && @dbg_stopped
    case mode
    when :in   then ::FMRB::Debug.step_in(@dbg_pid)
    when :over then ::FMRB::Debug.step_over(@dbg_pid)
    when :out  then ::FMRB::Debug.step_out(@dbg_pid)
    end
  end

  def dbg_select_frame(delta)
    return unless @dbg_active && @dbg_stopped
    n = @dbg_frames.size
    return if n == 0
    idx = @dbg_frame_idx + delta
    idx = 0 if idx < 0
    idx = n - 1 if idx >= n
    return if idx == @dbg_frame_idx
    @dbg_frame_idx = idx
    @dbg_vars = ::FMRB::Debug.frame_vars(@dbg_pid, idx) || []
    @need_redraw = true
  end

  def dbg_toggle_pane
    @dbg_pane = (@dbg_pane == :stack) ? :vars : :stack
    @need_redraw = true
  end

  # ---- Event polling (called from on_update, non-blocking) ----

  def poll_debug_events
    return unless @dbg_active
    drained = 0
    while drained < 8
      ev = ::FMRB::Debug.poll_event(0)
      break unless ev
      handle_debug_event(ev)
      drained += 1
    end
  end

  def handle_debug_event(ev)
    return unless ev[:pid] == @dbg_pid
    case ev[:type]
    when :stopped
      @dbg_stopped = true
      @dbg_stop_file = ev[:file]
      @dbg_stop_line = ev[:line]
      @dbg_frame_idx = 0
      focus_stop_location
      refresh_debug_data
      @need_redraw = true
    when :resumed
      @dbg_stopped = false
      @dbg_frames = []
      @dbg_vars = []
      @need_redraw = true
    when :exited
      @dbg_msg = "target exited"
      end_debug_session(false)
    end
  end

  # Fetch stack + vars while the target is parked (responds promptly).
  def refresh_debug_data
    return unless @dbg_active && @dbg_stopped
    @dbg_frames = ::FMRB::Debug.stack_trace(@dbg_pid, 16) || []
    @dbg_vars = ::FMRB::Debug.frame_vars(@dbg_pid, @dbg_frame_idx) || []
  end

  # Open the stopped file (if different) and move the cursor to the stop line.
  def focus_stop_location
    return unless @dbg_stop_file
    if @current_file.nil? || base(@current_file) != base(@dbg_stop_file)
      load_file(@dbg_stop_file)
    end
    if @dbg_stop_line && @dbg_stop_line >= 1
      @cy = @dbg_stop_line - 1
      @cy = @lines.length - 1 if @cy >= @lines.length
      @cy = 0 if @cy < 0
      clamp_cx
      ensure_cursor_visible
    end
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
        flash_status("Run pid #{@run_pid}")
        Log.info("Run started: #{msg["path"]} pid=#{@run_pid}")
      else
        flash_status("Run failed")
        Log.error("Run failed: #{msg["path"]}")
      end
    end
  end

  # ---- Run (F5) ----

  # Run the file in the buffer, replacing what the last RUN started.
  #
  # The kernel does the work: an app cannot spawn another app, so this sends a
  # run request (see FmrbApp#request_run). The kernel stops @run_pid first, and
  # spawning is what hands the keyboard to the new app -- coming back here is
  # then a matter of closing it, or Alt-Tab style window switching for a
  # windowed app. A fullscreen .bas app covers the editor until it ends.
  def run_current_file
    if @current_file.nil?
      # Nothing on disk yet: name it first and run once the save lands.
      @run_after_save = true
      @pending_file_op = :save
      request_file_select("save")
      return
    end
    unless runnable_path?(@current_file)
      flash_status("Run: /app or /home")
      return
    end
    save_file if @modified
    request_run(@current_file, @run_pid)
    flash_status("Running")
  end

  # What the spawner can load: user apps under /app, user files under /home.
  # The kernel enforces this too; checking here just gives a better message.
  def runnable_path?(path)
    path.start_with?("/app/") || path.start_with?("/home/")
  end

  # Show a short right-aligned badge on the status line for ~2s.
  def flash_status(text)
    @status_label = " #{text} "
    @save_ok_frames = SAVE_OK_FRAMES
    @need_redraw = true
  end

  def save_file_as(path)
    @current_file = path
    save_file
  end

  def on_event(ev)
    super(ev)

    if ev[:type] == :mouse_up
      # Open dropdown click handling
      if @active_menu
        handle_menu_click(ev[:x], ev[:y])
        return
      end

      # Menu bar click
      if ev[:y] >= @menu_y && ev[:y] < @menu_y + CHAR_H
        if @menu_file_x && ev[:x] >= @menu_file_x && ev[:x] < @menu_file_x + 4 * CHAR_W
          open_menu(:file)
          return
        end
        if @menu_edit_x && ev[:x] >= @menu_edit_x && ev[:x] < @menu_edit_x + 4 * CHAR_W
          open_menu(:edit)
          return
        end
        if @menu_search_x && ev[:x] >= @menu_search_x && ev[:x] < @menu_search_x + 6 * CHAR_W
          open_search_dialog
          return
        end
        if @menu_run_x && ev[:x] >= @menu_run_x && ev[:x] < @menu_run_x + 3 * CHAR_W
          run_current_file
          return
        end
        if @menu_hilight_x && ev[:x] >= @menu_hilight_x && ev[:x] < @menu_hilight_x + 7 * CHAR_W
          toggle_highlight
          return
        end
        if @menu_debug_x && ev[:x] >= @menu_debug_x && ev[:x] < @menu_debug_x + 5 * CHAR_W
          open_menu(:debug)
          return
        end
      end
    end

    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0

      # Latency clock starts here, except for bare modifier presses (they draw
      # nothing, so stamping them would charge their idle time to the next key).
      lat_key_arrived unless keycode >= 224 && keycode <= 231

      # Modal quit-confirm dialog steals all keys until dismissed.
      if @quit_dialog_open
        handle_quit_dialog_key(character)
        return
      end

      # Modal attach-target picker.
      if @target_picker_open
        handle_target_picker_key(ev)
        return
      end

      # Open dropdown is modal: arrows / Enter / hotkey letters / ESC.
      if @active_menu
        handle_menu_key(ev)
        return
      end

      # Find dialog is modal: typed chars build the query, Enter searches.
      if @search_open
        handle_search_dialog_key(ev)
        return
      end

      # F3 -> Find Next (uses last query). Available without modifiers.
      if (ev[:scancode] || 0) == 0x3C
        find_next
        return
      end

      # Debugger function keys. F9 toggles a breakpoint at any time; the
      # run-control keys act only during an active session (design doc 4.5).
      scancode = ev[:scancode] || 0
      if scancode == SC_F9
        toggle_breakpoint
        return
      end
      # F5 runs the file. During a debug session it means Continue instead,
      # which is the usual meaning of that key on both sides.
      if scancode == SC_F5 && !@dbg_active
        run_current_file
        return
      end
      if @dbg_active
        case scancode
        when SC_F5  then dbg_continue; return
        when SC_F6  then dbg_pause; return
        when SC_F10 then dbg_step(:over); return
        when SC_F11 then dbg_step(ev_shift?(ev) ? :out : :in); return
        when SC_F7  then dbg_select_frame(-1); return
        when SC_F8  then dbg_select_frame(1); return
        when SC_F4  then dbg_toggle_pane; return
        end
      end

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
          open_menu(:debug)  # input path cannot carry Alt, so Ctrl-D mirrors it)
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
        when 0x07  # Alt-D -> open Debug menu
          open_menu(:debug)
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

      # Printable / control characters
      if character > 0
        @input_buffer << character
      end
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
    poll_debug_events if @dbg_active

    # Key repeat: if a navigation key is held down, repeat the action
    if @held_keycode
      @hold_frames += 1
      if @hold_frames >= KEY_REPEAT_DELAY
        if (@hold_frames - KEY_REPEAT_DELAY) % KEY_REPEAT_RATE == 0
          execute_key_action(@held_keycode)
        end
      end
    end

    # Tick down the "Saved" badge and trigger a final repaint when it expires.
    if @save_ok_frames > 0
      @save_ok_frames -= 1
      @need_redraw = true if @save_ok_frames == 0
    end

    # Re-tokenize once the user has paused editing for HL_DEBOUNCE_FRAMES frames.
    if @highlight_dirty && @hl_idle_frames < HL_DEBOUNCE_FRAMES
      @hl_idle_frames += 1
      @need_redraw = true if @hl_idle_frames >= HL_DEBOUNCE_FRAMES
    end

    if @need_redraw
      t_start = Machine.uptime_us
      redraw_all
      @need_redraw = false
      lat_sample(t_start)
    end
    @frame_ms
  end

  def on_destroy
    # Release the debug session so a crash/exit never leaves a target parked.
    end_debug_session(true) if @dbg_active
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
