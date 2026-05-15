# About Dialog module for SystemDesktopApp
# Shows OS / Graphics-Audio / Link / system info (IDF, MAC, chip, flash, PSRAM, reset reason).

module AboutDialogMixin
  ABOUT_W = 220
  ABOUT_H = 146
  ABOUT_BG = FmrbConst::THEME_WINDOW_BG
  ABOUT_BORDER = FmrbConst::THEME_BORDER
  ABOUT_TITLE_BG = FmrbConst::THEME_MENU_BG
  ABOUT_TITLE_TEXT = FmrbConst::THEME_TEXT_LIGHT
  ABOUT_TEXT = FmrbConst::THEME_TEXT
  ABOUT_LABEL = FmrbGfx::GRAY

  def open_about_dialog
    @about_open = true
    @about_x = (@window_width - ABOUT_W) / 2
    @about_y = (@window_height - ABOUT_H) / 2
    @about_y = 12 if @about_y < 12
    notify_overlay_state(true, @about_x, @about_y, ABOUT_W, ABOUT_H)
    draw_foreground
  end

  def close_about_dialog
    return unless @about_open
    @about_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    draw_foreground
  end

  def draw_about_dialog
    return unless @about_open
    x = @about_x
    y = @about_y

    @gfx.fill_rect(x, y, ABOUT_W, ABOUT_H, ABOUT_BG)
    @gfx.draw_rect(x, y, ABOUT_W, ABOUT_H, ABOUT_BORDER)
    @gfx.fill_rect(x + 1, y + 1, ABOUT_W - 2, 13, ABOUT_TITLE_BG)
    @gfx.draw_text(x + 6, y + 3, "About Family mruby", ABOUT_TITLE_TEXT, ABOUT_TITLE_BG)

    ty = y + 20
    line_h = 10
    chip = "#{FmrbConst::CHIP_MODEL} #{FmrbConst::CHIP_REVISION} (#{FmrbConst::CHIP_CORES}c)"
    draw_about_row(x, ty,              "Copyright","(C) 2026 kishima")
    draw_about_row(x, ty + line_h,     "Core",     FmrbConst::OS_VERSION)
    draw_about_row(x, ty + line_h * 2, "GfxAudio", FmrbConst::GA_VERSION)
    draw_about_row(x, ty + line_h * 3, "Protocol", FmrbConst::LINK_VERSION.to_s)
    draw_about_row(x, ty + line_h * 4, "Platform", FmrbConst::PLATFORM)
    draw_about_row(x, ty + line_h * 5, "IDF Ver",  FmrbConst::IDF_VERSION)
    draw_about_row(x, ty + line_h * 6, "BT-MAC",   FmrbConst::MAC_ADDRESS)
    draw_about_row(x, ty + line_h * 7, "Chip",     chip)
    draw_about_row(x, ty + line_h * 8, "Flash",    "#{FmrbConst::FLASH_SIZE_MB}MB")
    draw_about_row(x, ty + line_h * 9, "PSRAM",    "#{FmrbConst::PSRAM_SIZE_MB}MB")
    draw_about_row(x, ty + line_h * 10,"Reset",    FmrbConst::RESET_REASON)

    @gfx.draw_text(x + ABOUT_W - 96, y + ABOUT_H - 10, "(click to close)", ABOUT_LABEL)
  end

  def draw_about_row(x, y, label, value)
    @gfx.draw_text(x + 10, y, label, ABOUT_LABEL, ABOUT_BG)
    @gfx.draw_text(x + 70, y, value, ABOUT_TEXT, ABOUT_BG)
  end

  def hit_about_dialog?(x, y)
    @about_open &&
      x >= @about_x && x < @about_x + ABOUT_W &&
      y >= @about_y && y < @about_y + ABOUT_H
  end
end
