# Zenoh round trip check (doc/ruby_asterism, Z1/Z2). The Zenoh gem is in the
# Linux sim and the Modern (ESP32-P4) builds.
#
# - Connects to the router. The locator is the first line of
#   /home/zenoh_echo.txt (e.g. tcp/192.168.10.2:7447 for a router on the
#   PC's LAN address); without that file it is tcp/zenohd:7447, the zenohd
#   service of the sim stack.
# - Puts a counter on fmrb/test/out once a second.
# - Subscribes to fmrb/test/in and shows the latest value with the number of
#   values received and dropped.
#
# From the PC (parent repo):
#   ruby tools/fmrb_zenoh.rb get fmrb/test/out
#   ruby tools/fmrb_zenoh.rb put fmrb/test/in hello
#
# On purpose this app never calls close: quitting it leaves the session and
# the subscriber to the gem's object cleanup, which is what reopening the app
# again and again exercises.

class ZenohEchoApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  OUT_KEY = "fmrb/test/out"
  IN_KEY = "fmrb/test/in"

  LOCATOR_FILE = "/home/zenoh_echo.txt"

  def locator
    text = File.open(LOCATOR_FILE, "r") { |f| f.read }
    line = text.split("\n")[0].to_s.strip
    line.empty? ? DEFAULT_LOCATOR : line
  rescue
    DEFAULT_LOCATOR
  end

  def on_create
    @locator = locator
    @state = "connecting"
    @session = nil
    @sub = nil
    @seq = 0
    @last = "-"
    @next_put = 0
    @next_log = Machine.board_millis + 5000
    draw_screen
  end

  # Session.open blocks this app while it connects; the time is logged.
  def connect
    t0 = Machine.board_millis
    begin
      @session = Zenoh::Session.open(@locator)
      @sub = @session.subscribe(IN_KEY)
      @state = "connected"
    rescue => e
      @session = nil
      @sub = nil
      @state = "failed: #{e.message}"
    end
    Log.info("zenoh_echo: #{@state} (#{@locator}, #{Machine.board_millis - t0} ms)")
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 6
    y = @user_area_y0 + 6
    @gfx.draw_text(x, y, @locator, theme_fg)
    @gfx.draw_text(x, y + 12, @state[0, 46], theme_fg)
    @gfx.draw_text(x, y + 24, "out: #{OUT_KEY} = #{@seq}", theme_fg)
    @gfx.draw_text(x, y + 36, "in: #{@last[0, 38]}", theme_fg)
    if @sub
      @gfx.draw_text(x, y + 48, "received: #{@sub.received}  dropped: #{@sub.dropped}", theme_fg)
    end
    draw_window_frame
    @gfx.present
  end

  # State changes and slow calls are logged: the disconnect measurements in
  # report/z2.md read how long the session takes to notice a lost router.
  def exchange
    unless @session.poll
      lost("disconnected")
      return
    end
    now = Machine.board_millis
    if now >= @next_put
      @seq += 1
      @session.put(OUT_KEY, @seq.to_s)
      took = Machine.board_millis - now
      Log.info("zenoh_echo: put #{@seq} took #{took} ms") if took >= 200
      @next_put = now + 1000
    end
    @sub.each_pending { |_key, payload| @last = payload }
  rescue => e
    lost("error: #{e.message}")
  end

  def lost(state)
    @state = state
    @session = nil
    Log.info("zenoh_echo: #{state} (last put #{@seq})")
  end

  # Memory line for the reopen test (report/z1.md): this VM's pool, the
  # shared system pool and the live object count, every 5 seconds.
  def log_memory
    now = Machine.board_millis
    return if now < @next_log
    @next_log = now + 5000
    sys = FmrbApp.sys_pool_info
    Log.info("zenoh_echo: seq=#{@seq} pool_used=#{FmrbApp.pool_used} " \
             "sys_used=#{sys[:used]} live=#{GC.stat[:live]}")
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
  app = ZenohEchoApp.new
  app.start
rescue => e
  puts "zenoh_echo: #{e.message}"
end
