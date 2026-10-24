# Step sensor (doc/headless_s3, S1): every step taken with the board in hand
# goes out on Asterism as soon as it is detected.
#
# - Sensor: the BMI270 (picoruby-bmi270) on the board's internal I2C bus,
#   sampling at its default 100 Hz into its FIFO, which on_update empties
#   every 10 ms (no sample is lost when an update runs late). Steps are found here, in
#   Ruby, from the magnitude of the acceleration (StepDetector below), not
#   with the sensor's own step counter (it reports several steps at once).
# - Network: waits for WiFi, then connects as <ID>/step (the node ID and the
#   router come from FmrbAsterism.settings, as asterism_demo). When it cannot
#   connect, or the connection is lost, it tries again every RETRY_MS (also
#   when the router still has the previous run of this app: "already on the
#   network").
# - Each step is put on asterism/<ID>/step/pedometer/step as a MessagePack
#   map (state_hash below): steps (total), t_ms (the board's clock at the
#   step), interval_ms, cadence (steps per minute over the last steps, 0
#   when walking stopped), ago_ms, sent_ms (the board's clock at the put) and
#   event ("step", "repeat" every REPEAT_MS so a late reader sees the
#   current value, or "reset").
# - Exposes <ID>/step/pedometer: reset -> the state after reset, state -> the
#   state, clock -> the board's clock in ms (to relate t_ms to the reader's
#   clock), tune(rise_mg, min_gap_ms) -> the detector's settings (not kept
#   across a restart; /home/step_sensor.toml keeps them: rise_mg = 150,
#   min_gap_ms = 250), raw(on) -> switches the raw values (below) on/off.
# - Raw values for a plot (asterism-console draws the numbers of a value):
#   asterism/<ID>/step/pedometer/accel, a MessagePack map at 20 Hz of
#   integers in mg: x, y, z (the last sample), m (its magnitude), m_peak
#   (the largest magnitude since the last put), dyn (the magnitude above its
#   slow average, what the detector compares with rise), rise, and steps.
#   Put after the step, never before it. On by default (RAW_DEFAULT).
# - Screen: WiFi and Asterism each ok / -- on the top line, the count as
#   large as it fits, the cadence on the bottom line (text at 2x at least). KEY1 (the front button) resets
#   the count.
#
# From the PC: ruby tool/imu/step_watch.rb <ID> --router tcp/127.0.0.1:7447

# --- StepDetector (tool/imu/test/step_detector_test.rb loads this part) ---
# One step = the magnitude of the acceleration rising RISE above its slow
# average (the gravity, about 1 g in any orientation). The step is reported
# on the sample that crosses, which is on the rising edge of the foot's
# impact, so the delay is the sensor's sampling and a short smoothing only.
# Another step needs the magnitude to fall back under the average first
# (once per stride's up and down) and MIN_GAP_MS since the last one (the
# impact's own ringing). Lying still, the magnitude stays within a few mg of
# the average and nothing is counted.
class StepDetector
  BASE_TAU_MS = 1000    # the slow average (gravity) follows within ~1 s
  SMOOTH_MS = 12        # a light low-pass on the magnitude (sensor noise)
  CADENCE_STEPS = 4     # cadence from the last few intervals
  CADENCE_MAX_GAP_MS = 2000 # a longer pause ends the walk (cadence 0)

  attr_reader :count, :last_ms, :interval_ms, :rise, :min_gap_ms, :dynamic

  def initialize(rise = 0.15, min_gap_ms = 250)
    @rise = rise
    @min_gap_ms = min_gap_ms
    @count = 0
    @base = nil
    @smooth = nil
    @prev_ms = nil
    @armed = true
    @last_ms = nil
    @interval_ms = 0
    @intervals = []
    @dynamic = 0.0
  end

  def tune(rise, min_gap_ms)
    @rise = rise
    @min_gap_ms = min_gap_ms
  end

  def reset_count
    @count = 0
    @intervals = []
    @interval_ms = 0
    @last_ms = nil
  end

  # Steps per minute over the last intervals; 0 before two steps or after a
  # pause longer than CADENCE_MAX_GAP_MS.
  def cadence(now_ms)
    return 0 if @last_ms.nil? || @intervals.size == 0
    return 0 if now_ms - @last_ms > CADENCE_MAX_GAP_MS
    sum = 0
    i = 0
    while i < @intervals.size
      sum += @intervals[i]
      i += 1
    end
    (60000 * @intervals.size + sum / 2) / sum
  end

  # One sample (time in ms, acceleration in g). True when it is a step.
  def feed(t_ms, x, y, z)
    m = ::Math.sqrt(x * x + y * y + z * z)
    if @base.nil?
      @base = m
      @smooth = m
      @prev_ms = t_ms
      return false
    end
    dt = t_ms - @prev_ms
    dt = 1 if dt < 1
    dt = 100 if dt > 100
    @prev_ms = t_ms
    @smooth += (m - @smooth) * dt / (SMOOTH_MS + dt).to_f
    @dynamic = @smooth - @base
    # The average does not follow the peaks: it would rise into a walk and
    # make the next impacts look smaller.
    if @dynamic < @rise
      @base += (@smooth - @base) * dt / (BASE_TAU_MS + dt).to_f
    end
    @armed = true if @dynamic < 0.0
    return false unless @armed && @dynamic >= @rise
    return false if @last_ms && t_ms - @last_ms < @min_gap_ms
    @armed = false
    if @last_ms
      gap = t_ms - @last_ms
      if gap <= CADENCE_MAX_GAP_MS
        @interval_ms = gap
        @intervals << gap
        @intervals.shift while @intervals.size > CADENCE_STEPS
      else
        @interval_ms = 0
        @intervals = []
      end
    end
    @last_ms = t_ms
    @count += 1
    true
  end
end
# --- StepDetector end ---

class StepSensorApp < FmrbApp
  APP = "step"
  OBJECT = "pedometer"
  CONF_FILE = "/etc/system_conf.toml"
  TUNE_FILE = "/home/step_sensor.toml"
  POLL_MS = 10          # read the sensor's FIFO this often
  ODR_MS = 10           # the sensor makes a sample every 10 ms (100 Hz)
  STATS_MS = 10000      # a line in the log: samples and steps
  RETRY_MS = 3000       # connect again after a failure or a lost connection
  REPEAT_MS = 3000      # the same value again while nothing changes
  WIFI_WAIT_MS = 20000
  KEY1_PIN = 11         # front button, Low when pressed
  RAW_EVERY = 5         # raw values: every 5th sample (20 Hz)
  RAW_DEFAULT = true    # raw values on from the start

  class Pedometer
    def initialize(app)
      @app = app
    end

    def reset
      @app.reset_steps("remote")
    end

    def state
      @app.state_hash("state")
    end

    def clock
      ::Machine.board_millis
    end

    def tune(rise_mg, min_gap_ms)
      @app.tune(rise_mg.to_i, min_gap_ms.to_i)
    end

    def raw(on)
      @app.set_raw(on)
    end
  end

  attr_reader :detector

  def on_create
    @started = Machine.board_millis
    st = ::FmrbAsterism.settings
    @node = st.node
    @locator = st.router
    @creds = st.credentials
    @creds_error = st.error
    @net = "waiting for WiFi"
    @next_try = 0
    @next_repeat = 0
    @event = "state"
    @connects = 0
    @puts = 0
    @key = "asterism/#{@node}/#{APP}/#{OBJECT}/step"
    @raw_key = "asterism/#{@node}/#{APP}/#{OBJECT}/accel"
    @raw_puts = 0
    set_raw(RAW_DEFAULT)
    @detector = StepDetector.new
    load_tune
    @sensor = "no sensor"
    @imu = nil
    @i2c = nil
    @samples = 0
    @ring = []
    @next_stats = 0
    @busy_max = 0
    @busy_sum = 0
    @key1 = nil
    @key1_was = 1
    begin
      @key1 = GPIO.new(KEY1_PIN, GPIO::IN)
    rescue => e
      Log.warn("step_sensor: KEY1: #{e.message}")
    end
    width = conf_int(CONF_FILE, "display_width")
    @small = width > 0 && width < 320
    request_fullscreen(true) if @small
    open_sensor
    @drawn = nil
    draw_screen
  end

  def on_resize(w, h)
    @drawn = nil
    draw_screen
  end

  def on_destroy
    ::Asterism.close
    @i2c.close if @i2c
  end

  def uptime_ms
    Machine.board_millis - @started
  end

  # The number in `<key> = 123` of a small TOML file, or nil.
  def conf_int(path, key)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n").each do |line|
      next unless line.start_with?(key)
      eq = line.index("=")
      return eq ? line[eq + 1, line.length].to_s.strip.to_i : nil
    end
    nil
  rescue
    nil
  end

  def load_tune
    rise = conf_int(TUNE_FILE, "rise_mg")
    gap = conf_int(TUNE_FILE, "min_gap_ms")
    return unless rise || gap
    tune(rise || 150, gap || 250)
  end

  def tune(rise_mg, min_gap_ms)
    rise_mg = 30 if rise_mg < 30
    min_gap_ms = 100 if min_gap_ms < 100
    @detector.tune(rise_mg / 1000.0, min_gap_ms)
    Log.info("step_sensor: tune rise=#{rise_mg} mg min_gap=#{min_gap_ms} ms")
    { "rise_mg" => rise_mg, "min_gap_ms" => min_gap_ms }
  end

  def open_sensor
    return @sensor = "no sensor here" if FmrbConst::PLATFORM != "esp32"
    begin
      @i2c = I2C.new(unit: :ESP32_I2C1, sda_pin: FmrbHw::PIN_I2C1_SDA, scl_pin: FmrbHw::PIN_I2C1_SCL)
      imu = BMI270.new(@i2c)
      t0 = Machine.board_millis
      if imu.init
        imu.fifo_accel_start
        @imu = imu
        @sensor = "ok"
        Log.info("step_sensor: BMI270 at 0x#{sprintf('%02X', imu.address)} (#{Machine.board_millis - t0} ms)")
      else
        @sensor = "BMI270: #{imu.error || 'not found'}"
        Log.warn("step_sensor: #{@sensor}")
      end
    rescue => e
      @sensor = "I2C: #{e.message}"
      Log.warn("step_sensor: #{@sensor}")
    end
  end

  # Feeds every sample the sensor made since the last call (its FIFO) to
  # the detector; a step goes out at once. The newest sample is taken as
  # made now and the earlier ones ODR_MS apart before it.
  def sample
    return unless @imu
    raw = @imu.read_fifo_accel
    return unless raw
    n = raw.size / 3
    return if n == 0
    now = Machine.board_millis
    k = BMI270::ACC_RANGE_G / 32768.0
    step = false
    i = 0
    while i < n
      o = i * 3
      x = raw[o] * k
      y = raw[o + 1] * k
      z = raw[o + 2] * k
      step = true if @detector.feed(now - (n - 1 - i) * ODR_MS, x, y, z)
      # The last few samples' dyn (mg), for the log line of a step.
      @ring << (@detector.dynamic * 1000).round
      @ring.shift if @ring.size > 8
      raw_sample(x, y, z) if @raw_on
      i += 1
    end
    @samples += n
    if step
      publish("step")
      Log.info("step_sensor: step #{@detector.count} cadence #{@detector.cadence(now)} " \
               "(#{n} samples read, #{Machine.board_millis - now} ms to put; dyn mg #{@ring.join(' ')})")
    end
    # After the step: the raw values never hold a step back.
    put_raw if @raw_ready
  end

  # The raw acceleration for a plot (pedometer.raw(true)): every RAW_EVERY
  # samples (20 Hz) the last sample, and the largest magnitude since the
  # last put (so the impacts show although most samples are left out).
  def raw_sample(x, y, z)
    m = ::Math.sqrt(x * x + y * y + z * z)
    @raw_peak = m if @raw_peak.nil? || m > @raw_peak
    @raw_n += 1
    return if @raw_n < RAW_EVERY
    @raw_n = 0
    @raw_last = [x, y, z, m, @raw_peak, @detector.dynamic]
    @raw_peak = nil
    @raw_ready = true
  end

  def put_raw
    @raw_ready = false
    return unless ::Asterism.connected? && !::Asterism.reconnecting?
    r = @raw_last
    v = { "x" => (r[0] * 1000).round, "y" => (r[1] * 1000).round, "z" => (r[2] * 1000).round,
          "m" => (r[3] * 1000).round, "m_peak" => (r[4] * 1000).round, "dyn" => (r[5] * 1000).round,
          "rise" => (@detector.rise * 1000).round, "steps" => @detector.count }
    begin
      ::Asterism.session.put(@raw_key, ::MessagePack.pack(v))
      @raw_puts += 1
    rescue => e
      Log.warn("step_sensor: raw put: #{e.class}: #{e.message}")
    end
  end

  def set_raw(on)
    @raw_on = on ? true : false
    @raw_n = 0
    @raw_peak = nil
    @raw_ready = false
    Log.info("step_sensor: raw #{@raw_on ? 'on' : 'off'}")
    @raw_on
  end

  def reset_steps(why)
    @detector.reset_count
    Log.info("step_sensor: reset (#{why})")
    publish("reset")
    state_hash("reset")
  end

  def state_hash(event)
    now = Machine.board_millis
    last = @detector.last_ms
    { "steps" => @detector.count, "t_ms" => last, "interval_ms" => @detector.interval_ms,
      "cadence" => @detector.cadence(now), "ago_ms" => last ? now - last : nil,
      "sent_ms" => now, "event" => event, "sensor" => @sensor, "raw" => @raw_on }
  end

  def publish(event)
    @event = event
    @next_repeat = Machine.board_millis + REPEAT_MS
    return unless ::Asterism.connected?
    begin
      ::Asterism.session.put(@key, ::MessagePack.pack(state_hash(event)))
      @puts += 1
    rescue ::Asterism::Error => e
      Log.warn("step_sensor: put: #{e.message}")
    rescue => e
      Log.warn("step_sensor: put: #{e.class}: #{e.message}")
    end
  end

  def network_ready?
    info = FmrbApp.wifi_info
    return true if info.nil? || info[:connected]
    uptime_ms > WIFI_WAIT_MS
  end

  def connect
    t0 = Machine.board_millis
    begin
      raise ::Asterism::Error, @creds_error if @creds_error
      ::Asterism.connect(@locator, node: @node, app: APP, credentials: @creds)
      ::Asterism.expose(OBJECT, StepSensorApp::Pedometer.new(self),
                        methods: { reset: 0, state: 0, clock: 0, tune: 2, raw: 1 })
      @connects += 1
      @net = "connected"
      Log.info("step_sensor: #{@node}/#{APP} connected (#{@locator}, #{Machine.board_millis - t0} ms)")
      publish("state")
    rescue ::Asterism::Error => e
      ::Asterism.close
      @net = e.message.include?("already on the network") ? "still on the network, waiting" : "retrying"
      @next_try = Machine.board_millis + RETRY_MS
      Log.warn("step_sensor: connect: #{e.message}")
    rescue => e
      ::Asterism.close
      @net = "retrying"
      @next_try = Machine.board_millis + RETRY_MS
      Log.warn("step_sensor: connect: #{e.class}: #{e.message}")
    end
  end

  def network
    now = Machine.board_millis
    if ::Asterism.connected?
      begin
        ::Asterism.poll
      rescue => e
        Log.warn("step_sensor: poll: #{e.class}: #{e.message}")
      end
      if ::Asterism.connected?
        @net = ::Asterism.reconnecting? ? "reconnecting" : "connected"
        publish("repeat") if now >= @next_repeat && !::Asterism.reconnecting?
        return
      end
      @net = "lost, retrying"
      Log.warn("step_sensor: lost: #{::Asterism.lost_reason}")
      ::Asterism.close
      @next_try = now + RETRY_MS
      return
    end
    return unless now >= @next_try
    return unless network_ready?
    @net = "connecting" if @net == "waiting for WiFi"
    connect
  end

  def check_key
    return unless @key1
    v = @key1.read
    reset_steps("KEY1") if v == 0 && @key1_was != 0
    @key1_was = v
  end

  def on_update
    t0 = Machine.board_millis
    sample
    check_key
    network
    draw_screen
    stats
    took = Machine.board_millis - t0
    @busy_max = took if took > @busy_max
    @busy_sum += took
    POLL_MS
  end

  def stats
    now = Machine.board_millis
    return if now < @next_stats
    if @next_stats > 0
      Log.info("step_sensor: #{@samples} samples in #{now - @stats_from} ms, steps #{@detector.count}, " \
               "puts #{@puts}, raw puts #{@raw_puts}, update busy #{@busy_sum} ms (max #{@busy_max}), " \
               "net #{@net}, sensor #{@sensor}")
    end
    @busy_max = 0
    @busy_sum = 0
    @samples = 0
    @stats_from = now
    @next_stats = now + STATS_MS
  end

  # The screen: WiFi and Asterism (the router session) each ok / -- on the
  # top line, the count as large as it fits (seven-segment digits), the
  # cadence (or the sensor's trouble) on the bottom line. Text is the 6x8
  # font at 2x, so it
  # reads on the 240x135 screen from arm's length; the details (node,
  # router, why it is offline) are in the log and in pedometer.state.
  def wifi_ok?
    now = Machine.board_millis
    if @wifi_at.nil? || now - @wifi_at >= 1000
      info = FmrbApp.wifi_info
      @wifi = info && info[:connected] ? true : false
      @wifi_at = now
    end
    @wifi
  end

  def bottom_line(cad)
    return "no IMU" unless @sensor == "ok"
    cad > 0 ? "#{cad} steps/min" : ""
  end

  # Segments a-g of each digit, bit 0 = a (top), clockwise, bit 6 = g.
  SEGMENTS = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F]

  def draw_digit(x, y, dw, dh, t, d, color)
    bits = SEGMENTS[d]
    h2 = (dh - 3 * t) / 2
    dh = 3 * t + 2 * h2
    @gfx.fill_rect(x, y, dw, t, color) if bits & 0x01 != 0                       # a
    @gfx.fill_rect(x + dw - t, y, t, t + h2 + t, color) if bits & 0x02 != 0      # b
    @gfx.fill_rect(x + dw - t, y + t + h2, t, h2 + 2 * t, color) if bits & 0x04 != 0 # c
    @gfx.fill_rect(x, y + dh - t, dw, t, color) if bits & 0x08 != 0              # d
    @gfx.fill_rect(x, y + t + h2, t, h2 + 2 * t, color) if bits & 0x10 != 0      # e
    @gfx.fill_rect(x, y, t, t + h2 + t, color) if bits & 0x20 != 0               # f
    @gfx.fill_rect(x, y + t + h2, dw, t, color) if bits & 0x40 != 0              # g
  end

  # Redraws only when something shown changed.
  def draw_screen
    now = Machine.board_millis
    cad = @detector.cadence(now)
    ast = @net == "connected"
    shown = [@detector.count, wifi_ok?, ast, bottom_line(cad)]
    return if @drawn && @drawn[0] == shown[0] && @drawn[1] == shown[1] &&
              @drawn[2] == shown[2] && @drawn[3] == shown[3]
    full = @drawn.nil?
    @drawn = shown
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    clear_user_area if full
    @gfx.set_text_size(2)
    # Top line (16 px): WiFi on the left, Asterism on the right.
    @gfx.fill_rect(x0, y0, w, 18, theme_bg)
    @gfx.draw_text(x0 + 2, y0 + 1, shown[1] ? "WiFi ok" : "WiFi --", shown[1] ? theme_fg : theme_accent)
    at = ast ? "AST ok" : "AST --"
    @gfx.draw_text(x0 + w - 2 - 12 * at.length, y0 + 1, at, ast ? theme_fg : theme_accent)
    # Bottom line: the cadence, or what is wrong with the sensor.
    @gfx.fill_rect(x0, y0 + h - 18, w, 18, theme_bg)
    bl = shown[3]
    @gfx.draw_text(x0 + (w - 12 * bl.length) / 2, y0 + h - 17, bl, @sensor == "ok" ? theme_fg : theme_accent) if bl.length > 0
    # The count between them, as large as fits. The 6x8 font scales to 4x
    # at most, so the digits are drawn as seven segments of rectangles.
    text = shown[0].to_s
    top = y0 + 21
    room = h - 42
    @gfx.fill_rect(x0, top, w, room, theme_bg)
    n = text.length
    dh = room
    dw = dh * 5 / 9
    gap = dw / 5
    fit = (w - 8 - gap * (n - 1)) / n
    if dw > fit
      dw = fit
      dh = dw * 9 / 5
      gap = dw / 5
    end
    t = dw / 6
    t = 2 if t < 2
    tw = n * dw + (n - 1) * gap
    x = x0 + (w - tw) / 2
    y = top + (room - dh) / 2
    i = 0
    while i < n
      draw_digit(x, y, dw, dh, t, text.getbyte(i) - 48, theme_fg)
      x += dw + gap
      i += 1
    end
    @gfx.set_text_size(1)
    draw_window_frame unless @small
    @gfx.present
  end
end

begin
  app = StepSensorApp.new
  app.start
rescue => e
  Log.error("step_sensor: #{e.message}")
end
