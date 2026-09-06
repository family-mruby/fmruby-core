# I2C Keyboard Input Display
# Reads keystrokes from I2C keyboard (addr 0x5F) and displays them.
# Publishes each keypress to "i2c_kbd" topic.
#
# Protocol: read 1 byte from 0x5F.
#   0x00 = no key, other = ASCII keycode (press), 0x00 after = release.

class I2cKbdApp < FmrbApp
  TOPIC = "i2c_kbd"
  KBD_ADDR = 0x5F
  POLL_MS = 50
  MAX_CHARS = 40  # Max characters to display in buffer

  def on_create
    @ox = @user_area_x0
    @oy = @user_area_y0
    @w = @user_area_width
    @h = @user_area_height
    @text_buf = ""
    @last_key = 0
    @i2c = nil
    @status = "Initializing..."

    if FmrbConst::PLATFORM == "esp32"
      begin
        @i2c = I2C.new(unit: :ESP32_I2C1,
                       sda_pin: FmrbHw::PIN_I2C1_SDA,
                       scl_pin: FmrbHw::PIN_I2C1_SCL)
        # Probe keyboard
        begin
          @i2c.read(KBD_ADDR, 1)
          @status = "Keyboard found at 0x#{sprintf('%02X', KBD_ADDR)}"
          Log.info("I2cKbd: keyboard detected at 0x#{sprintf('%02X', KBD_ADDR)}")
        rescue
          @status = "No keyboard at 0x#{sprintf('%02X', KBD_ADDR)}"
          Log.warn("I2cKbd: keyboard not detected, will retry")
        end
      rescue => e
        @status = "I2C error: #{e.message}"
        Log.error("I2cKbd: I2C init failed: #{e.message}")
        @i2c = nil
      end
    else
      @status = "Use USB keyboard (Linux mode)"
    end

    draw_screen
  end

  def on_update
    if @i2c
      poll_keyboard
    end
    POLL_MS
  end

  def on_event(ev)
    # Also accept USB keyboard input (Linux dev mode)
    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0
      if character == 27 || keycode == FmrbConst::KEY_ESC  # Escape: clear all
        clear_text
      elsif character >= 0x20 && character < 0x7F
        append_char(character.chr)
      elsif character == 8 || character == 127  # Backspace/Delete
        @text_buf = @text_buf[0..-2] if @text_buf.size > 0
        publish(TOPIC, { "char" => "", "text" => @text_buf })
        draw_screen
      elsif character == 13 || character == 10  # Enter
        append_char("\n")
      end
    end
  end

  def on_destroy
    @i2c.close if @i2c
  end

  private

  def poll_keyboard
    begin
      data = @i2c.read(KBD_ADDR, 1)
      keycode = data.getbyte(0)

      if keycode != 0 && @last_key == 0
        # New key press
        if keycode >= 0x20 && keycode < 0x7F
          append_char(keycode.chr)
        elsif keycode == 8 || keycode == 127
          @text_buf = @text_buf[0..-2] if @text_buf.size > 0
          draw_screen
        elsif keycode == 13 || keycode == 10
          append_char("\n")
        end
      end
      @last_key = keycode
    rescue
      # I2C error, skip this cycle
    end
  end

  def clear_text
    @text_buf = ""
    publish(TOPIC, { "char" => "", "text" => "", "clear" => true })
    draw_screen
  end

  def append_char(ch)
    @text_buf << ch
    if @text_buf.size > MAX_CHARS
      @text_buf = @text_buf[-MAX_CHARS, MAX_CHARS]
    end

    # Publish keypress
    publish(TOPIC, { "char" => ch, "text" => @text_buf })

    draw_screen
  end

  def draw_screen
    @gfx.fill_rect(@ox, @oy, @w, @h, FmrbGfx::WHITE)

    # Status line
    @gfx.draw_text(@ox + 2, @oy + 2, @status, FmrbGfx::GRAY)

    # Large text display
    @gfx.set_text_size(2)

    # Word wrap display
    line_y = @oy + 16
    line = ""
    chars_per_line = @w / 12  # Approximate chars per line at size 2

    @text_buf.each_char do |ch|
      if ch == "\n" || line.size >= chars_per_line
        @gfx.draw_text(@ox + 4, line_y, line, FmrbGfx::BLUE)
        line_y += 16
        line = (ch == "\n") ? "" : ch
        break if line_y > @oy + @h - 20
      else
        line << ch
      end
    end
    @gfx.draw_text(@ox + 4, line_y, line + "_", FmrbGfx::BLUE) if line_y <= @oy + @h - 20

    @gfx.set_text_size(1)

    draw_window_frame
    @gfx.present
  end
end

begin
  app = I2cKbdApp.new
  app.start
rescue => e
  Log.error("I2cKbd: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
