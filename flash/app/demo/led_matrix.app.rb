# LED Matrix Demo - WS2812B 8x8 RGB LED + GUI preview
# Controls 64 WS2812B LEDs via RMT and displays a color preview grid on screen.
# On Linux (no RMT), only the GUI preview is shown.
#
# ---- Inputs ----
# encoder8 topic (real-time control):
#   ch 0: hue offset, ch 1: saturation, ch 2: brightness,
#   ch 3: animation speed, ch 4: pattern,
#   btn 0-7: toggle row on/off, switch: pause
# i2c_kbd topic:
#   text       -> auto-switches to TextScroll pattern
#   clear / "" -> back to Rainbow
# Mouse (on the LED grid area):
#   left click  -> next pattern
#   right click -> previous pattern
#
# ---- Tuning knobs ----
# Overall brightness:
#   @brightness (initialize, default 32; clamp range 1-128) scales hsv_to_rgb
#   V for the real LEDs. The 128 upper bound is a safety cap to keep total
#   WS2812B current within a safe range. The GUI preview adds a +150 boost
#   (clamped to 255) so the on-screen grid stays bright independently of the
#   LED safety cap.
# Background / off cells:
#   pattern_color returns (hue, 0) to mark a cell as off. update_leds treats
#   sat==0 as fully dark (r=g=b=0), so TextScroll background, dead Life cells,
#   and non-sparkle Sparkle cells stay black regardless of @brightness.
# Animation speed (three independent knobs):
#   1. on_update return value (default 100 ms) - global frame interval.
#   2. @speed (default 4, encoder ch 3) - hue-cycle advance per frame.
#   3. Per-pattern step gates: (@frame % 3) advances Life, (@frame % 2)
#      advances TextScroll column. Shrink the divisor to speed up, enlarge
#      to slow down.

# Change this to the GPIO pin connected to WS2812B data line
LED_PIN = 48

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
  KBD_TOPIC = "i2c_kbd"

  # 5x7 font bitmaps for printable ASCII (32-126)
  # Each char is 5 columns wide, each column is 7 bits (LSB=top)
  FONT_5X7 = {
    ' ' => [0x00,0x00,0x00,0x00,0x00],
    '!' => [0x00,0x00,0x5F,0x00,0x00],
    '"' => [0x00,0x07,0x00,0x07,0x00],
    '#' => [0x14,0x7F,0x14,0x7F,0x14],
    '$' => [0x24,0x2A,0x7F,0x2A,0x12],
    '%' => [0x23,0x13,0x08,0x64,0x62],
    '&' => [0x36,0x49,0x55,0x22,0x50],
    '\'' => [0x00,0x05,0x03,0x00,0x00],
    '(' => [0x00,0x1C,0x22,0x41,0x00],
    ')' => [0x00,0x41,0x22,0x1C,0x00],
    '*' => [0x14,0x08,0x3E,0x08,0x14],
    '+' => [0x08,0x08,0x3E,0x08,0x08],
    ',' => [0x00,0x50,0x30,0x00,0x00],
    '-' => [0x08,0x08,0x08,0x08,0x08],
    '.' => [0x00,0x60,0x60,0x00,0x00],
    '/' => [0x20,0x10,0x08,0x04,0x02],
    '0' => [0x3E,0x51,0x49,0x45,0x3E],
    '1' => [0x00,0x42,0x7F,0x40,0x00],
    '2' => [0x42,0x61,0x51,0x49,0x46],
    '3' => [0x21,0x41,0x45,0x4B,0x31],
    '4' => [0x18,0x14,0x12,0x7F,0x10],
    '5' => [0x27,0x45,0x45,0x45,0x39],
    '6' => [0x3C,0x4A,0x49,0x49,0x30],
    '7' => [0x01,0x71,0x09,0x05,0x03],
    '8' => [0x36,0x49,0x49,0x49,0x36],
    '9' => [0x06,0x49,0x49,0x29,0x1E],
    ':' => [0x00,0x36,0x36,0x00,0x00],
    'A' => [0x7E,0x11,0x11,0x11,0x7E],
    'B' => [0x7F,0x49,0x49,0x49,0x36],
    'C' => [0x3E,0x41,0x41,0x41,0x22],
    'D' => [0x7F,0x41,0x41,0x22,0x1C],
    'E' => [0x7F,0x49,0x49,0x49,0x41],
    'F' => [0x7F,0x09,0x09,0x09,0x01],
    'G' => [0x3E,0x41,0x49,0x49,0x7A],
    'H' => [0x7F,0x08,0x08,0x08,0x7F],
    'I' => [0x00,0x41,0x7F,0x41,0x00],
    'J' => [0x20,0x40,0x41,0x3F,0x01],
    'K' => [0x7F,0x08,0x14,0x22,0x41],
    'L' => [0x7F,0x40,0x40,0x40,0x40],
    'M' => [0x7F,0x02,0x0C,0x02,0x7F],
    'N' => [0x7F,0x04,0x08,0x10,0x7F],
    'O' => [0x3E,0x41,0x41,0x41,0x3E],
    'P' => [0x7F,0x09,0x09,0x09,0x06],
    'Q' => [0x3E,0x41,0x51,0x21,0x5E],
    'R' => [0x7F,0x09,0x19,0x29,0x46],
    'S' => [0x46,0x49,0x49,0x49,0x31],
    'T' => [0x01,0x01,0x7F,0x01,0x01],
    'U' => [0x3F,0x40,0x40,0x40,0x3F],
    'V' => [0x1F,0x20,0x40,0x20,0x1F],
    'W' => [0x3F,0x40,0x38,0x40,0x3F],
    'X' => [0x63,0x14,0x08,0x14,0x63],
    'Y' => [0x07,0x08,0x70,0x08,0x07],
    'Z' => [0x61,0x51,0x49,0x45,0x43],
    'a' => [0x20,0x54,0x54,0x54,0x78],
    'b' => [0x7F,0x48,0x44,0x44,0x38],
    'c' => [0x38,0x44,0x44,0x44,0x20],
    'd' => [0x38,0x44,0x44,0x48,0x7F],
    'e' => [0x38,0x54,0x54,0x54,0x18],
    'f' => [0x08,0x7E,0x09,0x01,0x02],
    'g' => [0x0C,0x52,0x52,0x52,0x3E],
    'h' => [0x7F,0x08,0x04,0x04,0x78],
    'i' => [0x00,0x44,0x7D,0x40,0x00],
    'j' => [0x20,0x40,0x44,0x3D,0x00],
    'k' => [0x7F,0x10,0x28,0x44,0x00],
    'l' => [0x00,0x41,0x7F,0x40,0x00],
    'm' => [0x7C,0x04,0x18,0x04,0x78],
    'n' => [0x7C,0x08,0x04,0x04,0x78],
    'o' => [0x38,0x44,0x44,0x44,0x38],
    'p' => [0x7C,0x14,0x14,0x14,0x08],
    'q' => [0x08,0x14,0x14,0x18,0x7C],
    'r' => [0x7C,0x08,0x04,0x04,0x08],
    's' => [0x48,0x54,0x54,0x54,0x20],
    't' => [0x04,0x3F,0x44,0x40,0x20],
    'u' => [0x3C,0x40,0x40,0x20,0x7C],
    'v' => [0x1C,0x20,0x40,0x20,0x1C],
    'w' => [0x3C,0x40,0x30,0x40,0x3C],
    'x' => [0x44,0x28,0x10,0x28,0x44],
    'y' => [0x0C,0x50,0x50,0x50,0x3C],
    'z' => [0x44,0x64,0x54,0x4C,0x44],
  }

  def initialize
    super()
    @rmt = nil
    @hue_offset = 0
    @saturation = 255
    @brightness = 32 # Default brightness (1-128; capped to limit LED current)
    @speed = 15 # Default animation speed (0-30)
    @paused = false
    @row_mask = 0xFF
    @pattern = 0
    @frame = 0
    @life_grid = nil
    @life_gen = 0
    # Text scroll state
    @scroll_text = ""
    @scroll_bitmap = []   # array of column bitmaps (7-bit each)
    @scroll_offset = 0
    @text_hue = 0         # hue for text color
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

    # Subscribe to topics
    subscribe(TOPIC)
    subscribe(KBD_TOPIC)

    draw_frame
  end

  def on_control(msg)
    return unless msg["cmd"] == "topic_data"

    # Handle i2c_kbd topic: switch to text scroll pattern
    if msg["topic"] == KBD_TOPIC
      data = msg["data"]
      if data
        if data["clear"] || (data["text"] && data["text"] == "")
          @scroll_bitmap = []
          @scroll_text = ""
          @pattern = 0  # Back to Rainbow
        elsif data["text"]
          set_scroll_text(data["text"])
          @pattern = PATTERN_COUNT - 1  # Text Scroll pattern
        end
      end
      return
    end

    return unless msg["topic"] == TOPIC
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
        # Capped at 128 to keep total WS2812B current within a safe range.
        @brightness += delta * 4
        @brightness = 1 if @brightness < 1
        @brightness = 128 if @brightness > 128
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

  PATTERN_COUNT = 8
  PATTERN_NAMES = ["Rainbow", "Ripple", "Checker", "Spiral", "Fire", "Sparkle", "Life", "TextScroll"]

  def on_update
    unless @paused
      @hue_offset = (@hue_offset + @speed) % 360
      @frame += 1
      if @pattern == 6 && (@frame % 3) == 0
        life_step
      end
      # Scroll text every 2 frames
      if @pattern == 7 && (@frame % 1) == 0 && @scroll_bitmap.size > 0
        @scroll_offset = (@scroll_offset + 1) % (@scroll_bitmap.size + LED_COLS)
      end
    end
    update_leds
    100
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :mouse_up
    # Ignore clicks on the title bar (super handles close / reload there).
    return if ev[:y] < @user_area_y0
    return if ev[:x] < @user_area_x0 || ev[:x] >= @user_area_x1

    case ev[:button]
    when 1  # Left click: next pattern
      @pattern = (@pattern + 1) % PATTERN_COUNT
    when 3  # Right click: previous pattern
      @pattern = (@pattern - 1) % PATTERN_COUNT
    end
  end

  def on_destroy
    unsubscribe(TOPIC)
    unsubscribe(KBD_TOPIC)
    if @rmt
      @rmt.write([0] * (LED_COUNT * 3))
    end
    Log.info("LED Matrix destroyed")
  end

  private

  # Convert text string to column bitmap array for 8-row LED scroll
  def set_scroll_text(text)
    @scroll_text = text
    @scroll_bitmap = []
    @text_hue = (@text_hue + 30) % 360  # Shift hue on each new text
    @scroll_offset = 0

    text.each_char do |ch|
      cols = FONT_5X7[ch] || FONT_5X7[ch.upcase] || FONT_5X7[' ']
      cols.each do |col_bits|
        # Map 7-bit column to 8-row (top bit = row 0)
        byte = 0
        bit = 0
        while bit < 7
          byte |= (1 << (bit + 1)) if (col_bits & (1 << bit)) != 0
          bit += 1
        end
        @scroll_bitmap << byte
      end
      @scroll_bitmap << 0  # 1-column gap between characters
    end
    Log.info("LED Matrix: text='#{text}' -> #{@scroll_bitmap.size} cols")
  end

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

    when 7  # Text Scroll - scrolling text from i2c_kbd
      if @scroll_bitmap.size > 0
        bmp_col = col + @scroll_offset - LED_COLS
        if bmp_col >= 0 && bmp_col < @scroll_bitmap.size
          bits = @scroll_bitmap[bmp_col]
          if (bits & (1 << row)) != 0
            # Color each character differently based on column position
            char_idx = bmp_col / 6  # ~6 cols per char (5 + 1 space)
            hue = (@text_hue + char_idx * 40) % 360
            return hue, @saturation
          end
        end
      end
      return 0, 0  # off

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
    r = 0
    while r < LED_ROWS
      c = 0
      while c < LED_COLS
        seed = (r * 13 + c * 7 + @frame + @hue_offset) % 5
        @life_grid[r] |= (1 << c) if seed < 2
        c += 1
      end
      r += 1
    end
    @life_gen = 0
  end

  def life_step
    return unless @life_grid
    new_grid = Array.new(LED_ROWS, 0)

    r = 0
    while r < LED_ROWS
      c = 0
      while c < LED_COLS
        n = life_neighbors(r, c)
        alive = (@life_grid[r] & (1 << c)) != 0
        if alive
          # Survive with 2 or 3 neighbors
          new_grid[r] |= (1 << c) if n == 2 || n == 3
        else
          # Birth with exactly 3 neighbors
          new_grid[r] |= (1 << c) if n == 3
        end
        c += 1
      end
      r += 1
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
    dr = -1
    while dr <= 1
      dc = -1
      while dc <= 1
        unless dr == 0 && dc == 0
          nr = (r + dr) % LED_ROWS  # wrap around
          nc = (c + dc) % LED_COLS
          count += 1 if (@life_grid[nr] & (1 << nc)) != 0
        end
        dc += 1
      end
      dr += 1
    end
    count
  end

  def update_leds
    grb_data = @rmt ? [] : nil

    row = 0
    while row < LED_ROWS
      row_active = (@row_mask & (1 << row)) != 0

      col = 0
      while col < LED_COLS
        if row_active
          hue, sat = pattern_color(row, col)
          if sat == 0
            # Treat sat==0 as "off" for background/dead/idle cells.
            r = 0; g = 0; b = 0
          else
            r, g, b = FmrbGfx.hsv_to_rgb(hue, sat, @brightness)
          end
        else
          r = 0; g = 0; b = 0
        end

        if grb_data
          grb_data << g << r << b
        end

        # Draw GUI cell. Boost V by +150 (clamped to 255) so the preview is
        # bright and legible on screen regardless of the LED safety cap on
        # @brightness.
        if row_active && (r > 0 || g > 0 || b > 0)
          disp_v = @brightness + 150
          disp_v = 255 if disp_v > 255
          dr, dg, db = FmrbGfx.hsv_to_rgb(hue, sat, disp_v)
          color332 = FmrbGfx.rgb_to_332(dr, dg, db)
        else
          color332 = 0x00
        end
        cx = @grid_x + col * @cell
        cy = @grid_y + row * @cell
        @gfx.fill_rect(cx, cy, @cell - 1, @cell - 1, color332)
        col += 1
      end
      row += 1
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
