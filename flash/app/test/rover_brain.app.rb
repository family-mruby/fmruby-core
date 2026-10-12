# Rover brain (doc/ruby_asterism, S4): the board that drives the MuJoCo
# rover of the parent repository (docker-compose.mujoco.yml, ROS 2) and
# exposes it to other machines as one Asterism object, <ID>/brain/rover.
#
# - Connects like rover_cam: the first line of /home/zenoh_echo.txt is the
#   locator, else the machine's zenoh_router setting (system_conf.toml),
#   else tcp/zenohd:7447. The ID comes from /home/asterism_node.txt, the mDNS
#   name, the asterism_node setting (the browser's wasm-XXXXXX) or the board
#   name ("linux" in the simulator, "wasm" in the browser).
# - One connection (G1b): Asterism (the object) and, on Asterism.session,
#   the ROS 2 node fmruby_brain_<ID> (publishes /cmd_vel, subscribes /odom)
#   and the call log put on asterism/<ID>/brain/log for asterism-console's
#   log page. The node has the board's Zenoh ID, so the console draws it
#   inside the board. The brain is the only one that sends /cmd_vel.
# - The object (exposed with caller: :from: Asterism gives every method the
#   caller's node ID as from:, taken from the call, shown on the screen and
#   in the log; a caller cannot pass it itself):
#     rover.drive(v, w)  speeds (m/s, rad/s). Stops 0.5 s after the last call.
#     rover.go(m)        a job: m metres along the heading it starts with
#                        (negative backwards). Returns the job's number at once.
#     rover.turn(deg)    a job: deg degrees (positive to the left). Same.
#     rover.stop         stops the job and the speeds. true.
#     rover.state        a Hash: pose, speeds, the command, the job and its
#                        progress, the last caller.
#   The newest call wins: a job or drive cancels the job that runs.
# - The control loop runs once per update, after the new /odom is in. go and
#   turn measure from /odom (distance along the start heading, the sum of
#   the heading changes) and ask for the speed sqrt(2 * a * remaining),
#   capped at a cruise speed (0.3 m/s, 1.0 rad/s) and not below a creeping
#   one, with a gentle a (0.2 m/s^2, 0.8 rad/s^2) so that the rover is slow
#   when it gets there. The remaining is reduced by what the rover covers
#   before a command sent now takes effect: the odometry's delay (30 ms),
#   the age of the newest /odom, one loop period (measured) and the wheels
#   following (50 ms). The job is done, and one zero sent, when that is
#   under 3 mm or 0.23 deg. The heading is held during go. The command
#   changes by at most the controller's acceleration (0.5 m/s^2, 2 rad/s^2,
#   as S1 and S2) per step, and speeds are capped at 1.0 m/s and 3.0 rad/s
#   (the controller's limits).
#   A job whose /odom stops for 0.5 s is aborted.
# - /cmd_vel goes out every step while something runs; when it ends, one
#   zero, then nothing (another driver may take over).

# The light nav_msgs/msg/Odometry of rover_cam (S3): [x, y, qx, qy, qz, qw,
# linear.x, angular.z, stamp (s)].
class RoverBrainOdom
  TYPE_NAME = "nav_msgs::msg::dds_::Odometry_"
  TYPE_HASH = "RIHS01_3cc97dc7fb7502f8714462c526d369e35b603cfc34d946e3f2eda2766dfec6e0"
  COVARIANCE_BYTES = 36 * 8

  def self.decode(bytes)
    r = ::Asterism::CDR::Reader.new(bytes)
    sec = r.int32
    nsec = r.uint32
    r.string
    r.string
    x = r.float64
    y = r.float64
    r.float64
    qx = r.float64
    qy = r.float64
    qz = r.float64
    qw = r.float64
    at = r.pos + COVARIANCE_BYTES
    v = bytes.byteslice(at, 8).unpack(r.little_endian? ? "E" : "G")[0]
    w = bytes.byteslice(at + 40, 8).unpack(r.little_endian? ? "E" : "G")[0]
    [x, y, qx, qy, qz, qw, v, w, sec + nsec * 1.0e-9]
  end
end

class RoverBrainApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  CONF_FILE = "/etc/system_conf.toml"
  APP = "brain"
  # Limits (the rover's controller: docker/mujoco/config/controllers.yaml)
  V_MAX = 1.0
  W_MAX = 3.0
  ACCEL_V = 0.5
  ACCEL_W = 2.0
  # Jobs
  GO_V = 0.3          # cruise, m/s
  GO_V_MIN = 0.02     # creeping, m/s
  GO_BRAKE = 0.2      # planned deceleration, m/s^2 (well under the controller's 0.5)
  GO_TOL = 0.003      # m
  HEAD_K = 2.0        # heading hold during go, rad/s per rad
  HEAD_W_MAX = 0.3
  TURN_W = 1.0        # cruise, rad/s
  TURN_W_MIN = 0.08   # creeping, rad/s
  TURN_BRAKE = 0.8    # rad/s^2 (well under the controller's 2.0)
  TURN_TOL = 0.004    # rad (0.23 deg)
  ODOM_LAG = 0.03     # s: from the rover's state to /odom here (about)
  ACT_LAG = 0.05      # s: from /cmd_vel here to the wheels following it
  DRIVE_TIMEOUT_MS = 500
  ODOM_TIMEOUT_MS = 500
  DRAW_EVERY_MS = 200
  REPORT_EVERY_MS = 10_000
  DEG = 180.0 / Math::PI

  # What other machines call: <ID>/brain/rover.
  class Rover
    def initialize(app)
      @app = app
    end

    def drive(v, w, from:)
      @app.call_drive(v.to_f, w.to_f, from)
    end

    def go(m, from:)
      @app.call_job("go", m.to_f, from)
    end

    def turn(deg, from:)
      @app.call_job("turn", deg.to_f, from)
    end

    def stop(from:)
      @app.call_stop(from)
    end

    def state(from:)
      @app.call_state(from)
    end
  end

  def first_line(path)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n")[0].to_s.strip
  rescue
    ""
  end

  # The quoted value of `<key> = "..."` in system_conf.toml, or "".
  def conf_value(key)
    text = File.open(CONF_FILE, "r") { |f| f.read }
    text.split("\n").each do |line|
      next unless line.start_with?(key)
      a = line.index("\"")
      b = a ? line.index("\"", a + 1) : nil
      return b ? line[a + 1, b - a - 1].to_s.strip : ""
    end
    ""
  rescue
    ""
  end

  def ros_name(id)
    out = ""
    i = 0
    while i < id.bytesize
      c = id.byteslice(i, 1)
      out << (c == "-" ? "_" : c)
      i += 1
    end
    out
  end

  def on_create
    @started = Machine.board_millis
    @id = first_line(NODE_FILE)
    if @id.empty?
      info = FmrbApp.wifi_info
      @id = info ? info[:hostname].to_s : ""
    end
    @id = conf_value("asterism_node") if @id.empty?
    @id = FmrbConst::BOARD if @id.empty?
    @locator = first_line(LOCATOR_FILE)
    @locator = conf_value("zenoh_router") if @locator.empty?
    @locator = DEFAULT_LOCATOR if @locator.empty?
    @state = "connecting"
    @rs = nil         # the ROS 2 session
    @node = nil
    @cmd = nil
    # odometry
    @pose = nil       # [x, y, yaw (rad), v, w]
    @odom_at = 0      # board ms of the newest /odom
    @odom_n = 0
    @odom_new = false
    # what runs: :idle, :drive, :go, :turn
    @mode = :idle
    @dv = 0.0
    @dw = 0.0
    @drive_at = 0
    @out_v = 0.0      # the command sent last
    @out_w = 0.0
    @out_at = 0
    @sent = 0
    # the job
    @job_id = 0
    @job_kind = "-"
    @job_target = 0.0 # m or deg, as asked
    @job_done = 0.0   # m or deg, so far
    @job_status = "-"
    @job_t0 = 0
    @job_ms = 0
    @start = nil      # [x, y, yaw] when the job started
    @goal = 0.0       # m or rad
    @turned = 0.0     # rad
    @last_yaw = 0.0
    # calls
    @calls = 0
    @last_caller = "-"
    @last_call = "-"
    @last_call_at = 0
    @log_n = 0
    @drive_logged = nil
    @loop_ms = 0
    @loop_max = 0
    @tick_at = nil
    @period = 0.05    # s, the loop's period (smoothed)
    @lag = 0.0        # s, the delay coast used last
    @stop_note = ""
    @drawn_at = 0
    @report_at = Machine.board_millis
    @iram = nil
    draw_screen
  end

  def uptime_ms
    Machine.board_millis - @started
  end

  def connect
    t0 = Machine.board_millis
    begin
      ::Asterism.connect(@locator, node: @id, app: APP)
      ::Asterism.expose("rover", RoverBrainApp::Rover.new(self),
                        methods: { drive: 2, go: 1, turn: 1, stop: 0, state: 0 }, caller: :from)
      @rs = ::Asterism.session # Asterism's: Asterism.close closes it
      @node = Asterism::ROS::Node.new(@rs, "fmruby_brain_#{ros_name(@id)}")
      @cmd = @node.publisher("/cmd_vel", "geometry_msgs/msg/Twist")
      odom_type = ::Asterism::CDR::PACK ? RoverBrainOdom : "nav_msgs/msg/Odometry"
      @node.subscribe("/odom", odom_type, depth: 2) { |msg, info| odom(msg) }
      @log_key = "asterism/#{@id}/#{APP}/log"
      @state = "connected"
    rescue ::Asterism::Error => e
      @state = "failed: #{e.message}"
    end
    Log.info("rover_brain: #{@id} #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
    log("info", "#{@id}/#{APP}/rover is up") if @rs
  end

  # One line for asterism-console's log page (and the board's log).
  def log(level, text)
    Log.info("rover_brain: #{text}")
    return unless @rs
    @rs.put(@log_key, ::Asterism::Codec.pack({ "level" => level, "msg" => text,
                                               "time" => Asterism::ROS.now_ns / 1.0e9 }))
    @log_n += 1
  rescue => e
    Log.info("rover_brain: log: #{e.message}")
  end

  def wrap(a)
    a -= 2.0 * Math::PI while a > Math::PI
    a += 2.0 * Math::PI while a < -Math::PI
    a
  end

  def clamp(x, lim)
    return lim if x > lim
    return -lim if x < -lim
    x
  end

  def odom(msg)
    m = msg.is_a?(Array) ? msg : odom_array(msg)
    yaw = Math.atan2(2.0 * (m[5] * m[4] + m[2] * m[3]), 1.0 - 2.0 * (m[3] * m[3] + m[4] * m[4]))
    @pose = [m[0], m[1], yaw, m[6], m[7]]
    if @mode == :turn
      @turned += wrap(yaw - @last_yaw)
      @last_yaw = yaw
    end
    @odom_at = Machine.board_millis
    @odom_n += 1
    @odom_new = true
  end

  def odom_array(msg)
    p = msg.pose.pose.position
    q = msg.pose.pose.orientation
    [p.x, p.y, q.x, q.y, q.z, q.w, msg.twist.twist.linear.x, msg.twist.twist.angular.z, 0.0]
  end

  # ------------------------------------------------------------ the calls

  def called(from, what)
    @calls += 1
    @last_caller = from ? from.to_s : "?"
    @last_call = what
    @last_call_at = Machine.board_millis
  end

  # The job that runs (if any) ends as `status`.
  def end_job(status)
    return unless @mode == :go || @mode == :turn
    @job_status = status
    @job_ms = Machine.board_millis - @job_t0
    log("info", "job #{@job_id} #{@job_kind} #{@job_target} #{status} at #{@job_done.round(3)} " +
                "(#{@job_ms} ms#{status == 'done' ? ", #{@stop_note}" : ''})")
  end

  def call_drive(v, w, from)
    v = clamp(v, V_MAX)
    w = clamp(w, W_MAX)
    called(from, "drive(#{v.round(2)}, #{w.round(2)})")
    end_job("canceled by drive")
    @mode = :drive
    @dv = v
    @dw = w
    @drive_at = Machine.board_millis
    # The log gets the first drive and each change, not every repeat.
    key = "#{v.round(2)} #{w.round(2)} #{from}"
    if key != @drive_logged
      @drive_logged = key
      log("info", "drive(#{v.round(2)}, #{w.round(2)}) from #{@last_caller}")
    end
    [v, w]
  end

  def call_job(kind, target, from)
    called(from, "#{kind}(#{target})")
    end_job("canceled by #{kind}")
    @drive_logged = nil
    @job_id += 1
    @job_kind = kind
    @job_target = target
    @job_done = 0.0
    @job_status = "running"
    @job_t0 = Machine.board_millis
    @job_ms = 0
    if @pose.nil?
      @job_status = "aborted: no odom"
      @mode = :idle
    else
      @start = [@pose[0], @pose[1], @pose[2]]
      if kind == "go"
        @goal = target
        @mode = :go
      else
        @goal = target / DEG
        @turned = 0.0
        @last_yaw = @pose[2]
        @mode = :turn
      end
    end
    log("info", "#{kind}(#{target}) from #{@last_caller} -> job #{@job_id} #{@job_status}")
    @job_id
  end

  def call_stop(from)
    called(from, "stop")
    end_job("stopped")
    @drive_logged = nil
    log("info", "stop from #{@last_caller}") unless @mode == :idle
    halt
    true
  end

  def r(x, d)
    x.round(d)
  end

  def call_state(_from)
    p = @pose
    {
      "node" => @id,
      "x" => p ? r(p[0], 4) : nil, "y" => p ? r(p[1], 4) : nil,
      "heading" => p ? r(p[2] * DEG, 2) : nil,
      "v" => p ? r(p[3], 3) : nil, "w" => p ? r(p[4], 3) : nil,
      "cmd" => [r(@out_v, 3), r(@out_w, 3)],
      "mode" => @mode.to_s,
      "job" => { "id" => @job_id, "kind" => @job_kind, "target" => @job_target,
                 "done" => r(@job_done, 4), "progress" => progress, "status" => @job_status,
                 "finished" => @job_status != "running", "ms" => @job_ms },
      "last_caller" => @last_caller, "last_call" => @last_call, "calls" => @calls,
      "odom_age_ms" => @odom_at > 0 ? Machine.board_millis - @odom_at : nil,
      "period_ms" => (@period * 1000).round, "iram_free" => @iram, "up_ms" => uptime_ms
    }
  end

  def progress
    return 0.0 if @job_target == 0.0
    x = @job_done / @job_target
    x = 1.0 if x > 1.0
    r(x, 3)
  end

  # ------------------------------------------------------------ the loop

  # Speeds to zero now: one zero goes out, then nothing.
  def halt
    send_cmd(0.0, 0.0) if @mode != :idle || @out_v != 0.0 || @out_w != 0.0
    @mode = :idle
    @dv = 0.0
    @dw = 0.0
    @out_v = 0.0
    @out_w = 0.0
  end

  def send_cmd(v, w)
    return unless @cmd
    @cmd << { linear: { x: v }, angular: { z: w } }
    @sent += 1
  end

  def ramp(cur, want, step)
    return want if (want - cur).abs <= step
    want > cur ? cur + step : cur - step
  end

  # The speed for `rem` still to cover: brake to a stop at it, at most
  # cruise, at least creep (sign of rem).
  def profile(rem, cruise, creep, brake)
    s = Math.sqrt(2.0 * brake * rem.abs)
    s = cruise if s > cruise
    s = creep if s < creep
    rem < 0 ? -s : s
  end

  # How far the rover still goes at speed s before a command sent now takes
  # effect: the odometry's delay, one loop period and the actuation.
  def coast(s)
    @lag = ODOM_LAG + (Machine.board_millis - @odom_at) / 1000.0 + @period + ACT_LAG
    s * @lag
  end

  def control
    now = Machine.board_millis
    return if @mode == :idle
    if @mode == :drive
      if now - @drive_at > DRIVE_TIMEOUT_MS
        log("info", "drive: no call for #{DRIVE_TIMEOUT_MS} ms, stopped")
        @drive_logged = nil
        halt
        return
      end
      want_v = @dv
      want_w = @dw
    else
      if now - @odom_at > ODOM_TIMEOUT_MS
        end_job("aborted: no odom")
        halt
        return
      end
      p = @pose
      if @mode == :go
        c = Math.cos(@start[2])
        s = Math.sin(@start[2])
        d = (p[0] - @start[0]) * c + (p[1] - @start[1]) * s
        @job_done = d
        rem = @goal - d - coast(p[3])
        if (@goal >= 0 && rem <= GO_TOL) || (@goal < 0 && rem >= -GO_TOL)
          @stop_note = "v #{p[3].round(3)} lag #{(@lag * 1000).round} ms"
          end_job("done")
          halt
          return
        end
        want_v = profile(rem, GO_V, GO_V_MIN, GO_BRAKE)
        want_w = clamp(HEAD_K * wrap(@start[2] - p[2]), HEAD_W_MAX)
      else
        @job_done = @turned * DEG
        rem = @goal - @turned - coast(p[4])
        if (@goal >= 0 && rem <= TURN_TOL) || (@goal < 0 && rem >= -TURN_TOL)
          @stop_note = "w #{p[4].round(3)} lag #{(@lag * 1000).round} ms"
          end_job("done")
          halt
          return
        end
        want_v = 0.0
        want_w = profile(rem, TURN_W, TURN_W_MIN, TURN_BRAKE)
      end
    end
    dt = @out_at > 0 ? (now - @out_at) / 1000.0 : 0.05
    dt = 0.2 if dt > 0.2
    @out_v = ramp(@out_v, want_v, ACCEL_V * dt)
    @out_w = ramp(@out_w, want_w, ACCEL_W * dt)
    @out_at = now
    send_cmd(@out_v, @out_w)
  end

  def exchange
    unless ::Asterism.poll
      lost("asterism: #{::Asterism.lost_reason}")
      return
    end
    lost("ros: disconnected") unless @node.poll
  rescue => e
    lost("error: #{e.class}: #{e.message}")
  end

  def lost(state)
    @state = state
    @mode = :idle
    @node = nil
    @cmd = nil
    @rs = nil # Asterism.close closes it, with the node
    ::Asterism.close
    Log.info("rover_brain: #{state}")
  rescue => e
    Log.info("rover_brain: lost: #{e.message}")
  end

  def report
    now = Machine.board_millis
    heap = FmrbApp.heap_info
    @iram = heap ? heap[:iram_free] : nil
    return if now - @report_at < REPORT_EVERY_MS
    @report_at = now
    Log.info("rover_brain: iram_free #{@iram} odom #{@odom_n} cmd #{@sent} calls #{@calls} " +
             "loop #{@loop_ms} (max #{@loop_max}) ms period #{(@period * 1000).round} ms")
  end

  # ------------------------------------------------------------ the screen

  def fmt(x, digits)
    s = x.round(digits).to_s
    s = "+#{s}" unless x < 0
    s
  end

  def draw_lines
    x = @user_area_x0 + 4
    y = @user_area_y0 + 3
    @gfx.fill_rect(x, y, @user_area_width - 8, 13 * 10, theme_bg)
    @gfx.draw_text(x, y, "#{@id}/brain/rover #{@state}"[0, 52], theme_fg)
    p = @pose
    if p
      @gfx.draw_text(x, y + 13, "x #{fmt(p[0], 3)} y #{fmt(p[1], 3)} m  heading #{fmt(p[2] * DEG, 1)}", theme_fg)
      @gfx.draw_text(x, y + 26, "v #{fmt(p[3], 2)} m/s  w #{fmt(p[4], 2)} rad/s", theme_fg)
    else
      @gfx.draw_text(x, y + 13, "odom: nothing yet", theme_fg)
    end
    @gfx.draw_text(x, y + 39, "#{@mode}  cmd v #{fmt(@out_v, 2)} w #{fmt(@out_w, 2)}", theme_accent)
    if @job_id > 0
      unit = @job_kind == "go" ? "m" : "deg"
      @gfx.draw_text(x, y + 52, "job #{@job_id} #{@job_kind} #{@job_target} #{unit}: #{@job_status}"[0, 52], theme_fg)
      bw = @user_area_width - 70
      @gfx.draw_rect(x, y + 66, bw, 8, theme_border)
      fill = (bw - 2) * progress
      @gfx.fill_rect(x + 1, y + 67, fill.to_i, 6, theme_accent) if fill >= 1
      @gfx.draw_text(x + bw + 4, y + 65, "#{fmt(@job_done, 2)}", theme_fg)
    else
      @gfx.draw_text(x, y + 52, "job: none yet", theme_fg)
    end
    ago = @last_call_at > 0 ? "#{(Machine.board_millis - @last_call_at) / 1000}s ago" : ""
    @gfx.draw_text(x, y + 80, "from #{@last_caller}: #{@last_call} #{ago}"[0, 52], theme_fg)
    @gfx.draw_text(x, y + 93, "calls #{@calls}  cmd sent #{@sent}  odom #{@odom_n}", theme_border)
    @gfx.draw_text(x, y + 106, "loop #{@loop_ms}/#{@loop_max} ms  period #{(@period * 1000).round} ms  iram #{@iram}", theme_border)
  end

  def draw_screen
    clear_user_area
    draw_lines
    draw_window_frame
    @gfx.present
  end

  def on_update
    t = Machine.board_millis
    if @tick_at
      d = (t - @tick_at) / 1000.0
      d = 0.3 if d > 0.3
      @period += (d - @period) * 0.1
    end
    @tick_at = t
    connect if @state == "connecting"
    if @rs
      t0 = Machine.board_millis
      exchange
      control if @rs
      dt = Machine.board_millis - t0
      @loop_ms = dt
      @loop_max = dt if dt > @loop_max
    end
    report
    now = Machine.board_millis
    if now - @drawn_at >= DRAW_EVERY_MS
      @drawn_at = now
      draw_lines
      @gfx.present
    end
    20
  end

  def on_destroy
    halt if @cmd
    log("info", "#{@id}/#{APP}/rover is going away") if @rs
    @node.close if @node
    ::Asterism.close
    Log.info("rover_brain: closed")
  rescue => e
    Log.info("rover_brain: close: #{e.message}")
  end
end

begin
  app = RoverBrainApp.new
  app.start
rescue => e
  puts "rover_brain: #{e.message}"
end
