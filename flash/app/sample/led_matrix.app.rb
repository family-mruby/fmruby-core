# LED Matrix Demo - WS2812B 8x8 RGB LED + GUI preview
# Controls 64 WS2812B LEDs via RMT and displays a color preview grid on screen.
# On Linux (no RMT), only the GUI preview is shown.

# Change this to the GPIO pin connected to WS2812B data line
LED_PIN = 47

LED_COLS = 18
LED_ROWS = 18
LED_COUNT = LED_COLS * LED_ROWS

# WS2812B timing (nanoseconds)
WS_T0H = 400
WS_T0L = 850
WS_T1H = 800
WS_T1L = 450
WS_RESET = 50000

class LedMatrixApp < FmrbApp
  def initialize
    super()
    @rmt = nil
    @hue_offset = 0
  end

  def on_create
    if FmrbConst::PLATFORM != "linux"
      @rmt = RMT.new(LED_PIN, t0h_ns: WS_T0H, t0l_ns: WS_T0L,
                               t1h_ns: WS_T1H, t1l_ns: WS_T1L,
                               reset_ns: WS_RESET)
      Log.info("LED Matrix: RMT initialized on pin #{LED_PIN}")
    else
      @rmt = nil
      Log.info("LED Matrix: RMT not available (GUI preview only)")
    end

    # Grid layout
    cell_w = @user_area_width / LED_COLS
    cell_h = (@user_area_height - 14) / LED_ROWS
    @cell = [cell_w, cell_h].min
    @grid_x = @user_area_x0 + (@user_area_width - @cell * LED_COLS) / 2
    @grid_y = @user_area_y0 + 14

    draw_frame
  end

  def on_update
    @hue_offset = (@hue_offset + 4) % 360
    update_leds
    100
  end

  def on_event(ev)
    super(ev)
  end

  def on_destroy
    if @rmt
      @rmt.write([0] * (LED_COUNT * 3))
    end
    Log.info("LED Matrix destroyed")
  end

  private

  def draw_frame
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    label = @rmt ? "8x8 WS2812B" : "8x8 LED (preview)"
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 2, label, FmrbGfx::WHITE)
    draw_window_frame
    @gfx.present
  end

  def update_leds
    grb_data = @rmt ? [] : nil

    LED_ROWS.times do |row|
      LED_COLS.times do |col|
        hue = (@hue_offset + row * 30 + col * 15) % 360
        r, g, b = FmrbGfx.hsv_to_rgb(hue, 255, 64)

        if grb_data
          grb_data << g << r << b
        end

        # Draw GUI cell (use brighter value for display visibility)
        dr, dg, db = FmrbGfx.hsv_to_rgb(hue, 255, 200)
        color332 = FmrbGfx.rgb_to_332(dr, dg, db)
        cx = @grid_x + col * @cell
        cy = @grid_y + row * @cell
        @gfx.fill_rect(cx, cy, @cell - 1, @cell - 1, color332)
      end
    end

    if @rmt && grb_data
      @rmt.write(grb_data)
    end

    draw_window_frame
    @gfx.present
  end
end

Log.info("LedMatrixApp.new")
begin
  app = LedMatrixApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")