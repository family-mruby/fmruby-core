# GPIO Viewer - Real-time GPIO pin status monitor
# Displays all GPIO pins with color-coded usage status.

GRID_COLS = 13
GRID_ROWS = 4 
PIN_COUNT = 49  # GPIO 0-48

# Usage type -> color (RGB332)
USAGE_COLORS = {
  0 => 0x00,  # UNUSED:    black
  1 => 0x6D,  # SYSTEM:    gray
  2 => 0x1C,  # USER_GPIO: green
  3 => 0x03,  # USER_I2C:  blue
  4 => 0xE3,  # USER_RMT:  magenta
  5 => 0xFC,  # USER_SPI:  yellow
  6 => 0x1F,  # USER_PWM:  cyan
  7 => 0xE0,  # USER_UART: red
}

USAGE_LABELS = {
  0 => "",
  1 => "SYS",
  2 => "GPIO",
  3 => "I2C",
  4 => "RMT",
  5 => "SPI",
  6 => "PWM",
  7 => "UART",
}

class GpioViewerApp < FmrbApp
  def initialize
    super()
    @prev_status = nil
  end

  def on_create
    # Grid layout (reserve 24px at bottom for legend)
    @legend_h = 24
    @cell_w = (@user_area_width - 2) / GRID_COLS
    @cell_h = (@user_area_height - 14 - @legend_h) / GRID_ROWS
    @grid_x = @user_area_x0 + 1
    @grid_y = @user_area_y0 + 14

    draw_all
  end

  def on_update
    status = FmrbHw.pin_status_all
    if status != @prev_status
      @prev_status = status
      draw_all
    end
    500
  end

  def on_event(ev)
    super(ev)
  end

  def on_destroy
    Log.info("GPIO Viewer destroyed")
  end

  private

  def draw_all
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 2, "GPIO Status", FmrbGfx::WHITE)

    status = @prev_status || FmrbHw.pin_status_all

    pin = 0
    while pin < PIN_COUNT
      col = pin % GRID_COLS
      row = pin / GRID_COLS

      usage = status[pin] || 0
      bg = USAGE_COLORS[usage] || 0x00
      label = USAGE_LABELS[usage] || ""

      cx = @grid_x + col * @cell_w
      cy = @grid_y + row * @cell_h

      @gfx.fill_rect(cx, cy, @cell_w - 1, @cell_h - 1, bg)

      # Pin number
      text_color = (usage == 0) ? FmrbGfx::GRAY : FmrbGfx::WHITE
      @gfx.draw_text(cx + 1, cy + 1, pin.to_s, text_color)

      # Usage label (if not empty and cell is tall enough)
      if label != "" && @cell_h >= 18
        @gfx.draw_text(cx + 1, cy + 10, label, FmrbGfx::BLACK)
      end
      pin += 1
    end

    # Legend (2 rows)
    ly1 = @grid_y + GRID_ROWS * @cell_h + 2
    ly2 = ly1 + 10
    items = [
      [0x6D, "SYS"], [0x1C, "GPIO"], [0x03, "I2C"], [0xE3, "RMT"],
      [0xFC, "SPI"], [0x1F, "PWM"],  [0xE0, "UART"],
    ]
    x = @user_area_x0 + 4
    ly = ly1
    items.each_with_index do |item, i|
      if i == 4
        x = @user_area_x0 + 4
        ly = ly2
      end
      @gfx.fill_rect(x, ly + 1, 6, 6, item[0])
      @gfx.draw_text(x + 8, ly, item[1], FmrbGfx::GRAY)
      x += item[1].length * 6 + 16
    end

    draw_window_frame
    @gfx.present
  end
end

Log.info("GpioViewerApp.new")
begin
  app = GpioViewerApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
