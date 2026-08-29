# Network Test Application
# Smoke test for the Ruby networking API (doc/reference/ruby_network_api_design.md).
# Each row runs one test on click; results are shown in the log area below.
# Detailed step-by-step logs go to the system log with the "NetTest" prefix:
#   docker compose logs fmruby_core | grep NetTest
#
# Notes:
# - Tests run synchronously in on_event, so the window freezes while a test
#   runs (TIMEOUT intentionally blocks ~10s to verify the socket timeout).
# - Requires picoruby-socket / picoruby-net-http / picoruby-net-websocket
#   (esp32p4 and linux builds only; the script fails to load on Retro).
# - WS ECHO needs an echo server, e.g. on the PC:
#     python3 -m websockets serve ws://0.0.0.0:8765
#   then set WS_URL below to the PC address.

require 'net/http'
require 'net/websocket'
require 'socket'

class NetTestApp < FmrbApp
  HTTP_URL  = "http://example.com/"
  HTTPS_URL = "https://example.com/"
  BADSSL_URL = "https://self-signed.badssl.com/"
  WS_URL    = "ws://host.docker.internal:8765/"   # edit to your echo server
  # TEST-NET-1: unroutable, verifies the ~10s connect timeout
  DEAD_HOST = "192.0.2.1"

  CHAR_W = 6
  CHAR_H = 8
  ROW_H = 14
  COLOR_ROW_BG   = 0xDB   # light gray (RGB332)
  COLOR_ROW_TEXT = 0x00
  COLOR_OK   = 0x1C       # green
  COLOR_NG   = 0xE0       # red
  COLOR_RUN  = 0x83       # blue-ish
  COLOR_LOG_BG = 0xFF
  COLOR_LOG_TEXT = 0x00

  TESTS = [
    { key: :status,  label: "NET STATUS" },
    { key: :http,    label: "HTTP GET" },
    { key: :https,   label: "HTTPS GET" },
    { key: :badssl,  label: "BAD CERT (expect NG=OK)" },
    { key: :ws,      label: "WS ECHO" },
    { key: :timeout, label: "TIMEOUT (~10s)" },
  ]

  def initialize
    super()
    @results = {}   # key => :ok / :ng / :run
    @log_lines = []
  end

  def on_create
    dbg("on_create: window=#{@window_width}x#{@window_height} " \
        "user_area=#{@user_area_width}x#{@user_area_height}")
    dbg("config: HTTP=#{HTTP_URL} HTTPS=#{HTTPS_URL}")
    dbg("config: BADSSL=#{BADSSL_URL} WS=#{WS_URL} DEAD=#{DEAD_HOST}")
    log("Click a row to run a test")
    draw_full
  end

  def on_update
    200
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    idx = row_index_at(ev[:y])
    return if idx.nil?
    run_test(TESTS[idx][:key])
  end

  def on_resize(new_width, new_height)
    draw_full
  end

  def on_destroy
    Log.info("NetTest: Destroyed")
  end

  private

  # --- Test execution ---------------------------------------------------------

  def run_test(key)
    dbg("=== run_test(#{key}) start ===")
    @results[key] = :run
    draw_full
    t0 = now_ms
    begin
      case key
      when :status  then test_status
      when :http    then test_http
      when :https   then test_https
      when :badssl  then test_badssl
      when :ws      then test_ws
      when :timeout then test_timeout
      end
    rescue => e
      @results[key] = :ng
      log_exception(key, e, now_ms - t0)
      log("NG #{key}: #{e.class} #{e.message.to_s[0, 60]}")
    end
    dbg("=== run_test(#{key}) end: result=#{@results[key]} #{now_ms - t0}ms ===")
    draw_full
  end

  def log_exception(where, e, dt_ms)
    Log.error("NetTest: [#{where}] exception after #{dt_ms}ms")
    Log.error("NetTest: [#{where}] #{e.class}: #{e.message}")
    if e.backtrace
      e.backtrace.each { |line| Log.error("NetTest: [#{where}]   #{line}") }
    end
  end

  def test_status
    dbg("status: calling FmrbNet.connected?")
    ok = FmrbNet.connected?
    ip = FmrbNet.ip_address
    host = FmrbNet.hostname
    ssid = FmrbNet.ssid
    dbg("status: connected=#{ok} ip=#{ip} hostname=#{host} ssid=#{ssid.inspect}")
    log("connected=#{ok} ip=#{ip}")
    log("host=#{host} ssid=#{ssid}")
    @results[:status] = ok ? :ok : :ng
  end

  def http_get_with_logs(tag, url)
    dbg("#{tag}: URI.parse(#{url})")
    uri = URI.parse(url)
    dbg("#{tag}: host=#{uri.host} port=#{uri.port} scheme=#{uri.scheme} path=#{uri.path.inspect}")
    t0 = now_ms
    dbg("#{tag}: Net::HTTP.get_response start")
    res = Net::HTTP.get_response(uri)
    dt = now_ms - t0
    dbg("#{tag}: response in #{dt}ms: code=#{res.code} msg=#{res.message rescue ''}")
    dbg("#{tag}: body=#{res.body ? res.body.length : 'nil'}B")
    [res, dt]
  end

  def test_http
    res, dt = http_get_with_logs("http", HTTP_URL)
    good = res.code == "200"
    @results[:http] = good ? :ok : :ng
    log("HTTP #{res.code} #{res.body ? res.body.length : 0}B #{dt}ms")
  end

  def test_https
    res, dt = http_get_with_logs("https", HTTPS_URL)
    good = res.code == "200"
    @results[:https] = good ? :ok : :ng
    log("HTTPS #{res.code} #{res.body ? res.body.length : 0}B #{dt}ms")
  end

  # Certificate verification must reject a self-signed server, so the
  # expected outcome here is an exception (shown as OK).
  def test_badssl
    t0 = now_ms
    begin
      res, _dt = http_get_with_logs("badssl", BADSSL_URL)
      @results[:badssl] = :ng
      dbg("badssl: request SUCCEEDED unexpectedly: code=#{res.code}")
      log("BAD CERT accepted?! code=#{res.code} (verify NOT working)")
    rescue => e
      @results[:badssl] = :ok
      dbg("badssl: rejected as expected: #{e.class}: #{e.message}")
      log("bad cert rejected (#{e.class}) #{now_ms - t0}ms")
    end
  end

  def test_ws
    t0 = now_ms
    got = nil
    dbg("ws: connecting to #{WS_URL}")
    Net::WebSocket::Client.connect(WS_URL) do |ws|
      dbg("ws: connected in #{now_ms - t0}ms, sending text")
      ws.send_text("hello fmruby")
      dbg("ws: sent, waiting up to 5s for echo")
      got = ws.receive(timeout: 5)
      dbg("ws: received #{got.inspect}")
    end
    if got == "hello fmruby"
      @results[:ws] = :ok
      log("WS echo ok #{now_ms - t0}ms")
    else
      @results[:ws] = :ng
      dbg("ws: echo mismatch or timeout: #{got.inspect}")
      log("WS echo mismatch: #{got.inspect}")
    end
  end

  # Expected outcome: exception after roughly FMRB_SOCKET_CONNECT_TIMEOUT_MS.
  def test_timeout
    t0 = now_ms
    dbg("timeout: TCPSocket.new(#{DEAD_HOST}, 81) - expecting ~10s block")
    begin
      TCPSocket.new(DEAD_HOST, 81)
      @results[:timeout] = :ng
      dbg("timeout: connect SUCCEEDED unexpectedly")
      log("TIMEOUT: connected?! (unexpected)")
    rescue => e
      dt = now_ms - t0
      dbg("timeout: raised after #{dt}ms: #{e.class}: #{e.message}")
      # anywhere between 5-15s counts as "the timeout worked"
      @results[:timeout] = (dt > 5000 && dt < 15000) ? :ok : :ng
      log("connect gave up after #{dt}ms (#{e.class})")
    end
  end

  # --- Drawing ----------------------------------------------------------------

  def row_y(idx)
    @user_area_y0 + idx * ROW_H
  end

  def row_index_at(y)
    idx = (y - @user_area_y0) / ROW_H
    (idx >= 0 && idx < TESTS.length) ? idx : nil
  end

  def result_mark(key)
    case @results[key]
    when :ok  then ["OK", COLOR_OK]
    when :ng  then ["NG", COLOR_NG]
    when :run then ["..", COLOR_RUN]
    else ["--", COLOR_ROW_TEXT]
    end
  end

  def draw_full
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, COLOR_LOG_BG)

    TESTS.each_with_index do |t, i|
      y = row_y(i)
      @gfx.fill_rect(@user_area_x0, y, @user_area_width, ROW_H - 1, COLOR_ROW_BG)
      text_y = y + (ROW_H - CHAR_H) / 2
      @gfx.draw_text(@user_area_x0 + 2, text_y, t[:label],
                     COLOR_ROW_TEXT, COLOR_ROW_BG)
      mark, color = result_mark(t[:key])
      @gfx.draw_text(@user_area_x0 + @user_area_width - 2 * CHAR_W - 2, text_y,
                     mark, color, COLOR_ROW_BG)
    end

    log_y0 = row_y(TESTS.length) + 2
    max_lines = (@user_area_y0 + @user_area_height - log_y0) / CHAR_H
    lines = @log_lines.last(max_lines < 0 ? 0 : max_lines)
    lines.each_with_index do |line, i|
      @gfx.draw_text(@user_area_x0 + 2, log_y0 + i * CHAR_H,
                     line, COLOR_LOG_TEXT, COLOR_LOG_BG)
    end

    draw_window_frame
    @gfx.present
  end

  # On-screen log line (also mirrored to the system log)
  def log(msg)
    Log.info("NetTest: #{msg}")
    max_chars = (@user_area_width - 4) / CHAR_W
    @log_lines << msg.to_s[0, max_chars]
    @log_lines.shift while @log_lines.length > 12
  end

  # System-log-only debug line
  def dbg(msg)
    Log.info("NetTest: #{msg}")
  end

  def now_ms
    (Time.now.to_f * 1000).to_i
  end
end

Log.info("NetTest: Creating NetTestApp")
begin
  app = NetTestApp.new
  app.start
rescue => e
  Log.error("NetTest: Exception: #{e.class}")
  Log.error("NetTest: Message: #{e.message}")
  Log.error("NetTest: Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("NetTest: Script ended")
