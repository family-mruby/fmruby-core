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

  BTN_W = 40
  BTN_H = 16

  # Built once at startup and moved into place each time the dialog opens.
  def build_confirm_widgets
    @ui.button(:cdlg_yes, 0, 0, BTN_W, BTN_H, "Yes")
    @ui.button(:cdlg_no, 0, 0, BTN_W, BTN_H, "No")
    @ui.set_visible(:cdlg_yes, false)
    @ui.set_visible(:cdlg_no, false)
    nil
  end

  def place_confirm_widgets
    by = @cdlg_y + CDLG_H - 24
    @ui.move(:cdlg_yes, @cdlg_x + CDLG_W / 2 - 50, by, BTN_W, BTN_H)
    @ui.move(:cdlg_no, @cdlg_x + CDLG_W / 2 + 10, by, BTN_W, BTN_H)
    nil
  end

  def open_confirm_dialog(message, on_yes_cmd, on_yes_data = nil)
    @cdlg_open = true
    @cdlg_message = message
    @cdlg_on_yes_cmd = on_yes_cmd
    @cdlg_on_yes_data = on_yes_data
    @cdlg_x = (@window_width - CDLG_W) / 2
    @cdlg_y = (@window_height - CDLG_H) / 2
    place_confirm_widgets
    @ui.set_visible(:cdlg_yes, true)
    @ui.set_visible(:cdlg_no, true)
    notify_overlay_state(true, @cdlg_x, @cdlg_y, CDLG_W, CDLG_H)
    update_composite_regions
    draw_foreground
  end

  def close_confirm_dialog
    return unless @cdlg_open
    @cdlg_open = false
    # Hidden before the repaint, or their holes would be punched into
    # whatever is behind the dialog on the next flush.
    @ui.set_visible(:cdlg_yes, false)
    @ui.set_visible(:cdlg_no, false)
    @ui.flush
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
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

    # Message. Uses mixed:true so localized strings (e.g. JA "再起動しますか?")
    # render via misaki_8 for multi-byte and Font0 for ASCII in the same line.
    msg = @cdlg_message || ""
    msg = FmrbI18n.truncate_to(msg, CDLG_W - 16)
    @gfx.draw_text(x + 8, y + 24, msg, CDLG_TEXT, CDLG_BG, mixed: true)

    # The two buttons are FmrbUI's; they are repainted by the flush at the
    # end of draw_foreground, on top of the panel this just drew.
    @ui.invalidate_all
  end

  # id comes from FmrbUI; the coordinates are its business now.
  def handle_confirm_dialog_widget(id)
    if id == :cdlg_yes
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
    elsif id == :cdlg_no
      close_confirm_dialog
    end
    nil
  end

  def hit_confirm_dialog?(x, y)
    @cdlg_open &&
      x >= @cdlg_x && x < @cdlg_x + CDLG_W &&
      y >= @cdlg_y && y < @cdlg_y + CDLG_H
  end
end
