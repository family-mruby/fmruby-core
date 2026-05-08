# Clock Setting module for SystemDesktopApp
# Provides a dialog to manually set the system clock.
# On ESP32, also writes to RTC hardware (when RX8900 gem is available).

module ClockSettingMixin
  CLK_W = 200
  CLK_H = 90
  CLK_BG = FmrbConst::THEME_WINDOW_BG
  CLK_TITLE_BG = FmrbConst::THEME_MENU_BG
  CLK_TEXT = FmrbConst::THEME_TEXT
  CLK_BTN_BG = FmrbConst::THEME_BUTTON
  CLK_HL = FmrbConst::THEME_HIGHLIGHT

  # Field order: year, month, day, hour, minute, second
  CLK_FIELDS = [:year, :month, :day, :hour, :minute, :second]
  CLK_FIELD_LABELS = ["Y", "M", "D", "h", "m", "s"]
  CLK_FIELD_W = 28
  CLK_FIELD_H = 11

  def open_clock_setting
    @clk_open = true
    @clk_x = (@window_width - CLK_W) / 2
    @clk_y = (@window_height - CLK_H) / 2
    @clk_selected = 0  # Selected field index

    # Initialize from current wallclock
    wc = FmrbApp.wallclock
    if wc
      @clk_values = [wc[:year], wc[:month], wc[:day],
                     wc[:hour], wc[:minute], wc[:second]]
    else
      @clk_values = [2026, 1, 1, 0, 0, 0]
    end

    notify_overlay_state(true, @clk_x, @clk_y, CLK_W, CLK_H)
    draw_foreground
  end

  def close_clock_setting
    return unless @clk_open
    @clk_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    draw_foreground
  end

  def apply_clock_setting
    year, month, day, hour, minute, second = @clk_values

    Log.info("Clock setting (local): #{year}/#{month}/#{day} #{hour}:#{minute}:#{second}")

    # Set system clock and RTC (ESP32 only - avoid overwriting host PC clock on Linux)
    if FmrbConst::PLATFORM == "esp32"
      i2c = nil
      begin
        # FmrbApp.set_wallclock interprets the six fields as LOCAL time,
        # converts to a UTC epoch via mktime() (respects TZ env), and sets
        # the system clock. It returns the UTC equivalent as a hash, which
        # we feed straight into the RX8900 RTC so the chip stores UTC.
        utc = FmrbApp.set_wallclock(year, month, day, hour, minute, second)
        if utc.nil?
          Log.error("FmrbApp.set_wallclock failed (invalid date?)")
          close_clock_setting
          return
        end
        Log.info("System clock updated; UTC=#{utc[:year]}/#{utc[:month]}/#{utc[:day]} #{utc[:hour]}:#{utc[:minute]}:#{utc[:second]}")

        # Write UTC fields to RX8900 RTC hardware (rx8900.rb's
        # sync_system_clock at boot reads these back and passes them to
        # Machine.set_hwclock as a UTC epoch, so storing UTC keeps the
        # whole chain self-consistent).
        i2c = I2C.new(unit: :ESP32_I2C1,
                      sda_pin: FmrbHw::PIN_I2C1_SDA,
                      scl_pin: FmrbHw::PIN_I2C1_SCL)
        rtc = RX8900.new(i2c)
        rtc.write_time(utc)
        Log.info("RTC hardware updated (UTC)")
      rescue => e
        Log.error("Failed to set clock: #{e.message}")
      ensure
        i2c.close if i2c
      end
    end

    close_clock_setting
  end

  def draw_clock_setting
    return unless @clk_open
    x0 = @clk_x
    y0 = @clk_y

    # Background
    @gfx.fill_rect(x0, y0, CLK_W, CLK_H, CLK_BG)
    @gfx.draw_rect(x0, y0, CLK_W, CLK_H, 0x00)

    # Title bar
    @gfx.fill_rect(x0 + 1, y0 + 1, CLK_W - 2, 11, CLK_TITLE_BG)
    @gfx.draw_text(x0 + 4, y0 + 2, "Set Clock", FmrbGfx::WHITE, CLK_TITLE_BG)

    # Field labels and values
    fx = x0 + 8
    fy = y0 + 16

    # Label row
    CLK_FIELDS.size.times do |i|
      lx = fx + i * (CLK_FIELD_W + 2)
      @gfx.draw_text(lx + 4, fy, CLK_FIELD_LABELS[i], CLK_TEXT, CLK_BG)
    end

    # Up buttons
    fy += 12
    CLK_FIELDS.size.times do |i|
      lx = fx + i * (CLK_FIELD_W + 2)
      bg = (i == @clk_selected) ? CLK_HL : CLK_BTN_BG
      @gfx.fill_rect(lx, fy, CLK_FIELD_W, CLK_FIELD_H, bg)
      @gfx.draw_text(lx + 10, fy + 1, "+", FmrbGfx::WHITE, bg)
    end

    # Value row
    fy += CLK_FIELD_H + 2
    CLK_FIELDS.size.times do |i|
      lx = fx + i * (CLK_FIELD_W + 2)
      val = @clk_values[i]
      text = (i == 0) ? sprintf("%04d", val) : sprintf("%02d", val)
      bg = (i == @clk_selected) ? CLK_HL : CLK_BG
      @gfx.fill_rect(lx, fy, CLK_FIELD_W, CLK_FIELD_H, bg)
      tx = (i == 0) ? lx + 2 : lx + 6
      @gfx.draw_text(tx, fy + 1, text, CLK_TEXT, bg)
    end

    # Down buttons
    fy += CLK_FIELD_H + 2
    CLK_FIELDS.size.times do |i|
      lx = fx + i * (CLK_FIELD_W + 2)
      bg = (i == @clk_selected) ? CLK_HL : CLK_BTN_BG
      @gfx.fill_rect(lx, fy, CLK_FIELD_W, CLK_FIELD_H, bg)
      @gfx.draw_text(lx + 10, fy + 1, "-", FmrbGfx::WHITE, bg)
    end

    # OK / Cancel buttons
    fy += CLK_FIELD_H + 4
    ok_x = x0 + CLK_W / 2 - 50
    cancel_x = x0 + CLK_W / 2 + 10
    @gfx.fill_rect(ok_x, fy, 40, CLK_FIELD_H, 0x1C)  # Green
    @gfx.draw_text(ok_x + 10, fy + 1, "OK", FmrbGfx::WHITE, 0x1C)
    @gfx.fill_rect(cancel_x, fy, 50, CLK_FIELD_H, CLK_BTN_BG)
    @gfx.draw_text(cancel_x + 4, fy + 1, "Cancel", FmrbGfx::WHITE, CLK_BTN_BG)
  end

  def hit_clock_setting?(x, y)
    @clk_open && x >= @clk_x && x < @clk_x + CLK_W &&
      y >= @clk_y && y < @clk_y + CLK_H
  end

  def handle_clock_setting_click(x, y)
    fx = @clk_x + 8
    fy_up = @clk_y + 28
    fy_val = fy_up + CLK_FIELD_H + 2
    fy_down = fy_val + CLK_FIELD_H + 2
    fy_btns = fy_down + CLK_FIELD_H + 4

    # Up buttons
    if y >= fy_up && y < fy_up + CLK_FIELD_H
      CLK_FIELDS.size.times do |i|
        lx = fx + i * (CLK_FIELD_W + 2)
        if x >= lx && x < lx + CLK_FIELD_W
          @clk_selected = i
          clk_increment(i, 1)
          draw_foreground
          return
        end
      end
    end

    # Value row (select field)
    if y >= fy_val && y < fy_val + CLK_FIELD_H
      CLK_FIELDS.size.times do |i|
        lx = fx + i * (CLK_FIELD_W + 2)
        if x >= lx && x < lx + CLK_FIELD_W
          @clk_selected = i
          draw_foreground
          return
        end
      end
    end

    # Down buttons
    if y >= fy_down && y < fy_down + CLK_FIELD_H
      CLK_FIELDS.size.times do |i|
        lx = fx + i * (CLK_FIELD_W + 2)
        if x >= lx && x < lx + CLK_FIELD_W
          @clk_selected = i
          clk_increment(i, -1)
          draw_foreground
          return
        end
      end
    end

    # OK / Cancel
    if y >= fy_btns && y < fy_btns + CLK_FIELD_H
      ok_x = @clk_x + CLK_W / 2 - 50
      cancel_x = @clk_x + CLK_W / 2 + 10
      if x >= ok_x && x < ok_x + 40
        apply_clock_setting
        return
      end
      if x >= cancel_x && x < cancel_x + 50
        close_clock_setting
        return
      end
    end
  end

  def clk_increment(field_idx, delta)
    mins = [nil, 1, 1, 0, 0, 0]
    maxs = [nil, 12, 31, 23, 59, 59]

    if field_idx == 0  # year
      v = @clk_values[0] + delta
      v = 2000 if v < 2000
      v = 2099 if v > 2099
      @clk_values[0] = v
    else
      v = @clk_values[field_idx] + delta
      v = mins[field_idx] if v > maxs[field_idx]
      v = maxs[field_idx] if v < mins[field_idx]
      @clk_values[field_idx] = v
    end
  end
end
