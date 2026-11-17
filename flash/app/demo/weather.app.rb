# Weather widget
# Fetches the current weather from Open-Meteo (free, no API key, HTTPS)
# and shows it with a retro pictogram. Auto-refreshes every 10 minutes.
#
# Operation:
#   click (anywhere below the city name), Enter, Space, R or F5  refresh now
#   Left / Right, or click the arrows beside the city name      other city
#   (the arrows only show when the settings list more than one city)
#
# Settings: /home/weather.toml (this app's app_settings file). Without it the
# app shows Tokyo (35.68, 139.69) from Open-Meteo. An example with a few
# cities and every key explained is in
#   /usr/share/samples/weather/weather.toml.example
# -- copy it to /home/weather.toml and edit it. The city chosen with the
# arrows is written back into that file (the "city" line) and is the one shown
# next time. The server must answer like Open-Meteo's /v1/forecast
# (?latitude=..&longitude=..&current_weather=true).
# API: https://open-meteo.com/ (WMO weather codes)
#
# Text follows the system language (English / Japanese). When a fetch fails,
# the window shows a short reason (no network, the server cannot be reached,
# it does not answer, it answered an HTTP error, the data was not weather) and
# whether it retries; the exception itself goes to the log.

require 'net/http'
require 'json'

class WeatherApp < FmrbApp
  DEFAULT_API  = "https://api.open-meteo.com/v1/forecast"
  DEFAULT_CITY = "Tokyo"
  DEFAULT_LAT  = 35.68
  DEFAULT_LON  = 139.69
  REFRESH_INTERVAL_MS = 10 * 60 * 1000
  # Until the first successful fetch, retry quickly. On the device the very
  # first TCP/TLS flow after WiFi association is sometimes dropped by the
  # esp_hosted path (seen as a ~10s handshake timeout); a retry gets through.
  RETRY_INTERVAL_MS = 3000
  MAX_RETRIES = 5
  # A city chosen with the arrows is written to /home a moment later, so
  # stepping through the list writes the file once, not once per key.
  SAVE_DELAY_MS = 2000

  LINE_H = 13      # efontJA_12: 12 px high, ASCII 6 px wide
  ROW0_H = 15      # the city row; a click there switches the city
  ARROW_W = 22     # the "<" and ">" click zones at both ends of that row

  # RGB332 colours for a white window. Never 0x01: it is the colour key.
  COLOR_BG      = 0xFF   # white
  COLOR_TEXT    = 0x00   # black
  COLOR_DIM     = 0x6D   # dark gray
  COLOR_ACCENT  = 0x0B   # blue (city arrows)
  COLOR_SUN     = 0xF4   # amber disc
  COLOR_SUN_RIM = 0xE8   # orange-red rim and rays
  COLOR_CLOUD   = 0xDB   # light gray body
  COLOR_STORM   = 0x92   # darker body for rain / snow / thunder clouds
  COLOR_OUTLINE = 0x6D   # cloud edge, so a light cloud shows on white
  COLOR_RAIN    = 0x0B   # blue
  COLOR_SNOW    = 0x2F   # light blue flakes
  COLOR_BOLT    = 0xF0   # orange
  COLOR_FOG     = 0x92
  COLOR_ERR     = 0xC0   # dark red

  # [English, Japanese]
  TEXTS = {
    net:      ["Waiting for network...", "ネットワーク待ち..."],
    loading:  ["Loading...", "取得中..."],
    connect:  ["Can't reach the server", "サーバにつながりません"],
    noanswer: ["Server not answering", "サーバが応答しません"],
    http:     ["Server error (HTTP %s)", "サーバのエラー (HTTP %s)"],
    data:     ["Unexpected data", "データを読めません"],
    url:      ["Bad API URL in settings", "設定の URL が不正です"],
    other:    ["Couldn't get weather", "天気を取得できません"],
    retrying: ["Retrying (%d/%d)...", "再試行中 (%d/%d)"],
    click:    ["Click to retry", "クリックで再試行"],
    refresh:  ["click=refresh", "クリックで更新"],
    updated:  ["upd %s", "更新 %s"],
    wind:     ["wind %s km/h", "風 %s km/h"],
  }

  # WMO weather interpretation codes -> [kind, English, Japanese]
  KINDS = {
    sun:      ["Clear", "晴れ"],
    suncloud: ["Mostly clear", "晴れ時々曇り"],
    cloud:    ["Cloudy", "曇り"],
    fog:      ["Fog", "霧"],
    rain:     ["Rain", "雨"],
    showers:  ["Showers", "にわか雨"],
    snow:     ["Snow", "雪"],
    snowshow: ["Snow showers", "にわか雪"],
    thunder:  ["Thunderstorm", "雷雨"],
  }

  def initialize
    super()
    @weather = nil
    @error = nil          # [cause symbol, detail for the text] or nil
    @updated_at = nil
    @last_fetch_ms = 0
    @retries = 0
    @net_up = false
    @ja = false
    @cities = []
    @api = DEFAULT_API
    @idx = 0
    @cache = {}           # city index => [weather, updated_at, fetched ms]
    @save_at = nil
  end

  def on_create
    Log.info("Weather: on_create #{@user_area_width}x#{@user_area_height}")
    @ja = FmrbApp.language == "ja"
    @gfx.set_font(:ja, 12)
    load_settings
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
    save_city_if_due
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
    t = ev[:type]
    if t == :key_down
      case ev[:scancode]
      when FmrbConst::KEY_LEFT  then switch_city(-1)
      when FmrbConst::KEY_RIGHT then switch_city(1)
      when FmrbConst::KEY_ENTER, FmrbConst::KEY_SPACE, FmrbConst::KEY_R, FmrbConst::KEY_F5
        refresh_now
      end
    elsif t == :mouse_up && ev[:button] == 1
      if @cities.size > 1 && ev[:y] < ROW0_H
        switch_city(ev[:x] < ARROW_W ? -1 : 1)
      else
        refresh_now
      end
    end
  end

  def on_resize(new_width, new_height)
    draw_all
  end

  def on_destroy
    save_city_if_due(true)
    Log.info("Weather: Destroyed")
  end

  private

  def tr(key)
    pair = TEXTS[key]
    @ja ? pair[1] : pair[0]
  end

  # --- Settings -----------------------------------------------------------

  # /home/weather.toml (app_settings): api = "...", city = "<name shown>",
  # and [[cities]] tables of name / lat / lon. Anything missing or wrong
  # falls back to the defaults, with a log line saying what was skipped.
  def load_settings
    conf = app_settings
    api = conf["api"]
    @api = api.is_a?(String) && !api.strip.empty? ? api.strip : DEFAULT_API
    @cities = []
    list = conf["cities"]
    if list.is_a?(Array)
      i = 0
      while i < list.size
        c = list[i]
        city = c.is_a?(Hash) ? city_from(c) : nil
        if city
          @cities << city
        else
          Log.warn("Weather: #{app_settings_path}: cities[#{i}] skipped (needs name, lat, lon)")
        end
        i += 1
      end
    end
    if @cities.empty?
      @cities << { name: DEFAULT_CITY, lat: DEFAULT_LAT, lon: DEFAULT_LON }
    end
    @idx = 0
    want = conf["city"]
    if want.is_a?(String)
      i = 0
      while i < @cities.size
        if @cities[i][:name] == want
          @idx = i
          break
        end
        i += 1
      end
    end
    Log.info("Weather: #{@cities.size} cities, showing #{@cities[@idx][:name]}, api #{@api}")
  end

  def city_from(c)
    name = c["name"]
    lat = c["lat"]
    lon = c["lon"]
    return nil unless name.is_a?(String) && !name.empty?
    return nil unless (lat.is_a?(Float) || lat.is_a?(Integer)) && (lon.is_a?(Float) || lon.is_a?(Integer))
    { name: name, lat: lat, lon: lon }
  end

  def switch_city(step)
    n = @cities.size
    return if n < 2
    @cache[@idx] = [@weather, @updated_at, @last_fetch_ms] if @weather
    @idx = (@idx + step) % n
    @idx += n if @idx < 0
    hit = @cache[@idx]
    if hit
      @weather = hit[0]
      @updated_at = hit[1]
      @last_fetch_ms = hit[2]
    else
      @weather = nil
      @updated_at = nil
      @last_fetch_ms = 0
    end
    @error = nil
    @retries = 0
    @save_at = now_ms + SAVE_DELAY_MS
    Log.info("Weather: city #{@cities[@idx][:name]}")
    draw_all
    # A cached reading younger than the refresh interval is shown as it is;
    # otherwise ask now (on_update would only get to it a second later).
    if @net_up && (@weather.nil? || now_ms - @last_fetch_ms > REFRESH_INTERVAL_MS)
      fetch_weather
      draw_all
    end
  end

  # Writes the chosen city into /home/weather.toml. Only a file the user
  # made is written: with no file there is only the default city anyway.
  def save_city_if_due(force = false)
    return if @save_at.nil?
    return if !force && now_ms < @save_at
    @save_at = nil
    path = app_settings_path
    return unless File.exist?(path)
    name = @cities[@idx][:name]
    if save_app_settings({ "city" => name })
      Log.info("Weather: remembered #{name}")
    else
      Log.warn("Weather: #{FmrbToml.error}")
    end
  end

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

  def refresh_now
    unless network_up?
      Log.info("Weather: refresh ignored, network down")
      return
    end
    @retries = 0   # a manual refresh always retries
    fetch_weather
    draw_all
  end

  def fetch_weather
    @last_fetch_ms = now_ms
    city = @cities[@idx]
    ok = false
    if !(@api.start_with?("https://") || @api.start_with?("http://"))
      @error = [:url, nil]
      Log.error("Weather: api #{@api.inspect} is not an http(s) URL")
    else
      sep = @api.include?("?") ? "&" : "?"
      url = "#{@api}#{sep}latitude=#{city[:lat]}&longitude=#{city[:lon]}&current_weather=true"
      Log.info("Weather: GET #{url}")
      res = nil
      begin
        res = Net::HTTP.get_response(URI.parse(url))
      rescue => e
        @error = [request_cause(e), nil]
        Log.error("Weather: fetch failed: #{e.class}: #{e.message}")
      end
      if res
        if res.code != "200"
          @error = [:http, res.code.to_s]
          Log.error("Weather: HTTP #{res.code}")
        else
          ok = parse_body(res.body)
        end
      end
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

  # Net::HTTP wraps a failed TCP/TLS connect (DNS, refused, handshake
  # timeout) as IOError "Failed to connect ..."; a connection that opens but
  # then times out or closes surfaces as SocketError / EOFError / a bad
  # response. Class names are compared as text: not every build defines
  # every class.
  def request_cause(e)
    cls = e.class.to_s
    msg = e.message.to_s
    return :connect if msg.start_with?("Failed to connect")
    return :url if cls == "ArgumentError" || cls.include?("URI")
    return :data if cls.include?("BadResponse")
    return :noanswer if cls.include?("Socket") || cls.include?("EOF") || cls == "IOError"
    :other
  end

  def parse_body(body)
    data = nil
    begin
      data = ::JSON.parse(body.to_s)
    rescue => e
      @error = [:data, nil]
      Log.error("Weather: bad JSON: #{e.class}: #{e.message}")
      return false
    end
    cw = data.is_a?(Hash) ? data["current_weather"] : nil
    unless cw.is_a?(Hash) && cw["weathercode"]
      @error = [:data, nil]
      Log.error("Weather: no current_weather in the answer (#{body.to_s.bytesize} bytes)")
      return false
    end
    @weather = {
      temp: cw["temperature"],
      code: cw["weathercode"].to_i,
      wind: cw["windspeed"],
    }
    @error = nil
    t = Time.now
    @updated_at = "%02d:%02d" % [t.hour, t.min]
    Log.info("Weather: OK #{@cities[@idx][:name]} #{@weather[:temp]}C code=#{@weather[:code]}")
    true
  end

  # "api.open-meteo.com" out of the api URL (the whole text when it is not
  # an http(s) URL, so a typo in the settings is visible as written).
  def api_host
    s = @api
    i = s.index("://")
    return s if i.nil?
    s = s[i + 3, s.length - i - 3]
    j = s.index("/")
    j ? s[0, j] : s
  end

  def error_text
    return nil unless @error
    s = tr(@error[0])
    @error[1] ? (s % @error[1]) : s
  end

  def interpret(code)
    case code
    when 0        then :sun
    when 1, 2     then :suncloud
    when 3        then :cloud
    when 45, 48   then :fog
    when 51..67   then :rain
    when 71..77   then :snow
    when 80..82   then :showers
    when 85, 86   then :snowshow
    when 95..99   then :thunder
    else               nil
    end
  end

  # --- Drawing ------------------------------------------------------------

  def draw_all
    w = @user_area_width
    h = @user_area_height
    clear_user_area(COLOR_BG)
    draw_city_row(w)

    if @weather
      kind = interpret(@weather[:code])
      label = kind ? (@ja ? KINDS[kind][1] : KINDS[kind][0]) : "WMO #{@weather[:code]}"
      draw_icon(kind || :cloud, 6, 18)
      tx = 48
      tw = w - tx - 2
      @gfx.draw_text(tx, 18, "#{@weather[:temp]} C", COLOR_TEXT, COLOR_BG)
      @gfx.draw_text(tx, 18 + LINE_H, @gfx.fit_text(label, tw, ".."), COLOR_DIM, COLOR_BG)
      if @error
        # The reading on screen is the last good one; say why it is not newer.
        @gfx.draw_text(tx, 18 + LINE_H * 2, @gfx.fit_text(error_text, tw, ".."), COLOR_ERR, COLOR_BG)
      else
        @gfx.draw_text(tx, 18 + LINE_H * 2, @gfx.fit_text(tr(:wind) % @weather[:wind].to_s, tw, ".."), COLOR_DIM, COLOR_BG)
      end
    elsif !@net_up
      @gfx.draw_text(6, 24, tr(:net), COLOR_DIM, COLOR_BG)
    elsif @error
      # The reason, the server it is about (a wrong api line in the
      # settings shows here), and whether it tries again by itself.
      @gfx.draw_text(6, 19, @gfx.fit_text(error_text, w - 8, ".."), COLOR_ERR, COLOR_BG)
      @gfx.draw_text(6, 19 + LINE_H, @gfx.fit_text(api_host, w - 8, ".."), COLOR_DIM, COLOR_BG)
      if @retries < MAX_RETRIES
        @gfx.draw_text(6, 21 + LINE_H * 2, tr(:retrying) % [@retries, MAX_RETRIES], COLOR_DIM, COLOR_BG)
      else
        @gfx.draw_text(6, 21 + LINE_H * 2, tr(:click), COLOR_ACCENT, COLOR_BG)
      end
    else
      @gfx.draw_text(6, 24, tr(:loading), COLOR_DIM, COLOR_BG)
    end

    footer_y = h - LINE_H
    @gfx.draw_line(4, footer_y - 2, w - 5, footer_y - 2, COLOR_CLOUD)
    hint = tr(:refresh)
    hint_x = w - 4 - @gfx.text_width(hint)
    if @updated_at
      upd = tr(:updated) % @updated_at
      # The refresh hint wins when a narrow window has no room for both.
      if 6 + @gfx.text_width(upd) + 6 <= hint_x
        @gfx.draw_text(6, footer_y, upd, COLOR_DIM, COLOR_BG)
      end
    end
    @gfx.draw_text(hint_x, footer_y, hint, COLOR_ACCENT, COLOR_BG)

    @gfx.present
  end

  # The city name; with more than one city, arrows at the ends (their
  # click zones are ARROW_W wide, the rest of the row also steps forward).
  def draw_city_row(w)
    name = @cities[@idx][:name]
    if @cities.size > 1
      @gfx.fill_triangle(4, 7, 9, 2, 9, 12, COLOR_ACCENT)
      @gfx.fill_triangle(w - 5, 7, w - 10, 2, w - 10, 12, COLOR_ACCENT)
      pos = " #{@idx + 1}/#{@cities.size}"
      room = w - ARROW_W * 2
      name = @gfx.fit_text(name, room - @gfx.text_width(pos), "..")
      line = name + pos
      x = (w - @gfx.text_width(line)) / 2
      @gfx.draw_text(x, 1, name, COLOR_TEXT, COLOR_BG)
      @gfx.draw_text(x + @gfx.text_width(name), 1, pos, COLOR_DIM, COLOR_BG)
    else
      @gfx.draw_text(6, 1, @gfx.fit_text(name, w - 12, ".."), COLOR_TEXT, COLOR_BG)
    end
  end

  # 32x34-ish pictograms drawn with primitives at (x, y)
  def draw_icon(kind, x, y)
    case kind
    when :sun
      draw_sun(x + 16, y + 16, 8, true)
    when :suncloud
      draw_sun(x + 11, y + 10, 6, false)
      draw_cloud(x + 4, y + 13, COLOR_CLOUD)
    when :cloud
      draw_cloud(x + 1, y + 7, COLOR_CLOUD)
    when :fog
      draw_cloud(x + 1, y + 2, COLOR_CLOUD)
      @gfx.fill_rect(x + 3, y + 23, 26, 2, COLOR_FOG)
      @gfx.fill_rect(x + 7, y + 28, 22, 2, COLOR_FOG)
    when :rain, :showers
      draw_cloud(x + 1, y + 2, COLOR_STORM)
      draw_drop(x + 7, y + 23)
      draw_drop(x + 15, y + 26)
      draw_drop(x + 23, y + 23)
      if kind == :showers
        draw_sun_corner(x, y)
      end
    when :snow, :snowshow
      draw_cloud(x + 1, y + 2, COLOR_STORM)
      draw_flake(x + 8, y + 25)
      draw_flake(x + 17, y + 30)
      draw_flake(x + 25, y + 25)
      if kind == :snowshow
        draw_sun_corner(x, y)
      end
    when :thunder
      draw_cloud(x + 1, y + 2, COLOR_STORM)
      @gfx.fill_triangle(x + 19, y + 18, x + 11, y + 28, x + 17, y + 27, COLOR_BOLT)
      @gfx.fill_triangle(x + 17, y + 25, x + 21, y + 26, x + 13, y + 35, COLOR_BOLT)
    end
  end

  def draw_sun(cx, cy, r, rays)
    if rays
      k = r + 3
      d = (r * 7) / 10 + 2
      e = d + 3
      @gfx.draw_thick_line(cx, cy - k, cx, cy - k - 4, 2, COLOR_SUN_RIM)
      @gfx.draw_thick_line(cx, cy + k, cx, cy + k + 4, 2, COLOR_SUN_RIM)
      @gfx.draw_thick_line(cx - k, cy, cx - k - 4, cy, 2, COLOR_SUN_RIM)
      @gfx.draw_thick_line(cx + k, cy, cx + k + 4, cy, 2, COLOR_SUN_RIM)
      @gfx.draw_thick_line(cx - d, cy - d, cx - e, cy - e, 2, COLOR_SUN_RIM)
      @gfx.draw_thick_line(cx + d, cy - d, cx + e, cy - e, 2, COLOR_SUN_RIM)
      @gfx.draw_thick_line(cx - d, cy + d, cx - e, cy + e, 2, COLOR_SUN_RIM)
      @gfx.draw_thick_line(cx + d, cy + d, cx + e, cy + e, 2, COLOR_SUN_RIM)
    end
    @gfx.fill_circle(cx, cy, r + 1, COLOR_SUN_RIM)
    @gfx.fill_circle(cx, cy, r, COLOR_SUN)
  end

  # A small sun peeking out at the top right of a shower cloud.
  def draw_sun_corner(x, y)
    @gfx.fill_circle(x + 29, y + 3, 4, COLOR_SUN_RIM)
    @gfx.fill_circle(x + 29, y + 3, 3, COLOR_SUN)
  end

  # Cloud with a one-pixel darker edge: the edge shape first, the body inside.
  def draw_cloud(x, y, body)
    @gfx.fill_circle(x + 8,  y + 10, 7, COLOR_OUTLINE)
    @gfx.fill_circle(x + 16, y + 6,  9, COLOR_OUTLINE)
    @gfx.fill_circle(x + 24, y + 10, 7, COLOR_OUTLINE)
    @gfx.fill_rect(x + 5, y + 9, 26, 9, COLOR_OUTLINE)
    @gfx.fill_circle(x + 8,  y + 10, 6, body)
    @gfx.fill_circle(x + 16, y + 6,  8, body)
    @gfx.fill_circle(x + 24, y + 10, 6, body)
    @gfx.fill_rect(x + 6, y + 10, 24, 7, body)
  end

  def draw_drop(x, y)
    @gfx.draw_thick_line(x + 2, y, x, y + 6, 2, COLOR_RAIN)
  end

  def draw_flake(cx, cy)
    @gfx.draw_line(cx - 2, cy, cx + 2, cy, COLOR_SNOW)
    @gfx.draw_line(cx, cy - 2, cx, cy + 2, COLOR_SNOW)
    @gfx.draw_line(cx - 2, cy - 2, cx + 2, cy + 2, COLOR_SNOW)
    @gfx.draw_line(cx - 2, cy + 2, cx + 2, cy - 2, COLOR_SNOW)
  end

  def now_ms
    (Time.now.to_f * 1000).to_i
  end
end
