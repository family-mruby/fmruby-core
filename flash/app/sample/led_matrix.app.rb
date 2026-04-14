# LED Matrix Demo - WS2812B 8x8 RGB LED + GUI preview
# Controls 64 WS2812B LEDs via RMT and displays a color preview grid on screen.
# On Linux (no RMT), only the GUI preview is shown.
#
# Subscribes to "encoder8" topic for real-time control:
#   ch 0: hue offset, ch 1: saturation, ch 2: brightness,
#   ch 3: animation speed, btn: toggle row on/off, switch: pause

# Change this to the GPIO pin connected to WS2812B data line
LED_PIN = 47

LED_COLS = 8
LED_ROWS = 8
LED_COUNT = LED_COLS * LED_ROWS

# WS2812B timing (nanoseconds)
WS_T0H = 400
WS_T0L = 850
WS_T1H = 800
WS_T1L = 450
WS_RESET = 50000

class LedMatrixApp < FmrbApp
  TOPIC = "encoder8"

  def initialize
    super()
    @rmt = nil
    @hue_offset = 0
    @saturation = 255
    @brightness = 64
    @speed = 4          # hue increment per frame
    @paused = false
    @row_mask = 0xFF    # bitmask: which rows are active (all on)
    @pattern = 0        # animation pattern index
    @frame = 0          # frame counter for animation
    # Life game state (8x8 grid stored as array of 8 bytes, each bit = cell)
    @life_grid = nil
    @life_gen = 0
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

    # Subscribe to encoder8 topic
    subscribe(TOPIC)

    draw_frame
  end

  def on_control(msg)
    return unless msg["cmd"] == "topic_data" && msg["topic"] == TOPIC
    data = msg["data"]
    return unless data

    case data["type"]
    when "encoder"
      ch = data["ch"]
      delta = data["delta"]
      case ch
      when 0  # Hue offset
        @hue_offset = (@hue_offset + delta * 5) % 360
      when 1  # Saturation
        @saturation += delta * 8
        @saturation = 0 if @saturation < 0
        @saturation = 255 if @saturation > 255
      when 2  # Brightness
        @brightness += delta * 4
        @brightness = 1 if @brightness < 1
        @brightness = 255 if @brightness > 255
      when 3  # Speed
        @speed += delta
        @speed = 0 if @speed < 0
        @speed = 30 if @speed > 30
      when 4  # Pattern
        @pattern += delta
        @pattern = @pattern % PATTERN_COUNT
      end
    when "button"
      ch = data["ch"]
      # Toggle row on/off (ch 0-7 maps to row 0-7)
      if ch < LED_ROWS && data["value"] == 1
        @row_mask ^= (1 << ch)
      end
    when "switch"
      @paused = (data["value"] == 1)
    end
  end

  PATTERN_COUNT = 7
  PATTERN_NAMES = ["Rainbow", "Ripple", "Checker", "Spiral", "Fire", "Sparkle", "Life"]

  def on_update
    unless @paused
      @hue_offset = (@hue_offset + @speed) % 360
      @frame += 1
      # Advance life game every 3 frames (~300ms per generation)
      if @pattern == 6 && (@frame % 3) == 0
        life_step
      end
    end
    update_leds
    100
  end

  def on_event(ev)
    super(ev)
  end

  def on_destroy
    unsubscribe(TOPIC)
    if @rmt
      @rmt.write([0] * (LED_COUNT * 3))
    end
    Log.info("LED Matrix destroyed")
  end

  private

  def draw_frame
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    draw_header
    draw_window_frame
    @gfx.present
  end

  def draw_header
    x = @user_area_x0 + 4
    y = @user_area_y0 + 2
    label = PATTERN_NAMES[@pattern] || "?"
    # Clear header area
    @gfx.fill_rect(@user_area_x0, y, @user_area_width, 10, FmrbGfx::BLACK)
    @gfx.draw_text(x, y, label, FmrbGfx::WHITE)
  end

  # Calculate HSV color for a pixel based on current pattern
  def pattern_color(row, col)
    case @pattern
    when 0  # Rainbow - diagonal gradient
      hue = (@hue_offset + row * 30 + col * 15) % 360
      return hue, @saturation

    when 1  # Ripple - concentric circles from center
      dx = col - 3
      dy = row - 3
      dist = Math.sqrt(dx * dx + dy * dy)
      hue = (@hue_offset + (dist * 50).to_i) % 360
      return hue, @saturation

    when 2  # Checker - alternating color blocks
      phase = (row + col) % 2
      hue = (@hue_offset + phase * 180) % 360
      sat = phase == 0 ? @saturation : @saturation / 2
      return hue, sat

    when 3  # Spiral - rotating spiral arms
      dx = col - 3.5
      dy = row - 3.5
      angle = Math.atan2(dy, dx) * 180.0 / Math::PI  # -180..180
      dist = Math.sqrt(dx * dx + dy * dy)
      hue = (@hue_offset + angle.to_i + (dist * 40).to_i) % 360
      return hue, @saturation

    when 4  # Fire - bottom-up heat shimmer
      heat = (LED_ROWS - row) * 30 + (@frame * 3 + col * 7) % 40
      hue = (heat % 60)  # red-yellow range (0-60)
      sat = 255
      return hue, sat

    when 5  # Sparkle - random-ish twinkling
      seed = (row * 13 + col * 7 + @frame) % 37
      if seed < 8
        hue = (@hue_offset + seed * 45) % 360
        return hue, @saturation
      else
        return 0, 0  # off
      end

    when 6  # Life - Conway's Game of Life
      life_ensure_init
      alive = (@life_grid[row] & (1 << col)) != 0
      if alive
        # Color by generation age, hue shifts over time
        hue = (@hue_offset + @life_gen * 10) % 360
        return hue, @saturation
      else
        return 0, 0
      end

    else
      return @hue_offset, @saturation
    end
  end

  # ---- Life game logic ----

  def life_ensure_init
    if @life_grid.nil?
      life_randomize
    end
  end

  def life_randomize
    @life_grid = Array.new(LED_ROWS, 0)
    # Seed with pseudo-random pattern using frame counter
    LED_ROWS.times do |r|
      LED_COLS.times do |c|
        seed = (r * 13 + c * 7 + @frame + @hue_offset) % 5
        @life_grid[r] |= (1 << c) if seed < 2
      end
    end
    @life_gen = 0
  end

  def life_step
    return unless @life_grid
    new_grid = Array.new(LED_ROWS, 0)

    LED_ROWS.times do |r|
      LED_COLS.times do |c|
        n = life_neighbors(r, c)
        alive = (@life_grid[r] & (1 << c)) != 0
        if alive
          # Survive with 2 or 3 neighbors
          new_grid[r] |= (1 << c) if n == 2 || n == 3
        else
          # Birth with exactly 3 neighbors
          new_grid[r] |= (1 << c) if n == 3
        end
      end
    end

    # If grid is empty or unchanged, re-seed
    if new_grid == @life_grid || new_grid.all? { |row| row == 0 }
      life_randomize
    else
      @life_grid = new_grid
      @life_gen += 1
    end
  end

  def life_neighbors(r, c)
    count = 0
    (-1..1).each do |dr|
      (-1..1).each do |dc|
        next if dr == 0 && dc == 0
        nr = (r + dr) % LED_ROWS  # wrap around
        nc = (c + dc) % LED_COLS
        count += 1 if (@life_grid[nr] & (1 << nc)) != 0
      end
    end
    count
  end

  def update_leds
    grb_data = @rmt ? [] : nil

    LED_ROWS.times do |row|
      row_active = (@row_mask & (1 << row)) != 0

      LED_COLS.times do |col|
        if row_active
          hue, sat = pattern_color(row, col)
          r, g, b = FmrbGfx.hsv_to_rgb(hue, sat, @brightness)
        else
          r = 0; g = 0; b = 0
        end

        if grb_data
          grb_data << g << r << b
        end

        # Draw GUI cell (use brighter value for display visibility)
        if row_active && (r > 0 || g > 0 || b > 0)
          disp_v = @brightness > 128 ? @brightness : @brightness + 100
          disp_v = 255 if disp_v > 255
          dr, dg, db = FmrbGfx.hsv_to_rgb(hue, sat, disp_v)
          color332 = FmrbGfx.rgb_to_332(dr, dg, db)
        else
          color332 = 0x00
        end
        cx = @grid_x + col * @cell
        cy = @grid_y + row * @cell
        @gfx.fill_rect(cx, cy, @cell - 1, @cell - 1, color332)
      end
    end

    if @rmt && grb_data
      @rmt.write(grb_data)
    end

    draw_header
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
