# ROS 2 (rmw_zenoh) talker and listener (doc/ruby_asterism, R1). Uses the
# pure Ruby Asterism::ROS layer on Asterism::Zenoh: no ROS 2 libraries here.
#
# - Connects like zenoh_echo: the first line of /home/zenoh_echo.txt is the
#   locator (default tcp/zenohd:7447, the zenohd of the sim stack).
# - ID: the first line of /home/asterism_node.txt, else the board's mDNS name
#   (fmruby-XXXXXX), else "linux". The ROS node is fmruby_talker_<ID> with
#   "-" turned into "_" (ROS names take letters, digits and "_").
# - Publishes std_msgs/String "hello from <ID> N" on /chatter once a second
#   (node.every: the block runs from node.poll in on_update).
# - Subscribes to /chatter_back (std_msgs/String) and /cmd_vel_in
#   (geometry_msgs/Twist) with blocks (node.subscribe: also run from
#   node.poll) and shows what comes in; the Twist is taken apart with
#   case/in (deconstruct_keys).
# - The blocks never wait (no service call in them): they run on the update
#   loop's stack, which is 16 KB on the P4.
# - The node and both topics are announced with rmw_zenoh's liveliness
#   tokens, so they show in `ros2 node list` / `ros2 topic list -t`, and go
#   away when the app closes.
#
# From the PC (parent repo, docker-compose.ros2.yml):
#   docker exec -it fmruby_ros2 ros2 topic echo /chatter std_msgs/msg/String
#   docker exec -it fmruby_ros2 ros2 topic pub -r 1 /chatter_back \
#     std_msgs/msg/String "{data: 'hello from ROS 2'}"
#   docker exec -it fmruby_ros2 ros2 topic pub --once /cmd_vel_in \
#     geometry_msgs/msg/Twist "{linear: {x: 0.3}, angular: {z: -0.5}}"

class Ros2TalkerApp < FmrbApp
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
    @count = 0
    @received = 0
    @lines = []
    draw_screen
  end

  def note(text)
    Log.info("ros2_talker: #{text}")
    @lines << text
    @lines.shift while @lines.size > 5
  end

  def connect
    t0 = Machine.board_millis
    begin
      @session = Asterism::Zenoh::Session.open(@locator)
      @node = Asterism::ROS::Node.new(@session, "fmruby_talker_#{ros_name(@id)}")
      # The generated std_msgs/msg/String (/usr/share/asterism/msgs), loaded here.
      str = Asterism::ROS.require_type("std_msgs/msg/String")
      @pub = @node.publisher("/chatter", str)
      @node.every(PUBLISH_EVERY_MS / 1000) { hello }
      @node.subscribe("/chatter_back", str) { |msg, info| back(msg, info) }
      @node.subscribe("/cmd_vel_in", "geometry_msgs/msg/Twist") { |msg, _info| twist(msg) }
      @state = "connected"
    rescue => e
      @session = nil
      @node = nil
      @state = "failed: #{e.message}"
    end
    note("#{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
    note("node #{@node.name} zid #{@node.zid}") if @node
  end

  # node.every
  def hello
    @count += 1
    @pub << { data: "hello from #{@id} #{@count}" }
  end

  # node.subscribe("/chatter_back")
  def back(msg, info)
    @received += 1
    seq = info ? info.sequence : "-"
    note("back ##{seq}: #{msg.data}")
  end

  # node.subscribe("/cmd_vel_in"). In a method of its own: the app VM's
  # compiler does not bind a pattern variable that belongs to an outer
  # scope from inside a block, nor match a class (Float) as a hash
  # pattern's value (doc/ruby_asterism/report/c6.md).
  def twist(msg)
    case msg
    in { linear: { x: }, angular: { z: } }
      note("cmd_vel: forward #{x}, turn #{z}")
    end
  end

  # node.poll polls the session, then runs the every / subscribe blocks.
  def exchange
    lost("disconnected") unless @node.poll
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
    @gfx.draw_text(x, y + 11, "/chatter sent: #{@count}  /chatter_back got: #{@received}"[0, 52], theme_fg)
    row = 0
    @lines.each do |l|
      @gfx.draw_text(x, y + 25 + row * 11, l[0, 52], theme_fg)
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
    Log.info("ros2_talker: closed")
  rescue => e
    Log.info("ros2_talker: close: #{e.message}")
  end
end

begin
  app = Ros2TalkerApp.new
  app.start
rescue => e
  puts "ros2_talker: #{e.message}"
end
