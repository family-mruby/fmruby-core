# System Monitor
#   Page 1: heap usage color bars (memory)
#   Page 2: GFX stats time-series graphs (cmds/s, presents/s) over last 30s
#
# Click the "<" / ">" arrows in the bottom nav bar to switch pages.
# Polls FmrbApp.gfx_stats once per second; caller computes rate from deltas.

class MonitorApp < FmrbApp
  UPDATE_MS = 1000
  BAR_HEIGHT = 10
  BAR_PAD = 2
  CHAR_W = 6
  CHAR_H = 8
  NAV_H = 10          # bottom nav bar height (incl. padding)
  ARROW_HIT_W = 16    # click area width for < and >
  HIST_LEN = 30       # 30 samples = 30 seconds at 1 Hz
  NUM_PAGES = 2

  # Bar colors (page 1)
  COLOR_FREE = FmrbGfx.rgb_to_332(60, 180, 60)
  COLOR_USED = FmrbGfx.rgb_to_332(220, 60, 60)
  COLOR_BG   = FmrbGfx.rgb_to_332(30, 30, 40)
  COLOR_TEXT = FmrbGfx.rgb_to_332(220, 220, 220)
  COLOR_DIM  = FmrbGfx.rgb_to_332(120, 120, 120)
  COLOR_BORDER = FmrbGfx.rgb_to_332(80, 80, 100)

  # Graph colors (page 2)
  COLOR_GRAPH_CMDS = FmrbGfx.rgb_to_332(80, 200, 255)
  COLOR_GRAPH_PRES = FmrbGfx.rgb_to_332(255, 180, 60)
  COLOR_AXIS       = FmrbGfx.rgb_to_332(100, 100, 120)
  COLOR_NAV_HI     = FmrbGfx.rgb_to_332(255, 255, 255)

  def initialize
    super()
    @frame_ms = UPDATE_MS
  end

  def on_create
    # Layout
    @content_x = @user_area_x0 + 2
    @content_w = @user_area_width - 4
    @label_chars = 8
    @label_w = @label_chars * CHAR_W + 2
    @bar_x = @content_x + @label_w
    @bar_w = @content_w - @label_w
    @nav_y = @user_area_y0 + @user_area_height - NAV_H

    # Pagination + sampling state
    @page = 0
    @hist_cmds = []
    @hist_presents = []
    stats = FmrbApp.gfx_stats
    @prev_cum_cmds = stats[:cmds] || 0
    @prev_cum_presents = stats[:presents] || 0
    @cur_cmds_per_sec = 0
    @cur_presents_per_sec = 0

    draw_all
  end

  def on_update
    sample_gfx_stats
    draw_all
    @frame_ms
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    x = ev[:x]
    y = ev[:y]
    return unless y >= @nav_y && y < @nav_y + NAV_H
    if x >= @content_x && x < @content_x + ARROW_HIT_W
      @page = (@page - 1 + NUM_PAGES) % NUM_PAGES
      draw_all
    elsif x >= @content_x + @content_w - ARROW_HIT_W && x < @content_x + @content_w
      @page = (@page + 1) % NUM_PAGES
      draw_all
    end
  end

  def on_destroy
    Log.info("Monitor destroyed")
  end

  private

  def sample_gfx_stats
    stats = FmrbApp.gfx_stats
    cur_cmds = stats[:cmds] || 0
    cur_presents = stats[:presents] || 0
    # Signed subtraction. If the counter wrapped the signed mrb_int range
    # between samples (weeks of uptime), we clamp to 0 for that single sample.
    dc = cur_cmds - @prev_cum_cmds
    dc = 0 if dc < 0
    dp = cur_presents - @prev_cum_presents
    dp = 0 if dp < 0
    @prev_cum_cmds = cur_cmds
    @prev_cum_presents = cur_presents
    @cur_cmds_per_sec = dc
    @cur_presents_per_sec = dp
    @hist_cmds << dc
    @hist_cmds.shift if @hist_cmds.length > HIST_LEN
    @hist_presents << dp
    @hist_presents.shift if @hist_presents.length > HIST_LEN
  end

  def draw_all
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                    @user_area_width, @user_area_height, COLOR_BG)
    if @page == 0
      draw_page_memory
    else
      draw_page_gfx_stats
    end
    draw_nav
    draw_window_frame
    @gfx.present
  end

  def draw_page_memory
    y = @user_area_y0 + 2
    @gfx.draw_text(@content_x, y, "Memory", COLOR_TEXT, COLOR_BG)
    y += CHAR_H + 4

    # IRAM line above the per-task PSRAM pool bars.
    hi = FmrbApp.heap_info
    if hi
      iram_free = hi[:iram_free] || 0
      iram_total = hi[:iram_total] || 0
      if iram_total > 0
        iram_text = "IRAM free: #{iram_free / 1024}/#{iram_total / 1024}K"
      else
        iram_text = "IRAM free: #{iram_free / 1024}K"
      end
      @gfx.draw_text(@content_x, y, iram_text, COLOR_TEXT, COLOR_BG)
      y += CHAR_H + 2
    end

    procs = FmrbApp.ps
    return unless procs

    procs.each do |p|
      break if y + BAR_HEIGHT + BAR_PAD > @nav_y - 2

      total = p[:mem_total] || 0
      next if total == 0

      used = p[:mem_used] || 0
      name = p[:name] || "?"
      state = p[:state]

      name = name[0, @label_chars] if name.length > @label_chars

      label_color = state == 2 ? COLOR_TEXT : COLOR_DIM
      @gfx.draw_text(@content_x, y + 1, name, label_color, COLOR_BG)

      inner_w = @bar_w - 2
      @gfx.fill_rect(@bar_x, y, @bar_w, BAR_HEIGHT, COLOR_BORDER)
      @gfx.fill_rect(@bar_x + 1, y + 1, inner_w, BAR_HEIGHT - 2, COLOR_FREE)

      if total > 0 && used > 0
        used_w = (inner_w * used) / total
        used_w = 1 if used_w < 1
        used_w = inner_w if used_w > inner_w
        @gfx.fill_rect(@bar_x + 1, y + 1, used_w, BAR_HEIGHT - 2, COLOR_USED)
      end

      pct = total > 0 ? (used * 100) / total : 0
      pct_text = "#{used / 1024}/#{total / 1024}K #{pct}%"
      if pct_text.length * CHAR_W > inner_w
        pct_text = "#{pct}%"
      end
      text_x = @bar_x + (@bar_w - pct_text.length * CHAR_W) / 2
      @gfx.draw_text(text_x, y + 1, pct_text, FmrbGfx::WHITE)

      y += BAR_HEIGHT + BAR_PAD
    end
  end

  def draw_page_gfx_stats
    y = @user_area_y0 + 2
    @gfx.draw_text(@content_x, y, "GFX Stats", COLOR_TEXT, COLOR_BG)
    y += CHAR_H + 4

    content_top = y
    content_bot = @nav_y - 2
    avail_h = content_bot - content_top
    gap = 4
    graph_h = (avail_h - gap) / 2

    draw_graph(content_top, graph_h,
               "cmds/s", @cur_cmds_per_sec, @hist_cmds, COLOR_GRAPH_CMDS)
    draw_graph(content_top + graph_h + gap, graph_h,
               "prs/s", @cur_presents_per_sec, @hist_presents, COLOR_GRAPH_PRES)
  end

  # Single stacked graph: header line "<label> <cur> max=<peak>", then a
  # line chart scaled to the peak of the current history window.
  def draw_graph(y0, h, label, current, hist, color)
    peak = array_max(hist)
    peak = 1 if peak < 1
    header = "#{label} #{current} max=#{peak}"
    @gfx.draw_text(@content_x, y0, header, COLOR_TEXT, COLOR_BG)

    gy_top = y0 + CHAR_H + 1
    gy_bot = y0 + h - 1
    gh = gy_bot - gy_top
    gx_left = @content_x
    gx_right = @content_x + @content_w - 1
    gw = gx_right - gx_left

    # Axes
    @gfx.draw_line(gx_left, gy_bot, gx_right, gy_bot, COLOR_AXIS)
    @gfx.draw_line(gx_left, gy_top, gx_left, gy_bot, COLOR_AXIS)

    return if hist.length < 2 || gh <= 0 || gw <= 0

    denom = HIST_LEN - 1
    prev_x = nil
    prev_y = nil
    i = 0
    while i < hist.length
      v = hist[i]
      px = gx_left + (i * gw) / denom
      py = gy_bot - (v * gh) / peak
      py = gy_top if py < gy_top
      py = gy_bot if py > gy_bot
      if prev_x
        @gfx.draw_line(prev_x, prev_y, px, py, color)
      end
      prev_x = px
      prev_y = py
      i += 1
    end
  end

  def draw_nav
    # "< 1/2 >" centered on the bottom nav strip.
    y = @nav_y + 1
    @gfx.draw_text(@content_x, y, "<", COLOR_NAV_HI, COLOR_BG)
    right_x = @content_x + @content_w - CHAR_W
    @gfx.draw_text(right_x, y, ">", COLOR_NAV_HI, COLOR_BG)

    page_label = "#{@page + 1}/#{NUM_PAGES}"
    label_x = @content_x + (@content_w - page_label.length * CHAR_W) / 2
    @gfx.draw_text(label_x, y, page_label, COLOR_TEXT, COLOR_BG)
  end

  def array_max(arr)
    return 0 if arr.length == 0
    m = arr[0]
    i = 1
    while i < arr.length
      m = arr[i] if arr[i] > m
      i += 1
    end
    m
  end
end

Log.info("MonitorApp.new")
begin
  app = MonitorApp.new
  Log.info("MonitorApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
