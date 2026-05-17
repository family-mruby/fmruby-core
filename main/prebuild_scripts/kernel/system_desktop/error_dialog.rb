# Error Dialog module for SystemDesktopApp
# Displays script compilation errors and uncaught exceptions with backtrace

module ErrorDialogMixin
  EDLG_W = 310
  EDLG_LINE_H = 10
  EDLG_MAX_LINES = 12
  # Near-black opaque color. FmrbGfx::BLACK (0x00) collides with the foreground
  # canvas color-key on some paths and renders transparent.
  EDLG_BG = 0x24
  EDLG_BORDER = FmrbGfx::RED
  EDLG_TITLE_BG = FmrbGfx::RED
  EDLG_TEXT = FmrbGfx::YELLOW
  EDLG_TRACE = FmrbGfx::GRAY

  def open_error_dialog(app_name, error_msg)
    @error_dlg_open = true
    @error_dlg_name = app_name

    # Split error message into lines (backtrace separated by \n)
    # Then wrap long lines
    max_chars = (EDLG_W - 16) / 6
    raw_lines = (error_msg || "").split("\n")
    @error_dlg_lines = []
    @error_dlg_colors = []
    raw_lines.each_with_index do |line, i|
      color = (i == 0) ? EDLG_TEXT : EDLG_TRACE
      while line.length > max_chars
        @error_dlg_lines << line[0, max_chars]
        @error_dlg_colors << color
        line = line[max_chars..]
      end
      @error_dlg_lines << line
      @error_dlg_colors << color
    end

    # Calculate dialog height based on content
    n_lines = @error_dlg_lines.length
    if n_lines > EDLG_MAX_LINES
      n_lines = EDLG_MAX_LINES
    end
    @error_dlg_h = 18 + n_lines * EDLG_LINE_H + 16
    @error_dlg_x = (@window_width - EDLG_W) / 2
    @error_dlg_y = (@window_height - @error_dlg_h) / 2
    if @error_dlg_y < 12
      @error_dlg_y = 12
    end

    notify_overlay_state(true, @error_dlg_x, @error_dlg_y, EDLG_W, @error_dlg_h)
    update_composite_regions
    draw_foreground
  end

  def close_error_dialog
    return unless @error_dlg_open
    @error_dlg_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def draw_error_dialog
    return unless @error_dlg_open
    x = @error_dlg_x
    y = @error_dlg_y
    h = @error_dlg_h
    max_chars = (EDLG_W - 16) / 6

    # Window frame
    @gfx.fill_rect(x, y, EDLG_W, h, EDLG_BG)
    @gfx.draw_rect(x, y, EDLG_W, h, EDLG_BORDER)
    @gfx.draw_rect(x + 1, y + 1, EDLG_W - 2, h - 2, EDLG_BORDER)

    # Title bar
    @gfx.fill_rect(x + 2, y + 2, EDLG_W - 4, 13, EDLG_TITLE_BG)
    title = "Error: #{@error_dlg_name}"
    title = title[0, max_chars] if title.length > max_chars
    @gfx.draw_text(x + 4, y + 4, title, FmrbGfx::WHITE, EDLG_TITLE_BG)

    # Error message + backtrace lines (pre-wrapped)
    ty = y + 18
    lines = @error_dlg_lines || []
    colors = @error_dlg_colors || []
    displayed = 0
    lines.each_with_index do |line, i|
      break if displayed >= EDLG_MAX_LINES
      color = colors[i] || EDLG_TRACE
      @gfx.draw_text(x + 8, ty, line, color, EDLG_BG)
      ty += EDLG_LINE_H
      displayed += 1
    end

    # Footer
    @gfx.draw_text(x + EDLG_W - 96, y + h - 12, "(click to close)", FmrbGfx::GRAY, EDLG_BG)
  end

  def handle_error_dialog_click(x, y)
    close_error_dialog
  end

  def hit_error_dialog?(x, y)
    @error_dlg_open &&
      x >= @error_dlg_x && x < @error_dlg_x + EDLG_W &&
      y >= @error_dlg_y && y < @error_dlg_y + @error_dlg_h
  end
end
