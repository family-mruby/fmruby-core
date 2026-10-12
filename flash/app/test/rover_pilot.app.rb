# Rover pilot (doc/ruby_asterism, S4): drives the MuJoCo rover through the
# brain (rover_brain.app.rb, another machine) by calling its Asterism object
# <brain>/brain/rover, and shows the rover's camera straight from ROS 2.
#
# - Connects like rover_cam: the first line of /home/zenoh_echo.txt is the
#   locator, else the machine's zenoh_router setting (system_conf.toml; the
#   browser build has ws/localhost:7449 there, or ?zenoh= from the page's
#   URL), else tcp/zenohd:7447. The ID comes from /home/asterism_node.txt,
#   the mDNS name, the asterism_node setting (the browser's wasm-XXXXXX) or
#   the board name ("linux", "wasm").
# - One connection (G1b): Asterism (app "pilot", nothing exposed) and, on
#   Asterism.session, the ROS 2 node fmruby_pilot_<ID> (subscribes
#   /camera/image/compressed, and /odom only to measure the camera's delay;
#   it publishes nothing). One session also gets through a router door whose
#   pass lets one connection in.
# - Finding the brain: the objects alive that match */brain/rover (Asterism
#   liveliness), looked at again whenever a node joins or leaves
#   (Asterism.on_join / on_leave). The first one is kept while it lives.
#   Until there is one the screen says so.
# - Keys (the same steps as rover_cam):
#     Up / Down    speed +- 0.05 m/s (+-0.5)    Left / Right  turn +- 0.3 rad/s (+-1.5)
#     while the speeds are not zero, rover.drive(v, w) goes out 5 times a second;
#     the brain stops the rover 0.5 s after the last one.
#     g  rover.go(1.0)    t  rover.turn(90)    Space / s  rover.stop
#   g, t and Space also set the speeds here to zero (a drive would cancel
#   the job). Every call carries from: <ID> so that the brain shows who called.
# - rover.state is asked every 300 ms; the round trip of each call is shown
#   (the last, the average and the largest over 2 s). Calls never wait: they
#   are async and looked at on the next updates.
# - The picture: as rover_cam (the newest frame only; JPEG drawn on Modern
#   hardware and in the browser, its size and rate in the simulator).

class RoverPilotImage
  TYPE_NAME = "sensor_msgs::msg::dds_::CompressedImage_"
  TYPE_HASH = "RIHS01_15640771531571185e2efc8a100baf923961a4d15d5569652e6cb6691e8e371a"

  def self.decode(bytes)
    r = ::Asterism::CDR::Reader.new(bytes)
    sec = r.int32
    nsec = r.uint32
    r.string
    r.string
    [sec, nsec, r.bytes]
  end
end

# Only the stamp of nav_msgs/msg/Odometry (for the clock offset).
class RoverPilotOdom
  TYPE_NAME = "nav_msgs::msg::dds_::Odometry_"
  TYPE_HASH = "RIHS01_3cc97dc7fb7502f8714462c526d369e35b603cfc34d946e3f2eda2766dfec6e0"

  def self.decode(bytes)
    r = ::Asterism::CDR::Reader.new(bytes)
    [r.int32, r.uint32]
  end
end

class RoverPilotApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  CONF_FILE = "/etc/system_conf.toml"
  FRAME_FILE = "/tmp/rover_pilot.jpg"
  APP = "pilot"
  BRAIN = "*/brain/rover"
  CALL_TIMEOUT = 1.0
  DRIVE_EVERY_MS = 200
  STATE_EVERY_MS = 300
  SCAN_EVERY_MS = 2000
  RATE_EVERY_MS = 2000
  MAX_IN_FLIGHT = 4
  V_STEP = 0.05
  V_MAX = 0.5
  W_STEP = 0.3
  W_MAX = 1.5
  GO_M = 1.0
  TURN_DEG = 90.0
  IMG_W = 160
  IMG_H = 120

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
    @jpeg = (FmrbConst::PLATFORM == "esp32" && FmrbConst::HW_FAMILY == "modern") ||
            FmrbConst::BOARD == "wasm"
    @state = "connecting"
    @rs = nil
    @node = nil
    # the brain
    @brain = nil       # its node ID
    @rover = nil       # async proxy
    @scan = true
    @scan_at = 0
    @v = 0.0
    @w = 0.0
    @drive_at = 0
    @keys = []
    @flight = []       # [label, Future]
    @st = nil          # the last rover.state
    @state_f = nil
    @state_at = 0
    @note = "-"
    @calls = 0
    @errors = 0
    @skipped = 0
    @rtt = nil
    @rtt_sum = 0
    @rtt_n = 0
    @rtt_wmax = 0
    @rtt_avg = nil
    @rtt_max = nil
    # camera
    @frame = nil
    @frame_stamp = 0
    @frame_bytes = 0
    @recv_n = 0
    @shown_n = 0
    @recv_hz = 0.0
    @shown_hz = 0.0
    @rate_t0 = Machine.board_millis
    @rate_recv0 = 0
    @rate_shown0 = 0
    @off_cur = nil
    @off = nil
    @lat_sum = 0
    @lat_n = 0
    @lat_wmax = 0
    @lat_avg = nil
    @lat_max = nil
    @draw_ms = 0
    @img_fail = 0
    @dirty = true
    draw_screen
  end

  def connect
    t0 = Machine.board_millis
    begin
      ::Asterism.connect(@locator, node: @id, app: APP)
      ::Asterism.on_join { |n| @scan = true }
      ::Asterism.on_leave { |n| @scan = true }
      @rs = ::Asterism.session # Asterism's: Asterism.close closes it
      @node = Asterism::ROS::Node.new(@rs, "fmruby_pilot_#{ros_name(@id)}")
      @node.subscribe("/camera/image/compressed", RoverPilotImage, depth: 2) { |msg, info| camera(msg, info) }
      @node.subscribe("/odom", RoverPilotOdom, depth: 2) { |msg, info| odom(info) }
      @state = "connected"
    rescue ::Asterism::Error => e
      @state = "failed: #{e.message}"
    end
    Log.info("rover_pilot: #{@id} #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
    @dirty = true
  end

  # ------------------------------------------------------------ the brain

  def find_brain
    now = Machine.board_millis
    return unless @scan || now - @scan_at >= SCAN_EVERY_MS
    @scan = false
    @scan_at = now
    found = nil
    keep = false
    list = ::Asterism.each(BRAIN)
    i = 0
    while i < list.size
      n = list[i].asterism_path.split("/")[0]
      found = n if found.nil?
      keep = true if n == @brain
      i += 1
    end
    return if keep
    if found != @brain
      Log.info("rover_pilot: brain #{found ? found : 'gone'}")
      @note = found ? "brain found: #{found}" : "brain gone: #{@brain}"
      @brain = found
      @rover = found ? ::Asterism["#{found}/brain/rover", timeout: CALL_TIMEOUT].async : nil
      @st = nil
      @state_f = nil
      @v = 0.0
      @w = 0.0
      @dirty = true
    end
  end

  # Starts a call; returns false when too many are on their way.
  def call(label)
    return false unless @rover
    if @flight.size >= MAX_IN_FLIGHT
      @skipped += 1
      return false
    end
    f = yield(@rover)
    @flight << [label, f]
    @calls += 1
    true
  rescue ::Asterism::Error => e
    @errors += 1
    @note = "#{label}: #{e.class}: #{e.message}"
    false
  end

  def took(ms)
    return unless ms
    @rtt = ms
    @rtt_sum += ms
    @rtt_n += 1
    @rtt_wmax = ms if ms > @rtt_wmax
  end

  # Looks at the calls on their way.
  def collect
    i = 0
    while i < @flight.size
      label = @flight[i][0]
      f = @flight[i][1]
      if f.done?
        @flight.delete_at(i)
        took(f.took_ms)
        begin
          v = f.value
          @note = "#{label} -> #{v.inspect} (#{f.took_ms} ms)" unless label == "drive"
        rescue ::Asterism::Error => e
          @errors += 1
          @note = "#{label}: #{e.class}: #{e.message}"
        end
        @dirty = true
      else
        i += 1
      end
    end
    f = @state_f
    if f && f.done?
      @state_f = nil
      took(f.took_ms)
      begin
        @st = f.value
      rescue ::Asterism::Error => e
        @errors += 1
        @note = "state: #{e.class}: #{e.message}"
      end
      @dirty = true
    end
  end

  def ask_state
    now = Machine.board_millis
    return unless @rover && @state_f.nil? && now - @state_at >= STATE_EVERY_MS
    @state_at = now
    @state_f = @rover.state(from: @id)
  rescue ::Asterism::Error => e
    @errors += 1
    @note = "state: #{e.class}: #{e.message}"
  end

  def moving?
    @v != 0.0 || @w != 0.0
  end

  def keep_driving
    return unless moving?
    now = Machine.board_millis
    return if now - @drive_at < DRIVE_EVERY_MS
    @drive_at = now
    v = @v
    w = @w
    id = @id
    call("drive") { |r| r.drive(v, w, from: id) }
  end

  def clamp(x, lim)
    return lim if x > lim
    return -lim if x < -lim
    x
  end

  # Keys are noted in on_event and acted on here (see asterism_demo: calls
  # are made from on_update, one interpreter entry less on the C stack).
  def run_key(ev)
    sc = ev[0]
    ch = ev[1]
    id = @id
    if sc == FmrbConst::KEY_UP || sc == FmrbConst::KEY_DOWN || sc == FmrbConst::KEY_LEFT || sc == FmrbConst::KEY_RIGHT
      was = moving?
      @v = clamp(@v + V_STEP, V_MAX) if sc == FmrbConst::KEY_UP
      @v = clamp(@v - V_STEP, V_MAX) if sc == FmrbConst::KEY_DOWN
      @w = clamp(@w + W_STEP, W_MAX) if sc == FmrbConst::KEY_LEFT
      @w = clamp(@w - W_STEP, W_MAX) if sc == FmrbConst::KEY_RIGHT
      @v = (@v * 100.0).round / 100.0
      @w = (@w * 10.0).round / 10.0
      @drive_at = 0
      if moving?
        keep_driving
      elsif was
        # Stepped down to zero: one drive(0, 0); the brain stops on it.
        call("drive") { |r| r.drive(0.0, 0.0, from: id) }
      end
    elsif ch == 103 # g
      @v = 0.0
      @w = 0.0
      m = GO_M
      call("go(#{m})") { |r| r.go(m, from: id) }
    elsif ch == 116 # t
      @v = 0.0
      @w = 0.0
      d = TURN_DEG
      call("turn(#{d.to_i})") { |r| r.turn(d, from: id) }
    elsif ch == 32 || ch == 115 || ch == 83 # space, s, S
      @v = 0.0
      @w = 0.0
      call("stop") { |r| r.stop(from: id) }
    end
    @dirty = true
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    @keys << [ev[:scancode], ev[:character] || 0]
  end

  # ------------------------------------------------------------ the camera

  def odom(info)
    return unless info && info.stamp_ns > 0
    d = Asterism::ROS.now_ns - info.stamp_ns
    @off_cur = d if @off_cur.nil? || d < @off_cur
  end

  def camera(msg, info)
    @frame = msg[2]
    @frame_bytes = @frame.bytesize
    @frame_stamp = info ? info.stamp_ns : 0
    @recv_n += 1
    @dirty = true
  end

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
        @gfx.draw_image(img[:id], x: x, y: y)
        @gfx.delete_image(img[:id])
      else
        @img_fail += 1
      end
    else
      wh = jpeg_size(b)
      @gfx.fill_rect(x, y, IMG_W, IMG_H, FmrbGfx::BLACK)
      s = wh ? "#{wh[0]}x#{wh[1]}" : "?"
      @gfx.draw_text(x + 6, y + 44, "JPEG #{s} #{b.bytesize} B", FmrbGfx::WHITE)
      @gfx.draw_text(x + 6, y + 58, "no JPEG decoder here", FmrbGfx::WHITE)
      @gfx.draw_text(x + 6, y + 72, "frame #{@recv_n}", FmrbGfx::WHITE)
    end
    @draw_ms = Machine.board_millis - t0
    @shown_n += 1
    if @frame_stamp > 0 && @off
      lat = (Asterism::ROS.now_ns - @frame_stamp - @off) / 1_000_000
      @lat_wmax = lat if lat > @lat_wmax
      @lat_sum += lat
      @lat_n += 1
    end
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
    @lat_avg = @lat_n > 0 ? @lat_sum / @lat_n : nil
    @lat_max = @lat_n > 0 ? @lat_wmax : nil
    @rtt_avg = @rtt_n > 0 ? @rtt_sum / @rtt_n : nil
    @rtt_max = @rtt_n > 0 ? @rtt_wmax : nil
    Log.info("rover_pilot: brain #{@brain} rtt avg #{@rtt_avg} max #{@rtt_max} ms (#{@rtt_n} calls), " +
             "cam #{@recv_hz.round(2)} Hz recv #{@shown_hz.round(2)} Hz shown, delay avg #{@lat_avg} max #{@lat_max} ms, " +
             "draw #{@draw_ms} ms, calls #{@calls} errors #{@errors} skipped #{@skipped}")
    @lat_sum = 0
    @lat_n = 0
    @lat_wmax = 0
    @rtt_sum = 0
    @rtt_n = 0
    @rtt_wmax = 0
    @dirty = true
  end

  # ------------------------------------------------------------ the screen

  def fmt(x, digits)
    return "-" if x.nil?
    s = x.round(digits).to_s
    s = "+#{s}" unless x < 0
    s
  end

  def draw_text_lines
    x = @user_area_x0 + IMG_W + 10
    y = @user_area_y0 + 4
    w = @user_area_width - IMG_W - 12
    n = 40
    @gfx.fill_rect(x, y, w, 13 * 11, theme_bg)
    head = @brain ? "brain #{@brain}" : (@state == "connected" ? "brain: looking for #{BRAIN}" : @state)
    @gfx.draw_text(x, y, "#{@id} -> #{head}"[0, n], theme_fg)
    @gfx.draw_text(x, y + 13, "cmd v #{fmt(@v, 2)} w #{fmt(@w, 1)}", theme_accent)
    st = @st
    if st
      @gfx.draw_text(x, y + 26, "x #{fmt(st['x'], 3)} y #{fmt(st['y'], 3)} #{fmt(st['heading'], 1)} deg"[0, n], theme_fg)
      @gfx.draw_text(x, y + 39, "v #{fmt(st['v'], 2)} w #{fmt(st['w'], 2)} #{st['mode']}"[0, n], theme_fg)
      j = st["job"]
      if j && j["id"].to_i > 0
        pc = (j["progress"].to_f * 100).round
        @gfx.draw_text(x, y + 52, "job #{j['id']} #{j['kind']} #{j['target']} #{pc}%"[0, n], theme_fg)
        @gfx.draw_text(x, y + 65, "  #{j['status']} #{fmt(j['done'], 3)}"[0, n], theme_fg)
      else
        @gfx.draw_text(x, y + 52, "job: none", theme_fg)
      end
    else
      @gfx.draw_text(x, y + 26, "state: nothing yet", theme_fg)
    end
    rtt = @rtt ? "#{@rtt} (#{@rtt_avg || '-'}/#{@rtt_max || '-'})" : "-"
    @gfx.draw_text(x, y + 80, "rtt #{rtt} ms", theme_fg)
    lat = @lat_avg ? "#{@lat_avg}/#{@lat_max}" : "-"
    @gfx.draw_text(x, y + 93, "cam #{@recv_hz.round(1)} Hz delay #{lat} ms"[0, n], theme_fg)
    @gfx.draw_text(x, y + 106, @note[0, n], theme_border)
    @gfx.draw_text(x, y + 119, "arrows g:go t:turn spc:stop", theme_border)
  end

  def draw_screen
    clear_user_area
    @gfx.fill_rect(@user_area_x0 + 4, @user_area_y0 + 4, IMG_W, IMG_H, FmrbGfx::BLACK)
    @gfx.draw_text(@user_area_x0 + 30, @user_area_y0 + 58, "no picture yet", FmrbGfx::WHITE)
    draw_text_lines
    draw_window_frame
    @gfx.present
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
    @brain = nil
    @rover = nil
    @node = nil
    @rs = nil # Asterism.close closes it, with the node
    ::Asterism.close
    Log.info("rover_pilot: #{state}")
    @dirty = true
  rescue => e
    Log.info("rover_pilot: lost: #{e.message}")
  end

  def on_update
    connect if @state == "connecting"
    if @rs
      exchange
      if @rs
        find_brain
        run_key(@keys.shift) until @keys.empty?
        keep_driving
        collect
        ask_state
      end
    else
      @keys = []
    end
    update_rates
    if @dirty
      @dirty = false
      draw_frame if @frame
      draw_text_lines
      @gfx.present
    end
    30
  end

  def on_destroy
    @node.close if @node
    ::Asterism.close
    File.unlink(FRAME_FILE) if @jpeg && File.exist?(FRAME_FILE)
    Log.info("rover_pilot: closed")
  rescue => e
    Log.info("rover_pilot: close: #{e.message}")
  end
end

begin
  app = RoverPilotApp.new
  app.start
rescue => e
  puts "rover_pilot: #{e.message}"
end
