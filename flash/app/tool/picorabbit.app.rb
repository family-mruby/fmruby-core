# SlideShow - Fullscreen presentation tool
# Supports PicoRabbit-compatible markdown format
# Navigation: Click/Enter/Space/Right = advance, Left = back
#             Home = first slide, End = last slide
#             Up = rabbit jump, Escape = exit

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
      @slide_index = 0
      update_step
      draw_current
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
    end

    if ev[:type] == :key_down
      kc = ev[:keycode] || 0
      ch = ev[:character] || 0

      case kc
      when FmrbConst::KEY_RIGHT
        advance
      when FmrbConst::KEY_LEFT
        go_back
      when FmrbConst::KEY_UP  # rabbit jump
        @renderer.jump_rabbit if @renderer
      when FmrbConst::KEY_HOME
        go_first
      when FmrbConst::KEY_END
        go_last
      when FmrbConst::KEY_PGUP
        go_back
      when FmrbConst::KEY_PGDN
        advance
      else
        if ch == 10 || ch == 13 || ch == 32  # Enter or Space
          advance
        elsif ch == 27  # Escape
          stop
        end
      end
    end
  end

  def on_update
    if @renderer && @result
      @renderer.update_rabbit
      # Only redraw timer/footer area, not full slide
      @renderer.redraw_timer_area(@slide_index, @result.slides.length)
    end
    200
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
