# Asterism rover (doc/ruby_asterism, S2): drives the MuJoCo rover that a
# CRuby process runs and exposes as Asterism objects (asterism
# examples/mujoco/rover.rb: node "mujoco", app "rover"), with the arrow keys,
# and shows its pose. No ROS 2: plain Asterism calls (the portable API).
#
# - Connects like asterism_demo: the first line of /home/zenoh_echo.txt is
#   the locator (default tcp/zenohd:7447), the ID comes from
#   /home/asterism_node.txt, the mDNS name or "linux". This app is
#   <ID>/rover_drive; it calls mujoco/rover/drive, state and world.
# - Keys: Up / Down speed up / slow down (0.05 m/s a press, +-0.5 m/s),
#   Left / Right turn (0.3 rad/s a press, +-1.5 rad/s), Space or S stops,
#   R puts the world back to the start. A press changes the command (it is
#   not held), so injected key presses drive it too.
# - While the command is not zero, drive.cmd(v, w, by: <ID>) goes out every
#   200 ms (the rover stops 0.5 s after the last one, so it stops when this
#   app closes or loses the network). A stop is sent once: an idle driver
#   sends nothing, and another one (CRuby, the console) can take over. The
#   rover follows the last command, whoever sent it.
# - Every 250 ms asks state.all: shows the true pose, the wheel odometry,
#   the speed, who drives, and the time the answer took.
# - Calls are async (Future): the update loop never waits on the network.

class AsterismRoverApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  APP = "rover_drive"
  ROVER = "mujoco/rover"
  SEND_EVERY_MS = 200
  STATE_EVERY_MS = 250
  CALL_TIMEOUT = 1.0
  V_STEP = 0.05
  V_MAX = 0.5
  W_STEP = 0.3
  W_MAX = 1.5

  def first_line(path)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n")[0].to_s.strip
  rescue
    ""
  end

  def on_create
    @id = first_line(NODE_FILE)
    if @id.empty?
      info = FmrbApp.wifi_info
      @id = info ? info[:hostname].to_s : ""
    end
    @id = "linux" if @id.empty?
    @locator = first_line(LOCATOR_FILE)
    @locator = DEFAULT_LOCATOR if @locator.empty?
    @state = "connecting"
    @v = 0.0
    @w = 0.0
    @keys = []
    @stop_pending = false
    @reset_pending = false
    @next_send = 0
    @next_state = 0
    @cmd_f = nil
    @state_f = nil
    @sent = 0
    @errors = 0
    @last_error = ""
    @rover = nil
    @took = 0
    @took_max = 0
    @answers = 0
    @dirty = true
    draw_screen
  end

  def connect
    t0 = Machine.board_millis
    begin
      ::Asterism.connect(@locator, node: @id, app: APP)
      @drive = ::Asterism["#{ROVER}/drive", timeout: CALL_TIMEOUT]
      @status = ::Asterism["#{ROVER}/state", timeout: CALL_TIMEOUT]
      @world = ::Asterism["#{ROVER}/world", timeout: CALL_TIMEOUT]
      @state = "connected"
    rescue ::Asterism::Error => e
      @state = "failed: #{e.message}"
    end
    Log.info("asterism_rover: #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
    @dirty = true
  end

  def failed(e)
    @errors += 1
    @last_error = "#{e.class}: #{e.message}"
    Log.info("asterism_rover: #{@last_error}")
    @dirty = true
  end

  # Keys are noted here and acted on in on_update (one interpreter entry
  # less on the C stack; see asterism_demo).
  def on_event(ev)
    return unless ev[:type] == :key_down
    @keys << [ev[:scancode], ev[:character] || 0]
  end

  def clamp(x, lim)
    return lim if x > lim
    return -lim if x < -lim
    x
  end

  def run_key(key)
    sc = key[0]
    ch = key[1]
    if sc == FmrbConst::KEY_UP
      @v = clamp(@v + V_STEP, V_MAX)
    elsif sc == FmrbConst::KEY_DOWN
      @v = clamp(@v - V_STEP, V_MAX)
    elsif sc == FmrbConst::KEY_LEFT
      @w = clamp(@w + W_STEP, W_MAX)
    elsif sc == FmrbConst::KEY_RIGHT
      @w = clamp(@w - W_STEP, W_MAX)
    elsif ch == 32 || ch == 115 || ch == 83
      @v = 0.0
      @w = 0.0
    elsif ch == 114 || ch == 82
      @reset_pending = true
      return
    else
      return
    end
    # Rounding: 0.05 steps add up to 0.1500000002 otherwise.
    @v = (@v * 100.0).round / 100.0
    @w = (@w * 10.0).round / 10.0
    @stop_pending = @v == 0.0 && @w == 0.0
    @next_send = 0
    @dirty = true
  end

  # drive.cmd while moving, one drive.stop when it comes to zero.
  def send_cmd(now)
    if @cmd_f && @cmd_f.done?
      begin
        @cmd_f.value
      rescue ::Asterism::Error => e
        failed(e)
      end
      @cmd_f = nil
    end
    if @reset_pending
      @reset_pending = false
      @world.async.reset
    end
    moving = @v != 0.0 || @w != 0.0
    return unless moving || @stop_pending
    return if now < @next_send
    @next_send = now + SEND_EVERY_MS
    if moving
      @cmd_f = @drive.async.cmd(@v, @w, by: @id)
    else
      @cmd_f = @drive.async.stop(by: @id)
      @stop_pending = false
    end
    @sent += 1
  rescue ::Asterism::Error => e
    failed(e)
  end

  def ask_state(now)
    if @state_f && @state_f.done?
      begin
        @rover = @state_f.value
        @took = @state_f.took_ms
        @took_max = @took if @took > @took_max
        @answers += 1
      rescue ::Asterism::Error => e
        failed(e)
      end
      @state_f = nil
      @dirty = true
    end
    return unless @state_f.nil? && now >= @next_state
    @next_state = now + STATE_EVERY_MS
    @state_f = @status.async.all
  rescue ::Asterism::Error => e
    failed(e)
    @state_f = nil
  end

  def fmt(x, digits)
    return "-" if x.nil?
    s = x.round(digits).to_s
    s = "+#{s}" unless x < 0
    s
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @gfx.draw_text(x, y, "#{@id} #{@state}"[0, 52], theme_fg)
    @gfx.draw_text(x, y + 13, "cmd  v #{fmt(@v, 2)} m/s  w #{fmt(@w, 1)} rad/s", theme_accent)
    r = @rover
    if r
      p = r["pose"]
      o = r["odom"]
      s = r["speed"]
      @gfx.draw_text(x, y + 28, "pose x #{fmt(p["x"], 3)} y #{fmt(p["y"], 3)} #{fmt(p["yaw_deg"], 1)} deg", theme_fg)
      @gfx.draw_text(x, y + 41, "odom x #{fmt(o["x"], 3)} y #{fmt(o["y"], 3)} #{fmt(o["yaw_deg"], 1)} deg", theme_fg)
      @gfx.draw_text(x, y + 54, "speed #{fmt(s["v"], 2)} m/s #{fmt(s["w"], 2)} rad/s by #{s["driver"] || '-'}"[0, 52], theme_fg)
    else
      @gfx.draw_text(x, y + 28, "state: nothing yet", theme_fg)
    end
    @gfx.draw_text(x, y + 69, "sent #{@sent} state #{@answers} (#{@took}/#{@took_max} ms) err #{@errors}", theme_border)
    @gfx.draw_text(x, y + 82, @errors > 0 ? @last_error[0, 52] : "arrows: drive  space: stop  r: reset", theme_border)
    draw_window_frame
    @gfx.present
  end

  def on_update
    connect if @state == "connecting"
    if ::Asterism.connected?
      ::Asterism.poll
      run_key(@keys.shift) until @keys.empty?
      now = Machine.board_millis
      send_cmd(now)
      ask_state(now)
    elsif @state == "connected"
      @state = "disconnected: #{::Asterism.lost_reason}"
      Log.info("asterism_rover: #{@state}")
      @dirty = true
    end
    if @dirty
      @dirty = false
      draw_screen
    end
    50
  end

  def on_destroy
    if ::Asterism.connected? && (@v != 0.0 || @w != 0.0)
      @drive.async.stop(by: @id)
      ::Asterism.poll
    end
    ::Asterism.close
    Log.info("asterism_rover: closed")
  rescue => e
    Log.info("asterism_rover: close: #{e.message}")
  end
end

begin
  app = AsterismRoverApp.new
  app.start
rescue => e
  puts "asterism_rover: #{e.message}"
end
