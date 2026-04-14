# Unit 8Encoder Monitor
# Displays encoder position as dial circles, publishes state changes.
# When no I2C device is found, provides keyboard/mouse fallback UI.
#   Left/Right: select channel, Up/Down: rotate, Space: button toggle

require "unit8encoder"

class Encoder8App < FmrbApp
  TOPIC = "encoder8"
  POLL_MS = 100
  NUM_ENC = 8

  # Dial layout: 2 rows x 4 columns
  COLS = 4
  RADIUS = 10
  STEPS_PER_REV = 24  # one full circle = 24 encoder steps

  def on_create
    @positions = Array.new(NUM_ENC, 0)
    @buttons   = Array.new(NUM_ENC, false)
    @switch    = false
    @enc = nil
    @selected_ch = 0  # Selected channel for keyboard/mouse control

    if FmrbConst::PLATFORM == "esp32"
      begin
        @i2c = I2C.new(unit: :ESP32_I2C1,
                       sda_pin: FmrbHw::PIN_I2C1_SDA,
                       scl_pin: FmrbHw::PIN_I2C1_SCL)
        @enc = Unit8Encoder.new(@i2c)
        # Flush initial increment values
        NUM_ENC.times { |i| @enc.get_increment(i) }
        NUM_ENC.times { |i| @buttons[i] = @enc.get_button(i) }
        @switch = @enc.get_switch
      rescue => e
        Log.error("Encoder8App: I2C init failed: #{e.message}")
        @enc = nil
      end
    end

    draw_screen
  end

  def on_update
    if @enc
      changed = false

      NUM_ENC.times do |i|
        inc = @enc.get_increment(i)
        if inc != 0
          @positions[i] += inc
          publish(TOPIC, {"type" => "encoder", "ch" => i, "delta" => inc})
          changed = true
        end

        btn = @enc.get_button(i)
        if btn != @buttons[i]
          @buttons[i] = btn
          publish(TOPIC, {"type" => "button", "ch" => i,
                          "value" => btn ? 1 : 0})
          changed = true
        end
      end

      sw = @enc.get_switch
      if sw != @switch
        @switch = sw
        publish(TOPIC, {"type" => "switch", "value" => sw ? 1 : 0})
        changed = true
      end

      draw_screen if changed
    end

    POLL_MS
  end

  def on_event(ev)
    super(ev)

    # Keyboard/mouse fallback (always available, useful when no device)
    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0

      case keycode
      when 79  # Right - next channel
        @selected_ch = (@selected_ch + 1) % NUM_ENC
        draw_screen
      when 80  # Left - previous channel
        @selected_ch = (@selected_ch - 1) % NUM_ENC
        draw_screen
      when 82  # Up - rotate clockwise
        @positions[@selected_ch] += 1
        publish(TOPIC, {"type" => "encoder", "ch" => @selected_ch, "delta" => 1})
        draw_screen
      when 81  # Down - rotate counter-clockwise
        @positions[@selected_ch] -= 1
        publish(TOPIC, {"type" => "encoder", "ch" => @selected_ch, "delta" => -1})
        draw_screen
      end

      if character == 32  # Space - toggle button
        @buttons[@selected_ch] = !@buttons[@selected_ch]
        publish(TOPIC, {"type" => "button", "ch" => @selected_ch,
                        "value" => @buttons[@selected_ch] ? 1 : 0})
        draw_screen
      elsif character == 115 || character == 83  # 's' or 'S' - toggle switch
        @switch = !@switch
        publish(TOPIC, {"type" => "switch", "value" => @switch ? 1 : 0})
        draw_screen
      end
    end

    # Mouse click on dial to select channel
    if ev[:type] == :mouse_up
      ch = hit_dial(ev[:x], ev[:y])
      if ch
        @selected_ch = ch
        draw_screen
      end
    end
  end

  def on_destroy
    @enc.set_all_led_color(0x000000) if @enc
  end

  private

  def hit_dial(mx, my)
    x0 = @user_area_x0
    y0 = @user_area_y0
    w  = @user_area_width
    h  = @user_area_height
    cell_w = w / COLS
    cell_h = (h - 10) / 2

    NUM_ENC.times do |i|
      col = i % COLS
      row = i / COLS
      cx = x0 + col * cell_w + cell_w / 2
      cy = y0 + row * cell_h + cell_h / 2
      dx = mx - cx
      dy = my - cy
      return i if dx * dx + dy * dy <= RADIUS * RADIUS + 100
    end
    nil
  end

  def draw_screen
    x0 = @user_area_x0
    y0 = @user_area_y0
    w  = @user_area_width
    h  = @user_area_height

    @gfx.fill_rect(x0, y0, w, h, FmrbGfx::WHITE)

    # Cell size
    cell_w = w / COLS
    cell_h = (h - 10) / 2  # 2 rows, reserve bottom for switch

    NUM_ENC.times do |i|
      col = i % COLS
      row = i / COLS
      cx = x0 + col * cell_w + cell_w / 2
      cy = y0 + row * cell_h + cell_h / 2

      # Selection highlight
      if i == @selected_ch
        @gfx.draw_circle(cx, cy, RADIUS + 3, FmrbGfx::CYAN)
      end

      # Circle color changes when button is pressed
      circle_color = @buttons[i] ? FmrbGfx::RED : FmrbGfx::BLACK
      @gfx.draw_circle(cx, cy, RADIUS, circle_color)

      # Needle: angle from accumulated position
      angle = (@positions[i] % STEPS_PER_REV) * 2.0 * Math::PI / STEPS_PER_REV - Math::PI / 2.0
      nx = cx + (RADIUS * Math.cos(angle)).to_i
      ny = cy + (RADIUS * Math.sin(angle)).to_i
      needle_color = @buttons[i] ? FmrbGfx::RED : FmrbGfx::BLUE
      @gfx.draw_line(cx, cy, nx, ny, needle_color)

      # Channel number
      @gfx.draw_text(cx - 2, cy + RADIUS + 2, i.to_s, FmrbGfx::GRAY)
    end

    # Switch
    sy = y0 + h - 10
    sw_color = @switch ? FmrbGfx::RED : FmrbGfx::GRAY
    sw_text = @switch ? "SW:ON" : "SW:OFF"
    @gfx.draw_text(x0 + 4, sy, sw_text, sw_color)

    if !@enc
      @gfx.draw_text(x0 + 60, sy, "[keys]", FmrbGfx::GRAY)
    end

    draw_window_frame
    @gfx.present
  end
end

begin
  app = Encoder8App.new
  app.start
rescue => e
  Log.error("Encoder8App: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
