# Weather widget
# Fetches the current weather from Open-Meteo (free, no API key, HTTPS)
# and shows it with a retro pictogram. Click the window to refresh;
# auto-refreshes every 10 minutes.
#
# Edit LATITUDE/LONGITUDE/CITY below for your location.
# API: https://open-meteo.com/ (WMO weather codes)

require 'net/http'
require 'json'

class WeatherApp < FmrbApp
  LATITUDE  = 35.68
  LONGITUDE = 139.69
  CITY      = "Tokyo"
  REFRESH_INTERVAL_MS = 10 * 60 * 1000
  # Until the first successful fetch, retry quickly. On the device the very
  # first TCP/TLS flow after WiFi association is sometimes dropped by the
  # esp_hosted path (seen as a ~10s handshake timeout); a retry gets through.
  RETRY_INTERVAL_MS = 3000
  MAX_RETRIES = 5

  CHAR_W = 6
  CHAR_H = 8
  COLOR_BG    = 0x02   # deep night blue
  COLOR_TEXT  = 0xFF
  COLOR_DIM   = 0xB6   # light gray
  COLOR_SUN   = 0xE0   # red (rising-sun style)
  COLOR_CLOUD = 0xDB   # light gray
  COLOR_RAIN  = 0x1F   # cyan-blue
  COLOR_SNOW  = 0xFF
  COLOR_ERR   = 0xE0   # red

  def initialize
    super()
    @weather = nil
    @error = nil
    @updated_at = nil
    @last_fetch_ms = 0
    @retries = 0
    @net_up = false
  end

  def on_create
    Log.info("Weather: on_create #{@user_area_width}x#{@user_area_height}")
    @net_up = FmrbNet.connected?
    draw_all
    if @net_up
      fetch_weather
      draw_all
    else
      Log.info("Weather: network down, waiting...")
    end
  end

  def on_update
    # While the network is down (WiFi still associating, or no WiFi at all),
    # just watch FmrbNet once a second instead of burning fetch retries.
    unless network_up?
      return 1000
    end
    # Retry quickly until the first success, then settle to the slow refresh.
    interval = @weather ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS
    if now_ms - @last_fetch_ms > interval
      if @weather || @retries < MAX_RETRIES
        fetch_weather
        draw_all
      end
    end
    1000
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    unless network_up?
      Log.info("Weather: click ignored, network down")
      return
    end
    @retries = 0   # manual click always retries
    fetch_weather
    draw_all
  end

  def on_resize(new_width, new_height)
    draw_all
  end

  def on_destroy
    Log.info("Weather: Destroyed")
  end

  private

  # --- Data ---------------------------------------------------------------

  # Poll FmrbNet and redraw on state transitions. Regaining the network
  # resets the retry budget so fetching starts over immediately.
  def network_up?
    up = FmrbNet.connected?
    if up != @net_up
      @net_up = up
      Log.info("Weather: network #{up ? 'up' : 'down'}")
      if up
        @retries = 0
        @last_fetch_ms = 0
      end
      draw_all
    end
    up
  end

  def fetch_weather
    @last_fetch_ms = now_ms
    url = "https://api.open-meteo.com/v1/forecast" \
          "?latitude=#{LATITUDE}&longitude=#{LONGITUDE}&current_weather=true"
    Log.info("Weather: GET #{url}")
    ok = false
    begin
      res = Net::HTTP.get_response(URI.parse(url))
      if res.code != "200"
        @error = "HTTP #{res.code}"
      else
        data = JSON.parse(res.body)
        cw = data.is_a?(Hash) ? data["current_weather"] : nil
        if cw.is_a?(Hash)
          @weather = {
            temp: cw["temperature"],
            code: cw["weathercode"].to_i,
            wind: cw["windspeed"],
          }
          @error = nil
          t = Time.now
          @updated_at = "%02d:%02d" % [t.hour, t.min]
          Log.info("Weather: OK #{@weather[:temp]}C code=#{@weather[:code]}")
          ok = true
        else
          @error = "bad data"
        end
      end
    rescue => e
      @error = "#{e.class}"
      Log.error("Weather: fetch failed: #{e.class}: #{e.message}")
    end
    # Count every non-success so retries are bounded (see on_update guard);
    # otherwise a persistent failure would loop forever and drain internal RAM.
    if ok
      @retries = 0
    else
      @retries += 1
      Log.info("Weather: retry count=#{@retries}")
    end
    # Reclaim the per-request SSLContext/SSLSocket promptly: their mbedtls
    # buffers live in internal RAM that mruby's GC accounting does not see,
    # so without a nudge repeated requests exhaust DMA-capable RAM (seen as
    # esp-aes DMA descriptor allocation failures).
    GC.start rescue nil
  end

  # WMO weather interpretation codes -> kind symbol + label
  def interpret(code)
    case code
    when 0        then [:sun,     "Clear"]
    when 1, 2     then [:suncloud, "Mostly clear"]
    when 3        then [:cloud,   "Cloudy"]
    when 45, 48   then [:fog,     "Fog"]
    when 51..67   then [:rain,    "Rain"]
    when 71..77   then [:snow,    "Snow"]
    when 80..82   then [:rain,    "Showers"]
    when 85, 86   then [:snow,    "Snow showers"]
    when 95..99   then [:thunder, "Thunderstorm"]
    else               [:cloud,   "Code #{code}"]
    end
  end

  # --- Drawing ------------------------------------------------------------

  def draw_all
    x0 = @user_area_x0
    y0 = @user_area_y0
    @gfx.fill_rect(x0, y0, @user_area_width, @user_area_height, COLOR_BG)

    @gfx.draw_text(x0 + 6, y0 + 4, CITY, COLOR_TEXT, COLOR_BG)

    if @weather
      kind, label = interpret(@weather[:code])
      draw_icon(kind, x0 + 8, y0 + 18)
      tx = x0 + 52
      @gfx.draw_text(tx, y0 + 22, "#{@weather[:temp]} C", COLOR_TEXT, COLOR_BG)
      @gfx.draw_text(tx, y0 + 34, label, COLOR_DIM, COLOR_BG)
      @gfx.draw_text(tx, y0 + 46, "wind #{@weather[:wind]} km/h", COLOR_DIM, COLOR_BG)
    elsif !@net_up
      @gfx.draw_text(x0 + 6, y0 + 26, "waiting for network...", COLOR_DIM, COLOR_BG)
    elsif @error
      @gfx.draw_text(x0 + 6, y0 + 26, "error: #{@error}", COLOR_ERR, COLOR_BG)
      if @retries < MAX_RETRIES
        @gfx.draw_text(x0 + 6, y0 + 38, "retrying...", COLOR_DIM, COLOR_BG)
      else
        @gfx.draw_text(x0 + 6, y0 + 38, "click to retry", COLOR_DIM, COLOR_BG)
      end
    else
      @gfx.draw_text(x0 + 6, y0 + 26, "loading...", COLOR_DIM, COLOR_BG)
    end

    footer_y = y0 + @user_area_height - CHAR_H - 2
    note = @updated_at ? "upd #{@updated_at}  click=refresh" : "click=refresh"
    note = "no network  #{note}" if @weather && !@net_up
    @gfx.draw_text(x0 + 6, footer_y, note, COLOR_DIM, COLOR_BG)

    draw_window_frame
    @gfx.present
  end

  # 32x32-ish pictograms drawn with primitives at (x, y)
  def draw_icon(kind, x, y)
    case kind
    when :sun
      @gfx.fill_circle(x + 16, y + 16, 9, COLOR_SUN)
      [[16, 2, 16, 7], [16, 25, 16, 30], [2, 16, 7, 16], [25, 16, 30, 16],
       [6, 6, 10, 10], [22, 22, 26, 26], [26, 6, 22, 10], [10, 22, 6, 26]].each do |sx, sy, ex, ey|
        @gfx.draw_line(x + sx, y + sy, x + ex, y + ey, COLOR_SUN)
      end
    when :suncloud
      @gfx.fill_circle(x + 11, y + 11, 7, COLOR_SUN)
      draw_cloud(x + 6, y + 14)
    when :cloud
      draw_cloud(x + 2, y + 8)
    when :fog
      draw_cloud(x + 2, y + 4)
      @gfx.fill_rect(x + 4, y + 24, 24, 2, COLOR_DIM)
      @gfx.fill_rect(x + 8, y + 28, 20, 2, COLOR_DIM)
    when :rain
      draw_cloud(x + 2, y + 4)
      [6, 13, 20, 27].each do |rx|
        @gfx.draw_line(x + rx + 2, y + 22, x + rx, y + 30, COLOR_RAIN)
      end
    when :snow
      draw_cloud(x + 2, y + 4)
      [6, 14, 22].each do |sx|
        @gfx.fill_rect(x + sx, y + 25, 2, 2, COLOR_SNOW)
        @gfx.fill_rect(x + sx + 4, y + 29, 2, 2, COLOR_SNOW)
      end
    when :thunder
      draw_cloud(x + 2, y + 4)
      @gfx.draw_line(x + 18, y + 20, x + 12, y + 27, COLOR_SUN)
      @gfx.draw_line(x + 12, y + 27, x + 17, y + 27, COLOR_SUN)
      @gfx.draw_line(x + 17, y + 27, x + 12, y + 34, COLOR_SUN)
    end
  end

  def draw_cloud(x, y)
    @gfx.fill_circle(x + 8,  y + 10, 6, COLOR_CLOUD)
    @gfx.fill_circle(x + 16, y + 6,  8, COLOR_CLOUD)
    @gfx.fill_circle(x + 24, y + 10, 6, COLOR_CLOUD)
    @gfx.fill_rect(x + 6, y + 10, 24, 7, COLOR_CLOUD)
  end

  def now_ms
    (Time.now.to_f * 1000).to_i
  end
end

Log.info("Weather: Creating WeatherApp")
begin
  app = WeatherApp.new
  app.start
rescue => e
  Log.error("Weather: Exception: #{e.class}")
  Log.error("Weather: Message: #{e.message}")
end
Log.info("Weather: Script ended")
