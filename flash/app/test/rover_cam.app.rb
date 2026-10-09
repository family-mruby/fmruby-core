# Rover camera (doc/ruby_asterism, S3): drives the MuJoCo rover of the parent
# repository (docker-compose.mujoco.yml, ROS 2) with the arrow keys, shows its
# wheel odometry and the newest picture of its camera
# (/camera/image/compressed, 160x120 JPEG at 5 Hz).
#
# - Connects like ros2_drive: the first line of /home/zenoh_echo.txt is the
#   locator (default tcp/zenohd:7447), the ID comes from
#   /home/asterism_node.txt, the mDNS name or "linux". The ROS node is
#   fmruby_cam_<ID>.
# - Keys: Up / Down speed up / slow down (0.05 m/s a press, +-0.5 m/s),
#   Left / Right turn (0.3 rad/s a press, +-1.5 rad/s), Space or S stops.
# - One driver at a time: /cmd_vel goes out 10 times a second only while the
#   command is not zero; stopping sends one zero and then nothing, so another
#   driver (CRuby, the console) can take over without the two alternating.
# - The picture: only the newest frame is kept (the subscription's ring is
#   2 deep and older frames are dropped). On Modern hardware the JPEG is
#   written to /tmp (RAM) and drawn through create_image, which hands JPEG to
#   the SoC's decoder. The simulator's display side reads PNG only, so there
#   the picture area shows the frame's size and rate instead.
# - Shown: the camera's receive and draw rates, two delays, the time one
#   picture takes to draw, and the update loop's time (poll + decode).
#   - delay: from the image publisher sending the frame to the frame being
#     drawn. The publisher's time is the rmw_zenoh attachment's source time
#     (its wall clock); the board's clock may be off by hundreds of ms, so the
#     smallest (receive - source time) of /odom in the last 2 s is taken as
#     the clock offset. The delay is then "over the fastest /odom".
#   - age: how much older the frame's capture is than the newest /odom, both
#     in the simulator's time (header.stamp), when the frame is drawn. It
#     includes rendering and JPEG compression on the rover's side.

# A light sensor_msgs/msg/CompressedImage: the stamp and the data only (no
# Header object). Gives [sec, nanosec, data].
class RoverCamImage
  TYPE_NAME = "sensor_msgs::msg::dds_::CompressedImage_"
  TYPE_HASH = "RIHS01_15640771531571185e2efc8a100baf923961a4d15d5569652e6cb6691e8e371a"

  def self.decode(bytes)
    r = ::Asterism::CDR::Reader.new(bytes)
    sec = r.int32     # header.stamp.sec
    nsec = r.uint32   # header.stamp.nanosec
    r.string          # header.frame_id
    r.string          # format
    [sec, nsec, r.bytes]
  end
end

# The light nav_msgs/msg/Odometry of ros2_drive (S1): the pose and the
# speeds, skipping the two covariance matrices, plus the stamp. Gives
# [x, y, qx, qy, qz, qw, linear.x, angular.z, stamp (s)].
class RoverCamOdom
  TYPE_NAME = "nav_msgs::msg::dds_::Odometry_"
  TYPE_HASH = "RIHS01_3cc97dc7fb7502f8714462c526d369e35b603cfc34d946e3f2eda2766dfec6e0"
  COVARIANCE_BYTES = 36 * 8

  def self.decode(bytes)
    r = ::Asterism::CDR::Reader.new(bytes)
    sec = r.int32     # header.stamp
    nsec = r.uint32
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
    [x, y, qx, qy, qz, qw, v, w, sec + nsec * 1.0e-9]
  end
end

class RoverCamApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  FRAME_FILE = "/tmp/rover_cam.jpg"
  SEND_EVERY_MS = 100
  RATE_EVERY_MS = 2000
  V_STEP = 0.05
  V_MAX = 0.5
  W_STEP = 0.3
  W_MAX = 1.5
  IMG_W = 160
  IMG_H = 120

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
    # JPEG through create_image: Modern hardware only (see the top).
    @jpeg = FmrbConst::PLATFORM == "esp32" && FmrbConst::HW_FAMILY == "modern"
    @state = "connecting"
    @session = nil
    @node = nil
    @v = 0.0
    @w = 0.0
    @sent = 0
    @odom = nil
    @odom_n = 0
    # camera
    @frame = nil        # newest JPEG not drawn yet
    @frame_stamp = 0    # its publisher time (ns), 0 when unknown
    @frame_bytes = 0
    @frame_wh = nil
    @recv_n = 0
    @shown_n = 0
    @recv_hz = 0.0
    @shown_hz = 0.0
    @rate_t0 = Machine.board_millis
    @rate_recv0 = 0
    @rate_shown0 = 0
    @frame_sim = 0.0    # its capture time (simulator's seconds)
    @odom_sim = nil     # newest /odom stamp (simulator's seconds)
    @off_cur = nil      # smallest /odom (receive - source) this window, ns
    @off = nil          # the same for the last window: the clock offset
    @lat_ms = nil
    @lat_avg = nil
    @lat_max = 0
    @lat_wmax = 0
    @lat_sum = 0
    @lat_n = 0
    @age_ms = nil
    @draw_ms = 0
    @draw_max = 0
    @img_fail = 0
    @loop_ms = 0
    @loop_max = 0
    @tick_at = nil      # when on_update last ran: the period of the loop
    @tick_sum = 0
    @tick_n = 0
    @tick_wmax = 0
    @dirty = true
    draw_screen
  end

  def connect
    t0 = Machine.board_millis
    begin
      @session = Asterism::Zenoh::Session.open(@locator)
      @node = Asterism::ROS::Node.new(@session, "fmruby_cam_#{ros_name(@id)}")
      @cmd = @node.publisher("/cmd_vel", "geometry_msgs/msg/Twist")
      odom_type = ::Asterism::CDR::PACK ? RoverCamOdom : "nav_msgs/msg/Odometry"
      @node.subscribe("/odom", odom_type, depth: 2) { |msg, info| odom(msg, info) }
      @node.subscribe("/camera/image/compressed", RoverCamImage, depth: 2) { |msg, info| camera(msg, info) }
      @node.every(ms: SEND_EVERY_MS) { send_cmd if moving? }
      @state = "connected"
    rescue => e
      @session = nil
      @node = nil
      @state = "failed: #{e.message}"
    end
    Log.info("rover_cam: #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
    @dirty = true
  end

  def moving?
    @v != 0.0 || @w != 0.0
  end

  def send_cmd
    @cmd << { linear: { x: @v }, angular: { z: @w } }
    @sent += 1
  end

  # node.subscribe("/odom"): keep the planar pose and the speed.
  def odom(msg, info)
    m = msg.is_a?(Array) ? msg : odom_array(msg)
    @odom_sim = m[8]
    if info && info.stamp_ns > 0
      d = Asterism::ROS.now_ns - info.stamp_ns
      @off_cur = d if @off_cur.nil? || d < @off_cur
    end
    yaw = Math.atan2(2.0 * (m[5] * m[4] + m[2] * m[3]), 1.0 - 2.0 * (m[3] * m[3] + m[4] * m[4]))
    @odom = [m[0], m[1], yaw * 180.0 / Math::PI, m[6], m[7]]
    @odom_n += 1
    @dirty = true
  end

  def odom_array(msg)
    p = msg.pose.pose.position
    q = msg.pose.pose.orientation
    s = msg.header.stamp
    [p.x, p.y, q.x, q.y, q.z, q.w, msg.twist.twist.linear.x, msg.twist.twist.angular.z,
     s.sec + s.nanosec * 1.0e-9]
  end

  # node.subscribe("/camera/..."): keep the newest frame only. A frame that
  # was not drawn yet is replaced (dropped).
  def camera(msg, info)
    @frame = msg[2]
    @frame_sim = msg[0] + msg[1] * 1.0e-9
    @frame_bytes = @frame.bytesize
    @frame_stamp = info ? info.stamp_ns : 0
    @recv_n += 1
    @dirty = true
  end

  # Width and height from the JPEG's start-of-frame (SOF0..SOF2).
  def jpeg_size(b)
    i = 2
    n = b.bytesize
    while i + 9 < n
      return nil if b.getbyte(i) != 0xFF
      marker = b.getbyte(i + 1)
      len = (b.getbyte(i + 2) << 8) | b.getbyte(i + 3)
      if marker >= 0xC0 && marker <= 0xC2
        h = (b.getbyte(i + 5) << 8) | b.getbyte(i + 6)
        w = (b.getbyte(i + 7) << 8) | b.getbyte(i + 8)
        return [w, h]
      end
      i += 2 + len
    end
    nil
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
    was = moving?
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
    # The one stop: sent at once, then this app is silent.
    send_cmd if @cmd && was && !moving?
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
    Log.info("rover_cam: #{state}")
    @dirty = true
  end

  def fmt(x, digits)
    s = x.round(digits).to_s
    s = "+#{s}" unless x < 0
    s
  end

  # Draw the waiting frame into the picture area (no present here).
  def draw_frame
    b = @frame
    @frame = nil
    t0 = Machine.board_millis
    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    if @jpeg
      File.open(FRAME_FILE, "w") { |f| f.write(b) }
      img = @gfx.create_image(FRAME_FILE)
      if img
        @frame_wh = [img[:width], img[:height]]
        @gfx.draw_image(img[:id], x: x, y: y)
        @gfx.delete_image(img[:id])
      else
        @img_fail += 1
      end
    else
      @frame_wh = jpeg_size(b)
      @gfx.fill_rect(x, y, IMG_W, IMG_H, FmrbGfx::BLACK)
      wh = @frame_wh ? "#{@frame_wh[0]}x#{@frame_wh[1]}" : "?"
      @gfx.draw_text(x + 6, y + 44, "JPEG #{wh} #{b.bytesize} B", FmrbGfx::WHITE)
      @gfx.draw_text(x + 6, y + 58, "no JPEG decoder here", FmrbGfx::WHITE)
      @gfx.draw_text(x + 6, y + 72, "frame #{@recv_n}", FmrbGfx::WHITE)
    end
    now = Machine.board_millis
    @draw_ms = now - t0
    @draw_max = @draw_ms if @draw_ms > @draw_max
    @shown_n += 1
    if @frame_stamp > 0 && @off
      @lat_ms = (Asterism::ROS.now_ns - @frame_stamp - @off) / 1_000_000
      @lat_wmax = @lat_ms if @lat_ms > @lat_wmax
      @lat_sum += @lat_ms
      @lat_n += 1
    end
    @age_ms = ((@odom_sim - @frame_sim) * 1000.0).round if @odom_sim
  end

  def update_rates
    now = Machine.board_millis
    dt = now - @rate_t0
    return if dt < RATE_EVERY_MS
    @recv_hz = (@recv_n - @rate_recv0) * 1000.0 / dt
    @shown_hz = (@shown_n - @rate_shown0) * 1000.0 / dt
    @rate_t0 = now
    @rate_recv0 = @recv_n
    @rate_shown0 = @shown_n
    @off = @off_cur if @off_cur
    @off_cur = nil
    avg = @lat_n > 0 ? @lat_sum / @lat_n : nil
    tick = @tick_n > 0 ? @tick_sum / @tick_n : nil
    Log.info("rover_cam: cam #{@recv_hz.round(2)} Hz recv, #{@shown_hz.round(2)} Hz shown, " +
             "delay avg #{avg} max #{@lat_wmax} ms, age #{@age_ms} ms, " +
             "draw #{@draw_ms} (max #{@draw_max}) ms, poll #{@loop_ms} (max #{@loop_max}) ms, " +
             "period avg #{tick} max #{@tick_wmax} ms, " +
             "odom #{@odom_n}, cmd #{@sent}, fail #{@img_fail}, clock off #{@off ? @off / 1_000_000 : nil} ms")
    @lat_avg = avg
    @lat_max = @lat_wmax
    @lat_wmax = 0
    @lat_sum = 0
    @lat_n = 0
    @tick_sum = 0
    @tick_n = 0
    @tick_wmax = 0
    @dirty = true
  end

  def draw_text_lines
    x = @user_area_x0 + IMG_W + 10
    y = @user_area_y0 + 4
    w = @user_area_width - IMG_W - 12
    @gfx.fill_rect(x, y, w, 13 * 10, theme_bg)
    @gfx.draw_text(x, y, "#{@id} #{@state}"[0, 40], theme_fg)
    @gfx.draw_text(x, y + 13, "cmd v #{fmt(@v, 2)} w #{fmt(@w, 1)}", theme_accent)
    o = @odom
    if o
      @gfx.draw_text(x, y + 26, "x #{fmt(o[0], 3)} y #{fmt(o[1], 3)} m", theme_fg)
      @gfx.draw_text(x, y + 39, "heading #{fmt(o[2], 1)} deg", theme_fg)
      @gfx.draw_text(x, y + 52, "v #{fmt(o[3], 2)} m/s w #{fmt(o[4], 2)}", theme_fg)
    else
      @gfx.draw_text(x, y + 26, "odom: nothing yet", theme_fg)
    end
    @gfx.draw_text(x, y + 67, "cam #{@recv_hz.round(1)} Hz shown #{@shown_hz.round(1)}", theme_fg)
    lat = @lat_avg ? "#{@lat_avg}/#{@lat_max}" : "-"
    age = @age_ms ? @age_ms.to_s : "-"
    @gfx.draw_text(x, y + 80, "delay #{lat} ms age #{age} ms", theme_fg)
    @gfx.draw_text(x, y + 93, "#{@frame_bytes} B draw #{@draw_ms}/#{@draw_max} ms", theme_border)
    @gfx.draw_text(x, y + 106, "poll #{@loop_ms}/#{@loop_max} ms sent #{@sent}", theme_border)
    @gfx.draw_text(x, y + 119, "arrows: drive  space: stop", theme_border)
  end

  def draw_screen
    clear_user_area
    @gfx.fill_rect(@user_area_x0 + 4, @user_area_y0 + 4, IMG_W, IMG_H, FmrbGfx::BLACK)
    @gfx.draw_text(@user_area_x0 + 30, @user_area_y0 + 58, "no picture yet", FmrbGfx::WHITE)
    draw_text_lines
    draw_window_frame
    @gfx.present
  end

  def on_update
    t = Machine.board_millis
    if @tick_at
      d = t - @tick_at
      @tick_sum += d
      @tick_n += 1
      @tick_wmax = d if d > @tick_wmax
    end
    @tick_at = t
    connect if @state == "connecting"
    if @session
      t0 = Machine.board_millis
      exchange
      dt = Machine.board_millis - t0
      @loop_ms = dt
      @loop_max = dt if dt > @loop_max
    end
    update_rates
    if @dirty
      @dirty = false
      draw_frame if @frame
      draw_text_lines
      @gfx.present
    end
    50
  end

  def on_destroy
    @cmd << { linear: { x: 0.0 }, angular: { z: 0.0 } } if @cmd && moving?
    @node.close if @node
    @session.close if @session
    File.unlink(FRAME_FILE) if @jpeg && File.exist?(FRAME_FILE)
    Log.info("rover_cam: closed")
  rescue => e
    Log.info("rover_cam: close: #{e.message}")
  end
end

begin
  app = RoverCamApp.new
  app.start
rescue => e
  puts "rover_cam: #{e.message}"
end
