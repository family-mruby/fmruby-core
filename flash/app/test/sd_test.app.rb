# SD Card Connection Test
# Writes a text file to /mnt/sd/sd_test.txt, reads it back, and verifies the
# content matches. File I/O on the mruby task runs on a PSRAM stack, so the
# fmrb_hal_file layer routes the open/read/write/close calls through hw_proxy
# (internal-RAM stack) automatically. Press Space to re-run the test.

class SdTestApp < FmrbApp
  TEST_PATH = "/mnt/sd/sd_test.txt"

  def on_create
    @run = 0
    @result = "(idle)"
    @result_color = theme_fg
    @details = []
    if FmrbConst::PLATFORM == "esp32"
      run_test
    else
      @result = "ESP32 only"
      @result_color = 0xE8  # orange: reads on the pale page
      @details << "Run on ESP32 target."
    end
    draw_screen
  end

  def on_update
    300
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    ch = ev[:character] || 0
    if ch == 32 && FmrbConst::PLATFORM == "esp32"
      run_test
      draw_screen
    end
  end

  private

  def run_test
    @run += 1
    @details = []
    started = Machine.board_millis

    payload = "FmRuby SD test ##{@run} at #{started}ms\n" \
              "line2: hello hw_proxy SD card\n"

    begin
      written = 0
      File.open(TEST_PATH, "w") do |f|
        written = f.write(payload)
      end
      @details << "WRITE: #{written} bytes"

      read_back = nil
      File.open(TEST_PATH, "r") do |f|
        read_back = f.read
      end
      got = read_back ? read_back.size : 0
      @details << "READ : #{got} bytes"

      if read_back == payload
        elapsed = Machine.board_millis - started
        @result = "PASS (#{elapsed}ms)"
        @result_color = FmrbGfx::GREEN
        @details << "verified OK"
      else
        @result = "FAIL: mismatch"
        @result_color = FmrbGfx::RED
        @details << "expected #{payload.size}B, got #{got}B"
      end

      Log.info("SdTest: run ##{@run} #{@result}")
    rescue => e
      @result = "FAIL: #{e.class}"
      @result_color = FmrbGfx::RED
      @details << e.message.to_s
      Log.error("SdTest: #{e.class}: #{e.message}")
    end
  end

  def draw_screen
    return unless @gfx
    clear_user_area

    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @gfx.draw_text(x, y, "SD Card Test  run##{@run}", theme_fg)
    y += 12
    @gfx.draw_text(x, y, "Path: #{TEST_PATH}", theme_border)
    y += 12
    @gfx.draw_text(x, y, "Result: #{@result}", @result_color)
    y += 14

    i = 0
    while i < @details.size
      @gfx.draw_text(x, y, @details[i], theme_fg)
      y += 10
      i += 1
    end

    y += 4
    @gfx.draw_text(x, y, "[Space] re-run", theme_accent)

    draw_window_frame
    @gfx.present
  end
end

begin
  app = SdTestApp.new
  app.start
rescue => e
  Log.error("SdTestApp: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
