# System Monitor - Heap usage color bar display
# Shows memory usage for each running process as horizontal color bars.
# Updates every 5 seconds.

class MonitorApp < FmrbApp
  UPDATE_MS = 5000
  BAR_HEIGHT = 10
  BAR_PAD = 2
  CHAR_W = 6
  CHAR_H = 8

  # Bar colors
  COLOR_FREE = FmrbGfx.rgb_to_332(60, 180, 60)     # green
  COLOR_USED = FmrbGfx.rgb_to_332(220, 60, 60)      # red
  COLOR_BG   = FmrbGfx.rgb_to_332(30, 30, 40)       # dark background
  COLOR_TEXT = FmrbGfx.rgb_to_332(220, 220, 220)     # light text
  COLOR_DIM  = FmrbGfx.rgb_to_332(120, 120, 120)     # dimmed text
  COLOR_BORDER = FmrbGfx.rgb_to_332(80, 80, 100)     # bar border

  def initialize
    super()
    @frame_ms = UPDATE_MS
  end

  def on_create
    # Calculate layout based on actual window size
    @content_x = @user_area_x0 + 2
    @content_w = @user_area_width - 4
    # Name label: up to 8 chars
    @label_chars = 8
    @label_w = @label_chars * CHAR_W + 2
    # Bar occupies remaining width
    @bar_x = @content_x + @label_w
    @bar_w = @content_w - @label_w

    draw_all
  end

  def on_update
    draw_all
    @frame_ms
  end

  def on_event(ev)
    super(ev)
  end

  def on_destroy
    Log.info("Monitor destroyed")
  end

  private

  def draw_all
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                    @user_area_width, @user_area_height, COLOR_BG)

    y = @user_area_y0 + 2

    # Title
    @gfx.draw_text(@content_x, y, "Monitor", COLOR_TEXT, COLOR_BG)
    y += CHAR_H + 4

    procs = FmrbApp.ps
    return unless procs

    procs.each do |p|
      break if y + BAR_HEIGHT + BAR_PAD > @user_area_y0 + @user_area_height

      total = p[:mem_total] || 0
      next if total == 0

      used = p[:mem_used] || 0
      name = p[:name] || "?"
      state = p[:state]

      # Truncate name
      name = name[0, @label_chars] if name.length > @label_chars

      # Draw label
      label_color = state == 2 ? COLOR_TEXT : COLOR_DIM
      @gfx.draw_text(@content_x, y + 1, name, label_color, COLOR_BG)

      # Draw bar border + free (green) background
      inner_w = @bar_w - 2
      @gfx.fill_rect(@bar_x, y, @bar_w, BAR_HEIGHT, COLOR_BORDER)
      @gfx.fill_rect(@bar_x + 1, y + 1, inner_w, BAR_HEIGHT - 2, COLOR_FREE)

      # Draw used portion (red)
      if total > 0 && used > 0
        used_w = (inner_w * used) / total
        used_w = 1 if used_w < 1
        used_w = inner_w if used_w > inner_w
        @gfx.fill_rect(@bar_x + 1, y + 1, used_w, BAR_HEIGHT - 2, COLOR_USED)
      end

      # Draw text on bar: "used/total pct%"
      pct = total > 0 ? (used * 100) / total : 0
      pct_text = "#{used / 1024}/#{total / 1024}K #{pct}%"
      # Fit text: if too wide, show shorter version
      if pct_text.length * CHAR_W > inner_w
        pct_text = "#{pct}%"
      end
      text_x = @bar_x + (@bar_w - pct_text.length * CHAR_W) / 2
      @gfx.draw_text(text_x, y + 1, pct_text, FmrbGfx::WHITE)

      y += BAR_HEIGHT + BAR_PAD
    end

    draw_window_frame
    @gfx.present
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
