# Shell scroll and drawing mixin - wrapping, scrollback, scrollbar, rendering

module ShellScrollMixin
  # Logo rendering colors (used by draw_logo_line)
  LOGO_GRAD_COLORS = [0xE0, 0xE1, 0xE2, 0xE3, 0xE3]
  LOGO_SHADOW_COLORS = [0xB6, 0xB6, 0xB6, 0xB6, 0xB6]
  LOGO_AUTHOR_COLOR = 0xE3

  # ---- Wrapping & scroll helpers ----

  # Display columns of a text: ASCII is one cell (6px), kana/kanji are two
  # (12px glyphs). The input line wraps and places its cursor by columns;
  # history keeps the old character count (its entries are output, and a
  # miscounted width there costs a ragged wrap, not a lost cursor).
  def text_cols(text)
    n = text.length
    return n if text.bytesize == n  # pure ASCII: one column per byte
    cols = 0
    i = 0
    while i < n
      cols += text[i].bytesize > 1 ? 2 : 1
      i += 1
    end
    cols
  end

  # Split into rows of at most @max_chars display columns; a double-width
  # character never straddles the fold, it moves whole to the next row.
  # Always returns at least one (possibly empty) row.
  def wrap_chunks(text)
    # Pure ASCII: slice by count, at C speed.
    if text.bytesize == text.length
      return [text] if text.length <= @max_chars
      rows = []
      i = 0
      while i < text.length
        rows << text[i, @max_chars].to_s
        i += @max_chars
      end
      return rows
    end
    rows = []
    row = ""
    cols = 0
    i = 0
    n = text.length
    while i < n
      ch = text[i]
      w = ch.bytesize > 1 ? 2 : 1
      if cols + w > @max_chars && cols > 0
        rows << row
        row = ""
        cols = 0
      end
      row += ch
      cols += w
      i += 1
    end
    rows << row
    rows
  end

  def input_rows_for(text)
    rows = (text_cols(text) + @max_chars - 1) / @max_chars
    rows < 1 ? 1 : rows
  end

  # Cursor cell (row, column) in the input line, folded exactly the way
  # wrap_chunks folds the text. During foreground script input the cursor
  # simply follows the end of the text.
  def input_cursor_rc(input_text)
    upto = @fg_sandbox ? input_text.length : @prompt.length + @cursor_pos
    row = 0
    cols = 0
    i = 0
    while i < upto
      ch = input_text[i]
      break if ch.nil?
      w = ch.bytesize > 1 ? 2 : 1
      if cols + w > @max_chars && cols > 0
        row += 1
        cols = 0
      end
      cols += w
      i += 1
    end
    [row, cols]
  end

  def draw_input_cursor(content_x, input_y, input_text)
    r, c = input_cursor_rc(input_text)
    cursor_x = content_x + c * @char_width
    cursor_y = input_y + r * @char_height + @char_height - 1
    @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, @ch_col)
  end

  def display_rows_for(entry)
    if entry.is_a?(Hash) && entry[:type] == :logo_line
      1
    else
      text = entry.to_s
      return 1 if text.empty?
      if text.bytesize == text.length
        (text.length + @max_chars - 1) / @max_chars
      else
        # Must agree with wrap_chunks: an early fold before a double-width
        # character can use a row that ceil(cols/max) does not count.
        wrap_chunks(text).size
      end
    end
  end

  def total_display_rows
    rows = 0
    i = 0
    n = @history.length
    while i < n
      rows += display_rows_for(@history[i])
      i += 1
    end
    rows
  end

  def current_input_text
    if @fg_sandbox && @script_input_line
      @script_input_line
    elsif !@fg_sandbox
      @prompt + @current_line
    else
      ""
    end
  end

  def history_avail_rows
    input_rows = input_rows_for(current_input_text)
    avail = @visible_rows - input_rows
    avail < 1 ? 1 : avail
  end

  def scroll_to_bottom
    @auto_scroll = true
    max_s = total_display_rows - history_avail_rows
    @scroll = max_s > 0 ? max_s : 0
  end

  def scroll_up
    if @scroll > 0
      @scroll -= 1
      @auto_scroll = false
      @need_full_redraw = true
    end
  end

  def scroll_down
    max_s = total_display_rows - history_avail_rows
    max_s = 0 if max_s < 0
    if @scroll < max_s
      @scroll += 1
      @auto_scroll = (@scroll >= max_s)
      @need_full_redraw = true
    end
  end

  def scroll_page_up
    step = history_avail_rows / 2
    step = 1 if step < 1
    @scroll -= step
    @scroll = 0 if @scroll < 0
    @auto_scroll = false
    @need_full_redraw = true
  end

  def scroll_page_down
    step = history_avail_rows / 2
    step = 1 if step < 1
    max_s = total_display_rows - history_avail_rows
    max_s = 0 if max_s < 0
    @scroll += step
    if @scroll >= max_s
      @scroll = max_s
      @auto_scroll = true
    end
    @need_full_redraw = true
  end

  # ---- Drawing ----

  # All shell text draws in efontJA_12 (same terminal-cell model as the
  # editor: half-width one 6px cell, full-width two). The window chrome
  # outside these brackets stays on the default font.
  def begin_text_font
    @gfx.set_font(:ja, 12)
  end

  def end_text_font
    @gfx.set_font(:default)
  end

  def draw_wrapped_text_at(content_x, base_y, text, max_rows)
    chunks = wrap_chunks(text)
    r = 0
    n = chunks.size
    while r < n && r < max_rows
      chunk = chunks[r]
      @gfx.draw_text(content_x, base_y + r * @char_height, chunk, @ch_col) unless chunk.empty?
      r += 1
    end
    n
  end

  # Draw a single logo line with gradient background colors
  def draw_logo_line(x, y, logo_entry)
    data = logo_entry[:data]
    author_line = logo_entry[:author_line]
    margin = logo_entry[:margin] || ""
    is_last_line = logo_entry[:is_last_line]
    grad_slice_width = logo_entry[:grad_slice_width] || (data.length / LOGO_GRAD_COLORS.length)

    # Apply margin offset
    char_x = x + (margin.length * @char_width)

    i = 0
    n = data.length
    while i < n
      c = data[i]

      # Calculate gradient index (0 to LOGO_GRAD_COLORS.length - 1)
      grad_index = i / grad_slice_width
      grad_index = LOGO_GRAD_COLORS.length - 1 if grad_index >= LOGO_GRAD_COLORS.length

      # On the last line, author text takes priority over shadow
      if is_last_line && i < author_line.length && author_line[i] != ' '
        # Author text character on background
        @gfx.draw_text(char_x, y, author_line[i], LOGO_AUTHOR_COLOR, @bg_col)
      else
        case c
        when '1'
          # Logo body: space with gradient color background
          @gfx.draw_text(char_x, y, " ", FmrbGfx::WHITE, LOGO_GRAD_COLORS[grad_index])
        when '2'
          # Shadow: space with darker gradient color background
          @gfx.draw_text(char_x, y, " ", FmrbGfx::WHITE, LOGO_SHADOW_COLORS[grad_index])
        # when '0' - skip (use existing background)
        end
      end
      char_x += @char_width
      i += 1
    end
  end

  def draw_prompt
    begin_text_font
    content_x = @user_area_x0 + 2
    avail = history_avail_rows
    total = total_display_rows

    # Draw history (scrollable, with wrapping)
    start_row = @scroll
    current_row = 0
    screen_row = 0

    hi = 0
    hn = @history.length
    while hi < hn
      entry = @history[hi]
      entry_rows = display_rows_for(entry)
      entry_end = current_row + entry_rows

      if entry_end > start_row && screen_row < avail
        vis_start = start_row > current_row ? start_row - current_row : 0

        if entry.is_a?(Hash) && entry[:type] == :logo_line
          if vis_start == 0
            y = @user_area_y0 + 2 + screen_row * @char_height
            draw_logo_line(content_x, y, entry)
            screen_row += 1
          end
        else
          chunks = wrap_chunks(entry.to_s)
          r = vis_start
          while r < entry_rows && screen_row < avail
            chunk = chunks[r] || ""
            y = @user_area_y0 + 2 + screen_row * @char_height
            @gfx.draw_text(content_x, y, chunk, @ch_col) unless chunk.empty?
            screen_row += 1
            r += 1
          end
        end
      end

      current_row = entry_end
      break if screen_row >= avail
      hi += 1
    end

    # Draw input line (after history, or at bottom if history fills screen)
    input_text = current_input_text
    input_rows = input_rows_for(input_text)
    # Place input right after history, but no lower than the fixed bottom position
    input_y_after_history = @user_area_y0 + 2 + screen_row * @char_height
    input_y_bottom = @user_area_y0 + 2 + avail * @char_height
    input_y = input_y_after_history < input_y_bottom ? input_y_after_history : input_y_bottom
    draw_wrapped_text_at(content_x, input_y, input_text, input_rows)

    draw_input_cursor(content_x, input_y, input_text)
    end_text_font
  end

  def redraw_screen
    # Full redraw: Clear user area and redraw everything
    clear_user_area(@bg_col)
    draw_window_frame
    draw_prompt

    # Draw scrollbar if history exceeds visible area
    total = total_display_rows
    avail = history_avail_rows
    # ShellApp:: is required: a bare constant inside a mixin is looked up in
    # the module, not in the class that included it.
    sb_w = ShellApp::SB_W
    @ui.move(:sb, @user_area_width - sb_w, 0, sb_w, avail * @char_height + 2)
    @ui.set_range(:sb, total, avail)
    @ui.set_value(:sb, @scroll)
    @ui.invalidate_all
    @ui.flush

    @gfx.present
  end

  def redraw_input_line
    begin_text_font
    input_text = current_input_text
    input_rows = input_rows_for(input_text)

    # If input row count changed (wrap boundary crossed), do full redraw
    if input_rows != @prev_input_rows
      @prev_input_rows = input_rows
      @need_full_redraw = true
      end_text_font
      return
    end

    content_x = @user_area_x0 + 2
    avail = history_avail_rows
    total = total_display_rows
    # Visible history rows (may be less than avail if history is short)
    visible_hist = total - @scroll
    visible_hist = avail if visible_hist > avail
    visible_hist = 0 if visible_hist < 0
    input_y_after = @user_area_y0 + 2 + visible_hist * @char_height
    input_y_bottom = @user_area_y0 + 2 + avail * @char_height
    input_y = input_y_after < input_y_bottom ? input_y_after : input_y_bottom

    # Clear input area
    @gfx.fill_rect(@user_area_x0 + 1, input_y,
                    @user_area_width - 2 - FmrbApp::SCROLLBAR_W,
                    input_rows * @char_height, @bg_col)

    # Draw wrapped input
    draw_wrapped_text_at(content_x, input_y, input_text, input_rows)

    draw_input_cursor(content_x, input_y, input_text)
    end_text_font

    @gfx.present
  end
end
