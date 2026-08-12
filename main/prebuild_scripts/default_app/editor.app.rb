# Text Editor Application
# MS-DOS style text editor with menu bar and status line

class EditorApp < FmrbApp
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
  PROBLEM_BADGE_TEXT = FmrbGfx.rgb_to_332(255, 120, 120)  # Problem count badge
  PROBLEM_BG    = FmrbGfx.rgb_to_332(255, 190, 190)   # Row tint of a problem line
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
  # The edit area is a fixed grid of cells in efontJA_12: a half-width glyph is
  # exactly one 6px cell and a full-width one exactly two, so the classic
  # terminal model holds with no fractional positions anywhere. The chrome
  # (menu bar, status line, dialogs) stays on the 6x8 default font.
  CELL_W = 6
  LINE_H = 12
  EDIT_FONT_SIZE = 12
  # draw_text carries FMRB_GFX_MAX_TEXT_LEN (128) bytes per command and a
  # Japanese character is three of them, so a row is emitted in several
  # commands. Kept under the limit rather than at it: the split only ever
  # happens on a character boundary.
  DRAW_TEXT_MAX_BYTES = 120
  CHAR_H = 8
  TAB_SIZE = 2

  # Menu dropdown common style
  DROPDOWN_BG = FmrbGfx.rgb_to_332(255, 255, 255)
  DROPDOWN_TEXT = FmrbGfx.rgb_to_332(0, 0, 0)
  DROPDOWN_SEL_BG = FmrbGfx.rgb_to_332(100, 60, 100)
  DROPDOWN_SEL_TEXT = FmrbGfx.rgb_to_332(255, 255, 255)
  DROPDOWN_ITEM_H = 10

  # Per-menu config: scancode hotkeys (labels come from FmrbI18n).
  # Hotkey scancodes pick a distinguishing letter per item (DOS-Edit style) and
  # are the same in every language -- only the words next to them change.
  MENU_FILE_HOTKEYS  = [0x12, 0x16, 0x04, 0x17, 0x1B]  # O, S, A, T, X

  # App skeletons, as files so a user can add their own next to the shipped
  # ones. File > Template lists this directory and inserts the chosen file at
  # the cursor.
  TEMPLATE_DIR = "/lib/templates"

  MENU_EDIT_HOTKEYS  = [0x17, 0x06, 0x13, 0x04]  # T (cuT), C, P, A

  # Menu bar items. Integer ids rather than symbols so the parallel position and
  # width arrays built while drawing stay concretely typed in both engines.
  MENU_ID_FILE    = 0
  MENU_ID_EDIT    = 1
  MENU_ID_SEARCH  = 2
  MENU_ID_RUN     = 3
  MENU_ID_HILIGHT = 4
  MENU_ID_WRAP    = 5
  MENU_ID_DEBUG   = 6
  MENU_ID_FULL    = 7
  MENU_BAR_IDS  = [MENU_ID_FILE, MENU_ID_EDIT, MENU_ID_SEARCH, MENU_ID_RUN,
                   MENU_ID_HILIGHT, MENU_ID_WRAP, MENU_ID_DEBUG, MENU_ID_FULL]
  # Accelerator letter shown in parentheses after each label. Empty means the
  # item has no letter (Full is a direct toggle on F11).
  MENU_BAR_KEYS = ["F", "E", "S", "R", "H", "W", "D", ""]
  MENU_BAR_GAP  = 6   # px between menu bar items

  # Selection / clipboard colors
  SEL_BG = FmrbGfx.rgb_to_332(180, 200, 255)  # Light blue selection

  # Key repeat timing (in frames, ~33ms each)
  KEY_REPEAT_DELAY = 12  # ~400ms before repeat starts
  KEY_REPEAT_RATE = 3    # ~100ms between repeats

  STATUS_MSG_FRAMES = 150 # ~5s before a status message gives the line back

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

  # Layout bits the debugger's gutter needs; kept here because the edit area
  # geometry uses them whether or not a debug session exists.
  GUTTER_W  = 8                                 # gutter width in px (debug mode)
  GUTTER_BG = FmrbGfx.rgb_to_332(210, 195, 205) # gutter column background
  # Function-key scancodes (USB HID Usage IDs). F5 = Run and F11 = fullscreen
  # belong to the editor; the rest are the debugger's, used from its mixin.
  SC_F4 = 0x3D; SC_F5 = 0x3E; SC_F6 = 0x3F; SC_F7 = 0x40
  SC_F8 = 0x41; SC_F9 = 0x42; SC_F10 = 0x43; SC_F11 = 0x44
  # Tab, and the keys the completion list answers to (HID Usage IDs).
  SC_TAB = 0x2B; SC_ENTER = 0x28; SC_KP_ENTER = 0x58; SC_ESC = 0x29
  SC_UP = 0x52; SC_DOWN = 0x51
  # Ctrl+T shows the type under the cursor, Ctrl+E the type errors. Neither
  # letter was taken: the editor uses Ctrl+S/X/C/V/A/D, and Ctrl+Q, Ctrl+Tab
  # and Ctrl+Space belong to the system.
  SC_T = 0x17; SC_E = 0x08
  # F1 opens the help page for the method under the cursor (or the selected
  # candidate). Nothing else used it: the debugger's keys start at F4.
  SC_F1 = 0x3A

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

  # ---- Drawing ----

  # Localized word for a menu bar item (no accelerator, no state mark).
  def menu_bar_label(id)
    case id
    when MENU_ID_FILE    then FmrbI18n.t(:m_file).to_s
    when MENU_ID_EDIT    then FmrbI18n.t(:m_edit).to_s
    when MENU_ID_SEARCH  then FmrbI18n.t(:m_search).to_s
    when MENU_ID_RUN     then FmrbI18n.t(:m_run).to_s
    when MENU_ID_HILIGHT then FmrbI18n.t(:m_hilight).to_s
    when MENU_ID_WRAP    then FmrbI18n.t(:m_wrap).to_s
    when MENU_ID_DEBUG   then dbg_menu_label.to_s
    when MENU_ID_FULL    then FmrbI18n.t(:m_full).to_s
    else ""
    end
  end

  # Trailing state mark: "*" when the item's mode is on, a space when it is off
  # (so the label does not jump sideways), "" when the item has no state.
  def menu_bar_mark(id)
    case id
    when MENU_ID_HILIGHT then @hl_enabled ? "*" : " "
    when MENU_ID_WRAP    then @wrap_on ? "*" : " "
    when MENU_ID_DEBUG   then dbg_menu_mark.to_s
    when MENU_ID_FULL    then @fullscreen ? "*" : " "
    else ""
    end
  end

  # Which items the bar carries right now (Debug only during a debug build).
  def menu_bar_ids
    ids = []
    debug_visible = dbg_menu_visible?
    i = 0
    while i < MENU_BAR_IDS.size
      id = MENU_BAR_IDS[i]
      i += 1
      next if id == MENU_ID_DEBUG && debug_visible != true
      ids << id
    end
    ids
  end

  # Pixel width of one item, with or without its "(K)" accelerator.
  def menu_bar_item_width(id, show_keys)
    key = MENU_BAR_KEYS[id].to_s
    w = FmrbI18n.text_width(menu_bar_label(id)) + FmrbI18n.text_width(menu_bar_mark(id))
    w += 3 * CHAR_W if show_keys && key != ""   # "(", the letter, ")"
    w
  end

  # The menu bar is laid out from measured label widths rather than fixed
  # character counts: a translated label is a different number of characters and
  # a different number of pixels per character. Positions and widths are kept in
  # parallel arrays for the click hit test, which therefore follows the layout
  # for free.
  #
  # The accelerators are shown as "File(F)" / "ファイル(F)" -- the old trick of
  # colouring the first letter cannot survive translation, since a Japanese
  # label does not start with the letter it answers to. That costs three
  # characters an item, which a 240px window does not have, so the whole bar
  # drops the parentheses rather than lose its last two items; the keys
  # themselves work either way.
  def draw_menu_bar
    y = @menu_y
    # +1 covers the 1px separator row between menu bar and edit area
    @gfx.fill_rect(@user_area_x0, y, @user_area_width, CHAR_H + 1, MENU_BG)

    @menu_ids = []
    @menu_xs = []
    @menu_ws = []
    @menu_file_x = nil
    @menu_edit_x = nil
    @menu_debug_x = nil

    ids = menu_bar_ids
    avail = @user_area_width - 4
    total = 0
    i = 0
    while i < ids.size
      total += menu_bar_item_width(ids[i], true)
      total += MENU_BAR_GAP if i > 0
      i += 1
    end
    show_keys = total <= avail

    x = @user_area_x0 + 2
    right = @user_area_x0 + @user_area_width
    i = 0
    while i < ids.size
      id = ids[i]
      i += 1
      w = draw_menu_bar_item(x, y, id, right, show_keys)
      break if w <= 0   # ran out of room; the key still works

      @menu_ids << id
      @menu_xs << x
      @menu_ws << w
      @menu_file_x = x if id == MENU_ID_FILE
      @menu_edit_x = x if id == MENU_ID_EDIT
      @menu_debug_x = x if id == MENU_ID_DEBUG
      x += w + MENU_BAR_GAP
    end
  end

  # Draw one menu bar item and return its pixel width, or 0 when it would not
  # fit before `right`.
  def draw_menu_bar_item(x, y, id, right, show_keys)
    label = menu_bar_label(id)
    key = MENU_BAR_KEYS[id].to_s
    mark = menu_bar_mark(id)
    with_key = show_keys && key != ""

    head = with_key ? label + "(" : label
    tail = with_key ? ")" + mark : mark
    head_w = FmrbI18n.text_width(head)
    key_w = with_key ? CHAR_W : 0
    total = head_w + key_w + FmrbI18n.text_width(tail)
    return 0 if x + total > right

    @gfx.draw_text(x, y, head, MENU_TEXT, MENU_BG, mixed: true)
    if with_key
      @gfx.draw_text(x + head_w, y, key, MENU_KEY, MENU_BG)
    end
    if tail != ""
      @gfx.draw_text(x + head_w + key_w, y, tail, MENU_TEXT, MENU_BG, mixed: true)
    end
    total
  end

  # Index into the menu bar arrays for a click x, or -1.
  def menu_bar_hit(mx)
    return -1 unless @menu_xs
    i = 0
    while i < @menu_xs.size
      return i if mx >= @menu_xs[i] && mx < @menu_xs[i] + @menu_ws[i]
      i += 1
    end
    -1
  end

  def activate_menu_bar(id)
    case id
    when MENU_ID_FILE    then open_menu(:file)
    when MENU_ID_EDIT    then open_menu(:edit)
    when MENU_ID_SEARCH  then open_search_dialog
    when MENU_ID_RUN     then run_current_file
    when MENU_ID_HILIGHT then toggle_highlight
    when MENU_ID_WRAP    then toggle_wrap
    when MENU_ID_DEBUG   then open_menu(:debug)
    when MENU_ID_FULL    then toggle_fullscreen
    end
  end

  # The status line has two zones and one way in.
  #
  # The right edge belongs to the permanent badges -- kana mode, and the
  # problem count once diagnostics have run. They are measured first and drawn
  # last, so nothing can paint over them: P2 learned that the hard way by
  # letting a completion's doc comment cover the kana badge.
  #
  # Everything left of them is either a transient message (hover results,
  # diagnostic summaries, the doc of the selected candidate, "file too big",
  # "Saved") or, when none is pending, the usual file / line / column. Every
  # message goes through flash_status; this is the only method that draws here.
  def draw_status_line
    y = @status_y
    @gfx.fill_rect(@user_area_x0, y, @user_area_width, CHAR_H, STATUS_BG)

    # ---- right zone: measure the badges, right to left ----
    right = @user_area_x0 + @user_area_width
    if @kana_mode
      @kana_badge_w = FmrbI18n.text_width(kana_badge) + 2
      right -= @kana_badge_w
      @kana_badge_x = right
    else
      @kana_badge_x = nil
    end
    problems = problem_badge
    problems_x = nil
    if problems.length > 0
      right -= FmrbI18n.text_width(problems) + 2
      problems_x = right
    end

    # ---- left zone: the pending message, or the usual reading ----
    x0 = @user_area_x0 + 2
    room = right - x0 - 2
    if @status_msg
      text = fit_status_text(@status_msg, room)
      if @status_msg_ok
        bw = FmrbI18n.text_width(text)
        @gfx.fill_rect(x0, y, bw, CHAR_H, STATUS_OK_BG)
        @gfx.draw_text(x0, y, text, STATUS_OK_TEXT, STATUS_OK_BG, mixed: true)
      else
        @gfx.draw_text(x0, y, text, STATUS_TEXT, STATUS_BG, mixed: true)
      end
    else
      @gfx.draw_text(x0, y, fit_status_text(status_reading, room),
                     STATUS_TEXT, STATUS_BG, mixed: true)
    end

    # ---- badges last ----
    if problems_x
      @gfx.draw_text(problems_x, y, problems, PROBLEM_BADGE_TEXT, STATUS_BG,
                     mixed: true)
    end
    if @kana_badge_x
      @gfx.draw_text(@kana_badge_x, y, kana_badge, STATUS_TEXT, STATUS_BG,
                     mixed: true)
    end
  end

  # What the line says when nothing else is going on.
  def status_reading
    fname = @current_file ? @current_file.split("/").last : FmrbI18n.t(:st_new).to_s
    fname = "[Help] " + fname if @help_open
    s = " #{fname}  #{FmrbI18n.t(:st_ln).to_s} #{@cy + 1}, #{FmrbI18n.t(:st_col).to_s} #{@cx + 1}"
    s += " *" if @modified
    s += "  " + FmrbI18n.t(:st_hl_off).to_s unless @hl_enabled
    s
  end

  # Problems found by the last diagnostic run, as a badge. Empty when none were
  # found or none has been run -- a clean file says so once, in the message
  # zone, and then leaves the line alone.
  def problem_badge
    return "" if @diag_count.nil? || @diag_count <= 0
    "[!#{@diag_count}]"
  end

  # Trim a message to the pixels it may use, so it can never reach the badges.
  def fit_status_text(text, room)
    return "" if room <= 0
    s = text
    while s.length > 0 && FmrbI18n.text_width(s) > room
      s = s[0, s.length - 1]
    end
    s
  end

  # What the host says kana input is doing. Shown only once the host has told
  # us (a US keyboard never sends it), so nothing changes for English users.
  def kana_badge
    case @kana_mode
    when 1 then "[あ]"
    when 2 then "[ア]"
    else "[A]"
    end
  end

  # Clicking the badge steps off -> hiragana -> katakana -> off. The host owns
  # the mode; this only asks, and the badge redraws when the answer arrives.
  def cycle_kana_mode
    FmrbApp.set_kana_mode(((@kana_mode || 0) + 1) % 3)
  end

  def hit_kana_badge?(mx, my)
    return false unless @kana_badge_x
    my >= @status_y && my < @status_y + CHAR_H &&
      mx >= @kana_badge_x && mx < @kana_badge_x + @kana_badge_w
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

  # Fold long lines into the window, or go back to scrolling sideways. The
  # anchor is rebuilt around the cursor so the view does not jump: whichever
  # mode is being entered, the cursor's row becomes the top of the window and
  # ensure_cursor_visible settles the rest.
  def toggle_wrap
    @wrap_on = !@wrap_on
    @scroll_x = 0
    @scroll_y = @cy
    @anchor_seg = @wrap_on ? segment_of(@cy, @cx) : 0
    @cur_line_segs = line_segments(@cy)
    ensure_cursor_visible
    @need_redraw = true
    Log.info("Wrap #{@wrap_on ? 'ON' : 'OFF'}")
  end

  # Mark buffer as edited (the status line shows the "*").
  def mark_edited
    @modified = true
    @dirty_status = true
    # Problem markers name lines of the text as it was when they were found, so
    # the first edit retires them. The next save (or Ctrl+E) brings them back.
    clear_diagnostics
    # An edit that changes how many screen rows the line takes reflows every
    # row below it; one that does not touches only its own. Comparing against
    # the count from the last draw keeps that decision to one integer, rather
    # than a repaint of the window on every keystroke.
    if @wrap_on
      segs = line_segments(@cy)
      if segs != @cur_line_segs
        @cur_line_segs = segs
        mark_dirty_from(@cy)
      end
    end
  end

  # The edit area draws in efontJA_12 and everything else in the default 6x8
  # font. set_font is a queued command like any other, so it is enough to
  # bracket the drawing that needs it rather than track a mode.
  def begin_edit_font
    @gfx.set_font(:ja, EDIT_FONT_SIZE)
  end

  def end_edit_font
    @gfx.set_font(:default)
  end

  def draw_edit_area
    # One fill for the whole area: it also clears the leftover strip below the
    # last full row (edit_height is not always a multiple of LINE_H).
    @gfx.fill_rect(@user_area_x0, @edit_y,
                    @user_area_width, @edit_height, BG_COLOR)

    # Breakpoint gutter column (debug mode only).
    if @gutter_w > 0
      @gfx.fill_rect(@user_area_x0, @edit_y, @gutter_w, @edit_height, GUTTER_BG)
    end

    sel_range = selection_range  # [sx, sy, ex, ey] or nil

    begin_edit_font
    row = 0
    ly = @scroll_y
    ls = @anchor_seg
    while row < @edit_rows
      break if ly >= EditorCore.line_count
      draw_edit_row(row, ly, ls, sel_range, false)
      ls += 1
      if ls >= line_segments(ly)
        ly += 1
        ls = 0
      end
      row += 1
    end

    draw_cursor
    end_edit_font
  end

  # Draw one screen row of the edit area. +blank_bg+ repaints the row
  # background first; the full-area draw has already cleared it, the dirty-line
  # path has not. A row past the last document line just gets cleared.
  # +seg+ is which wrapped segment of the line this screen row shows (always 0
  # when wrapping is off, where the row starts at the horizontal scroll).
  def draw_edit_row(row, line_idx, seg, sel_range, blank_bg)
    x = @user_area_x0 + 1 + @gutter_w
    y = @edit_y + row * LINE_H

    if blank_bg
      @gfx.fill_rect(@user_area_x0, y, @user_area_width, LINE_H, BG_COLOR)
      @gfx.fill_rect(@user_area_x0, y, @gutter_w, LINE_H, GUTTER_BG) if @gutter_w > 0
    end
    return if line_idx >= EditorCore.line_count

    line_len = EditorCore.line_length(line_idx)
    # EditorCore slices by character, so no String of the full line is ever
    # built here. col0 is the horizontal scroll when wrapping is off and the
    # segment start when it is on. The width map is the same slice with one byte
    # per character saying whether it takes one cell or two; asking for
    # @edit_cols characters is an upper bound, since no character is narrower
    # than a cell.
    col0 = segment_start(line_idx, seg)
    widths = EditorCore.render_width(line_idx, col0, @edit_cols)
    nchars = visible_chars(widths, @edit_cols)
    seg_chars = segment_chars(line_idx, seg)
    nchars = seg_chars if @wrap_on && seg_chars < nchars
    text = nchars > 0 ? EditorCore.render_text(line_idx, col0, nchars) : ""
    used_cells = cell_offset(widths, nchars)

    # Current-stop line gets a full-row highlight (text area only, so the
    # gutter dot stays visible). Breakpoints are shown as a gutter dot.
    # A line the last diagnostic run complained about is tinted red, unless the
    # debugger is stopped on it -- where execution is now matters more.
    line_bg = dbg_line_background(line_idx)
    line_bg = PROBLEM_BG if line_bg == BG_COLOR && problem_on_line?(line_idx)
    if line_bg != BG_COLOR
      @gfx.fill_rect(@user_area_x0 + @gutter_w, y,
                     @user_area_width - @gutter_w, LINE_H, line_bg)
    end
    dbg_draw_gutter(line_idx, y) if @gutter_w > 0

    # Selection background goes down before the text; the glyphs are then drawn
    # over it with SEL_BG as their own background, so the row is one pass.
    sel_from = -1
    sel_to = -1
    if sel_range
      sel = line_selection_cols(line_idx, line_len)
      if sel
        vstart = sel[0] - col0
        vend   = sel[1] - col0
        vstart = 0 if vstart < 0
        vend = nchars if vend > nchars
        if vstart < vend
          c0 = cell_offset(widths, vstart)
          c1 = cell_offset(widths, vend)
          @gfx.fill_rect(x + c0 * CELL_W, y, (c1 - c0) * CELL_W, LINE_H, SEL_BG)
          sel_from = vstart
          sel_to = vend
        end
      end
      # Multi-line selection: fill from end of visible text to the right
      # edit margin so the wrapped newline is visible.
      last_seg = (col0 + nchars >= line_len)
      if last_seg && line_idx >= sel_range[1] && line_idx < sel_range[3]
        fill_x0 = x + used_cells * CELL_W
        fill_x1 = x + @edit_cols * CELL_W
        if fill_x0 < fill_x1
          @gfx.fill_rect(fill_x0, y, fill_x1 - fill_x0, LINE_H, SEL_BG)
        end
      end
    end

    return if nchars == 0

    # Tinted (breakpoint/stop) lines always render plain over their background.
    # The category map is per visible character and comes from EditorCore's
    # cache (recomputed only when the line changes).
    hl = (line_bg == BG_COLOR && @hl_enabled) ?
           EditorCore.render_hl(line_idx, col0, nchars) : ""
    draw_row_text(x, y, text, widths, nchars, hl, line_bg, sel_from, sel_to)
  end

  # How many characters of the width map fit in +max_cells+. A full-width
  # character at the right edge is left out rather than drawn half.
  def visible_chars(widths, max_cells)
    cells = 0
    i = 0
    n = widths.bytesize
    while i < n
      w = widths.getbyte(i)
      break if cells + w > max_cells
      cells += w
      i += 1
    end
    i
  end

  # Cell offset of visible character +i+ (its column on screen).
  def cell_offset(widths, i)
    c = 0
    k = 0
    n = widths.bytesize
    n = i if i < n
    while k < n
      c += widths.getbyte(k)
      k += 1
    end
    c
  end

  # Byte length of the UTF-8 character whose first byte is +b0+.
  def char_bytes(b0)
    return 1 if b0 < 0x80
    return 2 if b0 < 0xE0
    return 3 if b0 < 0xF0
    4
  end

  # Draw one visible row in as few draw_text commands as the colours and the
  # gfx text buffer allow: the byte walk emits a command when the highlight
  # category changes, when the selection starts or ends, or when the next
  # character would push the command past DRAW_TEXT_MAX_BYTES.
  #
  # +widths+ places each character (one or two cells), +hl+ carries one category
  # byte per character, and both are indexed by the same character number as
  # +text+ -- which is why the walk tracks a byte offset and a character index
  # separately.
  def draw_row_text(x, y, text, widths, nchars, hl, plain_bg, sel_from, sel_to)
    hl_len = hl.bytesize
    i = 0
    b = 0
    cell = 0
    while i < nchars
      cat = i < hl_len ? hl.getbyte(i) : 0
      sel = (i >= sel_from && i < sel_to)
      color = sel ? TEXT_COLOR : (HL_COLORS[cat] || TEXT_COLOR)
      bg = sel ? SEL_BG : plain_bg

      b0 = b
      c0 = cell
      bytes = 0
      while i < nchars
        nb = char_bytes(text.getbyte(b))
        break if bytes > 0 && bytes + nb > DRAW_TEXT_MAX_BYTES
        ncat = i < hl_len ? hl.getbyte(i) : 0
        break if ncat != cat
        nsel = (i >= sel_from && i < sel_to)
        break if nsel != sel
        bytes += nb
        b += nb
        cell += widths.getbyte(i)
        i += 1
      end
      @gfx.draw_text(x + c0 * CELL_W, y, text.byteslice(b0, bytes), color, bg)
    end
  end

  # Where the cursor sits on screen: [x, y, cells] in pixels, or nil when it is
  # not on a visible row. The completion popup anchors on this too, so both
  # agree in every layout (wrapped or not, window or fullscreen).
  def cursor_cell_box
    # Which screen row holds the cursor: its segment, counted from the anchor.
    seg = segment_of(@cy, @cx)
    screen_row = row_of(@cy, seg)
    return nil if screen_row < 0
    col0 = segment_start(@cy, seg)
    return nil if @cx < col0

    # The cursor sits on a character, so its column and its width both come from
    # the width map: it covers two cells on a full-width character.
    widths = EditorCore.render_width(@cy, col0, @edit_cols)
    idx = @cx - col0
    cell = cell_offset(widths, idx)
    return nil if cell >= @edit_cols
    w_cells = idx < widths.bytesize ? widths.getbyte(idx) : 1
    w_cells = @edit_cols - cell if cell + w_cells > @edit_cols

    [@user_area_x0 + 1 + @gutter_w + cell * CELL_W,
     @edit_y + screen_row * LINE_H,
     w_cells]
  end

  def draw_cursor
    box = cursor_cell_box
    return if box.nil?
    x = box[0]
    y = box[1]
    w_cells = box[2]

    # Draw block cursor
    @gfx.fill_rect(x, y, w_cells * CELL_W, LINE_H, CURSOR_COLOR)

    # Draw character under cursor in contrasting color
    ch = EditorCore.char_at(@cy, @cx)
    @gfx.draw_text(x, y, ch, BG_COLOR, CURSOR_COLOR) if ch.bytesize > 0
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
    draw_completion if @comp_open
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
    begin_edit_font
    row = 0
    ly = @scroll_y
    ls = @anchor_seg
    while row < @edit_rows
      if ly >= EditorCore.line_count
        row += 1
        next
      end
      draw_edit_row(row, ly, ls, sel_range, true) if dirty_line?(ly)
      ls += 1
      if ls >= line_segments(ly)
        ly += 1
        ls = 0
      end
      row += 1
    end
    draw_cursor
    end_edit_font
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
       @comp_open || dbg_modal?
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
    @gfx.draw_text(tx, ty, FmrbI18n.t(:unsaved).to_s,
                   QUIT_DLG_TEXT, QUIT_DLG_BG, mixed: true)
    @gfx.draw_text(tx, ty + CHAR_H + 2, FmrbI18n.t(:save_before_exit).to_s,
                   QUIT_DLG_TEXT, QUIT_DLG_BG, mixed: true)

    # Laid out left to right from measured widths: the words differ per
    # language, the bracketed keys do not.
    by = ty + (CHAR_H + 2) * 2 + 2
    bx = tx
    bx += draw_quit_choice(bx, by, "Y", FmrbI18n.t(:q_yes).to_s) + CHAR_W
    bx += draw_quit_choice(bx, by, "N", FmrbI18n.t(:q_no).to_s) + CHAR_W
    draw_quit_choice(bx, by, "C", FmrbI18n.t(:q_cancel).to_s)
  end

  # Draws "[K]word" and returns its pixel width.
  def draw_quit_choice(x, y, key_char, rest)
    @gfx.draw_text(x,            y, "[",       QUIT_DLG_TEXT, QUIT_DLG_BG)
    @gfx.draw_text(x + CHAR_W,   y, key_char,  QUIT_DLG_KEY,  QUIT_DLG_BG)
    @gfx.draw_text(x + 2*CHAR_W, y, "]" + rest, QUIT_DLG_TEXT, QUIT_DLG_BG, mixed: true)
    2 * CHAR_W + FmrbI18n.text_width("]" + rest)
  end

  # Answered by scancode (HID Usage ID), not by character: kana input mode
  # withholds the ASCII letter of every romaji key, and "n" is one of them, so
  # the character form left this dialog unanswerable while kana was on. The
  # scancode is passed through untouched in every mode. It also gets ESC
  # working here for the first time -- the keymap gives it no character at all,
  # so the old `when 27` never fired.
  # 0x1C=Y, 0x11=N, 0x06=C, 0x29=Esc.
  def handle_quit_dialog_key(ev)
    case ev[:scancode] || 0
    when 0x1C
      save_file
      stop unless @modified  # Save failed (e.g. no current_file) -> stay open
      @need_redraw = true
    when 0x11
      stop
    when 0x06, 0x29
      @quit_dialog_open = false
      @need_redraw = true
    end
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

  # ---- Key handling ----

  def handle_key(ch)
    if ch >= 0x80
      s = utf8_feed(ch)
      insert_char(s) if s
      return
    end
    utf8_reset
    case ch
    when 10, 13  # Enter
      handle_enter
    when 8       # Backspace
      handle_backspace
    when 127     # Delete (some terminals)
      handle_delete
    when 9       # Tab
      insert_indent
    when 32..126 # Printable
      insert_char(printable_char(ch))
      # "(" and "," are where the answer to "what goes here" changes, so they
      # are the only keys that ask for it.
      show_signature_help if ch == 40 || ch == 44
    end
  end

  # Feed one byte of a possibly multi-byte character. Returns the finished
  # character as a String, or nil while more bytes are needed. Used by both
  # the document and the Find field, so Japanese can be typed into either.
  def utf8_feed(byte)
    if @u8_need == 0
      if byte >= 0xF0
        need = 3
      elsif byte >= 0xE0
        need = 2
      elsif byte >= 0xC0
        need = 1
      else
        return nil  # continuation byte with nothing in front of it
      end
      @u8_buf = one_byte_string(byte)
      @u8_need = need
      return nil
    end
    # A byte that is not a continuation means the sequence was cut short.
    # Drop what we had and start over with this byte.
    if byte < 0x80 || byte >= 0xC0
      utf8_reset
      return utf8_feed(byte)
    end
    @u8_buf += one_byte_string(byte)
    @u8_need -= 1
    return nil if @u8_need > 0
    s = @u8_buf
    utf8_reset
    s
  end

  def utf8_reset
    @u8_buf = ""
    @u8_need = 0
  end

  # One-character String holding +byte+ (no Array#pack in picoruby).
  def one_byte_string(byte)
    s = " ".dup
    s.setbyte(0, byte)
    s
  end

  # One-character String for a printable ASCII code ("" outside 32..126).
  def printable_char(code)
    return "" if code < 32 || code > 126
    ASCII_PRINTABLE[code - 32, 1]
  end

  def insert_indent
    ti = 0
    while ti < TAB_SIZE
      insert_char(' ')
      ti += 1
    end
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

  # Up and down move by screen row, not by logical line: inside a folded line
  # that means the previous or next segment, and the column is kept as a cell
  # position so the cursor tracks down the page the way it looks like it should.
  def move_up
    if @wrap_on
      seg = segment_of(@cy, @cx)
      if seg > 0
        move_cursor_to_segment(@cy, seg - 1)
        return
      end
      if @cy > 0
        move_cursor_to_segment(@cy - 1, line_segments(@cy - 1) - 1)
      end
      return
    end
    if @cy > 0
      @cy -= 1
      clamp_cx
      ensure_cursor_visible
      mark_dirty_range(@cy, @cy + 1)
    end
  end

  def move_down
    if @wrap_on
      seg = segment_of(@cy, @cx)
      if seg + 1 < line_segments(@cy)
        move_cursor_to_segment(@cy, seg + 1)
        return
      end
      if @cy < EditorCore.line_count - 1
        move_cursor_to_segment(@cy + 1, 0)
      end
      return
    end
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
    if @wrap_on
      move_cursor_rows(-@edit_rows)
      return
    end
    prev_cy = @cy
    @cy -= @edit_rows
    @cy = 0 if @cy < 0
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  def page_down
    if @wrap_on
      move_cursor_rows(@edit_rows)
      return
    end
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

  # Cell column of the cursor within its own segment. Vertical movement aims
  # for the same one on the target segment, which is what makes the cursor look
  # like it goes straight down.
  def cursor_cell_column
    seg = segment_of(@cy, @cx)
    col0 = segment_start(@cy, seg)
    widths = EditorCore.render_width(@cy, col0, @edit_cols)
    cell_offset(widths, @cx - col0)
  end

  # Character index in (y, seg) nearest to cell column +want+.
  def char_at_cell(y, seg, want)
    col0 = segment_start(y, seg)
    limit = segment_chars(y, seg)
    widths = EditorCore.render_width(y, col0, @edit_cols)
    n = widths.bytesize
    n = limit if limit < n
    cells = 0
    i = 0
    while i < n
      w = widths.getbyte(i)
      break if cells + w > want
      cells += w
      i += 1
    end
    col0 + i
  end

  def move_cursor_to_segment(y, seg)
    want = cursor_cell_column
    prev_cy = @cy
    @cy = y
    @cx = char_at_cell(y, seg, want)
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  # Move the cursor +rows+ screen rows, following segments across lines.
  def move_cursor_rows(rows)
    want = cursor_cell_column
    prev_cy = @cy
    y = @cy
    seg = segment_of(@cy, @cx)
    step = rows > 0 ? 1 : -1
    n = rows > 0 ? rows : -rows
    i = 0
    while i < n
      if step > 0
        if seg + 1 < line_segments(y)
          seg += 1
        else
          break if y + 1 >= EditorCore.line_count
          y += 1
          seg = 0
        end
      else
        if seg > 0
          seg -= 1
        else
          break if y == 0
          y -= 1
          seg = line_segments(y) - 1
        end
      end
      i += 1
    end
    @cy = y
    @cx = char_at_cell(y, seg, want)
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  def clamp_cx
    len = EditorCore.line_length(@cy)
    @cx = len if @cx > len
  end

  # ---- Menu dropdown (File / Edit) ----

  # Dropdown contents. Built per open rather than held in a constant: the words
  # depend on the language, and a menu opens rarely enough for the allocation
  # not to matter.
  def menu_file_items
    [FmrbI18n.t(:open).to_s, FmrbI18n.t(:save).to_s, FmrbI18n.t(:save_as).to_s,
     FmrbI18n.t(:template).to_s, FmrbI18n.t(:exit).to_s]
  end

  def menu_edit_items
    [FmrbI18n.t(:cut).to_s, FmrbI18n.t(:copy).to_s, FmrbI18n.t(:paste).to_s,
     FmrbI18n.t(:select_all).to_s]
  end

  def menu_items
    case @active_menu
    when :file then menu_file_items
    when :edit then menu_edit_items
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

  # Dropdown panel width: the widest item plus padding. Measured rather than
  # fixed, for the same reason the menu bar is.
  def dropdown_width(items)
    widest = 0
    items.each do |item|
      w = FmrbI18n.text_width(item.to_s)
      widest = w if w > widest
    end
    widest + 10
  end

  def menu_width
    case @active_menu
    when :template then dropdown_width(@template_labels)
    when :debug then dbg_menu_width
    else dropdown_width(menu_items)
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
        @gfx.draw_text(x + 4, item_y + 1, item.to_s,
                       DROPDOWN_SEL_TEXT, DROPDOWN_SEL_BG, mixed: true)
      else
        @gfx.draw_text(x + 4, item_y + 1, item.to_s,
                       DROPDOWN_TEXT, DROPDOWN_BG, mixed: true)
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
      flash_status(FmrbI18n.t(:b_no_templates).to_s)
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
      flash_status(FmrbI18n.t(:b_load_failed).to_s)
      Log.error("Template read failed: #{path} (#{err.message})")
      return
    end
    body = text.to_s
    if body.bytesize == 0
      flash_status(FmrbI18n.t(:b_empty).to_s)
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
    flash_status(FmrbI18n.t(:b_inserted).to_s)
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
        flash_status(FmrbI18n.t(:b_too_large).to_s)
        Log.error("Load failed (document arena full): #{path}")
      else
        flash_status(FmrbI18n.t(:b_load_failed).to_s)
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
    flash_status(FmrbI18n.t(:b_doc_full).to_s)
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
      flash_status(FmrbI18n.t(:b_save_failed).to_s)
      Log.error("Failed to save file: #{@current_file} (err=#{written})")
    elsif written != expected
      flash_status(FmrbI18n.t(:b_save_failed).to_s)
      Log.error("Save mismatch for #{@current_file}: expected=#{expected}, written=#{written}")
    else
      @modified = false
      flash_status(FmrbI18n.t(:b_saved).to_s, true)  # green: a save that worked
      Log.info("Saved file: #{@current_file} (#{written} bytes)")
      # A save is the natural moment to check the file: the text has just
      # stopped moving, and the reply keeps the "Saved" message unless there is
      # something to say.
      diagnose_after_save
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
        flash_status("#{FmrbI18n.t(:b_run_pid).to_s} #{@run_pid}")
        Log.info("Run started: #{msg["path"]} pid=#{@run_pid}")
      else
        flash_status(FmrbI18n.t(:b_run_failed).to_s)
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
      flash_status(FmrbI18n.t(:b_run_path).to_s)
      return
    end
    save_file if @modified
    request_run(@current_file, @run_pid)
    flash_status(FmrbI18n.t(:b_running).to_s)
  end

  # What the spawner can load: any absolute path, wherever the buffer was
  # saved. The kernel enforces the same rule (run_path_allowed?); checking here
  # just gives a better message than a silent no-op.
  def runnable_path?(path)
    path.start_with?("/")
  end

  # Put a message in the status line's message zone -- the only way anything
  # writes there. It clears on the key after the one that raised it, or after
  # STATUS_MSG_FRAMES ticks, whichever comes first. +ok+ marks the green
  # success flavour (Save uses it).
  def flash_status(text, ok = false)
    @status_msg = " #{text} "
    @status_msg_ok = ok
    @status_msg_frames = STATUS_MSG_FRAMES
    # The key that raised this message must not also clear it.
    @status_msg_fresh = true
    @dirty_status = true
  end

  def clear_status_message
    return if @status_msg.nil?
    @status_msg = nil
    @status_msg_ok = false
    @status_msg_frames = 0
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

    if ev[:type] == :mouse_up
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
