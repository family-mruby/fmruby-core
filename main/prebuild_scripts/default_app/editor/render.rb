# All the editor's drawing: menu bar, status line, edit rows, cursor,
# plus the render-latency instrumentation and the quit-confirm dialog.
# Split out of editor.app.rb (doc/editor_refactor). The many theme/layout/
# menu constants come from EditorConst (included below).
module EditorRender
  include EditorConst

  # ---- Drawing ----

  # Localized word for a menu bar item (no accelerator, no state mark).
  def menu_bar_label(id)
    case id
    when MENU_ID_FILE    then FmrbI18n.t(:m_file).to_s
    when MENU_ID_EDIT    then FmrbI18n.t(:m_edit).to_s
    when MENU_ID_SEARCH  then FmrbI18n.t(:m_search).to_s
    when MENU_ID_RUN     then FmrbI18n.t(:m_run).to_s
    when MENU_ID_VIEW    then FmrbI18n.t(:m_view).to_s
    when MENU_ID_DEBUG   then dbg_menu_label.to_s
    when MENU_ID_KEYS    then FmrbI18n.t(:m_keys).to_s
    else ""
    end
  end

  # Trailing state mark: "*" when the item's mode is on, a space when it is off
  # (so the label does not jump sideways), "" when the item has no state.
  def menu_bar_mark(id)
    case id
    when MENU_ID_DEBUG then dbg_menu_mark.to_s
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
    @menu_view_x = nil
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
      @menu_view_x = x if id == MENU_ID_VIEW
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
    when MENU_ID_VIEW    then open_menu(:view)
    when MENU_ID_DEBUG   then open_menu(:debug)
    when MENU_ID_KEYS    then open_keys_list
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
    draw_keys_list if @keys_open
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
       @comp_open || @keys_open || dbg_modal?
      redraw_all
    else
      redraw_dirty
    end
    clear_dirty
    lat_sample(t_start)
  end

  # ---- Quit-confirm dialog ----


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

end
