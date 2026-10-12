# Test card for a small screen (doc/headless_s3/, the AtomS3R's 128x128).
#
# Draws whatever size it is given, so it also runs on any other machine:
#   - a one-pixel white border on the outermost pixels (a clipped or shifted
#     picture shows at once), and a different colour in each corner:
#     red top-left, green top-right, blue bottom-left, yellow bottom-right
#   - eight colour bars (black, red, green, yellow, blue, magenta, cyan,
#     white), which tell a swapped byte order or colour order apart
#   - the size, and a counter that moves every update (the screen is alive)
#
# Keys (where there is a keyboard): Q / Esc quits.

class Atoms3rHelloApp < FmrbApp
  SC_Q = 0x14
  SC_ESC = 0x29
  BARS = [FmrbGfx::COLOR_BLACK, FmrbGfx::COLOR_RED, FmrbGfx::COLOR_GREEN,
          FmrbGfx::COLOR_YELLOW, FmrbGfx::COLOR_BLUE, FmrbGfx::COLOR_MAGENTA,
          FmrbGfx::COLOR_CYAN, FmrbGfx::COLOR_WHITE]

  def on_create
    @w = @window_width
    @h = @window_height
    @count = 0
    @t0 = Machine.board_millis
    Log.info("atoms3r_hello: #{@w}x#{@h}")
    redraw
  end

  def on_resize(new_width, new_height)
    @w = new_width
    @h = new_height
    redraw
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    sc = ev[:scancode]
    stop if sc == SC_Q || sc == SC_ESC
  end

  def on_update
    @count += 1
    draw_counter
    @gfx.present
    500
  end

  private

  def redraw
    w = @w
    h = @h
    @gfx.fill_rect(0, 0, w, h, FmrbGfx::COLOR_BLACK)

    bar_w = (w - 4) / 8
    bar_h = h / 4
    i = 0
    while i < 8
      @gfx.fill_rect(2 + i * bar_w, h - bar_h - 2, bar_w, bar_h, BARS[i])
      i += 1
    end

    @gfx.draw_rect(0, 0, w, h, FmrbGfx::COLOR_WHITE)
    @gfx.fill_rect(1, 1, 4, 4, FmrbGfx::COLOR_RED)
    @gfx.fill_rect(w - 5, 1, 4, 4, FmrbGfx::COLOR_GREEN)
    @gfx.fill_rect(1, h - 5, 4, 4, FmrbGfx::COLOR_BLUE)
    @gfx.fill_rect(w - 5, h - 5, 4, 4, FmrbGfx::COLOR_YELLOW)

    @gfx.set_font(:default)
    @gfx.draw_text(8, 8, "Family mruby", FmrbGfx::COLOR_WHITE)
    @gfx.draw_text(8, 20, "#{w}x#{h}", FmrbGfx::COLOR_CYAN)
    draw_counter
    @gfx.present
  end

  def draw_counter
    secs = (Machine.board_millis - @t0) / 1000
    @gfx.fill_rect(8, 32, @w - 16, 10, FmrbGfx::COLOR_BLACK)
    @gfx.draw_text(8, 32, "#{@count} / #{secs}s", FmrbGfx::COLOR_YELLOW)
  end
end

begin
  app = Atoms3rHelloApp.new
  app.start
rescue => e
  Log.error("atoms3r_hello: #{e.message}")
end
