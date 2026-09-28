# Fullscreen high-resolution mode test (Modern / P4 and the browser only).
#
# doc/fullscreen_hires/ H1: the display can show one fullscreen canvas on a
# 640x360 framebuffer at 2x instead of the usual 426x240 at 3x. The kernel
# decides that by itself (H2): this app's .app.toml says fullscreen_hires, so
# it starts on the 640x360 screen. Space still switches by hand through the
# development hook FmrbGfx#_dev_screen_mode, to exercise the display on its
# own (the kernel does not hear about those switches).
#
# It draws a test card for whatever size it has: a one-pixel white border on
# the outermost pixels (so a clipped or shifted picture shows), a grid every
# 40 px with finer ticks every 10, a one-pixel checkerboard patch (it only
# looks grey and even when every pixel lands on its own spot), and text in
# the three fonts. The status line says which mode is on and how many
# switches have been done, so a screenshot is self-describing.
#
# Keys:
#   Space / H  switch between 426x240 and 640x360
#   A          automatic: 10 round trips (20 switches), 2 s apart
#   F          fast: redraw the status every 30 ms instead of 100 ms
#   Q / Esc    quit (the display goes back to 426x240 by itself)
#
# Without the hook (Retro, the Linux simulator, a release build) it says so
# and stays at its size.

class HiresTestApp < FmrbApp
  HIRES_W = 640
  HIRES_H = 360
  AUTO_SWITCHES = 20
  AUTO_INTERVAL_MS = 2000

  SC_A = 0x04
  SC_F = 0x09
  SC_H = 0x0B
  SC_Q = 0x14
  SC_SPACE = 0x2C
  SC_ESC = 0x29

  def on_create
    # With fullscreen_hires the kernel starts this app on the 640x360 screen;
    # the base screen is then the usual 426x240.
    @hires = (@window_width == HIRES_W && @window_height == HIRES_H)
    @base_w = @hires ? 426 : @window_width
    @base_h = @hires ? 240 : @window_height
    @w = @window_width
    @h = @window_height
    @switches = 0
    @auto_left = 0
    @auto_next = 0
    @note = ""
    @tick = 0
    @period = 100
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
    if sc == SC_SPACE || sc == SC_H
      toggle
    elsif sc == SC_A
      @auto_left = AUTO_SWITCHES
      @auto_next = Machine.board_millis
      @note = "auto"
    elsif sc == SC_F
      @period = @period == 100 ? 30 : 100
    elsif sc == SC_Q || sc == SC_ESC
      stop
    end
  end

  def on_update
    if @auto_left > 0 && Machine.board_millis >= @auto_next
      @auto_left -= 1
      @auto_next = Machine.board_millis + AUTO_INTERVAL_MS
      toggle
      @note = "" if @auto_left == 0
    end
    @tick += 1
    draw_status
    @gfx.present
    @period
  end

  private

  def toggle
    want_hires = !@hires
    w = want_hires ? HIRES_W : @base_w
    h = want_hires ? HIRES_H : @base_h
    ok = false
    begin
      ok = @gfx._dev_screen_mode(w, h)
    rescue NoMethodError
      @note = "no _dev_screen_mode in this build"
      @auto_left = 0
      redraw
      return
    end
    if ok
      @hires = want_hires
      @switches += 1
      Log.info("hires_test: switch #{@switches} -> #{w}x#{h}")
    else
      @note = "_dev_screen_mode failed"
      Log.info("hires_test: _dev_screen_mode(#{w}, #{h}) failed")
      redraw
    end
  end

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
    @gfx.draw_text(12, h - 20, "Space/H: switch  A: auto x10  F: fast  Q: quit", FmrbGfx::COLOR_GRAY)

    draw_status
    @gfx.present
  end

  def draw_status
    @gfx.set_font(:default)
    @gfx.fill_rect(12, 20, 300, 20, FmrbGfx::COLOR_BLACK)
    mode = @hires ? "HIRES" : "BASE"
    @gfx.draw_text(12, 22, "#{mode} #{@w}x#{@h}  switches=#{@switches}  tick=#{@tick}", FmrbGfx::COLOR_GREEN)
    line2 = @auto_left > 0 ? "auto: #{@auto_left} left" : @note
    @gfx.draw_text(12, 32, line2, FmrbGfx::COLOR_MAGENTA)
  end
end

begin
  app = HiresTestApp.new
  app.start
rescue => e
  Log.error("HiresTest: #{e}")
end
