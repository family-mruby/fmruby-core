# About Dialog module for SystemDesktopApp
# Shows OS / Graphics-Audio / Link / system info (IDF, MAC, chip, flash, PSRAM, reset reason).

module AboutDialogMixin
  ABOUT_W = 220
  # Base height covers title bar + 11 info rows + copyright + close hint.
  # USB device rows are added on top of this in @about_h.
  ABOUT_H_BASE = 156
  ABOUT_LINE_H = 10
  ABOUT_BG = FmrbConst::THEME_WINDOW_BG
  ABOUT_BORDER = FmrbConst::THEME_BORDER
  ABOUT_TITLE_BG = FmrbConst::THEME_MENU_BG
  ABOUT_TITLE_TEXT = FmrbConst::THEME_TEXT_LIGHT
  ABOUT_TEXT = FmrbConst::THEME_TEXT
  ABOUT_LABEL = FmrbGfx::GRAY

  def open_about_dialog
    @about_open = true
    # Snapshot connected USB HID devices once at open time so the dialog
    # is consistent for as long as it stays on screen.
    @about_usb_devices = (FmrbApp.respond_to?(:usb_devices) ? FmrbApp.usb_devices : []) rescue []
    # USB section = 1 header row + N device rows (at least 1 for "(none)").
    usb_rows = 1 + [@about_usb_devices.size, 1].max
    @about_h = ABOUT_H_BASE + usb_rows * ABOUT_LINE_H
    @about_x = (@window_width - ABOUT_W) / 2
    @about_y = (@window_height - @about_h) / 2
    @about_y = 12 if @about_y < 12
    notify_overlay_state(true, @about_x, @about_y, ABOUT_W, @about_h)
    update_composite_regions
    draw_foreground
  end

  def close_about_dialog
    return unless @about_open
    @about_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def draw_about_dialog
    return unless @about_open
    x = @about_x
    y = @about_y
    h = @about_h

    @gfx.fill_rect(x, y, ABOUT_W, h, ABOUT_BG)
    @gfx.draw_rect(x, y, ABOUT_W, h, ABOUT_BORDER)
    @gfx.fill_rect(x + 1, y + 1, ABOUT_W - 2, 13, ABOUT_TITLE_BG)
    @gfx.draw_text(x + 6, y + 3, "About Family mruby", ABOUT_TITLE_TEXT, ABOUT_TITLE_BG)

    ty = y + 20
    line_h = ABOUT_LINE_H
    chip = "#{FmrbConst::CHIP_MODEL} #{FmrbConst::CHIP_REVISION} (#{FmrbConst::CHIP_CORES}c)"
    draw_about_row(x, ty,              "Core",     FmrbConst::OS_VERSION)
    draw_about_row(x, ty + line_h,     "GfxAudio", FmrbConst::GA_VERSION)
    draw_about_row(x, ty + line_h * 2, "Protocol", FmrbConst::LINK_VERSION.to_s)
    draw_about_row(x, ty + line_h * 3, "Platform", FmrbConst::PLATFORM)
    draw_about_row(x, ty + line_h * 4, "IDF Ver",  FmrbConst::IDF_VERSION)
    # Asked each time the dialog opens: on a machine whose radio is a separate
    # chip the address only becomes known once BLE has synced, long after the
    # constants were frozen.
    draw_about_row(x, ty + line_h * 5, "BT-MAC",   FmrbConst.bt_mac)
    draw_about_row(x, ty + line_h * 6, "Chip",     chip)
    draw_about_row(x, ty + line_h * 7, "Flash",    "#{FmrbConst::FLASH_SIZE_MB}MB")
    draw_about_row(x, ty + line_h * 8, "PSRAM",    "#{FmrbConst::PSRAM_SIZE_MB}MB")
    draw_about_row(x, ty + line_h * 9, "Reset",    FmrbConst::RESET_REASON)
    draw_about_row(x, ty + line_h * 10, "Built",   FmrbConst::BUILD_DATE)

    # USB section — header followed by one row per connected device, or
    # "(none)" if nothing is plugged in.
    row = 11
    @gfx.draw_text(x + 10, ty + line_h * row, "USB", ABOUT_LABEL, ABOUT_BG)
    row += 1
    if @about_usb_devices.empty?
      @gfx.draw_text(x + 70, ty + line_h * row, "(none)", ABOUT_TEXT, ABOUT_BG)
      row += 1
    else
      @about_usb_devices.each_with_index do |dev, i|
        label = " USB#{i + 1}"
        value = "#{dev[:type].ljust(7)} #{sprintf('%04X:%04X', dev[:vid], dev[:pid])}"
        draw_about_row(x, ty + line_h * row, label, value)
        row += 1
      end
    end

    @gfx.draw_text(x + 10, ty + line_h * row, "Copyright (C) 2026 kishima", ABOUT_TEXT, ABOUT_BG)

    @gfx.draw_text(x + ABOUT_W - 96, y + h - 10, "(click to close)", ABOUT_LABEL)
  end

  def draw_about_row(x, y, label, value)
    @gfx.draw_text(x + 10, y, label, ABOUT_LABEL, ABOUT_BG)
    @gfx.draw_text(x + 70, y, value, ABOUT_TEXT, ABOUT_BG)
  end

  def hit_about_dialog?(x, y)
    @about_open &&
      x >= @about_x && x < @about_x + ABOUT_W &&
      y >= @about_y && y < @about_y + @about_h
  end
end
