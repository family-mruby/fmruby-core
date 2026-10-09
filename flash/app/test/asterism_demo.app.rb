# Asterism demo (doc/ruby_asterism, A1): objects of this app called from
# another machine, and that machine's objects called from here.
#
# - Node ID: the first line of /home/asterism_node.txt, else the board's
#   mDNS name (fmruby-XXXXXX), else "linux" (the sim has no mDNS name).
# - Connects like zenoh_echo: the first line of /home/zenoh_echo.txt is the
#   locator (default tcp/zenohd:7447); "listen <loc>" / "peer <loc>" select
#   peer mode (no router).
# - Exposes, as <ID>/demo/<name>:
#     apu     play(mml) -> number of notes, stop -> true (the APU, via MML)
#     screen  say(text) -> text length (shown in this window)
#     info    status -> Hash (name, board, free memory, uptime),
#             relay(from, n) -> calls <from>/demo/info.status back and
#             returns [n, its name] (for calls in both directions at once),
#             boom -> raises (to see RemoteError),
#             echo(value, tag: nil) -> [value, tag] (values and keywords)
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
#
# From the PC (parent repo):
#   ruby tools/fmrb_zenoh.rb call <ID>/demo/info status
#   ruby tools/fmrb_zenoh.rb meta <ID>/demo/apu

class AsterismDemoApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  APP = "demo"
  STATUS_EVERY_MS = 3000
  TUNE = "o4 l8 cdefgab>c"

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
    @node = "linux" if @node.empty?
    @locator = first_line(LOCATOR_FILE)
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
      ::Asterism.expose("info", AsterismDemoApp::Info.new(self), methods: { status: 0, relay: 2, boom: 0, echo: 1 })
      # Called from Asterism.poll (on_update), not from a waiting call.
      ::Asterism.on_join { |n| joined(n) }
      ::Asterism.on_leave { |n| left(n) }
      @state = "connected"
    rescue ::Asterism::Error => e
      @state = "failed: #{e.message}"
    end
    note("#{@node} #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
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
    when 114 # r
      try("meta") do
        px = peer("info").asterism_refresh
        "secret?=#{px.respond_to?(:secret)} status?=#{px.respond_to?(:status)} #{px.remote_methods.inspect}"
      end
    end
  end

  def draw_screen
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

  def on_update
    connect if @state == "connecting"
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
