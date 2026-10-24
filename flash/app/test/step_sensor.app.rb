# Step sensor (doc/headless_s3, S1): every step taken with the board in hand
# goes out on Asterism as soon as it is detected.
#
# - Sensor: the BMI270's FIFO (100 Hz), emptied every POLL_MS. Steps are
#   found here from the magnitude of the acceleration (StepDetector), not
#   with the sensor's own step counter.
# - Network: FmrbAsterism::Link connects as <ID>/step and reconnects.
#   asterism/<ID>/step/pedometer/step gets state_hash on each step, every
#   REPEAT_MS and on a reset; .../pedometer/accel the raw values (AccelTrace)
#   at 20 Hz while raw is on.
# - Object <ID>/step/pedometer: reset, state, clock, tune(rise_mg,
#   min_gap_ms), save (to /home/step_sensor.toml), raw(on). The tuning panel
#   of walk_scroller (T) uses state, tune and save.
# - Screen: WiFi and AST on the top line, the count, the cadence at the
#   bottom. KEY1 (the front button) resets the count.
#
# From the PC: ruby tool/imu/step_watch.rb <ID> --router tcp/127.0.0.1:7447

# --- StepDetector (tool/imu/test/step_detector_test.rb loads this part) ---
# One step = the magnitude of the acceleration rising RISE above its slow
# average (gravity), reported on the sample that crosses. Another step needs
# the magnitude to fall back under the average first (arming) and MIN_GAP_MS
# since the last step; a rise inside the gap uses up the arming.
# The average has three speeds (tool/imu/test shows each one is needed):
# - BASE_SLOW_TAU_MS above average + rise, so a walk's impacts hardly pull
#   it up (with one speed the steps are seen later),
# - BASE_TAU_MS otherwise, so it never stops following,
# - STEADY_TAU_MS once the magnitude has stayed within STEADY_G for
#   STEADY_MS: after shaking the average lags far behind, and this brings
#   dyn back to 0 within a few seconds of putting the board down.
class StepDetector
  BASE_TAU_MS = 1000
  BASE_SLOW_TAU_MS = 10000
  STEADY_G = 0.06
  STEADY_MS = 400
  STEADY_TAU_MS = 400
  SMOOTH_MS = 12            # a light low-pass on the magnitude (sensor noise)
  CADENCE_STEPS = 4         # cadence from the last few intervals
  CADENCE_MAX_GAP_MS = 2000 # a longer pause ends the walk (cadence 0)

  attr_reader :count, :last_ms, :interval_ms, :rise, :min_gap_ms, :dynamic

  def initialize(rise = 0.15, min_gap_ms = 250)
    @rise = rise
    @min_gap_ms = min_gap_ms
    @base = nil
    @armed = true
    @dynamic = 0.0
    reset_count
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
      steady_from(t_ms)
      return false
    end
    dt = t_ms - @prev_ms
    dt = 1 if dt < 1
    dt = 100 if dt > 100
    @prev_ms = t_ms
    @smooth += (m - @smooth) * dt / (SMOOTH_MS + dt).to_f
    @dynamic = @smooth - @base
    @base += (@smooth - @base) * dt / (base_tau(t_ms) + dt).to_f
    @armed = true if @dynamic < 0.0
    return false unless @armed && @dynamic >= @rise
    @armed = false
    return false if @last_ms && t_ms - @last_ms < @min_gap_ms
    count_step(t_ms)
    true
  end

  # The span of the magnitude restarts here.
  def steady_from(t_ms)
    @steady_lo = @smooth
    @steady_hi = @smooth
    @steady_t = t_ms
  end

  def base_tau(t_ms)
    @steady_hi = @smooth if @smooth > @steady_hi
    @steady_lo = @smooth if @smooth < @steady_lo
    steady_from(t_ms) if @steady_hi - @steady_lo > STEADY_G
    return STEADY_TAU_MS if t_ms - @steady_t >= STEADY_MS
    @dynamic >= @rise ? BASE_SLOW_TAU_MS : BASE_TAU_MS
  end

  def count_step(t_ms)
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
  end
end

# The raw values for a plot (pedometer/accel), in g, kept after every sample
# fed to the detector: every EVERY samples (20 Hz) the last sample (x, y, z,
# its magnitude m, the detector's dyn) and m_peak, the largest m since the
# previous snapshot. dyn_peak is the largest dyn since the previous take: at
# 100 Hz an impact is too short for dyn at 20 Hz to catch, so a counted step
# always shows as dyn_peak >= rise.
class AccelTrace
  EVERY = 5

  attr_reader :ready

  def initialize
    clear
  end

  def clear
    @n = 0
    @m_top = nil
    @dyn_top = nil
    @ready = false
  end

  def add(x, y, z, dyn)
    m = ::Math.sqrt(x * x + y * y + z * z)
    @m_top = m if @m_top.nil? || m > @m_top
    @dyn_top = dyn if @dyn_top.nil? || dyn > @dyn_top
    @n += 1
    return if @n < EVERY
    @n = 0
    @x = x
    @y = y
    @z = z
    @m = m
    @m_peak = @m_top
    @m_top = nil
    @dyn = dyn
    @ready = true
  end

  # The last snapshot as the accel map (mg); starts the next dyn_peak.
  def take(rise_mg, steps)
    @ready = false
    pk = @dyn_top || @dyn
    @dyn_top = nil
    { "x" => mg(@x), "y" => mg(@y), "z" => mg(@z), "m" => mg(@m), "m_peak" => mg(@m_peak),
      "dyn" => mg(@dyn), "dyn_peak" => mg(pk), "rise" => rise_mg, "steps" => steps }
  end

  def mg(g)
    (g * 1000).round
  end
end
# --- StepDetector end ---

class StepSensorApp < FmrbApp
  APP = "step"
  OBJECT = "pedometer"
  CONF_FILE = "/etc/system_conf.toml"
  POLL_MS = 10          # read the sensor's FIFO this often
  ODR_MS = 10           # the sensor makes a sample every 10 ms (100 Hz)
  REPEAT_MS = 3000      # the same value again while nothing changes
  SCREEN_WAIT_MS = 2000 # connect anyway if the fullscreen answer never comes
  KEY1_PIN = 11         # front button, Low when pressed
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

    def save
      @app.save_tune
    end

    def raw(on)
      @app.set_raw(on)
    end
  end

  # A line in the log every 10 s (samples, steps, puts, how long the
  # updates took), kept off the main path.
  class Stats
    EVERY_MS = 10000

    def initialize
      @next = 0
      @samples = 0
      @puts = 0
      @raw_puts = 0
      @busy_sum = 0
      @busy_max = 0
    end

    def samples(n)
      @samples += n
    end

    def put
      @puts += 1
    end

    def raw_put
      @raw_puts += 1
    end

    # At the end of every update (t0: when it began).
    def update_done(t0, app)
      now = ::Machine.board_millis
      took = now - t0
      @busy_max = took if took > @busy_max
      @busy_sum += took
      return if now < @next
      ::Log.info("step_sensor: #{@samples} samples in #{now - @from} ms, #{app.stats_line}, puts #{@puts}, " \
               "raw puts #{@raw_puts}, update busy #{@busy_sum} ms (max #{@busy_max})") if @next > 0
      @busy_max = 0
      @busy_sum = 0
      @samples = 0
      @from = now
      @next = now + EVERY_MS
    end
  end

  def on_create
    # On a small screen the first connect (a TLS handshake that blocks for
    # 10-20 s) waits for the fullscreen canvas, or the canvas stays black.
    @link = ::FmrbAsterism::Link.new(self, APP, tag: "step_sensor", screen_wait_ms: SCREEN_WAIT_MS)
    @key = "asterism/#{@link.node}/#{APP}/#{OBJECT}/step"
    @raw_key = "asterism/#{@link.node}/#{APP}/#{OBJECT}/accel"
    @next_repeat = 0
    @stats = Stats.new
    @detector = StepDetector.new
    @trace = AccelTrace.new
    set_raw(RAW_DEFAULT)
    @saved_tune = nil # [rise_mg, min_gap_ms] as in app_settings, nil without them
    load_tune
    @key1_was = 1
    begin
      @key1 = GPIO.new(KEY1_PIN, GPIO::IN)
    rescue => e
      @key1 = nil
      Log.warn("step_sensor: KEY1: #{e.message}")
    end
    conf = FmrbToml.load(CONF_FILE)
    width = conf ? conf["display_width"] : nil
    @small = width.is_a?(Integer) && width > 0 && width < 320
    request_fullscreen(true) if @small
    @link.screen_ready unless @small
    open_sensor
    @drawn = nil
    draw_screen
  end

  def on_resize(w, h)
    @link.screen_ready
    @drawn = nil
    draw_screen
  end

  def on_destroy
    @link.close
    @i2c.close if @i2c
  end

  def on_update
    t0 = Machine.board_millis
    sample
    check_key
    network
    draw_screen
    @stats.update_done(t0, self)
    POLL_MS
  end

  # --- Settings (/home/step_sensor.toml through app_settings) ---

  def load_tune
    s = app_settings
    rise = s["rise_mg"]
    gap = s["min_gap_ms"]
    rise = nil unless rise.is_a?(Integer)
    gap = nil unless gap.is_a?(Integer)
    return unless rise || gap
    t = tune(rise || 150, gap || 250)
    @saved_tune = [t["rise_mg"], t["min_gap_ms"]]
  end

  def rise_mg
    (@detector.rise * 1000).round
  end

  def tune_saved?
    s = @saved_tune
    s ? s[0] == rise_mg && s[1] == @detector.min_gap_ms : false
  end

  def tune(rise_mg, min_gap_ms)
    rise_mg = 30 if rise_mg < 30
    min_gap_ms = 100 if min_gap_ms < 100
    @detector.tune(rise_mg / 1000.0, min_gap_ms)
    Log.info("step_sensor: tune rise=#{rise_mg} mg min_gap=#{min_gap_ms} ms")
    { "rise_mg" => rise_mg, "min_gap_ms" => min_gap_ms, "saved" => tune_saved? }
  end

  # Rewrites only the rise_mg and min_gap_ms lines (FmrbToml.update); the
  # only thing here that writes to the flash.
  def save_tune
    rise = rise_mg
    gap = @detector.min_gap_ms
    v = { "rise_mg" => rise, "min_gap_ms" => gap, "saved" => false, "file" => app_settings_path }
    if save_app_settings("rise_mg" => rise, "min_gap_ms" => gap)
      @saved_tune = [rise, gap]
      v["saved"] = true
      Log.info("step_sensor: saved rise=#{rise} mg min_gap=#{gap} ms to #{v["file"]}")
    else
      v["error"] = FmrbToml.error.to_s
      Log.warn("step_sensor: save: #{v["error"]}")
    end
    v
  end

  # --- Sensor and detection ---

  def open_sensor
    @sensor = "no sensor here"
    return if FmrbConst::PLATFORM != "esp32"
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

  # Feeds every sample in the sensor's FIFO to the detector; a step goes out
  # at once, the raw values after it. The newest sample is taken as made now
  # and the earlier ones ODR_MS apart before it.
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
      x = raw[i * 3] * k
      y = raw[i * 3 + 1] * k
      z = raw[i * 3 + 2] * k
      step = true if @detector.feed(now - (n - 1 - i) * ODR_MS, x, y, z)
      @trace.add(x, y, z, @detector.dynamic) if @raw_on
      i += 1
    end
    @stats.samples(n)
    if step
      publish("step")
      Log.info("step_sensor: step #{@detector.count} cadence #{@detector.cadence(now)} " \
               "(#{n} samples read, #{Machine.board_millis - now} ms to put)")
    end
    put_raw if @trace.ready
  end

  # --- Network ---

  def put_raw
    v = @trace.take(rise_mg, @detector.count)
    return unless @link.up?
    begin
      ::Asterism.session.put(@raw_key, ::MessagePack.pack(v))
      @stats.raw_put
    rescue => e
      Log.warn("step_sensor: raw put: #{e.class}: #{e.message}")
    end
  end

  def set_raw(on)
    @raw_on = on ? true : false
    @trace.clear
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
      "sent_ms" => now, "event" => event, "sensor" => @sensor, "raw" => @raw_on,
      "rise_mg" => rise_mg, "min_gap_ms" => @detector.min_gap_ms, "saved" => tune_saved? }
  end

  def publish(event)
    @next_repeat = Machine.board_millis + REPEAT_MS
    return unless @link.connected?
    begin
      ::Asterism.session.put(@key, ::MessagePack.pack(state_hash(event)))
      @stats.put
    rescue ::Asterism::Error => e
      Log.warn("step_sensor: put: #{e.message}")
    rescue => e
      Log.warn("step_sensor: put: #{e.class}: #{e.message}")
    end
  end

  # After every connect (FmrbAsterism::Link): Asterism.close forgot the
  # exposed object.
  def link_up(link)
    ::Asterism.expose(OBJECT, StepSensorApp::Pedometer.new(self),
                      methods: { reset: 0, state: 0, clock: 0, tune: 2, save: 0, raw: 1 })
    publish("state")
  end

  def network
    return unless @link.update
    publish("repeat") if @link.up? && Machine.board_millis >= @next_repeat
  end

  def stats_line
    "steps #{@detector.count}, net #{@link.state}, sensor #{@sensor}"
  end

  def check_key
    return unless @key1
    v = @key1.read
    reset_steps("KEY1") if v == 0 && @key1_was != 0
    @key1_was = v
  end

  # --- Screen ---
  # Text is the 6x8 font: the status lines at 2x, the count at 4x (the
  # largest size). The details (node, router, why it is offline) are in the
  # log and in pedometer.state.

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

  # Draws only what changed since the last call.
  def draw_screen
    st = @link.state
    ast = st == :connected ? "ok" : (st == :connecting || st == :reconnecting ? ".." : "--")
    shown = [@detector.count, wifi_ok?, ast, bottom_line(@detector.cadence(Machine.board_millis))]
    d = @drawn
    return if d && d[0] == shown[0] && d[1] == shown[1] && d[2] == shown[2] && d[3] == shown[3]
    @drawn = shown
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    clear_user_area if d.nil?
    @gfx.set_text_size(2)
    @gfx.fill_rect(x0, y0, w, 18, theme_bg)
    @gfx.draw_text(x0 + 2, y0 + 1, shown[1] ? "WiFi ok" : "WiFi --", shown[1] ? theme_fg : theme_accent)
    at = "AST #{ast}"
    @gfx.draw_text(x0 + w - 2 - 12 * at.length, y0 + 1, at, ast == "ok" ? theme_fg : theme_accent)
    bl = shown[3]
    @gfx.fill_rect(x0, y0 + h - 18, w, 18, theme_bg)
    @gfx.draw_text(x0 + (w - 12 * bl.length) / 2, y0 + h - 17, bl, @sensor == "ok" ? theme_fg : theme_accent) if bl.length > 0
    count = shown[0].to_s
    @gfx.fill_rect(x0, y0 + 21, w, h - 42, theme_bg)
    @gfx.set_text_size(4)
    @gfx.draw_text(x0 + (w - 24 * count.length) / 2, y0 + (h - 32) / 2, count, theme_fg)
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
