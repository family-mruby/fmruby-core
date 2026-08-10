# Text Editor Application
# MS-DOS style text editor with menu bar and status line

class EditorApp < FmrbApp
  # The on-device debugger lives in editor/debug_pane.rb (mruby build) or in a
  # no-op stub of the same name (Spinel build). Everything below talks to it
  # through the dbg_* hooks only.
  include EditorDebugPane
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

  # Printable ASCII indexed by (code - 32). Used instead of Integer#chr, which
  # the Spinel runtime does not provide -- and a table lookup is the same in both
  # builds, so the source stays single-backend.
  ASCII_PRINTABLE = " !\"\#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

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
  MENU_FILE_ITEMS    = ["Open", "Save", "Save as", "Template", "Exit"]
  MENU_FILE_HOTKEYS  = [0x12, 0x16, 0x04, 0x17, 0x1B]  # O, S, A, T, X
  MENU_FILE_W        = 62

  # App skeletons, as files so a user can add their own next to the shipped
  # ones. File > Template lists this directory and inserts the chosen file at
  # the cursor.
  TEMPLATE_DIR = "/lib/templates"

  MENU_EDIT_ITEMS    = ["Cut", "Copy", "Paste", "Select All"]
  MENU_EDIT_HOTKEYS  = [0x17, 0x06, 0x13, 0x04]  # T (cuT), C, P, A
  MENU_EDIT_W        = 72

  # Selection / clipboard colors
  SEL_BG = FmrbGfx.rgb_to_332(180, 200, 255)  # Light blue selection

  # Key repeat timing (in frames, ~33ms each)
  KEY_REPEAT_DELAY = 12  # ~400ms before repeat starts
  KEY_REPEAT_RATE = 3    # ~100ms between repeats

  SAVE_OK_FRAMES = 60     # ~2s flash of "Saved" on the status line

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

  # Layout bits the debugger's gutter needs; kept here because the edit area
  # geometry uses them whether or not a debug session exists.
  GUTTER_W  = 8                                 # gutter width in px (debug mode)
  GUTTER_BG = FmrbGfx.rgb_to_332(210, 195, 205) # gutter column background
  # Function-key scancodes (USB HID Usage IDs). F5 = Run and F11 = fullscreen
  # belong to the editor; the rest are the debugger's, used from its mixin.
  SC_F4 = 0x3D; SC_F5 = 0x3E; SC_F6 = 0x3F; SC_F7 = 0x40
  SC_F8 = 0x41; SC_F9 = 0x42; SC_F10 = 0x43; SC_F11 = 0x44

  def initialize
    super()
    # The document lives in EditorCore (C, POOL_ID_EDITOR_DOC arena): the editor
    # holds only cursor / selection / view state. Nothing here keeps line text.
    EditorCore.reset
    @cx = 0             # Cursor column in current line
    @cy = 0             # Cursor line index
    @scroll_y = 0       # First visible line index
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
    @gutter_w = 0             # breakpoint gutter width (set by recompute_layout)
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
    @edit_cols = (@user_area_width - 2 - @gutter_w) / CHAR_W
    @edit_rows = @edit_height / CHAR_H
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
    if dbg_menu_visible?
      @menu_debug_x = x
      # Trailing "*" marks an active debug session.
      draw_menu_item(x, y, "D", dbg_menu_label)
      x += 7 * CHAR_W  # past "Debug*" (6 chars) + 1-char gap
    else
      @menu_debug_x = nil
    end
    # Window <-> fullscreen toggle (F11). No hotkey letter: "F" is taken by the
    # File menu, and this one is a direct toggle rather than a dropdown.
    # Dropped when the window is too narrow for it -- the key still works.
    if x + 5 * CHAR_W <= @user_area_x0 + @user_area_width
      @menu_full_x = x
      @gfx.draw_text(x, y, @fullscreen ? "Full*" : "Full ", MENU_TEXT, MENU_BG)
    else
      @menu_full_x = nil
    end
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
    status += "  [HL off]" unless @hl_enabled

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

  # Highlight default for a path: Ruby on, anything else off. The tokenizer only
  # knows Ruby, so a .bas or .toml file would otherwise get Ruby colours applied
  # to it. A buffer with no name yet counts as Ruby.
  def hl_default_for(path)
    return true if path.nil?
    path.end_with?(".rb")
  end

  def apply_hl_enabled
    EditorCore.hl_enabled = @hl_enabled
  end

  def toggle_highlight
    @hl_enabled = !@hl_enabled
    apply_hl_enabled
    # Remember that the user decided, so opening a file does not silently undo
    # it -- until a *different* file is opened, which re-evaluates the default.
    @hl_manual = true
    @need_redraw = true
    Log.info("Highlight #{@hl_enabled ? 'ON' : 'OFF'} (manual)")
  end

  # Mark buffer as edited (the status line shows the "*").
  def mark_edited
    @modified = true
    @dirty_status = true
  end

  def draw_edit_area
    # One fill for the whole area: it also clears the leftover strip below the
    # last full row (edit_height is not always a multiple of CHAR_H).
    @gfx.fill_rect(@user_area_x0, @edit_y,
                    @user_area_width, @edit_height, BG_COLOR)

    # Breakpoint gutter column (debug mode only).
    if @gutter_w > 0
      @gfx.fill_rect(@user_area_x0, @edit_y, @gutter_w, @edit_height, GUTTER_BG)
    end

    sel_range = selection_range  # [sx, sy, ex, ey] or nil

    row = -1
    while (row += 1) < @edit_rows
      line_idx = @scroll_y + row
      break if line_idx >= EditorCore.line_count
      draw_edit_row(row, line_idx, sel_range, false)
    end

    draw_cursor
  end

  # Draw one screen row of the edit area. +blank_bg+ repaints the row
  # background first; the full-area draw has already cleared it, the dirty-line
  # path has not. A row past the last document line just gets cleared.
  def draw_edit_row(row, line_idx, sel_range, blank_bg)
    x = @user_area_x0 + 1 + @gutter_w
    y = @edit_y + row * CHAR_H

    if blank_bg
      @gfx.fill_rect(@user_area_x0, y, @user_area_width, CHAR_H, BG_COLOR)
      @gfx.fill_rect(@user_area_x0, y, @gutter_w, CHAR_H, GUTTER_BG) if @gutter_w > 0
    end
    return if line_idx >= EditorCore.line_count

    line_len = EditorCore.line_length(line_idx)
    # EditorCore slices by character, horizontal scroll included, so no String
    # of the full line is ever built here.
    text = EditorCore.render_text(line_idx, @scroll_x, @edit_cols)
    visible_len = text.length

    # Current-stop line gets a full-row highlight (text area only, so the
    # gutter dot stays visible). Breakpoints are shown as a gutter dot.
    line_bg = dbg_line_background(line_idx)
    if line_bg != BG_COLOR
      @gfx.fill_rect(@user_area_x0 + @gutter_w, y,
                     @user_area_width - @gutter_w, CHAR_H, line_bg)
    end
    dbg_draw_gutter(line_idx, y) if @gutter_w > 0

    # Selection background goes down before the text so we can overpaint
    # the selected glyphs with SEL_BG as their background.
    sel_vstart = nil
    sel_vend = nil
    if sel_range
      sel = line_selection_cols(line_idx, line_len)
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

    return if visible_len == 0

    # Tinted (breakpoint/stop) lines always render plain over their background.
    # The category map is per visible character and comes from EditorCore's
    # cache (recomputed only when the line changes).
    hl = (line_bg == BG_COLOR && @hl_enabled) ?
           EditorCore.render_hl(line_idx, @scroll_x, @edit_cols) : nil
    if hl && hl.length > 0
      draw_highlighted_line(x, y, text, visible_len, hl)
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

  # +hl+ holds one category byte per VISIBLE character (EditorCore.render_hl
  # applies the same scroll window as render_text), so index i lines up with
  # text character i.
  def draw_highlighted_line(x, y, text, visible_len, hl)
    i = 0
    while i < visible_len
      cat = hl.getbyte(i) || 0
      color = HL_COLORS[cat] || TEXT_COLOR

      # Gather consecutive characters with the same category
      j = i + 1
      while j < visible_len
        next_cat = hl.getbyte(j) || 0
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
    ch = EditorCore.char_at(@cy, @cx)
    @gfx.draw_text(x, y, ch, BG_COLOR, CURSOR_COLOR) if ch.length > 0
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
    # p99 first: it reads the histogram, which the string build below clears.
    p99 = lat_p99_ms
    hist = ""
    b = 0
    while b <= LAT_BUCKETS
      hist += "," if b > 0
      hist += @lat_hist[b].to_s
      @lat_hist[b] = 0
      b += 1
    end
    msg = "edit_lat: n=#{@lat_n} mean=#{@lat_sum / @lat_n}us max=#{@lat_max}us"
    msg += " p99<=#{p99}ms over25ms=#{@lat_slow}"
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
    dbg_draw_pane(@dbg_pane_y, @dbg_pane_h) if @dbg_pane_h > 0
    draw_status_line
    draw_active_menu if @active_menu
    dbg_draw_modal
    draw_search_dialog if @search_open
    draw_quit_dialog if @quit_dialog_open
    # Re-issue the title-bar / border GfxBlock with current window size so the
    # frame survives canvas resize (the block is bound to @window_width / @window_height kwargs).
    draw_window_frame
    @gfx.present
  end

  # Repaint only the rows an edit or a cursor move touched, plus the cursor and
  # the status line (Ln/Col changes with every key, and it is two commands).
  # The menu bar, the window frame and any open overlay are untouched by row
  # drawing, so they are not re-issued -- that is most of the saving.
  def redraw_dirty
    sel_range = selection_range
    row = 0
    while row < @edit_rows
      line_idx = @scroll_y + row
      draw_edit_row(row, line_idx, sel_range, true) if dirty_line?(line_idx)
      row += 1
    end
    draw_cursor
    draw_status_line
    @gfx.present
  end

  # Single entry point for "put what changed on screen". Called from on_event
  # (right after the key that caused the change) and from on_update (for
  # changes that come from timers, key repeat and debugger events).
  def redraw_if_dirty
    return unless dirty?
    t_start = Machine.uptime_us
    # Row drawing would paint over an open overlay, so anything modal forces the
    # full path (which re-draws the overlay on top).
    if @need_redraw || @active_menu || @search_open || @quit_dialog_open ||
       dbg_modal?
      redraw_all
    else
      redraw_dirty
    end
    clear_dirty
    lat_sample(t_start)
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
    # Enter / ESC by scancode (HID Usage ID), like handle_menu_key: on the Linux
    # sim ev[:keycode] carries the SDL keysym (13 / 27) instead of 40 / 41, so
    # the keycode form never fired there.
    scancode = ev[:scancode] || 0
    character = ev[:character] || 0

    case scancode
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
        @search_query += printable_char(character)
        @search_query_dirty = true
        @search_status = ""
        @need_redraw = true
      end
    end
  end

  # Find +query+ starting from the cursor, wrapping to the top once.
  # When +after_cursor+ is true (F3 / Find Next), skip the character at the
  # cursor so we advance past the previous hit. Returns true on match.
  # EditorCore searches line by line inside the arena, so the document is never
  # joined into one String here. A query cannot span a newline (the Find field is
  # a single line), which the old whole-document search allowed in theory.
  def find_from_cursor(query, after_cursor)
    return false if query.nil? || query.length == 0
    rec = EditorCore.find(query, @cy, @cx, after_cursor)
    return false unless EditorCore.found?(rec)
    @cy = EditorCore.find_y(rec)
    @cx = EditorCore.find_x(rec)
    ensure_cursor_visible
    @need_redraw = true
    true
  end

  def find_next
    return false if @search_last.length == 0
    found = find_from_cursor(@search_last, true)
    Log.info("Find: not found '#{@search_last}'") unless found
    found
  end

  # ---- Scrolling ----

  def ensure_cursor_visible
    old_scroll_y = @scroll_y
    old_scroll_x = @scroll_x
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
    # A scroll moves every row, so the fine-grained marks are useless here.
    @need_redraw = true if @scroll_y != old_scroll_y || @scroll_x != old_scroll_x
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
      insert_char(printable_char(ch))
    end
  end

  # One-character String for a printable ASCII code ("" outside 32..126).
  def printable_char(code)
    return "" if code < 32 || code > 126
    ASCII_PRINTABLE[code - 32, 1]
  end

  def insert_char(c)
    delete_selection if has_selection?
    nx = EditorCore.insert_text(@cy, @cx, c)
    if nx < 0
      doc_full
      return
    end
    @cx = nx
    mark_edited
    ensure_cursor_visible
    mark_dirty_line(@cy)
  end

  def handle_enter
    delete_selection if has_selection?
    if EditorCore.split_line(@cy, @cx) < 0
      doc_full
      return
    end
    # Everything below the split shifted down one row.
    mark_dirty_from(@cy)
    @cy += 1
    @cx = 0
    mark_edited
    ensure_cursor_visible
  end

  def handle_backspace
    return if delete_selection
    if @cx > 0
      EditorCore.delete_char(@cy, @cx - 1)
      @cx -= 1
      mark_edited
      ensure_cursor_visible
      mark_dirty_line(@cy)
    elsif @cy > 0
      # Merge with previous line; join_line returns where the cursor belongs.
      prev_len = EditorCore.join_line(@cy - 1)
      return if prev_len < 0
      @cy -= 1
      @cx = prev_len
      mark_edited
      ensure_cursor_visible
      # The merged line and everything that shifted up.
      mark_dirty_from(@cy)
    end
  end

  def handle_delete
    return if delete_selection
    if @cx < EditorCore.line_length(@cy)
      EditorCore.delete_char(@cy, @cx)
      mark_edited
      mark_dirty_line(@cy)
    elsif @cy < EditorCore.line_count - 1
      # Merge next line
      EditorCore.join_line(@cy)
      mark_edited
      mark_dirty_from(@cy)
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

  # Cursor moves repaint the line the cursor left and the one it arrived on
  # (a selection being extended covers the lines in between, hence the range).
  # A move that scrolls is upgraded to a full repaint inside
  # ensure_cursor_visible.

  def move_up
    if @cy > 0
      @cy -= 1
      clamp_cx
      ensure_cursor_visible
      mark_dirty_range(@cy, @cy + 1)
    end
  end

  def move_down
    if @cy < EditorCore.line_count - 1
      @cy += 1
      clamp_cx
      ensure_cursor_visible
      mark_dirty_range(@cy - 1, @cy)
    end
  end

  def move_left
    if @cx > 0
      @cx -= 1
      ensure_cursor_visible
      mark_dirty_line(@cy)
    elsif @cy > 0
      @cy -= 1
      @cx = EditorCore.line_length(@cy)
      ensure_cursor_visible
      mark_dirty_range(@cy, @cy + 1)
    end
  end

  def move_right
    if @cx < EditorCore.line_length(@cy)
      @cx += 1
      ensure_cursor_visible
      mark_dirty_line(@cy)
    elsif @cy < EditorCore.line_count - 1
      @cy += 1
      @cx = 0
      ensure_cursor_visible
      mark_dirty_range(@cy - 1, @cy)
    end
  end

  def page_up
    prev_cy = @cy
    @cy -= @edit_rows
    @cy = 0 if @cy < 0
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  def page_down
    prev_cy = @cy
    @cy += @edit_rows
    max = EditorCore.line_count - 1
    @cy = max if @cy > max
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  def move_home
    @cx = 0
    ensure_cursor_visible
    mark_dirty_line(@cy)
  end

  def move_end
    @cx = EditorCore.line_length(@cy)
    ensure_cursor_visible
    mark_dirty_line(@cy)
  end

  def clamp_cx
    len = EditorCore.line_length(@cy)
    @cx = len if @cx > len
  end

  # ---- Menu dropdown (File / Edit) ----

  def menu_items
    case @active_menu
    when :file then MENU_FILE_ITEMS
    when :edit then MENU_EDIT_ITEMS
    when :template then @template_labels
    when :debug then dbg_menu_items
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
    when :template then template_menu_width
    when :debug then dbg_menu_width
    end
  end

  # Top-left of the dropdown panel for the active menu (just below its label).
  # The Debug label sits at the right end of the menu bar, so its dropdown is
  # right-anchored to stay inside the (narrow) editor window.
  def menu_origin
    case @active_menu
    when :file then [@menu_file_x, @menu_y + CHAR_H]
    when :template then [@menu_file_x, @menu_y + CHAR_H]
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

  # Ask for a full repaint; redraw_all puts the open dropdown on top of it.
  # (Used on menu open and on dropdown navigation.)
  def render_with_dropdown
    @need_redraw = true
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
    when :template then insert_template(idx)
    when :debug then dbg_activate_item(idx)
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
    when 3  # Template
      open_template_menu
    when 4  # Exit
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

  # ---- Templates (File > Template) ----
  #
  # Skeletons are files under TEMPLATE_DIR rather than text baked into this
  # app, so a user can drop their own in and see it in the list. The list is
  # the ordinary menu dropdown, which already has the keyboard and mouse
  # handling; only the item text is different.

  def open_template_menu
    load_template_list
    if @template_labels.empty?
      flash_status("No templates")
      return
    end
    open_menu(:template)
  end

  def load_template_list
    names = []
    labels = []
    begin
      dir = Dir.open(TEMPLATE_DIR)
      while (e = dir.read)
        name = e.to_s
        names << name if name.end_with?(".rb")
      end
      dir.close
    rescue => err
      Log.error("Cannot list #{TEMPLATE_DIR}: #{err.message}")
      names = []
    end
    names = names.sort
    names.each do |n|
      labels << n[0, n.length - 3]
    end
    @template_names = names
    @template_labels = labels
  end

  def template_menu_width
    widest = 0
    @template_labels.each do |t|
      len = t.length
      widest = len if len > widest
    end
    widest * CHAR_W + 10
  end

  # Insert the chosen skeleton at the cursor. Same shape as a paste: the
  # document model does the work and reports where the cursor ended up.
  def insert_template(idx)
    return if idx < 0 || idx >= @template_names.size
    path = "#{TEMPLATE_DIR}/#{@template_names[idx]}"
    text = nil
    begin
      f = File.open(path, "r")
      text = f.read
      f.close
    rescue => err
      flash_status("Load failed")
      Log.error("Template read failed: #{path} (#{err.message})")
      return
    end
    body = text.to_s
    if body.bytesize == 0
      flash_status("Empty")
      return
    end
    delete_selection if has_selection?
    start_y = @cy
    rec = EditorCore.insert_multiline(@cy, @cx, body)
    @cy = EditorCore.pos_y(rec)
    @cx = EditorCore.pos_x(rec)
    mark_dirty_from(start_y)
    mark_edited
    ensure_cursor_visible
    flash_status("Inserted")
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
    # The whole highlighted span has to lose its background.
    mark_dirty_range(@sel_anchor_y, @cy)
    @sel_anchor_x = nil
    @sel_anchor_y = nil
  end

  def begin_selection_if_needed
    return if has_selection?
    @sel_anchor_x = @cx
    @sel_anchor_y = @cy
  end

  def select_all
    @sel_anchor_x = 0
    @sel_anchor_y = 0
    @cy = EditorCore.line_count - 1
    @cx = EditorCore.line_length(@cy)
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

  def delete_selection
    range = selection_range
    return false unless range
    sx, sy, ex, ey = range
    EditorCore.delete_range(sy, sx, ey, ex)
    @cy = sy
    @cx = sx
    clear_selection
    mark_edited
    ensure_cursor_visible
    # Single-line deletions touch one row; a multi-line one shifts the rest up.
    if sy == ey
      mark_dirty_line(sy)
    else
      mark_dirty_from(sy)
    end
    true
  end

  # ---- Clipboard ops ----

  def copy_selection
    return unless has_selection?
    sx, sy, ex, ey = selection_range
    n = EditorCore.copy_range(sy, sx, ey, ex)
    if n < 0
      doc_full
      return
    end
    Log.info("Copied #{n} bytes")
  end

  def cut_selection
    return unless has_selection?
    copy_selection
    delete_selection
  end

  def paste_clipboard
    return if EditorCore.clipboard_length == 0
    delete_selection if has_selection?
    start_y = @cy
    rec = EditorCore.paste_at(@cy, @cx)
    ny = EditorCore.pos_y(rec)
    nx = EditorCore.pos_x(rec)
    if ny == start_y
      mark_dirty_line(start_y)
    else
      mark_dirty_from(start_y)
    end
    @cy = ny
    @cx = nx
    mark_edited
    ensure_cursor_visible
  end

  # ---- File operations ----

  # EditorCore reads the file straight into its arena in chunks, so nothing here
  # holds the contents: no whole-file String, no Array of lines. A negative
  # return means the file could not be read or the arena is full -- the editor
  # says so and keeps the buffer it had.
  def load_file(path)
    n = EditorCore.load_file(path)
    if n < 0
      if n == -2
        flash_status("Too large")
        Log.error("Load failed (document arena full): #{path}")
      else
        flash_status("Load failed")
        Log.error("Failed to load file '#{path}' (err=#{n})")
      end
      @need_redraw = true
      return
    end
    @cx = 0
    @cy = 0
    @scroll_y = 0
    @scroll_x = 0
    @modified = false
    # Highlight default is per buffer: a manual toggle on the previous file is
    # not carried over. Size plays no part any more (the highlight cache in
    # EditorCore is per line).
    @hl_manual = false
    @hl_enabled = hl_default_for(path)
    apply_hl_enabled
    @current_file = path
    @need_redraw = true
    # One line with both numbers, so the same measurement is available in the sim
    # and on the device: how much of THIS VM's mruby pool the open file costs
    # (percent) versus how much of the document arena it takes (bytes).
    Log.info("edit_doc: lines=#{n} bytes=#{EditorCore.doc_bytesize} arena=#{EditorCore.mem_used} pool=#{FmrbApp.pool_usage}% file=#{path}")
  end

  # Shown when the arena cannot grow: the editor stays alive and editable, which
  # is the whole point of returning an error code instead of aborting.
  def doc_full
    flash_status("Doc full")
    Log.error("Editor document arena full (#{EditorCore.mem_used} bytes used)")
    @need_redraw = true
  end

  def save_file
    unless @current_file
      # A buffer with no name yet (the editor started empty): ask for one rather
      # than failing silently. Ctrl-S used to do nothing at all here, so the only
      # way to save a new file was to know about File > Save as.
      Log.info("Save: no file name yet, asking for one")
      @pending_file_op = :save
      request_file_select("save")
      return
    end

    expected = EditorCore.doc_bytesize
    written = EditorCore.save_file(@current_file)
    if written < 0
      flash_status("Save failed")
      Log.error("Failed to save file: #{@current_file} (err=#{written})")
    elsif written != expected
      flash_status("Save failed")
      Log.error("Save mismatch for #{@current_file}: expected=#{expected}, written=#{written}")
    else
      @modified = false
      flash_status("Saved")  # status line only
      Log.info("Saved file: #{@current_file} (#{written} bytes)")
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
      flash_status("Run: need a path")
      return
    end
    save_file if @modified
    request_run(@current_file, @run_pid)
    flash_status("Running")
  end

  # What the spawner can load: any absolute path, wherever the buffer was
  # saved. The kernel enforces the same rule (run_path_allowed?); checking here
  # just gives a better message than a silent no-op.
  def runnable_path?(path)
    path.start_with?("/")
  end

  # Show a short right-aligned badge on the status line for ~2s.
  def flash_status(text)
    @status_label = " #{text} "
    @save_ok_frames = SAVE_OK_FRAMES
    @dirty_status = true
  end

  def save_file_as(path)
    # Naming a buffer (or renaming it) re-decides the highlight default, unless
    # the user has already made that call for this buffer.
    unless @hl_manual
      @hl_enabled = hl_default_for(path)
      apply_hl_enabled
    end
    @current_file = path
    save_file
  end

  def on_event(ev)
    super(ev)
    handle_editor_event(ev)
    # Draw here rather than leaving it to the next on_update. Going through
    # on_update cost up to a whole 33ms frame per key on top of the drawing
    # itself, which was most of the measured key-to-present time.
    redraw_if_dirty
  end

  def handle_editor_event(ev)
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
        if @menu_full_x && ev[:x] >= @menu_full_x && ev[:x] < @menu_full_x + 4 * CHAR_W
          toggle_fullscreen
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

      # Modal owned by the debugger (attach-target picker).
      return if dbg_handle_modal_key(ev)

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
          open_menu(:debug) if dbg_menu_visible?
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

    # Tick down the "Saved" badge and repaint the status line when it expires.
    if @save_ok_frames > 0
      @save_ok_frames -= 1
      @dirty_status = true if @save_ok_frames == 0
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
