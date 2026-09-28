# Fullscreen high-resolution mode test (Modern / P4 and the browser only).
#
# doc/fullscreen_hires/: the display can show one fullscreen canvas on a
# 640x360 framebuffer at 2x instead of the usual 426x240 at 3x. The kernel
# decides that by itself: this app's .app.toml says fullscreen_hires, so it
# starts on the 640x360 screen. F11 asks the kernel for a window (on the
# 426x240 screen) and for fullscreen again (FmrbApp#toggle_fullscreen), the
# way the editor does; the screen follows because the kernel decides it. There
# is no way for an app to switch the screen on its own.
#
# It draws a test card for whatever size it has: a one-pixel white border on
# the outermost pixels (so a clipped or shifted picture shows), a grid every
# 40 px with finer ticks every 10, a one-pixel checkerboard patch (it only
# looks grey and even when every pixel lands on its own spot), and text in
# the three fonts. The status line says which size it has and how often it
# was resized, so a screenshot is self-describing.
#
# Keys:
#   F11        window / fullscreen
#   F          fast: redraw the status every 30 ms instead of 100 ms
#   Q / Esc    quit (the display goes back to 426x240 by itself)
#
# Where there is no high-resolution mode (Retro, the Linux simulator) it is
# an ordinary fullscreen app at the screen's size.

class HiresTestApp < FmrbApp
  HIRES_W = 640
  HIRES_H = 360

  SC_F = 0x09
  SC_F11 = 0x44
  SC_Q = 0x14
  SC_ESC = 0x29

  def on_create
    @w = @window_width
    @h = @window_height
    @resizes = 0
    @tick = 0
    @period = 100
    redraw
  end

  def on_resize(new_width, new_height)
    @w = new_width
    @h = new_height
    @resizes += 1
    Log.info("hires_test: resize #{@resizes} -> #{@w}x#{@h}")
    redraw
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    sc = ev[:scancode]
    if sc == SC_F11
      toggle_fullscreen
    elsif sc == SC_F
      @period = @period == 100 ? 30 : 100
    elsif sc == SC_Q || sc == SC_ESC
      stop
    end
  end

  def on_update
    @tick += 1
    draw_status
    @gfx.present
    @period
  end

  private

  def redraw
    w = @w
    h = @h
    @gfx.fill_rect(0, 0, w, h, FmrbGfx::COLOR_BLACK)

    # Grid: 40 px lines, 10 px ticks along the top and left edges.
    x = 0
    while x < w
      @gfx.draw_line(x, 0, x, h - 1, 0x49)
      x += 40
    end
    y = 0
    while y < h
      @gfx.draw_line(0, y, w - 1, y, 0x49)
      y += 40
    end
    x = 0
    while x < w
      @gfx.draw_line(x, 0, x, 4, FmrbGfx::COLOR_CYAN)
      x += 10
    end
    y = 0
    while y < h
      @gfx.draw_line(0, y, 4, y, FmrbGfx::COLOR_CYAN)
      y += 10
    end

    # Diagonals corner to corner.
    @gfx.draw_line(0, 0, w - 1, h - 1, FmrbGfx::COLOR_GRAY)
    @gfx.draw_line(w - 1, 0, 0, h - 1, FmrbGfx::COLOR_GRAY)

    # The outermost pixels, in white.
    @gfx.draw_rect(0, 0, w, h, FmrbGfx::COLOR_WHITE)

    # Corner markers: a 3x3 block in each corner, a different colour each.
    @gfx.fill_rect(1, 1, 3, 3, FmrbGfx::COLOR_RED)
    @gfx.fill_rect(w - 4, 1, 3, 3, FmrbGfx::COLOR_GREEN)
    @gfx.fill_rect(1, h - 4, 3, 3, FmrbGfx::COLOR_BLUE)
    @gfx.fill_rect(w - 4, h - 4, 3, 3, FmrbGfx::COLOR_YELLOW)

    # One-pixel checkerboard, 40x40, at (w - 60, 60).
    cx = w - 60
    cy = 60
    @gfx.fill_rect(cx, cy, 40, 40, FmrbGfx::COLOR_BLACK)
    yy = 0
    while yy < 40
      xx = yy % 2
      while xx < 40
        @gfx.set_pixel(cx + xx, cy + yy, FmrbGfx::COLOR_WHITE)
        xx += 2
      end
      yy += 1
    end

    # Text in the three fonts the editor can choose from.
    @gfx.set_font(:default)
    @gfx.draw_text(12, 50, "Font0 6x8: #{w}x#{h} ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789", FmrbGfx::COLOR_WHITE)
    @gfx.set_font(:ja, 8)
    @gfx.draw_text(12, 64, "misaki 8: 高解像度の全画面 かなカナ漢字", FmrbGfx::COLOR_WHITE)
    @gfx.set_font(:ja, 12)
    @gfx.draw_text(12, 78, "efont 12: 高解像度の全画面 #{w}x#{h}", FmrbGfx::COLOR_YELLOW)
    @gfx.set_font(:ja, 16)
    @gfx.draw_text(12, 96, "efont 16: 高解像度 #{w}x#{h}", FmrbGfx::COLOR_CYAN)
    @gfx.set_font(:default)
    @gfx.draw_text(12, h - 20, "F11: window/fullscreen  F: fast  Q: quit", FmrbGfx::COLOR_GRAY)

    # In a window (after F11) the title bar goes over the card; a no-op while
    # fullscreen.
    draw_window_frame
    draw_status
    @gfx.present
  end

  def draw_status
    @gfx.set_font(:default)
    @gfx.fill_rect(12, 20, 300, 20, FmrbGfx::COLOR_BLACK)
    mode = (@w == HIRES_W && @h == HIRES_H) ? "HIRES" : "BASE"
    @gfx.draw_text(12, 22, "#{mode} #{@w}x#{@h}  resizes=#{@resizes}  tick=#{@tick}", FmrbGfx::COLOR_GREEN)
  end
end

begin
  app = HiresTestApp.new
  app.start
rescue => e
  Log.error("HiresTest: #{e}")
end
