# Confirm Dialog module for SystemDesktopApp
# Provides a generic Yes/No confirmation dialog

module ConfirmDialogMixin
  CDLG_W = 200
  CDLG_H = 80
  CDLG_BG = FmrbConst::THEME_WINDOW_BG
  CDLG_TITLE_BG = FmrbConst::THEME_MENU_BG
  CDLG_TEXT = FmrbConst::THEME_TEXT
  CDLG_BTN_YES = 0x34  # Green
  CDLG_BTN_NO = FmrbConst::THEME_BUTTON

  def open_confirm_dialog(message, on_yes_cmd, on_yes_data = nil)
    @cdlg_open = true
    @cdlg_message = message
    @cdlg_on_yes_cmd = on_yes_cmd
    @cdlg_on_yes_data = on_yes_data
    @cdlg_x = (@window_width - CDLG_W) / 2
    @cdlg_y = (@window_height - CDLG_H) / 2
    notify_overlay_state(true, @cdlg_x, @cdlg_y, CDLG_W, CDLG_H)
    draw_foreground
  end

  def close_confirm_dialog
    return unless @cdlg_open
    @cdlg_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    draw_foreground
  end

  def draw_confirm_dialog
    return unless @cdlg_open
    x = @cdlg_x
    y = @cdlg_y

    # Window frame
    @gfx.fill_rect(x, y, CDLG_W, CDLG_H, CDLG_BG)
    @gfx.draw_rect(x, y, CDLG_W, CDLG_H, FmrbConst::THEME_BORDER)

    # Title bar
    @gfx.fill_rect(x + 1, y + 1, CDLG_W - 2, 13, CDLG_TITLE_BG)
    @gfx.draw_text(x + 4, y + 3, "Confirm", FmrbGfx::WHITE, CDLG_TITLE_BG)

    # Message
    msg = @cdlg_message || ""
    max_chars = (CDLG_W - 16) / 6
    msg = msg[0, max_chars] if msg.length > max_chars
    @gfx.draw_text(x + 8, y + 24, msg, CDLG_TEXT, CDLG_BG)

    # Yes button
    yes_x = x + CDLG_W / 2 - 50
    yes_y = y + CDLG_H - 24
    @gfx.fill_rect(yes_x, yes_y, 40, 16, CDLG_BTN_YES)
    @gfx.draw_text(yes_x + 10, yes_y + 4, "Yes", FmrbGfx::WHITE, CDLG_BTN_YES)

    # No button
    no_x = x + CDLG_W / 2 + 10
    no_y = y + CDLG_H - 24
    @gfx.fill_rect(no_x, no_y, 40, 16, CDLG_BTN_NO)
    @gfx.draw_text(no_x + 12, no_y + 4, "No", FmrbGfx::WHITE, CDLG_BTN_NO)
  end

  def handle_confirm_dialog_click(x, y)
    yes_x = @cdlg_x + CDLG_W / 2 - 50
    yes_y = @cdlg_y + CDLG_H - 24
    no_x = @cdlg_x + CDLG_W / 2 + 10
    no_y = @cdlg_y + CDLG_H - 24

    if x >= yes_x && x < yes_x + 40 && y >= yes_y && y < yes_y + 16
      # Yes clicked - send callback to kernel
      cmd = @cdlg_on_yes_cmd
      data = @cdlg_on_yes_data
      close_confirm_dialog
      if cmd
        msg = { "cmd" => cmd }
        if data.is_a?(Hash)
          data.each { |k, v| msg[k] = v }
        end
        send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, msg)
      end
    elsif x >= no_x && x < no_x + 40 && y >= no_y && y < no_y + 16
      # No clicked
      close_confirm_dialog
    end
  end

  def hit_confirm_dialog?(x, y)
    @cdlg_open &&
      x >= @cdlg_x && x < @cdlg_x + CDLG_W &&
      y >= @cdlg_y && y < @cdlg_y + CDLG_H
  end
end
