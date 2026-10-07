# ROS 2 (rmw_zenoh) message types made by the generator (doc/ruby_asterism,
# R3): nested types, a Header with the board's time, a sequence. The types
# come from /usr/share/asterism/msgs and are loaded when the app starts
# (Asterism::ROS.require_type), not built into the firmware.
#
# - Connects like ros2_talker: the first line of /home/zenoh_echo.txt is the
#   locator (default tcp/zenohd:7447). The ID is the first line of
#   /home/asterism_node.txt, else the board's mDNS name, else "linux". The
#   ROS node is fmruby_types_<ID> ("-" -> "_").
# - /cmd_vel (geometry_msgs/Twist): shown on the screen, and sent back as it
#   came on /cmd_vel_echo.
# - /imu (sensor_msgs/Imu): made-up values once a second, the Header stamped
#   with the board's clock, frame_id "fmruby_imu".
# - /array (std_msgs/Float32MultiArray): once a second, 4 values and a
#   one-dimension layout. /array_in (the same type) is shown and sent back as
#   it came on /array_out.
# - The log has how much of the app's VM each type took to load.
#
# From the PC (parent repo, docker-compose.ros2.yml):
#   docker exec -it fmruby_ros2 ros2 topic pub -t 3 /cmd_vel geometry_msgs/msg/Twist \
#     "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {z: 1.25}}"
#   docker exec -it fmruby_ros2 ros2 topic echo /cmd_vel_echo
#   docker exec -it fmruby_ros2 ros2 topic echo /imu
#   docker exec -it fmruby_ros2 ros2 topic echo /array
#   docker exec -it fmruby_ros2 ros2 topic pub -t 3 /array_in std_msgs/msg/Float32MultiArray \
#     "{layout: {dim: [{label: 'v', size: 3, stride: 3}]}, data: [1.5, -2.0, 3.25]}"
#   docker exec -it fmruby_ros2 ros2 topic echo /array_out

class Ros2TypesApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  PUBLISH_EVERY_MS = 1000

  def first_line(path)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n")[0].to_s.strip
  rescue
    ""
  end

  # "fmruby-90bce8" -> "fmruby_90bce8"
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
    @count = 0
    @next_pub = 0
    @twists = 0
    @arrays = 0
    @twist_text = "-"
    @array_text = "-"
    @lines = []
    load_types
    draw_screen
  end

  def note(text)
    Log.info("ros2_types: #{text}")
    @lines << text
    @lines.shift while @lines.size > 4
  end

  # Loads the types one by one and logs what each took from the VM pool
  # (after a GC, so it is what stays).
  def load_types
    GC.start
    base = FmrbApp.pool_used
    last = base
    t0 = Machine.board_millis
    [["geometry_msgs/msg/Twist", :@twist], ["sensor_msgs/msg/Imu", :@imu],
     ["std_msgs/msg/Float32MultiArray", :@f32a]].each do |name, ivar|
      t1 = Machine.board_millis
      instance_variable_set(ivar, Asterism::ROS.require_type(name))
      ms = Machine.board_millis - t1
      GC.start
      now = FmrbApp.pool_used
      Log.info("ros2_types: mem #{name} +#{now - last} B (#{ms} ms)")
      last = now
    end
    Log.info("ros2_types: mem types total +#{last - base} B (#{Machine.board_millis - t0} ms), pool #{last} B")
  end

  def connect
    t0 = Machine.board_millis
    begin
      @session = Asterism::Zenoh::Session.open(@locator)
      @node = Asterism::ROS::Node.new(@session, "fmruby_types_#{ros_name(@id)}")
      @cmd_sub = @node.subscription("/cmd_vel", @twist)
      @cmd_echo = @node.publisher("/cmd_vel_echo", @twist)
      @imu_pub = @node.publisher("/imu", @imu)
      @array_pub = @node.publisher("/array", @f32a)
      @array_sub = @node.subscription("/array_in", @f32a)
      @array_out = @node.publisher("/array_out", @f32a)
      @state = "connected"
    rescue => e
      @session = nil
      @node = nil
      @state = "failed: #{e.message}"
    end
    note("#{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
  end

  # The board's clock as a builtin_interfaces/Time Hash.
  def stamp
    ns = Asterism::ROS.now_ns
    { sec: ns / 1_000_000_000, nanosec: ns % 1_000_000_000 }
  end

  def publish_all
    @count += 1
    a = @count * 0.1
    # A rotation about z by a, and made-up rates and acceleration.
    @imu_pub << {
      header: { stamp: stamp, frame_id: "fmruby_imu" },
      orientation: { x: 0.0, y: 0.0, z: ::Math.sin(a / 2), w: ::Math.cos(a / 2) },
      orientation_covariance: [0.01, 0.0, 0.0, 0.0, 0.01, 0.0, 0.0, 0.0, 0.01],
      angular_velocity: { x: 0.0, y: 0.0, z: 0.1 },
      angular_velocity_covariance: [-1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
      linear_acceleration: { x: 0.25 * @count, y: -0.5, z: 9.80665 },
      linear_acceleration_covariance: [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    }
    @array_pub << {
      layout: { dim: [{ label: "n", size: 4, stride: 4 }], data_offset: 0 },
      data: [@count.to_f, @count * 0.5, -1.25, 1.0 / @count]
    }
  end

  def f2(v)
    format("%.2f", v.to_f)
  end

  def exchange
    unless @session.poll
      lost("disconnected")
      return
    end
    now = Machine.board_millis
    if now >= @next_pub
      publish_all
      @next_pub = now + PUBLISH_EVERY_MS
    end
    @cmd_sub.each_pending do |tw, info|
      @twists += 1
      l = tw.linear
      g = tw.angular
      @twist_text = "lin #{f2(l.x)} #{f2(l.y)} #{f2(l.z)} ang #{f2(g.x)} #{f2(g.y)} #{f2(g.z)}"
      note("cmd_vel ##{info ? info.sequence : '-'}: #{@twist_text}")
      @cmd_echo << tw
    end
    @array_sub.each_pending do |arr, info|
      @arrays += 1
      dims = arr.layout.dim.map { |d| "#{d.label}:#{d.size}" }.join(",")
      @array_text = "[#{arr.data.map { |v| f2(v) }.join(', ')}] dim #{dims}"
      note("array_in ##{info ? info.sequence : '-'}: #{@array_text}")
      @array_out << arr
    end
  rescue => e
    lost("error: #{e.class}: #{e.message}")
  end

  def lost(state)
    @state = state
    @session = nil
    @node = nil
    note(state)
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @gfx.draw_text(x, y, "#{@id} #{@state}"[0, 52], theme_fg)
    @gfx.draw_text(x, y + 11, "imu/array sent: #{@count}  cmd_vel: #{@twists}  array_in: #{@arrays}"[0, 52], theme_fg)
    @gfx.draw_text(x, y + 25, "/cmd_vel"[0, 52], theme_fg)
    @gfx.draw_text(x + 8, y + 36, @twist_text[0, 51], theme_fg)
    @gfx.draw_text(x, y + 50, "/array_in"[0, 52], theme_fg)
    @gfx.draw_text(x + 8, y + 61, @array_text[0, 51], theme_fg)
    row = 0
    @lines.each do |l|
      @gfx.draw_text(x, y + 75 + row * 11, l[0, 52], theme_fg)
      row += 1
    end
    draw_window_frame
    @gfx.present
  end

  def on_update
    connect if @state == "connecting"
    exchange if @session
    draw_screen
    50
  end

  def on_destroy
    @node.close if @node
    @session.close if @session
    Log.info("ros2_types: closed")
  rescue => e
    Log.info("ros2_types: close: #{e.message}")
  end
end

begin
  app = Ros2TypesApp.new
  app.start
rescue => e
  puts "ros2_types: #{e.message}"
end
