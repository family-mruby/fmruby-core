# Log Viewer - Real-time system log display
# Shows log messages from all tasks with color-coded levels

class LogViewerApp < FmrbApp
  # Page colours come from the system theme ([theme] in system_conf.toml), so
  # the log reads the same as every other window and can be toned down for an
  # NTSC screen in one place.
  LOG_BG = FmrbConst::THEME_WINDOW_BG
  # A step off the system menu colour rather than the colour itself: the
  # toolbar sits directly under the window title bar, and in the same colour
  # the two merged into one thick band. This was a fixed pale pink for that
  # reason, which then ignored the theme; shade() keeps it clear of the title
  # bar AND of the page below it, whatever the theme is.
  TOOLBAR_BG = FmrbColors.shade(FmrbConst::THEME_MENU_BG, LOG_BG)
  BUTTON_BG = FmrbConst::THEME_BUTTON
  BUTTON_TEXT = FmrbConst::THEME_TEXT_LIGHT
  LOG_LINES_MAX = 50
  TOOLBAR_H = 14
  SB_W = 10          # scrollbar width

  # Level colors (RGB332), chosen to read on the pale page: the old yellow
  # for warnings vanished against white, so it is orange now.
  COLOR_ERROR = 0xE0  # Red
  COLOR_WARN  = 0xE8  # Orange
  COLOR_INFO  = FmrbConst::THEME_TEXT
  COLOR_DEBUG = FmrbConst::THEME_BORDER

  # Buffer level options
  LEVELS = ["I", "D"]
  LEVEL_LABELS = ["E/W/I", "E/W/I/D"]

  def initialize
    super()
    @lines = []
    @read_pos = 0
    @scroll = 0
    @auto_scroll = true
    @level_idx = 0  # 0=I (default), 1=D
    @scroll_hold = 0  # -1=up, 0=none, 1=down
    # Long lines are the normal case in a log, and cutting them throws away
    # the half that usually matters, so wrapping is on until asked otherwise.
    # Scroll positions are display rows either way, which is why every count
    # below goes through total_display_rows rather than @lines.size.
    @wrap = true
  end

  def on_create
    Log.info("LogViewer started")
    # The bar is the only widget here; the toolbar is still drawn by hand.
    @ui = FmrbUI.new(self, bg: LOG_BG)
    @ui.scrollbar(:sb, @user_area_width - SB_W, TOOLBAR_H, SB_W,
                  @user_area_height - TOOLBAR_H, 0, 1)
    # Initial read of existing logs
    fetch_logs
    draw_view
  end

  def on_update
    # Continuous scroll while holding scroll bar
    if @scroll_hold != 0
      if @scroll_hold > 0
        scroll_down
      else
        scroll_up
      end
      return 50
    end

    # Fetch and display new logs (only in AUTO mode)
    if @auto_scroll
      old_wp = @last_write_pos || 0
      new_wp = Log.write_pos
      if new_wp != old_wp
        @last_write_pos = new_wp
        fetch_logs
        draw_view
      end
    end
    200
  end

  def fetch_logs
    result = Log.read_lines(LOG_LINES_MAX, @read_pos)
    if result
      new_lines = result[0]
      @read_pos = result[1]
      if new_lines && new_lines.size > 0
        new_lines.each do |line|
          @lines << line
        end
        # Trim old lines
        while @lines.size > 500
          @lines.shift
        end
        # Auto-scroll to bottom
        if @auto_scroll
          max_scroll = visible_max_scroll
          @scroll = max_scroll if max_scroll > 0
        end
      end
    end
  end

  def visible_lines
    (@user_area_height - TOOLBAR_H - 2) / 8
  end

  # Characters that fit across the log area (the scrollbar's width is already
  # out of it). Recomputed rather than stored: the window can be resized.
  def log_max_chars
    n = (@user_area_width - 10) / 6
    n < 1 ? 1 : n
  end

  # How many rows one line takes at this width.
  def segs_for(len, max_chars)
    return 1 unless @wrap
    return 1 if len <= max_chars
    n = len / max_chars
    n += 1 if len % max_chars != 0
    n
  end

  def total_display_rows
    return @lines.size unless @wrap
    max_chars = log_max_chars
    total = 0
    i = 0
    n = @lines.size
    while i < n
      total += segs_for(@lines[i].length, max_chars)
      i += 1
    end
    total
  end

  def visible_max_scroll
    total = total_display_rows
    vis = visible_lines
    total > vis ? total - vis : 0
  end

  def level_color(line)
    if line.start_with?("[E]")
      COLOR_ERROR
    elsif line.start_with?("[W]")
      COLOR_WARN
    elsif line.start_with?("[D]")
      COLOR_DEBUG
    else
      COLOR_INFO
    end
  end

  def draw_view
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height

    @gfx.fill_rect(x0, y0, w, h, LOG_BG)

    # Toolbar
    draw_toolbar(x0, y0, w)

    # Log lines. @scroll counts display rows, so a wrapped line is walked
    # segment by segment: rows before the scroll position are stepped over
    # without being built, and only what is on screen is sliced.
    log_y0 = y0 + TOOLBAR_H
    vis = visible_lines
    max_chars = log_max_chars

    row = 0
    drawn = 0
    i = 0
    n = @lines.size
    while i < n && drawn < vis
      line = @lines[i]
      color = level_color(line)
      segs = segs_for(line.length, max_chars)
      seg = 0
      while seg < segs && drawn < vis
        if row >= @scroll
          if @wrap
            text = line[seg * max_chars, max_chars]
          else
            text = line.length > max_chars ? line[0, max_chars] : line
          end
          @gfx.draw_text(x0 + 2, log_y0 + 1 + drawn * 8, text, color, LOG_BG) if text
          drawn += 1
        end
        row += 1
        seg += 1
      end
      i += 1
    end

    @ui.set_range(:sb, total_display_rows, vis)
    @ui.set_value(:sb, @scroll)
    @ui.invalidate_all

    draw_window_frame
    @gfx.present unless @ui.flush
  end

  def level_label
    "Lv:" + LEVEL_LABELS[@level_idx]
  end

  def level_btn_w
    level_label.length * 6 + 8
  end

  def wrap_label
    @wrap ? "Wrap" : "Cut"
  end

  def wrap_btn_w
    wrap_label.length * 6 + 8
  end

  def draw_toolbar(x0, y0, w)
    @gfx.fill_rect(x0, y0, w, TOOLBAR_H, TOOLBAR_BG)

    # Level button, then the wrap toggle beside it.
    btn_x = x0 + 2
    btn_y = y0 + 1
    @gfx.fill_rect(btn_x, btn_y, level_btn_w, 12, BUTTON_BG)
    @gfx.draw_text(btn_x + 4, btn_y + 2, level_label, BUTTON_TEXT, BUTTON_BG)
    wrap_x = btn_x + level_btn_w + 4
    @gfx.fill_rect(wrap_x, btn_y, wrap_btn_w, 12, BUTTON_BG)
    @gfx.draw_text(wrap_x + 4, btn_y + 2, wrap_label, BUTTON_TEXT, BUTTON_BG)

    # Auto-scroll indicator
    as_label = @auto_scroll ? "[AUTO]" : "[HOLD]"
    as_x = x0 + w - as_label.length * 6 - 6
    @gfx.draw_text(as_x, btn_y + 2, as_label, @auto_scroll ? 0x1C : 0xE0, TOOLBAR_BG)
  end

  def on_event(ev)
    super(ev)
    # w also toggles wrapping, for a window too narrow to show the button.
    if ev[:type] == :key_down && ev[:character] == 119
      toggle_wrap
      return
    end
    x = ev[:x]
    y = ev[:y]

    # The bar acts on the press and keeps going while held, so it is fed
    # before anything else and its direction drives the repeat in on_update.
    # The widget says which way; the log viewer moves half a page, which is
    # its own step. Its value is overwritten from @scroll on the next redraw.
    if @ui.handle(ev) == :sb
      d = @ui.direction(:sb)
      @scroll_hold = d
      d < 0 ? scroll_up : scroll_down
      return
    end

    if ev[:type] == :mouse_up
      # Stop scroll hold
      @scroll_hold = 0

      # Toolbar click: the two buttons, measured the way they are drawn.
      if y >= @user_area_y0 && y < @user_area_y0 + TOOLBAR_H
        lv_x0 = @user_area_x0 + 2
        wrap_x0 = lv_x0 + level_btn_w + 4
        if x >= lv_x0 && x < lv_x0 + level_btn_w
          @level_idx = (@level_idx + 1) % LEVELS.size
          Log.set_buffer_level(LEVELS[@level_idx])
          draw_view
        elsif x >= wrap_x0 && x < wrap_x0 + wrap_btn_w
          toggle_wrap
        end
        return
      end

      # Click on log area (not scroll bar) = toggle auto-scroll
      log_y0 = @user_area_y0 + TOOLBAR_H
      # Hoisted rather than written as !bar.hit?(...): Spinel emits broken C
      # for a call negated inside a condition (ruby_writing_constraints.md).
      on_bar = @ui.find(:sb).hit?(x, y)
      if y >= log_y0 && !on_bar
        @auto_scroll = !@auto_scroll
        if @auto_scroll
          # Resuming AUTO: catch up on missed logs
          fetch_logs
          @scroll = visible_max_scroll
        end
        draw_view
      end
    end
  end

  # Wrapping changes what a scroll position means, so it is re-anchored: the
  # view stays at the bottom when it was following the tail, and is clamped
  # otherwise.
  def toggle_wrap
    @wrap = !@wrap
    max_scroll = visible_max_scroll
    @scroll = max_scroll if @auto_scroll || @scroll > max_scroll
    draw_view
    nil
  end

  # The window can be dragged to a new size. Everything is derived from the
  # user area at draw time, except the scrollbar widget, which is anchored
  # where it was built -- so that one is built again (and the old one
  # detached, or it would be repainted at the old edge).
  def on_resize(new_width, new_height)
    detach_ui(@ui) if @ui
    @ui = FmrbUI.new(self, bg: LOG_BG)
    @ui.scrollbar(:sb, @user_area_width - SB_W, TOOLBAR_H, SB_W,
                  @user_area_height - TOOLBAR_H, 0, 1)
    max_scroll = visible_max_scroll
    @scroll = max_scroll if @auto_scroll || @scroll > max_scroll
    draw_view
    Log.info("LogViewer resize: #{new_width}x#{new_height}")
    nil
  end

  def scroll_up
    if @scroll > 0
      step = visible_lines / 2
      step = 1 if step < 1
      @scroll -= step
      @scroll = 0 if @scroll < 0
      draw_view
    end
  end

  def scroll_down
    max_s = visible_max_scroll
    if @scroll < max_s
      step = visible_lines / 2
      step = 1 if step < 1
      @scroll += step
      @scroll = max_s if @scroll > max_s
      draw_view
    end
  end

  def on_destroy
    Log.info("LogViewer destroyed")
  end
end

Log.info("LogViewerApp.new")
begin
  app = LogViewerApp.new
  Log.info("LogViewerApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
