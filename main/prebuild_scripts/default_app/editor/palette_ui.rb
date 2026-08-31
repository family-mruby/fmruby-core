# The editor's Colors dialog.
#
# Every colour the editor draws with, listed with the value in force, changed
# by typing a colour name (skyblue) or a number (0x1F), and kept in
# /home/colors.toml under [editor] -- the editor's own, leaving the system
# theme and every other app alone.
#
# The values arrive as constants (const.rb resolves theme + file at start), so
# a change here shows on the next start rather than under the cursor. The
# dialog says so instead of pretending otherwise; the shell, whose two colours
# live in variables, applies its `color` command at once.
#
# The file name is not the module name on purpose: the editor's mixins are
# globbed and sorted, and a file named colors_* would sort ahead of const.rb
# and find no EditorConst to include.
module EditorPalette
  include EditorConst

  PALETTE_ROWS = 8            # rows visible at once
  PALETTE_INPUT_MAX = 16
  PALETTE_SWATCH_COL = 18     # in characters, from the left edge of the list

  def palette_dialog_rect
    w = 30 * CHAR_W + 12
    h = (PALETTE_ROWS + 4) * CHAR_H + 16
    x = @user_area_x0 + (@user_area_width  - w) / 2
    y = @user_area_y0 + (@user_area_height - h) / 2
    [x, y, w, h]
  end

  def open_palette_dialog
    @palette_open = true
    @palette_index = 0
    @palette_top = 0
    @palette_input = nil
    @palette_status = FmrbI18n.t(:col_hint).to_s
    # A copy of the current values, so a row shows what was just chosen even
    # though the constant behind it belongs to this run of the editor.
    @palette_values = []
    i = 0
    while i < EDITOR_COLOR_VALUES.size
      @palette_values << EDITOR_COLOR_VALUES[i]
      i += 1
    end
    @need_redraw = true
  end

  def close_palette_dialog
    @palette_open = false
    @palette_input = nil
    @need_redraw = true
  end

  def draw_palette_dialog
    x, y, w, h = palette_dialog_rect
    @gfx.fill_rect(x, y, w, h, QUIT_DLG_BG)
    @gfx.draw_rect(x, y, w, h, QUIT_DLG_BORDER)
    @gfx.draw_rect(x + 1, y + 1, w - 2, h - 2, QUIT_DLG_BORDER)

    tx = x + 5
    ty = y + 4
    @gfx.draw_text(tx, ty, FmrbI18n.t(:m_colors).to_s,
                   QUIT_DLG_TEXT, QUIT_DLG_BG, mixed: true)

    ry = ty + CHAR_H + 3
    r = 0
    while r < PALETTE_ROWS
      idx = @palette_top + r
      break if idx >= EDITOR_COLOR_KEYS.size
      row_y = ry + r * CHAR_H
      selected = (idx == @palette_index)
      bg = selected ? SEL_BG : QUIT_DLG_BG
      @gfx.fill_rect(tx - 2, row_y, w - 8, CHAR_H, bg) if selected
      @gfx.draw_text(tx, row_y, EDITOR_COLOR_KEYS[idx], QUIT_DLG_TEXT, bg)
      sx = tx + PALETTE_SWATCH_COL * CHAR_W
      @gfx.fill_rect(sx, row_y + 1, CHAR_W * 3, CHAR_H - 2, @palette_values[idx])
      @gfx.draw_rect(sx, row_y + 1, CHAR_W * 3, CHAR_H - 2, QUIT_DLG_BORDER)
      r += 1
    end

    # The value of the selected row, or the field being typed into.
    vy = ry + PALETTE_ROWS * CHAR_H + 4
    key = EDITOR_COLOR_KEYS[@palette_index]
    if @palette_input.nil?
      shown = FmrbColors.to_text(@palette_values[@palette_index])
    else
      shown = @palette_input
    end
    line = key + " = " + shown
    @gfx.draw_text(tx, vy, line, QUIT_DLG_TEXT, QUIT_DLG_BG)
    unless @palette_input.nil?
      cx = tx + line.length * CHAR_W
      @gfx.fill_rect(cx, vy, CHAR_W, CHAR_H, CURSOR_COLOR)
    end

    sy = vy + CHAR_H + 3
    @gfx.draw_text(tx, sy, @palette_status, QUIT_DLG_TEXT, QUIT_DLG_BG,
                   mixed: true)
  end

  def handle_palette_key(ev)
    scancode = ev[:scancode] || 0
    character = ev[:character] || 0

    if @palette_input.nil?
      case scancode
      when 41                       # ESC
        close_palette_dialog
        return
      when 82                       # Up
        palette_move(-1)
        return
      when 81                       # Down
        palette_move(1)
        return
      when 75                       # PageUp
        palette_move(-PALETTE_ROWS)
        return
      when 78                       # PageDown
        palette_move(PALETTE_ROWS)
        return
      when 40, 88                   # Enter / Keypad-Enter: start typing
        @palette_input = ""
        @palette_status = FmrbI18n.t(:col_typing).to_s
        @need_redraw = true
        return
      end
      # r: this one back to the theme. x: everything the editor has saved.
      if character == 114 || character == 82
        palette_reset_one
      elsif character == 120 || character == 88
        palette_reset_all
      end
      return
    end

    case scancode
    when 41                         # ESC: leave the field alone
      @palette_input = nil
      @palette_status = FmrbI18n.t(:col_hint).to_s
      @need_redraw = true
      return
    when 40, 88                     # Enter: take it
      palette_commit
      return
    end

    case character
    when 8, 127                     # Backspace / Delete
      if @palette_input.length > 0
        @palette_input = @palette_input[0, @palette_input.length - 1]
        @need_redraw = true
      end
    when 32..126
      if @palette_input.length < PALETTE_INPUT_MAX
        @palette_input += printable_char(character)
        @need_redraw = true
      end
    end
  end

  def palette_move(delta)
    n = EDITOR_COLOR_KEYS.size
    i = @palette_index + delta
    i = 0 if i < 0
    i = n - 1 if i >= n
    @palette_index = i
    @palette_top = i if i < @palette_top
    @palette_top = i - PALETTE_ROWS + 1 if i >= @palette_top + PALETTE_ROWS
    @palette_top = 0 if @palette_top < 0
    @need_redraw = true
  end

  def palette_commit
    text = @palette_input.strip
    if text.length == 0
      @palette_input = nil
      @palette_status = FmrbI18n.t(:col_hint).to_s
      @need_redraw = true
      return
    end
    value = FmrbColors.to_color(text)
    if value.nil?
      @palette_status = FmrbI18n.t(:col_unknown).to_s
      @need_redraw = true
      return
    end
    @palette_values[@palette_index] = value
    ok = FmrbColors.set("editor", EDITOR_COLOR_KEYS[@palette_index], value)
    @palette_input = nil
    @palette_status = (ok ? FmrbI18n.t(:col_saved) : FmrbI18n.t(:col_failed)).to_s
    @need_redraw = true
  end

  def palette_reset_one
    ok = FmrbColors.set("editor", EDITOR_COLOR_KEYS[@palette_index], nil)
    @palette_status = (ok ? FmrbI18n.t(:col_saved) : FmrbI18n.t(:col_failed)).to_s
    @need_redraw = true
  end

  def palette_reset_all
    ok = FmrbColors.clear("editor")
    @palette_status = (ok ? FmrbI18n.t(:col_saved) : FmrbI18n.t(:col_failed)).to_s
    @need_redraw = true
  end
end
