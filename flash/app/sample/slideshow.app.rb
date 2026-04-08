# SlideShow - Fullscreen presentation tool sample
# Click to advance slides, Ctrl+Alt+Del to exit

class SlideShowApp < FmrbApp
  BG_COLORS = [0x03, 0xE0, 0x1C, 0xFC, 0xE3, 0x00]
  TEXT_COLOR = 0xFF

  SLIDES = [
    { title: "Family mruby OS", body: "A tiny OS for ESP32" },
    { title: "mruby VM", body: "Run Ruby on microcontrollers" },
    { title: "Multi-VM", body: "Ruby, Lua, BASIC supported" },
    { title: "Graphics", body: "LovyanGFX + NTSC output" },
    { title: "Audio", body: "NES APU emulator" },
    { title: "Thank you!", body: "Ctrl+Q to exit" },
  ]

  def initialize
    super()
    @slide_index = 0
    @need_redraw = true
  end

  def on_create
    Log.info("SlideShow fullscreen app started")
    draw_slide
  end

  def draw_slide
    slide = SLIDES[@slide_index]
    bg = BG_COLORS[@slide_index % BG_COLORS.size]

    @gfx.clear(bg)

    # Title (centered)
    title = slide[:title]
    tx = (@window_width - title.length * 6) / 2
    ty = @window_height / 2 - 20
    @gfx.draw_text(tx, ty, title, TEXT_COLOR, bg)

    # Body (centered, below title)
    body = slide[:body]
    bx = (@window_width - body.length * 6) / 2
    by = @window_height / 2 + 10
    @gfx.draw_text(bx, by, body, TEXT_COLOR, bg)

    # Slide number
    num = "#{@slide_index + 1}/#{SLIDES.size}"
    nx = @window_width - num.length * 6 - 10
    ny = @window_height - 14
    @gfx.draw_text(nx, ny, num, TEXT_COLOR, bg)

    @gfx.present
  end

  def on_event(ev)
    # No super - fullscreen app has no window frame
    if ev[:type] == :mouse_up
      @slide_index += 1
      if @slide_index >= SLIDES.size
        @slide_index = 0
      end
      draw_slide
    end
  end

  def on_update
    100
  end

  def on_destroy
    Log.info("SlideShow destroyed")
  end
end

Log.info("SlideShowApp.new")
begin
  app = SlideShowApp.new
  Log.info("SlideShowApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
