# SlideShow - Fullscreen presentation tool
# Supports PicoRabbit-compatible markdown format
#
# Keys follow Rabbit upstream (doc/picorabbit/rabbit_behavior.md), which
# separates "next" (walk one wait step) from "next slide" (skip the rest of
# the steps):
#   next        Space / Enter / PgDn / Tab / n f j l
#   next slide  Right / Down
#   previous    PgUp / BackSpace / p b h k
#   prev slide  Left / Up
#   first/last  Home, a / End, e
#   quit        Esc / q
#   rabbit jump u      (not in upstream; Up is "previous slide" there)
#   reset timer t      (upstream Alt+t; Alt does not reach us in the sim)

class SlideShowApp < FmrbApp
  SLIDES_DIR = "/home/slides"

  def initialize
    super()
    @slide_index = 0
    @step = 0
    @max_step = 0
    @result = nil
    @renderer = nil
  end

  def on_create
    Log.info("SlideShow started")

    @md_files = scan_md_files
    if @md_files.length == 0
      Log.error("No .md files found in #{SLIDES_DIR}")
      stop
      return
    end

    load_presentation(@md_files[0])
    @renderer.load_sprites if @renderer
    draw_current
  end

  def scan_md_files
    files = []
    begin
      dir = Dir.open(SLIDES_DIR)
      while (entry = dir.read)
        next if entry == "." || entry == ".."
        files << entry if entry.end_with?(".md")
      end
      dir.close
    rescue => e
      Log.error("Failed to scan #{SLIDES_DIR}: #{e.message}")
    end
    files.sort!
    files
  end

  def load_presentation(filename)
    path = "#{SLIDES_DIR}/#{filename}"
    begin
      @result = PicoRabbit::Parser.parse_file(path)
      @renderer = PicoRabbit::FmrbRenderer.new(
        @gfx, @window_width, @window_height, @result.metadata)
      @renderer.precompile(@result.slides)
      @slide_index = 0
      update_step
      Log.info("Loaded #{filename}: #{@result.slides.length} slides")
    rescue => e
      Log.error("Failed to load #{filename}: #{e.message}")
    end
  end

  def update_step
    return unless @result
    slide = @result.slides[@slide_index]
    @max_step = slide ? slide.wait_count : 0
    @step = 0
  end

  def draw_current
    return unless @result && @renderer
    slide = @result.slides[@slide_index]
    return unless slide
    step_val = @max_step > 0 ? @step : nil
    @renderer.render_slide(slide, step_val, @slide_index, @result.slides.length)
  end

  def advance
    return unless @result
    if @step < @max_step
      @step += 1
      draw_current
      return
    end
    if @slide_index < @result.slides.length - 1
      @slide_index += 1
      update_step
      draw_current
    end
  end

  def go_back
    return unless @result
    if @step > 0
      @step -= 1
      draw_current
      return
    end
    if @slide_index > 0
      @slide_index -= 1
      update_step
      @step = @max_step
      draw_current
    end
  end

  # Skip the remaining wait steps of this slide and show the next one from
  # its own first step ("next slide" upstream).
  def next_slide
    return unless @result
    return if @slide_index >= @result.slides.length - 1
    @slide_index += 1
    update_step
    draw_current
  end

  def prev_slide
    return unless @result
    return if @slide_index <= 0
    @slide_index -= 1
    update_step
    draw_current
  end

  def go_first
    return unless @result
    @slide_index = 0
    update_step
    draw_current
  end

  def go_last
    return unless @result
    @slide_index = @result.slides.length - 1
    update_step
    @step = @max_step
    draw_current
  end

  def on_event(ev)
    if ev[:type] == :mouse_up
      advance
      return
    end
    return unless ev[:type] == :key_down

    # Scancodes are USB HID Usage IDs on the device and in the simulator
    # alike; ev[:keycode] is an SDL keysym on Linux and would not match.
    # Letters are read as scancodes too, so kana mode cannot shift them.
    case ev[:scancode] || 0
    when FmrbConst::KEY_SPACE, FmrbConst::KEY_ENTER, FmrbConst::KEY_PGDN,
         FmrbConst::KEY_TAB, FmrbConst::KEY_N, FmrbConst::KEY_F,
         FmrbConst::KEY_J, FmrbConst::KEY_L
      advance
    when FmrbConst::KEY_RIGHT, FmrbConst::KEY_DOWN
      next_slide
    when FmrbConst::KEY_PGUP, FmrbConst::KEY_BACKSPACE, FmrbConst::KEY_P,
         FmrbConst::KEY_B, FmrbConst::KEY_H, FmrbConst::KEY_K
      go_back
    when FmrbConst::KEY_LEFT, FmrbConst::KEY_UP
      prev_slide
    when FmrbConst::KEY_HOME, FmrbConst::KEY_A
      go_first
    when FmrbConst::KEY_END, FmrbConst::KEY_E
      go_last
    when FmrbConst::KEY_ESC, FmrbConst::KEY_Q
      stop
    when FmrbConst::KEY_U
      @renderer.jump_rabbit if @renderer
    when FmrbConst::KEY_T
      @renderer.reset_timer if @renderer
    end
  end

  # The race is two sprites composited over the slide, so a tick that changes
  # nothing costs nothing: once a second is enough to walk the turtle. A jump
  # is the exception -- at one frame a second it would not read as a hop, so
  # the physics runs at ten while the rabbit is off the ground.
  def on_update
    return 1000 unless @renderer && @result
    was_jumping = @renderer.rabbit_jumping?
    @renderer.update_rabbit if was_jumping
    @gfx.present if @renderer.update_sprites(@slide_index, @result.slides.length)
    was_jumping ? 100 : 1000
  end

  # Ctrl+Tab. The suspend hides the canvas, but a sprite lives in its own
  # layer and would otherwise stay on top of the desktop.
  def on_suspend
    return unless @renderer
    @renderer.sprites_visible = false
    @gfx.present
  end

  def on_resume
    return unless @renderer
    @renderer.sprites_visible = true
    draw_current
  end

  def on_destroy
    @renderer.destroy_sprites if @renderer
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
