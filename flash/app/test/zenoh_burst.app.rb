# Zenoh send-limit check (doc/ruby_asterism/report/z2.md). Puts 4 KB values
# on fmrb/test/burst back to back for 200 ms of every update, so that, when the router
# stops reading (e.g. `docker pause` on the PC), the socket's send buffer
# fills and the gem's send time limit (Asterism::Zenoh::SEND_TIMEOUT_MS) has to act.
# Logs every put that takes 200 ms or more and how the session ends.
#
# Same locator as zenoh_echo: the first line of /home/zenoh_echo.txt, or
# tcp/zenohd:7447. Nothing subscribes on the PC side; the router just drops
# the values.

class ZenohBurstApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  KEY = "fmrb/test/burst"

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
    @count = 0
    @slowest = 0
    @payload = "z" * 4096
    draw_screen
  end

  def connect
    @session = Asterism::Zenoh::Session.open(@locator)
    @state = "connected"
  rescue => e
    @session = nil
    @state = "failed: #{e.message}"
  ensure
    Log.info("zenoh_burst: #{@state} (send limit #{Asterism::Zenoh::SEND_TIMEOUT_MS} ms)")
  end

  def burst
    unless @session.poll
      finish("disconnected")
      return
    end
    start = Machine.board_millis
    t0 = start
    while t0 - start < 200
      @session.put(KEY, @payload)
      took = Machine.board_millis - t0
      @count += 1
      @slowest = took if took > @slowest
      Log.info("zenoh_burst: put #{@count} took #{took} ms") if took >= 200
      t0 = Machine.board_millis
    end
  rescue => e
    took = Machine.board_millis - t0
    finish("error after #{took} ms: #{e.message}")
  end

  def finish(state)
    closed = @session.closed?
    @session = nil
    @state = state
    Log.info("zenoh_burst: #{state} (puts #{@count}, slowest #{@slowest} ms, closed?=#{closed})")
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 6
    y = @user_area_y0 + 6
    @gfx.draw_text(x, y, @locator, theme_fg)
    @gfx.draw_text(x, y + 12, @state[0, 46], theme_fg)
    @gfx.draw_text(x, y + 24, "puts: #{@count}  slowest: #{@slowest} ms", theme_fg)
    draw_window_frame
    @gfx.present
  end

  def on_update
    connect if @state == "connecting"
    burst if @session
    draw_screen
    50
  end
end

begin
  app = ZenohBurstApp.new
  app.start
rescue => e
  puts "zenoh_burst: #{e.message}"
end
