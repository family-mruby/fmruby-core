# TBD Dialog module for SystemDesktopApp
# Generic placeholder window shown for menu items whose target app or
# feature is not yet implemented (e.g. the "Config" entry today).

module TbdDialogMixin
  TBD_W = 160
  TBD_H = 60
  TBD_BG = FmrbConst::THEME_WINDOW_BG
  TBD_BORDER = FmrbConst::THEME_BORDER
  TBD_TITLE_BG = FmrbConst::THEME_MENU_BG
  TBD_TITLE_TEXT = FmrbConst::THEME_TEXT_LIGHT
  TBD_TEXT = FmrbConst::THEME_TEXT
  TBD_LABEL = FmrbGfx::GRAY

  def open_tbd_dialog(title = "TBD")
    @tbd_open = true
    @tbd_title = title
    @tbd_x = (@window_width - TBD_W) / 2
    @tbd_y = (@window_height - TBD_H) / 2
    @tbd_y = 12 if @tbd_y < 12
    notify_overlay_state(true, @tbd_x, @tbd_y, TBD_W, TBD_H)
    update_composite_regions
    draw_foreground
  end

  def close_tbd_dialog
    return unless @tbd_open
    @tbd_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def draw_tbd_dialog
    return unless @tbd_open
    x = @tbd_x
    y = @tbd_y
    @gfx.fill_rect(x, y, TBD_W, TBD_H, TBD_BG)
    @gfx.draw_rect(x, y, TBD_W, TBD_H, TBD_BORDER)
    @gfx.fill_rect(x + 1, y + 1, TBD_W - 2, 13, TBD_TITLE_BG)
    @gfx.draw_text(x + 6, y + 3, @tbd_title.to_s, TBD_TITLE_TEXT, TBD_TITLE_BG, mixed: true)
    @gfx.draw_text(x + TBD_W / 2 - 12, y + 26, "TBD", TBD_TEXT, TBD_BG)
    @gfx.draw_text(x + TBD_W - 96, y + TBD_H - 10, "(click to close)", TBD_LABEL)
  end

  def hit_tbd_dialog?(x, y)
    @tbd_open &&
      x >= @tbd_x && x < @tbd_x + TBD_W &&
      y >= @tbd_y && y < @tbd_y + TBD_H
  end
end
