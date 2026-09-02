# Check FmrbNet.request from an app, with the same source on every machine.
#
# The counter is the point: it has to keep moving while the request is in
# flight. In the browser that is the whole reason the API is shaped this way,
# and if it ever stops there, the fetch has ended up on the machine's own
# thread instead of the page's.

class NetProbeApp < FmrbApp
  # Read from /home/net_probe_url.txt when it is there, so the probe can be
  # pointed somewhere else without a rebuild.
  DEFAULT_URL = "https://raw.githubusercontent.com/family-mruby/family-mruby-apps/main/registry.json"

  def probe_url
    File.open("/home/net_probe_url.txt", "r") { |f| f.read.strip }
  rescue
    DEFAULT_URL
  end

  def on_create
    @ticks = 0
    @state = "requesting"
    @detail = ""
    @started_at = Machine.board_millis
    @url = probe_url
    @req = FmrbNet.request(@url)
    draw_screen
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 6
    y = @user_area_y0 + 6
    @gfx.draw_text(x, y, "#{FmrbConst::BOARD}: #{@url.to_s[8, 34]}", theme_fg)
    @gfx.draw_text(x, y + 12, "ticks: #{@ticks}", theme_fg)
    @gfx.draw_text(x, y + 24, "state: #{@state}", theme_fg)
    @gfx.draw_text(x, y + 36, @detail, theme_fg) unless @detail.empty?
    draw_window_frame
    @gfx.present
  end

  def on_update
    @ticks += 1
    if @req && @req.done?
      ms = Machine.board_millis - @started_at
      if @req.ok?
        @state = "ok #{@req.status} in #{ms}ms"
        @detail = "#{@req.body.bytesize} bytes, #{@ticks} ticks"
        Log.info("net_probe: #{@state}, #{@detail}")
      else
        @state = "failed in #{ms}ms"
        @detail = (@req.error || "status #{@req.status}").to_s
        Log.info("net_probe: #{@state}: #{@detail}")
      end
      @req = nil
    end
    draw_screen
    100
  end
end

begin
  app = NetProbeApp.new
  app.start
rescue => e
  puts "net_probe: #{e.message}"
end
