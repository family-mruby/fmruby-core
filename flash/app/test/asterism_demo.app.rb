# Asterism demo (doc/ruby_asterism, A1): objects of this app called from
# another machine, and that machine's objects called from here.
#
# - Node ID: the first line of /home/asterism_node.txt, else the board's
#   mDNS name (fmruby-XXXXXX), else the machine's asterism_node setting (the
#   browser's own ID, wasm-XXXXXX, which the page makes and keeps), else the
#   board name ("linux" in the sim, "wasm" in the browser).
# - Connects like zenoh_echo: the first line of /home/zenoh_echo.txt is the
#   locator, else the machine's zenoh_router setting (system_conf.toml; the
#   browser build has ws/localhost:7449 there, or ?zenoh= from the page's
#   URL), else tcp/zenohd:7447; "listen <loc>" / "peer <loc>" select peer
#   mode (no router; not in the browser).
# - Exposes, as <ID>/demo/<name>:
#     apu     play(mml) -> number of notes, stop -> true (the APU, via MML)
#     screen  say(text) -> text length (shown in this window)
#     info    status -> Hash (name, board, free memory, uptime),
#             relay(from, n) -> calls <from>/demo/info.status back and
#             returns [n, its name] (for calls in both directions at once),
#             boom -> raises (to see RemoteError),
#             echo(value, tag: nil) -> [value, tag] (values and keywords),
#             profile(name, own_icon) -> sets this node's profile (V2.5):
#             the display name ("" for none) and the app's own icon
#             (asterism_demo.png next to this file) or the firmware's
#             default; returns the profile without the icon bytes
#     info has a public method `secret` that is NOT exposed.
# - Lists the nodes alive, and shows the nodes that join and leave
#   (Asterism.on_join / on_leave, called from Asterism.poll in on_update).
#   The peer is the first other node that exposes screen. Every 3 s asks
#   the peer's info.status without waiting (async).
#
# Keys: s screen.say   a apu.play   x apu.stop   n call the unexposed secret
#       e info.boom    t send an Object (EncodeError, nothing sent)
#       m 10 calls of info.relay (both machines at once: no lock up; a
#         relay that comes in is logged here, "relay from ...")
#       r respond_to? / methods of the peer's info
#       p switch this node's profile: "Asterism Demo" and the app's own
#         icon, or back to the firmware's default (no name, its icon)
#
# On a small screen (display_width under 320 in system_conf.toml, e.g. the
# AtomS3R's 128x128) it asks for the whole screen and shows a short summary,
# one item per line, with the log lines wrapped to the width.
#
# From the PC (parent repo):
#   ruby tools/fmrb_zenoh.rb call <ID>/demo/info status
#   ruby tools/fmrb_zenoh.rb meta <ID>/demo/apu

class AsterismDemoApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  CONF_FILE = "/etc/system_conf.toml"
  APP = "demo"
  STATUS_EVERY_MS = 3000
  TUNE = "o4 l8 cdefgab>c"
  # The profile's icons (V2.5): the app's own, and the firmware's default
  # (the browser bundle has its own icon at the same path).
  OWN_ICON = "/app/test/asterism_demo.png"
  DEFAULT_ICON = "/usr/share/asterism/icon.png"

  class Apu
    def initialize(app)
      @app = app
      @player = FmrbMidi::MmlPlayer.new(FmrbMidi.device(app))
      @player.bpm = 120
    end

    attr_reader :player

    def play(mml)
      raise ArgumentError, "no notes in #{mml}" unless @player.load_string(mml.to_s)
      @player.start
      n = @player.event_count
      Log.info("asterism_demo: apu.play #{mml} events=#{n}")
      n
    end

    def stop
      @player.stop
      Log.info("asterism_demo: apu.stop")
      true
    end
  end

  class Screen
    def initialize(app)
      @app = app
    end

    def say(text)
      @app.said = text.to_s
      Log.info("asterism_demo: screen.say #{text}")
      text.to_s.length
    end
  end

  class Info
    def initialize(app)
      @app = app
    end

    def status
      heap = FmrbApp.heap_info || {}
      { "name" => @app.node, "board" => FmrbConst::BOARD, "iram_free" => heap[:iram_free],
        "free" => heap[:free], "pool_used" => FmrbApp.pool_used, "up_ms" => @app.uptime_ms }
    end

    def relay(from, n)
      st = ::Asterism["#{from}/demo/info"].status
      Log.info("asterism_demo: relay from #{from} ##{n}")
      [n, st["name"]]
    end

    def boom
      raise "boom on #{@app.node}"
    end

    # Returns what it got, to check how values travel (and keywords).
    def echo(value, tag: nil)
      [value, tag]
    end

    # This node's profile: a display name ("" for none) and either the app's
    # own icon or the firmware's default. Returns it without the icon bytes.
    def profile(name, own_icon)
      @app.set_profile(name.to_s, own_icon ? true : false)
    end

    # Public, but not exposed: a remote call must not reach it.
    def secret
      "the secret of #{@app.node}"
    end
  end

  attr_accessor :said
  attr_reader :node

  def first_line(path)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n")[0].to_s.strip
  rescue
    ""
  end

  # The number in `<key> = 123` in system_conf.toml, or 0.
  def conf_int(key)
    text = File.open(CONF_FILE, "r") { |f| f.read }
    text.split("\n").each do |line|
      next unless line.start_with?(key)
      eq = line.index("=")
      return eq ? line[eq + 1, line.length].to_s.strip.to_i : 0
    end
    0
  rescue
    0
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

  def uptime_ms
    Machine.board_millis - @started
  end

  def on_create
    @started = Machine.board_millis
    @node = first_line(NODE_FILE)
    if @node.empty?
      info = FmrbApp.wifi_info
      @node = info ? info[:hostname].to_s : ""
    end
    @node = conf_value("asterism_node") if @node.empty?
    @node = FmrbConst::BOARD if @node.empty?
    @locator = first_line(LOCATOR_FILE)
    @locator = conf_value("zenoh_router") if @locator.empty?
    @locator = DEFAULT_LOCATOR if @locator.empty?
    @state = "connecting"
    @said = "-"
    @lines = []
    @peer = nil
    @status_f = nil
    @peer_status = "-"
    @joins = 0
    @leaves = 0
    @next_status = 0
    @keys = []
    @apu = AsterismDemoApp::Apu.new(self)
    width = conf_int("display_width")
    @small = width > 0 && width < 320
    request_fullscreen(true) if @small
    draw_screen
  end

  def on_resize(w, h)
    draw_screen
  end

  def note(text)
    Log.info("asterism_demo: #{text}")
    @lines << text
    @lines.shift while @lines.size > 5
  end

  def connect
    t0 = Machine.board_millis
    words = @locator.split(" ")
    begin
      if words[0] == "listen"
        ::Asterism.connect(nil, node: @node, app: APP, mode: :peer, listen: words[1])
      elsif words[0] == "peer"
        ::Asterism.connect(words[1], node: @node, app: APP, mode: :peer)
      else
        ::Asterism.connect(@locator, node: @node, app: APP)
      end
      ::Asterism.expose("apu", @apu, methods: { play: 1, stop: 0 })
      ::Asterism.expose("screen", AsterismDemoApp::Screen.new(self), methods: [:say])
      ::Asterism.expose("info", AsterismDemoApp::Info.new(self),
                        methods: { status: 0, relay: 2, boom: 0, echo: 1, profile: 2 })
      # Called from Asterism.poll (on_update), not from a waiting call.
      ::Asterism.on_join { |n| joined(n) }
      ::Asterism.on_leave { |n| left(n) }
      @state = "connected"
    rescue ::Asterism::Error => e
      @state = "failed: #{e.message}"
    end
    note("#{@node} #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
  end

  def read_file(path)
    File.open(path, "r") { |f| f.read }
  rescue
    nil
  end

  # Asterism.profile with a name and one of the two icons (key p, and
  # info.profile from another machine). The change is put on the profile's
  # key, so a console redraws the node.
  def set_profile(name, own_icon)
    icon = read_file(own_icon ? OWN_ICON : DEFAULT_ICON)
    pr = ::Asterism.profile(name: name.empty? ? nil : name, icon: icon)
    @own_profile = own_icon
    note("profile: #{pr["name"] || "-"} icon #{pr["icon_hash"] || "-"}")
    out = {}
    pr.each { |k, v| out[k] = v unless k == "icon" }
    out
  end

  def joined(node)
    @joins += 1
    note("joined: #{node}")
  end

  def left(node)
    @leaves += 1
    note("left: #{node}")
  end

  # The peer is followed through liveliness (Asterism.each). When it goes
  # away, the keys keep calling the last one, to see the time-out.
  def find_peer
    found = nil
    ::Asterism.each("*/#{APP}/screen") do |px|
      n = px.asterism_path.split("/")[0]
      found = n if found.nil? && n != @node
    end
    if found != @peer
      note(found ? "peer up: #{found}" : "peer gone: #{@peer}")
      @last_peer = @peer if @peer
      @peer = found
    end
  end

  # Runs one call, shows and logs the value or the exception and the time.
  def try(label)
    return note("#{label}: no peer") unless @peer || @last_peer
    t0 = Machine.board_millis
    begin
      v = yield
      note("#{label} -> #{v.inspect} (#{Machine.board_millis - t0} ms)")
    rescue ::Asterism::RemoteError => e
      note("#{label} RemoteError #{e.remote_class}: #{e.remote_message} (#{Machine.board_millis - t0} ms)")
    rescue ::Asterism::Error => e
      note("#{label} #{e.class}: #{e.message} (#{Machine.board_millis - t0} ms)")
    end
  end

  def peer(obj)
    ::Asterism["#{@peer || @last_peer}/#{APP}/#{obj}"]
  end

  def ask_status
    now = Machine.board_millis
    if @status_f && @status_f.done?
      begin
        st = @status_f.value
        @peer_status = "#{st["name"]} iram=#{st["iram_free"]} up=#{st["up_ms"].to_i / 1000}s (#{@status_f.took_ms} ms)"
      rescue ::Asterism::Error => e
        @peer_status = "#{e.class}: #{e.message}"
      end
      Log.info("asterism_demo: status #{@peer_status}")
      @status_f = nil
    end
    return unless @status_f.nil? && @peer && now >= @next_status
    @next_status = now + STATUS_EVERY_MS
    @status_f = peer("info").async.status
  rescue ::Asterism::Error => e
    @peer_status = "#{e.class}: #{e.message}"
    @status_f = nil
  end

  def mutual
    return note("relay: no peer") unless @peer
    t0 = Machine.board_millis
    ok = 0
    10.times do |i|
      begin
        r = peer("info").relay(@node, i)
        ok += 1 if r[0] == i
      rescue ::Asterism::Error => e
        note("relay #{i}: #{e.class}: #{e.message}")
      end
    end
    note("relay x10: #{ok} ok (#{Machine.board_millis - t0} ms)")
  end

  # Keys are only noted here and acted on in on_update: a call waits (and
  # polls Zenoh) inside the handler that makes it, and on_event already runs
  # one interpreter entry deeper on the C stack than on_update (the event is
  # handed over by C). On the P4's 16 KB app stack that difference matters.
  def on_event(ev)
    return unless ev[:type] == :key_down
    @keys << (ev[:character] || 0)
  end

  def run_key(ch)
    case ch
    when 115 # s
      @says = (@says || 0) + 1
      try("say") { peer("screen").say("hello #{@says} from #{@node}") }
    when 97 # a
      try("play") { peer("apu").play(TUNE) }
    when 120 # x
      try("stop") { peer("apu").stop }
    when 110 # n
      try("secret") { peer("info").secret }
    when 101 # e
      try("boom") { peer("info").boom }
    when 116 # t
      try("encode") { peer("screen").say(Object.new) }
    when 109 # m
      mutual
    when 112 # p
      if @own_profile
        set_profile("", false)
      else
        set_profile("Asterism Demo", true)
      end
    when 114 # r
      try("meta") do
        px = peer("info").asterism_refresh
        "secret?=#{px.respond_to?(:secret)} status?=#{px.respond_to?(:status)} #{px.remote_methods.inspect}"
      end
    end
  end

  # Small screens: one item per line, the log wrapped to the width, the
  # newest lines kept when they do not all fit.
  def draw_small
    clear_user_area
    x = @user_area_x0 + 1
    y = @user_area_y0 + 1
    cols = (@user_area_width - 2) / 6
    cols = 1 if cols < 1
    rows = (@user_area_height - 2) / 9
    nodes = ::Asterism.connected? ? ::Asterism.nodes : []
    head = [@node, @state, "nodes #{nodes.size} +#{@joins} -#{@leaves}",
            "peer #{@peer || '-'}", "said #{@said}"]
    tail = []
    @lines.each do |l|
      i = 0
      while i < l.length
        tail << l[i, cols]
        i += cols
      end
    end
    room = rows - head.size
    room = 0 if room < 0
    tail.shift while tail.size > room
    row = 0
    (head + tail).each do |l|
      @gfx.draw_text(x, y + row * 9, l[0, cols], theme_fg)
      row += 1
    end
    @gfx.present
  end

  def draw_screen
    return draw_small if @small
    clear_user_area
    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @gfx.draw_text(x, y, "#{@node} #{@state}"[0, 52], theme_fg)
    nodes = ::Asterism.connected? ? ::Asterism.nodes : []
    @gfx.draw_text(x, y + 11, "nodes: #{nodes.join(' ')} (+#{@joins} -#{@leaves})"[0, 52], theme_fg)
    @gfx.draw_text(x, y + 22, "peer: #{@peer || '-'} #{@peer_status}"[0, 52], theme_fg)
    @gfx.draw_text(x, y + 33, "said: #{@said}"[0, 52], theme_fg)
    row = 0
    @lines.each do |l|
      @gfx.draw_text(x, y + 47 + row * 11, l[0, 52], theme_fg)
      row += 1
    end
    draw_window_frame
    @gfx.present
  end

  # Started at boot (a single-app machine's startup_app), the app comes up
  # before WiFi has an address: wait for it, up to WIFI_WAIT_MS, rather than
  # fail the one connection attempt. Machines without WiFi (wifi_info nil)
  # connect at once.
  WIFI_WAIT_MS = 20000

  def network_ready?
    info = FmrbApp.wifi_info
    return true if info.nil? || info[:connected]
    uptime_ms > WIFI_WAIT_MS
  end

  def on_update
    connect if @state == "connecting" && network_ready?
    if ::Asterism.connected?
      ::Asterism.poll
      find_peer
      run_key(@keys.shift) until @keys.empty?
      ask_status
    elsif @state == "connected"
      @state = "disconnected: #{::Asterism.lost_reason}"
      note(@state)
    end
    draw_screen
    wait = @apu.player.playing? ? @apu.player.next_delay(50) : 50
    wait
  end
end

begin
  app = AsterismDemoApp.new
  app.start
rescue => e
  puts "asterism_demo: #{e.message}"
end
