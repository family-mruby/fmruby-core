# Machines that see each other over Zenoh (doc/ruby_asterism, Z3): query and
# reply (get / queryable) and liveliness between two or more boards.
#
# - Name: the first line of /home/zenoh_node.txt, or else the board name
#   (FmrbConst::BOARD: "linux" in the sim, "naryav4", "tab5", ...).
# - Connects like zenoh_echo: the first line of /home/zenoh_echo.txt is the
#   locator (default tcp/zenohd:7447). These forms of that line select peer
#   mode instead of a router:
#     listen tcp/0.0.0.0:7447    peer, listening (no router)
#     peer tcp/192.168.10.15:7447   peer, connecting to that peer
# - Declares the liveliness token fmrb/alive/<name> and answers queries on
#   fmrb/node/<name>/**: .../info gets a short status line; .../silent is
#   held without an answer for 10 s (to try the requester's time limit).
# - Watches fmrb/alive/** and lists the machines alive (zenoh-pico does not
#   report this session's own token, so this machine is listed as "self").
#   Every 3 s it asks each of them for .../info, and every 5 s it asks the
#   first one for .../silent with a 1.5 s limit, showing how that ended.
# - Puts a counter on fmrb/node/<name>/beat every second and subscribes to
#   fmrb/node/*/beat, showing the last value from each machine (put /
#   subscribe between machines, with or without a router).
# - Logs a poll that takes 30 ms or more (a poll must not wait for data).
#
# From the PC (parent repo): ruby tools/fmrb_zenoh.rb query fmrb/node/<name>/info
#                            ruby tools/fmrb_zenoh.rb alive

class ZenohNodesApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NAME_FILE = "/home/zenoh_node.txt"
  ALIVE_PREFIX = "fmrb/alive/"
  INFO_EVERY_MS = 3000
  SILENT_EVERY_MS = 5000
  SILENT_LIMIT_MS = 1500
  HOLD_MS = 10000

  def first_line(path)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n")[0].to_s.strip
  rescue
    ""
  end

  def on_create
    @name = first_line(NAME_FILE)
    @name = FmrbConst::BOARD if @name.empty?
    @locator = first_line(LOCATOR_FILE)
    @locator = DEFAULT_LOCATOR if @locator.empty?
    @state = "connecting"
    @session = nil
    @nodes = {}        # name => { info:, got_at: }
    @gets = []         # [name, Get, started_ms]
    @held = []         # [Query, release_ms]
    @silent = nil      # [name, Get, started_ms]
    @silent_text = "-"
    @answered = 0
    @beat = 0
    @next_beat = 0
    @slow_polls = 0
    @next_info = 0
    @next_silent = Machine.board_millis + SILENT_EVERY_MS
    @next_log = Machine.board_millis + 10000
    @started = Machine.board_millis
    draw_screen
  end

  # "listen <loc>" / "peer <loc>" select peer mode; anything else is a router.
  def open_session
    words = @locator.split(" ")
    if words[0] == "listen"
      Zenoh::Session.open(nil, mode: :peer, listen: words[1])
    elsif words[0] == "peer"
      Zenoh::Session.open(words[1], mode: :peer)
    else
      Zenoh::Session.open(@locator)
    end
  end

  def connect
    t0 = Machine.board_millis
    begin
      @session = open_session
      @token = @session.liveliness(ALIVE_PREFIX + @name)
      @qa = @session.queryable("fmrb/node/#{@name}/**")
      @watch = @session.liveliness_watch(ALIVE_PREFIX + "**")
      @beats = @session.subscribe("fmrb/node/*/beat")
      @state = "connected"
    rescue => e
      @session = nil
      @state = "failed: #{e.message}"
    end
    Log.info("zenoh_nodes: #{@name} #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
  end

  def status_line
    up = (Machine.board_millis - @started) / 1000
    "#{@name} up=#{up}s pool=#{FmrbApp.pool_used / 1024}KB answered=#{@answered}"
  end

  def answer_queries
    # The query key may be a wildcard (fmrb/node/*/info): look at its last
    # chunk and answer with this machine's own key.
    @qa.each_pending.each do |q|
      last = q.key.split("/").last
      if last == "silent"
        @held << [q, Machine.board_millis + HOLD_MS]
      else
        q.reply("fmrb/node/#{@name}/info", status_line) if last == "info" || last == "**"
        q.finish
        @answered += 1
      end
    end
    now = Machine.board_millis
    keep = []
    @held.each do |pair|
      if now >= pair[1]
        pair[0].finish
      else
        keep << pair
      end
    end
    @held = keep
  end

  def watch_alive
    @watch.each_pending do |key, alive|
      name = key[ALIVE_PREFIX.length, key.length - ALIVE_PREFIX.length].to_s
      next if name == @name
      if alive
        Log.info("zenoh_nodes: alive #{name}") unless @nodes[name]
        @nodes[name] ||= { info: "-", got_at: 0, beat: "-" }
      else
        Log.info("zenoh_nodes: gone #{name}")
        @nodes.delete(name)
      end
    end
  end

  def ask_nodes
    now = Machine.board_millis
    if now >= @next_info
      @next_info = now + INFO_EVERY_MS
      @nodes.each_key do |name|
        @gets << [name, @session.get("fmrb/node/#{name}/info", 2000), now]
      end
    end
    if @silent.nil? && now >= @next_silent && !@nodes.empty?
      name = @nodes.keys[0]
      @silent = [name, @session.get("fmrb/node/#{name}/silent", SILENT_LIMIT_MS), now]
    end
  end

  def collect_replies
    now = Machine.board_millis
    keep = []
    @gets.each do |entry|
      name = entry[0]
      get = entry[1]
      get.each_reply do |_key, payload|
        node = @nodes[name]
        node[:info] = payload if node
        node[:got_at] = now if node
      end
      if get.done?
        Log.info("zenoh_nodes: get #{name} got nothing (#{now - entry[2]} ms)") if get.received == 0
      else
        keep << entry
      end
    end
    @gets = keep
    if @silent
      get = @silent[1]
      get.each_reply { |_key, _payload| }
      if get.done?
        took = now - @silent[2]
        @silent_text = "#{@silent[0]}/silent: done in #{took} ms, #{get.received} replies"
        Log.info("zenoh_nodes: #{@silent_text}")
        @silent = nil
        @next_silent = now + SILENT_EVERY_MS
      end
    end
  end

  def beat
    now = Machine.board_millis
    if now >= @next_beat
      @next_beat = now + 1000
      @beat += 1
      @session.put("fmrb/node/#{@name}/beat", @beat.to_s)
    end
    @beats.each_pending do |key, payload|
      name = key.split("/")[2].to_s
      node = @nodes[name]
      node[:beat] = payload if node
    end
  end

  def exchange
    t0 = Machine.board_millis
    open = @session.poll
    took = Machine.board_millis - t0
    if took >= 30
      @slow_polls += 1
      Log.info("zenoh_nodes: poll took #{took} ms")
    end
    unless open
      lost("disconnected")
      return
    end
    answer_queries
    beat
    watch_alive
    ask_nodes
    collect_replies
  rescue => e
    lost("error: #{e.message}")
  end

  def lost(state)
    @state = state
    @session = nil
    @nodes = {}
    @gets = []
    @held = []
    @silent = nil
    Log.info("zenoh_nodes: #{state}")
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 6
    y = @user_area_y0 + 6
    @gfx.draw_text(x, y, "#{@name} #{@state}"[0, 48], theme_fg)
    @gfx.draw_text(x, y + 12, "self: #{@name} (alive)", theme_fg)
    row = 2
    @nodes.each do |name, node|
      @gfx.draw_text(x, y + row * 12, "#{name}: b=#{node[:beat]} #{node[:info]}"[0, 48], theme_fg)
      row += 1
      break if row > 8
    end
    @gfx.draw_text(x, y + 9 * 12, "timeout test: #{@silent_text}"[0, 48], theme_fg)
    draw_window_frame
    @gfx.present
  end

  def log_memory
    now = Machine.board_millis
    return if now < @next_log
    @next_log = now + 10000
    Log.info("zenoh_nodes: nodes=#{@nodes.size} gets=#{@gets.size} held=#{@held.size} " \
             "beat=#{@beat} slow_polls=#{@slow_polls} peers=#{@session ? @session.peers : 0} " \
             "pool_used=#{FmrbApp.pool_used} live=#{GC.stat[:live]}")
  end

  def on_update
    connect if @state == "connecting"
    exchange if @session
    log_memory
    draw_screen
    50
  end
end

begin
  app = ZenohNodesApp.new
  app.start
rescue => e
  puts "zenoh_nodes: #{e.message}"
end
