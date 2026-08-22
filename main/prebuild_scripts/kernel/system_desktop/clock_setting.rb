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
    show_clock_widgets(true)

    # Initialize from current wallclock
    wc = FmrbApp.wallclock
    if wc
      @clk_values = [wc[:year], wc[:month], wc[:day],
                     wc[:hour], wc[:minute], wc[:second]]
    else
      @clk_values = [2026, 1, 1, 0, 0, 0]
    end

    notify_overlay_state(true, @clk_x, @clk_y, CLK_W, CLK_H)
    update_composite_regions
    draw_foreground
  end

  def close_clock_setting
    return unless @clk_open
    @clk_open = false
    show_clock_widgets(false)
    @ui.flush
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
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

        # Write UTC fields to the RTC hardware (the RTC driver's
        # sync_system_clock at boot reads these back and passes them to
        # Machine.set_hwclock as a UTC epoch, so storing UTC keeps the
        # whole chain self-consistent).
        # Retro carries an RX8900; Modern (Tab5 / ESP32-P4) an RX8130.
        i2c = I2C.new(unit: :ESP32_I2C1,
                      sda_pin: FmrbHw::PIN_I2C1_SDA,
                      scl_pin: FmrbHw::PIN_I2C1_SCL)
        # Call write_time on a concrete receiver in each branch: a ternary
        # `RX8130.new : RX8900.new` unifies to a poly receiver and Spinel cannot
        # resolve `write_time` on it (NoMethodError). Concrete per-branch keeps
        # static dispatch; identical behavior on mruby (dual-safe).
        if FmrbConst::CHIP_MODEL == "ESP32-P4"
          RX8130.new(i2c).write_time(utc)
        else
          RX8900.new(i2c).write_time(utc)
        end
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
    nf = CLK_FIELDS.size
    i = 0
    while i < nf
      lx = fx + i * (CLK_FIELD_W + 2)
      @gfx.draw_text(lx + 4, fy, CLK_FIELD_LABELS[i], CLK_TEXT, CLK_BG)
      i += 1
    end

    # Up buttons
    fy += 12
    i = 0
    while i < nf
      lx = fx + i * (CLK_FIELD_W + 2)
      bg = (i == @clk_selected) ? CLK_HL : CLK_BTN_BG
      @gfx.fill_rect(lx, fy, CLK_FIELD_W, CLK_FIELD_H, bg)
      @gfx.draw_text(lx + 10, fy + 1, "+", FmrbGfx::WHITE, bg)
      i += 1
    end

    # Value row
    fy += CLK_FIELD_H + 2
    i = 0
    while i < nf
      lx = fx + i * (CLK_FIELD_W + 2)
      val = @clk_values[i]
      text = (i == 0) ? sprintf("%04d", val) : sprintf("%02d", val)
      bg = (i == @clk_selected) ? CLK_HL : CLK_BG
      @gfx.fill_rect(lx, fy, CLK_FIELD_W, CLK_FIELD_H, bg)
      tx = (i == 0) ? lx + 2 : lx + 6
      @gfx.draw_text(tx, fy + 1, text, CLK_TEXT, bg)
      i += 1
    end

    # Down buttons
    fy += CLK_FIELD_H + 2
    i = 0
    while i < nf
      lx = fx + i * (CLK_FIELD_W + 2)
      bg = (i == @clk_selected) ? CLK_HL : CLK_BTN_BG
      @gfx.fill_rect(lx, fy, CLK_FIELD_W, CLK_FIELD_H, bg)
      @gfx.draw_text(lx + 10, fy + 1, "-", FmrbGfx::WHITE, bg)
      i += 1
    end

    # OK / Cancel buttons
    fy += CLK_FIELD_H + 4
    @ui.move(:clk_ok, x0 + CLK_W / 2 - 50, fy, 40, CLK_FIELD_H)
    @ui.move(:clk_cancel, x0 + CLK_W / 2 + 10, fy, 50, CLK_FIELD_H)
    @ui.invalidate_all
  end

  # OK and Cancel only. The six date fields keep their own up/value/down
  # columns: they are stacked vertically, they clamp and wrap per field
  # (months against days, hours against 24) and the value row doubles as the
  # selector. A Stepper is a horizontal "< n >" and means none of that.
  def build_clock_widgets
    # OK stays green: it is the one that sets the clock.
    @ui.button(:clk_ok, 0, 0, 40, CLK_FIELD_H, "OK", accent: 0x1C)
    @ui.button(:clk_cancel, 0, 0, 50, CLK_FIELD_H, "Cancel")
    show_clock_widgets(false)
    nil
  end

  def show_clock_widgets(on)
    @ui.set_visible(:clk_ok, on)
    @ui.set_visible(:clk_cancel, on)
    nil
  end

  def handle_clock_setting_widget(id)
    case id
    when :clk_ok then apply_clock_setting
    when :clk_cancel then close_clock_setting
    end
    nil
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

    nf = CLK_FIELDS.size

    # Up buttons
    if y >= fy_up && y < fy_up + CLK_FIELD_H
      i = 0
      while i < nf
        lx = fx + i * (CLK_FIELD_W + 2)
        if x >= lx && x < lx + CLK_FIELD_W
          @clk_selected = i
          clk_increment(i, 1)
          draw_foreground
          return
        end
        i += 1
      end
    end

    # Value row (select field)
    if y >= fy_val && y < fy_val + CLK_FIELD_H
      i = 0
      while i < nf
        lx = fx + i * (CLK_FIELD_W + 2)
        if x >= lx && x < lx + CLK_FIELD_W
          @clk_selected = i
          draw_foreground
          return
        end
        i += 1
      end
    end

    # Down buttons
    if y >= fy_down && y < fy_down + CLK_FIELD_H
      i = 0
      while i < nf
        lx = fx + i * (CLK_FIELD_W + 2)
        if x >= lx && x < lx + CLK_FIELD_W
          @clk_selected = i
          clk_increment(i, -1)
          draw_foreground
          return
        end
        i += 1
      end
    end

    # OK / Cancel are widgets and never reach here.
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
