# ROS 2 (rmw_zenoh) service server and client (doc/ruby_asterism, R2). Uses
# the pure Ruby Asterism::ROS layer on Asterism::Zenoh: no ROS 2 libraries.
#
# - Connects like zenoh_echo / ros2_talker: the first line of
#   /home/zenoh_echo.txt is the locator (default tcp/zenohd:7447). The ID is
#   the first line of /home/asterism_node.txt, else the board's mDNS name,
#   else "linux". The ROS node is fmruby_service_<ID> ("-" -> "_").
# - Serves /<node>/add_two_ints (example_interfaces/srv/AddTwoInts) and
#   shows each request and its answer.
# - Keys (acted on in on_update, not in on_event: a waiting call polls on
#   the C stack, see the picoruby-asterism README):
#     c: calls the PC's /add_two_ints and waits for the answer
#     a: the same call without waiting (the answer is picked up by later
#        updates)
#     n: the same call through node.call (a client made on first use)
#   Each shows the sum and the round trip in ms.
#
# From the PC (parent repo, docker-compose.ros2.yml):
#   docker exec -it fmruby_ros2 ros2 service call \
#     /fmruby_service_linux/add_two_ints example_interfaces/srv/AddTwoInts "{a: 2, b: 3}"
#   docker exec -it fmruby_ros2 ros2 run demo_nodes_cpp add_two_ints_server

class Ros2ServiceApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  PC_SERVICE = "/add_two_ints"
  CALL_TIMEOUT = 3.0      # seconds
  CALL_TIMEOUT_MS = 3000  # the same, for call_async (both units work)

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
    @served = 0
    @calls = 0
    @pending = nil
    @keys = []
    @lines = []
    draw_screen
  end

  def note(text)
    Log.info("ros2_service: #{text}")
    @lines << text
    @lines.shift while @lines.size > 6
  end

  def connect
    t0 = Machine.board_millis
    begin
      @session = Asterism::Zenoh::Session.open(@locator)
      @node = Asterism::ROS::Node.new(@session, "fmruby_service_#{ros_name(@id)}")
      # The generated example_interfaces/srv/AddTwoInts, loaded here.
      add = Asterism::ROS.require_type("example_interfaces/srv/AddTwoInts")
      @srv = @node.service("/#{@node.name}/add_two_ints", add) do |req|
        @served += 1
        sum = req.a + req.b
        note("served ##{@served}: #{req.a} + #{req.b} = #{sum}")
        { sum: sum }
      end
      @cli = @node.client(PC_SERVICE, add)
      @state = "connected"
    rescue => e
      @session = nil
      @node = nil
      @state = "failed: #{e.message}"
    end
    note("#{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
    note("serving /#{@node.name}/add_two_ints") if @node
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    @keys << (ev[:character] || 0)
  end

  # A waiting call: polls the node (and so keeps serving) until the answer.
  def call_wait
    @calls += 1
    a = @calls
    b = @calls * 10
    t0 = Machine.board_millis
    res = @cli.call(a: a, b: b, timeout: CALL_TIMEOUT)
    note("call #{a}+#{b} = #{res.sum} (#{Machine.board_millis - t0} ms)")
  rescue Asterism::ROS::TimeoutError => e
    note("call: #{e.message} (#{Machine.board_millis - t0} ms)")
  end

  # node.call: the shortcut that keeps one client per service name.
  def call_node
    @calls += 1
    t0 = Machine.board_millis
    res = @node.call(PC_SERVICE, "example_interfaces/srv/AddTwoInts",
                     a: @calls, b: -1, timeout: CALL_TIMEOUT)
    note("node.call #{@calls}-1 = #{res.sum} (#{Machine.board_millis - t0} ms)")
  rescue Asterism::ROS::TimeoutError => e
    note("node.call: #{e.message} (#{Machine.board_millis - t0} ms)")
  end

  # A call that does not wait: picked up by check_pending.
  def call_async
    if @pending
      note("async: still waiting for ##{@pending_a}")
      return
    end
    @calls += 1
    @pending_a = @calls
    @pending = @cli.call_async(request: { a: @calls, b: 100 }, timeout_ms: CALL_TIMEOUT_MS)
  end

  def check_pending
    return unless @pending && @pending.done?
    c = @pending
    @pending = nil
    begin
      note("async #{@pending_a}+100 = #{c.value.sum} (#{c.took_ms} ms)")
    rescue Asterism::ROS::TimeoutError => e
      note("async: #{e.message} (#{c.took_ms} ms)")
    end
  end

  def exchange
    unless @node.poll
      lost("disconnected")
      return
    end
    while @keys.size > 0
      ch = @keys.shift
      if ch == 99 # c
        call_wait
      elsif ch == 97 # a
        call_async
      elsif ch == 110 # n
        call_node
      end
    end
    check_pending
  rescue => e
    lost("error: #{e.class}: #{e.message}")
  end

  def lost(state)
    @state = state
    @session = nil
    @node = nil
    @pending = nil
    note(state)
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @gfx.draw_text(x, y, "#{@id} #{@state}"[0, 52], theme_fg)
    @gfx.draw_text(x, y + 11, "served: #{@served}  calls: #{@calls}  (keys c / a / n)"[0, 52], theme_fg)
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
    Log.info("ros2_service: closed")
  rescue => e
    Log.info("ros2_service: close: #{e.message}")
  end
end

begin
  app = Ros2ServiceApp.new
  app.start
rescue => e
  puts "ros2_service: #{e.message}"
end
