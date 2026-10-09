# ROS 2 drive (doc/ruby_asterism, S1): drives a ROS 2 robot with the arrow
# keys and shows its wheel odometry. Made for the MuJoCo rover of the parent
# repository (docker-compose.mujoco.yml); any robot with a Twist /cmd_vel
# and an Odometry /odom works.
#
# - Connects like ros2_talker: the first line of /home/zenoh_echo.txt is the
#   locator (default tcp/zenohd:7447), the ID comes from
#   /home/asterism_node.txt, the mDNS name or "linux". The ROS node is
#   fmruby_drive_<ID>.
# - Keys: Up / Down speed up / slow down (0.05 m/s a press, +-0.5 m/s),
#   Left / Right turn (0.3 rad/s a press, +-1.5 rad/s), Space or S stops.
#   A press changes the command; it is not held, so injected key presses
#   (fmrb_input, tab5_input) drive it too.
# - The command goes out on /cmd_vel 10 times a second, also when it is
#   zero. The rover's controller stops it after 0.5 s without one, so the
#   robot stops when this app closes or loses the network.
# - Shows /odom: position, heading and speed (twist), how many were shown
#   (only the newest is decoded), and the update loop's time (poll + decode).
#
# From the PC (parent repo):
#   docker exec -it fmruby_mujoco ros2 topic echo /cmd_vel
#   docker exec -it fmruby_mujoco ros2 topic echo /odom --field pose.pose

# A lighter nav_msgs/msg/Odometry for this app: reads the pose and the
# speeds only and skips the two covariance matrices (72 float64), which the
# generated type decodes one by one. Subscription needs just TYPE_NAME,
# TYPE_HASH (the Jazzy type's, as in /usr/share/asterism/msgs) and decode.
# Gives [x, y, qx, qy, qz, qw, linear.x, angular.z].
class Ros2DriveOdom
  TYPE_NAME = "nav_msgs::msg::dds_::Odometry_"
  TYPE_HASH = "RIHS01_3cc97dc7fb7502f8714462c526d369e35b603cfc34d946e3f2eda2766dfec6e0"
  COVARIANCE_BYTES = 36 * 8

  def self.decode(bytes)
    r = ::Asterism::CDR::Reader.new(bytes)
    r.int32           # header.stamp.sec
    r.uint32          # header.stamp.nanosec
    r.string          # header.frame_id
    r.string          # child_frame_id
    x = r.float64     # pose.pose.position
    y = r.float64
    r.float64
    qx = r.float64    # pose.pose.orientation
    qy = r.float64
    qz = r.float64
    qw = r.float64
    # pose.covariance, then twist.twist.linear (x at +0) and .angular (z at +40)
    at = r.pos + COVARIANCE_BYTES
    v = bytes.byteslice(at, 8).unpack(r.little_endian? ? "E" : "G")[0]
    w = bytes.byteslice(at + 40, 8).unpack(r.little_endian? ? "E" : "G")[0]
    [x, y, qx, qy, qz, qw, v, w]
  end
end

class Ros2DriveApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  SEND_EVERY_MS = 100
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

  # "fmruby-bbbbbb" -> "fmruby_bbbbbb"
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
    @id = first_line(NODE_FILE)
    if @id.empty?
      info = FmrbApp.wifi_info
      @id = info ? info[:hostname].to_s : ""
    end
    @id = "linux" if @id.empty?
    @locator = first_line(LOCATOR_FILE)
    @locator = DEFAULT_LOCATOR if @locator.empty?
    @state = "connecting"
    @session = nil
    @node = nil
    @v = 0.0
    @w = 0.0
    @sent = 0
    @odom = nil
    @odom_n = 0
    @loop_ms = 0
    @loop_max = 0
    @dirty = true
    draw_screen
  end

  def connect
    t0 = Machine.board_millis
    begin
      @session = Asterism::Zenoh::Session.open(@locator)
      @node = Asterism::ROS::Node.new(@session, "fmruby_drive_#{ros_name(@id)}")
      @cmd = @node.publisher("/cmd_vel", "geometry_msgs/msg/Twist")
      # depth 2: only the newest odometry is decoded (the ring drops the
      # oldest), with the light type above when String#unpack is there.
      # Decoding all 20 a second with the full type took most of the update
      # loop on the P4 and delayed the commands.
      type = ::Asterism::CDR::PACK ? Ros2DriveOdom : "nav_msgs/msg/Odometry"
      @node.subscribe("/odom", type, depth: 2) { |msg, _info| odom(msg) }
      @node.every(ms: SEND_EVERY_MS) { send_cmd }
      @state = "connected"
    rescue => e
      @session = nil
      @node = nil
      @state = "failed: #{e.message}"
    end
    Log.info("ros2_drive: #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
    @dirty = true
  end

  # node.every
  def send_cmd
    @cmd << { linear: { x: @v }, angular: { z: @w } }
    @sent += 1
  end

  # node.subscribe("/odom"): keep the planar pose and the speed.
  def odom(msg)
    m = msg.is_a?(Array) ? msg : odom_array(msg)
    yaw = Math.atan2(2.0 * (m[5] * m[4] + m[2] * m[3]), 1.0 - 2.0 * (m[3] * m[3] + m[4] * m[4]))
    @odom = [m[0], m[1], yaw * 180.0 / Math::PI, m[6], m[7]]
    @odom_n += 1
    @dirty = true
  end

  # The generated Odometry, in Ros2DriveOdom's shape.
  def odom_array(msg)
    p = msg.pose.pose.position
    q = msg.pose.pose.orientation
    [p.x, p.y, q.x, q.y, q.z, q.w, msg.twist.twist.linear.x, msg.twist.twist.angular.z]
  end

  def clamp(x, lim)
    return lim if x > lim
    return -lim if x < -lim
    x
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    sc = ev[:scancode]
    ch = ev[:character] || 0
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
    else
      return
    end
    # Rounding: 0.05 steps add up to 0.1500000002 otherwise.
    @v = (@v * 100.0).round / 100.0
    @w = (@w * 10.0).round / 10.0
    @dirty = true
  end

  def exchange
    lost("disconnected") unless @node.poll
  rescue => e
    lost("error: #{e.class}: #{e.message}")
  end

  def lost(state)
    @state = state
    @session = nil
    @node = nil
    Log.info("ros2_drive: #{state}")
    @dirty = true
  end

  def fmt(x, digits)
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
    o = @odom
    if o
      @gfx.draw_text(x, y + 28, "odom x #{fmt(o[0], 3)} m  y #{fmt(o[1], 3)} m", theme_fg)
      @gfx.draw_text(x, y + 41, "     heading #{fmt(o[2], 1)} deg", theme_fg)
      @gfx.draw_text(x, y + 54, "     speed #{fmt(o[3], 2)} m/s  turn #{fmt(o[4], 2)} rad/s", theme_fg)
    else
      @gfx.draw_text(x, y + 28, "odom: nothing yet", theme_fg)
    end
    @gfx.draw_text(x, y + 69, "/odom shown #{@odom_n}  /cmd_vel sent #{@sent}  poll #{@loop_ms}/#{@loop_max} ms", theme_border)
    @gfx.draw_text(x, y + 82, "arrows: drive  space: stop", theme_border)
    draw_window_frame
    @gfx.present
  end

  def on_update
    connect if @state == "connecting"
    if @session
      t0 = Machine.board_millis
      exchange
      dt = Machine.board_millis - t0
      @loop_ms = dt
      @loop_max = dt if dt > @loop_max
    end
    if @dirty
      @dirty = false
      draw_screen
    end
    50
  end

  def on_destroy
    if @cmd
      @cmd << { linear: { x: 0.0 }, angular: { z: 0.0 } }
    end
    @node.close if @node
    @session.close if @session
    Log.info("ros2_drive: closed")
  rescue => e
    Log.info("ros2_drive: close: #{e.message}")
  end
end

begin
  app = Ros2DriveApp.new
  app.start
rescue => e
  puts "ros2_drive: #{e.message}"
end
