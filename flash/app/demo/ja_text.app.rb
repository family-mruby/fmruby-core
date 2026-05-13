# Japanese text display demo
# Verifies Graphics#set_font(:ja, size) renders UTF-8 strings via efontJA_*.
# Click to cycle through display modes.

class JaTextApp < FmrbApp
  MODES = ["Default", "JA 8 (misaki)", "JA 12 (efont)", "Mixed", "Hybrid", "Scaled"]

  def initialize
    super()
    @mode = 0
  end

  def on_create
    Log.info("JA text demo started")
    draw_current
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_up
      close_btn_x = @window_width - 10
      if ev[:x] >= close_btn_x && ev[:y] >= 2 && ev[:y] < 10
        return
      end
      @mode = (@mode + 1) % MODES.size
      draw_current
    end
  end

  def draw_current
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height

    @gfx.fill_rect(x0, y0, w, h, FmrbGfx::BLACK)

    # Header always uses default font so the title stays readable.
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 4, y0 + 2, "[#{@mode + 1}/#{MODES.size}] #{MODES[@mode]}", FmrbGfx::WHITE)

    case @mode
    when 0 then draw_default
    when 1 then draw_japanese(8)
    when 2 then draw_japanese(12)
    when 3 then draw_mixed
    when 4 then draw_hybrid
    when 5 then draw_sizes
    end

    draw_window_frame
    @gfx.present
  end

  def draw_default
    x0 = @user_area_x0
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 24, "ASCII only (Font0 6x8)", FmrbGfx::CYAN)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 40, "Hello, Family mruby!", FmrbGfx::YELLOW)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 56, "0123456789 !?@#$%", FmrbGfx::GREEN)
  end

  def draw_japanese(size)
    x0 = @user_area_x0
    @gfx.set_font(:ja, size)
    @gfx.set_text_size(1)
    line_h = size + 4
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22 + line_h * 0, "こんにちは、世界！", FmrbGfx::WHITE)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22 + line_h * 1, "ファミリーmruby", FmrbGfx::CYAN)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22 + line_h * 2, "ABCDEFGHIJKLMOKPQRSTUVWXYZ", FmrbGfx::CYAN)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22 + line_h * 3, "abcdefghijklmnopqrstuvwxyz", FmrbGfx::CYAN)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22 + line_h * 4, "1234567890#!#\"`@*,./_-~=(){}[]}", FmrbGfx::CYAN)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22 + line_h * 5, "日本語ひらがなカタカナ漢字", FmrbGfx::YELLOW)
  end

  def draw_mixed
    x0 = @user_area_x0
    @gfx.set_font(:ja, 8)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22, "Score: 1234 点", FmrbGfx::WHITE)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 34, "ABC あいう 123", FmrbGfx::GREEN)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 46, "残り時間: 60秒", FmrbGfx::RED)
  end

  # Demonstrates draw_text(..., mixed: true): ASCII uses Font0 (6x8) while
  # multi-byte UTF-8 falls back to misaki_8 (8x8) inside the same call.
  # The current font state (set below) is preserved across the hybrid draw.
  def draw_hybrid
    x0 = @user_area_x0
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22, "Hybrid: ASCII -> Font0", FmrbGfx::WHITE)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 36, "        UTF-8 -> misaki_8", FmrbGfx::GRAY)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 54, "Score: 1234 点",  FmrbGfx::WHITE, mixed: true)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 66, "残り時間 60秒",   FmrbGfx::YELLOW, mixed: true)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 78, "puts 'こんにちは'", FmrbGfx::GREEN, mixed: true)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 94, "ABCDEFGHIJKLMOKPQRSTUVWXYZ基本的な漢字ABCＡＢＣ", FmrbGfx::CYAN, mixed: true)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 110, "abcdefghijklmnopqrstuvwxyz基本的な漢字abcＡＢＣ", FmrbGfx::CYAN, mixed: true)

  end

  def draw_sizes
    x0 = @user_area_x0
    @gfx.set_font(:ja, 8)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 22, "8px x1: あいう漢字", FmrbGfx::CYAN)
    @gfx.set_text_size(1)
    @gfx.set_font(:ja, 12)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 40, "12px x1: あいう漢字", FmrbGfx::YELLOW)
    @gfx.set_text_size(2)
    @gfx.set_font(:ja, 8)
    @gfx.draw_text(x0 + 8, @user_area_y0 + 62, "8px x2: あ", FmrbGfx::GREEN)
    @gfx.set_text_size(1)
  end

  def on_destroy
    if @gfx
      @gfx.set_text_size(1)
      @gfx.set_font(:default)
    end
    Log.info("JA text demo destroyed")
  end
end

begin
  app = JaTextApp.new
  app.start
rescue => e
  Log.error("JaText: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
