# Six-axis IMU demo (M5Stack Tab5 / BMI270)
#
# Reads the accelerometer and the gyroscope over I2C and shows the tilt as a
# bubble level plus the raw values. The sensor sits on the internal I2C bus,
# which the display driver serializes, so an ordinary I2C instance is enough.
#
# On a board without a BMI270 the demo keeps drawing with a synthetic tilt so
# the display can still be exercised; the status line says so.

class ImuApp < FmrbApp
  POLL_MS = 50

  LEVEL_R    = 44    # bubble level outer radius
  BALL_R     = 6
  TARGET_R   = 10
  TEXT_X     = 118   # left edge of the numeric column
  LINE_H     = 10

  def initialize
    super()
    @imu = nil
    @i2c = nil
    @sim = false
    @tick = 0
    @status = "starting"
    @accel = { x: 0.0, y: 0.0, z: 0.0 }
    @gyro  = { x: 0.0, y: 0.0, z: 0.0 }
    @temp = nil
  end

  def on_create
    open_sensor
    @sim = @imu.nil?
    draw_screen
  end

  def on_update
    @tick += 1
    sample
    draw_screen
    POLL_MS
  end

  def on_destroy
    @i2c.close if @i2c
  end

  private

  def open_sensor
    if FmrbConst::PLATFORM != "esp32"
      @status = "no IMU here (simulated)"
      return
    end

    begin
      @i2c = I2C.new(unit: :ESP32_I2C1,
                     sda_pin: FmrbHw::PIN_I2C1_SDA,
                     scl_pin: FmrbHw::PIN_I2C1_SCL)
      imu = BMI270.new(@i2c)
      if imu.init
        @imu = imu
        @status = sprintf("BMI270 at 0x%02X", imu.address)
        Log.info("Imu: #{@status}")
      elsif imu.address
        @status = "BMI270 init: #{imu.error} (simulated)"
        Log.warn("Imu: found the sensor at 0x#{sprintf('%02X', imu.address)} " \
                 "but init stopped at: #{imu.error}")
      else
        @status = "no BMI270 on I2C1 (simulated)"
        Log.warn("Imu: nothing answered as a BMI270")
      end
    rescue => e
      @status = "no IMU (simulated)"
      Log.warn("Imu: #{e.class}: #{e.message}")
    end

    if @imu.nil? && @i2c
      @i2c.close
      @i2c = nil
    end
  end

  def sample
    if @imu
      accel = @imu.read_accel
      gyro = @imu.read_gyro
      @accel = accel if accel
      @gyro = gyro if gyro
      # The die temperature moves slowly; one read a second is plenty.
      @temp = @imu.read_temperature if (@tick % 20) == 0
    else
      simulate
    end
  end

  # A slow tilt in a circle, so the drawing can be checked without hardware.
  def simulate
    a = @tick * 0.05
    @accel = { x: 0.35 * Math.sin(a), y: 0.35 * Math.cos(a), z: 0.94 }
    @gyro  = { x: 12.0 * Math.cos(a), y: -12.0 * Math.sin(a), z: 0.0 }
    @temp = 30.0
  end

  def draw_screen
    x0 = @user_area_x0
    y0 = @user_area_y0
    @gfx.fill_rect(x0, y0, @user_area_width, @user_area_height, FmrbGfx::BLACK)

    draw_level(x0 + 4 + LEVEL_R, y0 + 6 + LEVEL_R)
    draw_values(x0 + TEXT_X, y0 + 6)

    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 4, y0 + @user_area_height - 10, @status,
                   @sim ? FmrbGfx::YELLOW : FmrbGfx::GRAY)

    draw_window_frame
    @gfx.present
  end

  # Bubble level: the ball sits where the machine is leaning. One g of tilt
  # moves it to the rim.
  def draw_level(cx, cy)
    @gfx.draw_circle(cx, cy, LEVEL_R, FmrbGfx::GRAY)
    @gfx.draw_circle(cx, cy, TARGET_R, FmrbGfx::GRAY)
    @gfx.draw_line(cx - LEVEL_R, cy, cx + LEVEL_R, cy, FmrbGfx::GRAY)
    @gfx.draw_line(cx, cy - LEVEL_R, cx, cy + LEVEL_R, FmrbGfx::GRAY)

    bx, by = ball_offset
    level = (bx * bx + by * by) < (TARGET_R * TARGET_R)
    @gfx.fill_circle(cx + bx, cy + by, BALL_R,
                     level ? FmrbGfx::GREEN : FmrbGfx::CYAN)
  end

  # Tilt in g mapped onto the face of the level, clamped to the rim.
  #
  # The sensor axes are not the screen axes: the panel is mounted portrait and
  # turned 90 degrees for display. Measured on the machine, lowering the right
  # edge reads y = -0.49 and lowering the near edge reads x = +0.78, so the
  # sensor's +x runs down the screen and its +y runs to the left. The ball
  # rolls to the low side, which makes the mapping below.
  def ball_offset
    limit = LEVEL_R - BALL_R
    bx = (-@accel[:y] * limit).to_i
    by = (@accel[:x] * limit).to_i
    bx = limit if bx > limit
    bx = -limit if bx < -limit
    by = limit if by > limit
    by = -limit if by < -limit
    [bx, by]
  end

  def draw_values(x, y)
    @gfx.set_text_size(1)

    @gfx.draw_text(x, y, "ACCEL (g)", FmrbGfx::WHITE)
    y += LINE_H
    y = draw_axis(x, y, "X", @accel[:x], FmrbGfx::CYAN)
    y = draw_axis(x, y, "Y", @accel[:y], FmrbGfx::CYAN)
    y = draw_axis(x, y, "Z", @accel[:z], FmrbGfx::CYAN)

    y += 6
    @gfx.draw_text(x, y, "GYRO (dps)", FmrbGfx::WHITE)
    y += LINE_H
    y = draw_axis(x, y, "X", @gyro[:x], FmrbGfx::MAGENTA, "%+8.1f")
    y = draw_axis(x, y, "Y", @gyro[:y], FmrbGfx::MAGENTA, "%+8.1f")
    y = draw_axis(x, y, "Z", @gyro[:z], FmrbGfx::MAGENTA, "%+8.1f")

    if @temp
      y += 6
      @gfx.draw_text(x, y, sprintf("TEMP %5.1f C", @temp), FmrbGfx::GRAY)
    end
  end

  def draw_axis(x, y, label, value, color, format = "%+8.2f")
    @gfx.draw_text(x, y, label, FmrbGfx::GRAY)
    @gfx.draw_text(x + 12, y, sprintf(format, value), color)
    y + LINE_H
  end
end

begin
  app = ImuApp.new
  app.start
rescue => e
  Log.error("Imu: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
