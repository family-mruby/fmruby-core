# Real-time probe (doc/ruby_asterism, G1, G-RT): not a game, a measurement.
# Two machines each put their own moving dot 25 times a second and draw the
# other's dot as it arrives, and measure what that costs and how late it is.
#
# - Connects like othello (/home/zenoh_echo.txt, the zenoh_router setting,
#   tcp/zenohd:7447; the ID from /home/asterism_node.txt, the mDNS name, the
#   asterism_node setting, the board name). Asterism app "game", object "rt"
#   (stats -> the last second's numbers), so the probe shows in the console's
#   graph; a plain Zenoh session for the stream, polled every update (the
#   object layer only every OBJ_POLL_MS).
# - Puts asterism/<ID>/game/rt/pos: MessagePack [seq, x, y, ack, hold_ms]
#   at RATE per second (+ / - change it: 10, 20, 25, 30, 50). ack is the
#   newest seq received from the peer, hold_ms how long ago it was taken
#   off the queue. The peer is the first other node whose pos arrives (it is
#   dropped after 3 s of silence).
# - Round trip without comparing clocks: when the peer's packet acks our
#   seq s, rtt = now - (when s was sent) - hold_ms. Both ends are this app's
#   own update loop, so the numbers include its polling; one way is about
#   half of it.
# - Loss / reordering from the peer's seqs (a gap counts as lost until the
#   missing seq arrives late, then it counts as reordered). Bytes per second
#   sent (payload + key). The update loop's period and the work in it. The
#   internal RAM free (FmrbApp.heap_info[:iram_free]) and its lowest.
# - Puts asterism/<ID>/game/rt/stats once a second: a Hash with the
#   second's counts, every rtt sample of that second and the totals (for the
#   console's recording and for tools on the PC).
# - i toggles interpolation (one trick against jumps, nothing else): the
#   peer's dot is drawn INTERP_DELAY_MS late, between the two samples around
#   that moment (by arrival time), in blue, next to the raw dot (red).
# - d stops / starts drawing the dots, to see what drawing costs the loop.
# - On a small screen (display_width under 320) it takes the whole screen.

class RtProbeApp < FmrbApp
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  CONF_FILE = "/etc/system_conf.toml"
  APP = "game"
  RATES = [10, 20, 25, 30, 50]
  PEER_SILENT_MS = 3000
  INTERP_DELAY_MS = 100
  WIFI_WAIT_MS = 20000
  RING = 256
  DRAW_EVERY_MS = 33
  # The object layer (Asterism.poll) only answers stats calls and follows who
  # is alive; on the boards each poll costs a time that grows with the number
  # of nodes alive (report/g1.md), so it is polled 10 times a second and the
  # stream's own session every update.
  OBJ_POLL_MS = 100
  C_FIELD = 0x00
  C_ME = 0x1C
  C_PEER = 0xE0
  C_INTERP = 0x0B

  class Probe
    def initialize(app)
      @app = app
    end

    def stats
      @app.last_stats
    end
  end

  attr_reader :last_stats

  def first_line(path)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n")[0].to_s.strip
  rescue
    ""
  end

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

  def on_create
    @started = Machine.board_millis
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
    width = conf_int("display_width")
    @small = width > 0 && width < 320
    request_fullscreen(true) if @small
    @net = :connecting
    @retry_at = 0
    @zs = nil
    @sub = nil
    @pos_key = "asterism/#{@id}/#{APP}/rt/pos"
    @stats_key = "asterism/#{@id}/#{APP}/rt/stats"
    @rate_i = 2
    @interp = false
    @last_stats = {}
    # sending
    @seq = 0
    @next_send = 0
    @sent_seq = Array.new(RING, -1)
    @sent_at = Array.new(RING, 0)
    # the peer
    @peer = nil
    @peer_seen = 0
    @peer_last = -1      # its newest seq
    @peer_last_at = 0    # when we took it
    @pmax = nil
    @seen = Array.new(RING, -1)
    @px = nil
    @py = nil
    @hist = []           # [[arrival ms, x, y]] for interpolation
    # totals
    @t_sent = 0
    @t_recv = 0
    @t_lost = 0
    @t_reorder = 0
    @t_dup = 0
    @t_rtt_n = 0
    # this second
    reset_window
    @win_at = Machine.board_millis
    @prev_tick = nil
    @iram_min = nil
    @shown = "starting"
    # drawing
    @me_x = nil
    @me_y = nil
    @dot_old = []
    @drawn_at = 0
    @ast_at = 0
    @draw = true
    layout
    draw_screen
  end

  def reset_window
    @w_sent = 0
    @w_bytes = 0
    @w_recv = 0
    @w_rbytes = 0
    @w_lost = 0
    @w_reorder = 0
    @w_dup = 0
    @w_rtt = []
    @w_loop_n = 0
    @w_loop_sum = 0
    @w_loop_max = 0
    @w_work_max = 0
    @w_poll_sum = 0
    @w_ast_sum = 0
    @w_zs_sum = 0
    @w_take_sum = 0
    @w_send_sum = 0
    @w_draw_sum = 0
    @w_draw_n = 0
  end

  def on_resize(w, h)
    layout
    draw_screen
  end

  def layout
    @fx = @user_area_x0 + 2
    @fy = @user_area_y0 + 2
    @fw = @user_area_width - 4
    lines = @small ? 4 : 5
    @fh = @user_area_height - 4 - lines * 11
    @ty = @fy + @fh + 2
  end

  def uptime_ms
    Machine.board_millis - @started
  end

  def network_ready?
    return true if FmrbConst::PLATFORM != "esp32"
    info = FmrbApp.wifi_info
    return true if info.nil? || info[:connected]
    uptime_ms > WIFI_WAIT_MS
  end

  def connect
    begin
      ::Asterism.close
      ::Asterism.connect(@locator, node: @id, app: APP)
      ::Asterism.expose("rt", RtProbeApp::Probe.new(self), methods: { stats: 0 })
      @zs.close if @zs
      @zs = Asterism::Zenoh::Session.open(@locator)
      @sub = @zs.subscribe("asterism/*/#{APP}/rt/pos", depth: 64)
      @net = :up
    rescue ::Asterism::Error => e
      @net = :down
      @retry_at = Machine.board_millis + 3000
      Log.info("rt_probe: connect: #{e.message}")
    end
    Log.info("rt_probe: #{@id} #{@net} (#{@locator})")
  end

  def lost(why)
    Log.info("rt_probe: lost: #{why}")
    @net = :down
    @retry_at = Machine.board_millis + 3000
    begin
      @zs.close if @zs
    rescue
    end
    @zs = nil
    @sub = nil
    begin
      ::Asterism.close
    rescue
    end
  end

  # ------------------------------------------------------------ the stream

  def motion(now)
    t = (now - @started) / 1000.0
    x = (@fw / 2 + (@fw / 2 - 6) * Math.sin(t * 1.3)).to_i
    y = (@fh / 2 + (@fh / 2 - 6) * Math.sin(t * 2.1 + 0.5)).to_i
    [x, y]
  end

  def send_pos(now)
    period = 1000 / RATES[@rate_i]
    return if now < @next_send
    @next_send = now + period if @next_send == 0 || now - @next_send > period
    @next_send += period
    xy = motion(now)
    @me_x = xy[0]
    @me_y = xy[1]
    hold = @peer_last >= 0 ? now - @peer_last_at : 0
    payload = ::Asterism::Codec.pack([@seq, @me_x, @me_y, @peer_last, hold])
    @zs.put(@pos_key, payload)
    i = @seq % RING
    @sent_seq[i] = @seq
    @sent_at[i] = now
    @seq += 1
    @t_sent += 1
    @w_sent += 1
    @w_bytes += payload.bytesize + @pos_key.bytesize
  end

  def take(now)
    @sub.each_pending do |key, payload, att|
      next if key == @pos_key
      node = key.split("/")[1]
      if @peer && node != @peer
        next if now - @peer_seen < PEER_SILENT_MS
      end
      if node != @peer
        @peer = node
        @pmax = nil
        @peer_last = -1
        @hist = []
        Log.info("rt_probe: peer #{node}")
      end
      @peer_seen = now
      begin
        m = ::Asterism::Codec.unpack(payload)
      rescue
        next
      end
      next unless m.is_a?(Array) && m.size >= 5
      got(now, m, payload.bytesize + key.bytesize)
    end
  end

  def got(now, m, bytes)
    s = m[0].to_i
    @w_rbytes += bytes
    # A restarted peer starts again from 0.
    @pmax = nil if @pmax && s < @pmax - 1000
    if @pmax.nil?
      @pmax = s
      @seen = Array.new(RING, -1)
    elsif s > @pmax
      gap = s - @pmax - 1
      @w_lost += gap
      @t_lost += gap
      @pmax = s
    elsif @seen[s % RING] == s
      @w_dup += 1
      @t_dup += 1
      return
    else
      @w_reorder += 1
      @t_reorder += 1
      @w_lost -= 1
      @t_lost -= 1
    end
    @seen[s % RING] = s
    @w_recv += 1
    @t_recv += 1
    if s >= @pmax
      @peer_last = s
      @peer_last_at = now
      @px = m[1].to_i
      @py = m[2].to_i
      @hist << [now, @px, @py]
      @hist.shift while @hist.size > 16
    end
    ack = m[3].to_i
    hold = m[4].to_i
    if ack >= 0 && @sent_seq[ack % RING] == ack
      rtt = now - @sent_at[ack % RING] - hold
      @sent_seq[ack % RING] = -1 # one sample per seq
      if rtt >= 0
        @w_rtt << rtt
        @t_rtt_n += 1
      end
    end
  end

  # The peer's dot INTERP_DELAY_MS ago, between the samples around it.
  def interpolated(now)
    t = now - INTERP_DELAY_MS
    h = @hist
    i = h.size - 1
    while i > 0
      a = h[i - 1]
      b = h[i]
      if a[0] <= t && t <= b[0]
        span = b[0] - a[0]
        return [b[1], b[2]] if span <= 0
        f = (t - a[0]).to_f / span
        return [(a[1] + (b[1] - a[1]) * f).to_i, (a[2] + (b[2] - a[2]) * f).to_i]
      end
      i -= 1
    end
    h.empty? ? nil : [h[0][1], h[0][2]]
  end

  # ------------------------------------------------------------ the numbers

  def sorted(a)
    b = a.dup
    # insertion sort (a second holds at most a few dozen samples)
    i = 1
    while i < b.size
      v = b[i]
      j = i - 1
      while j >= 0 && b[j] > v
        b[j + 1] = b[j]
        j -= 1
      end
      b[j + 1] = v
      i += 1
    end
    b
  end

  def close_window(now)
    dt = now - @win_at
    return if dt < 1000
    heap = FmrbApp.heap_info || {}
    iram = heap[:iram_free]
    @iram_min = iram if iram && (@iram_min.nil? || iram < @iram_min)
    r = sorted(@w_rtt)
    med = r.empty? ? nil : r[r.size / 2]
    mx = r.empty? ? nil : r[r.size - 1]
    loop_avg = @w_loop_n > 0 ? @w_loop_sum / @w_loop_n : 0
    @last_stats = {
      "node" => @id, "peer" => @peer, "rate" => RATES[@rate_i], "ms" => dt, "interp" => @interp,
      "sent" => @w_sent, "sent_bytes" => @w_bytes, "recv" => @w_recv, "recv_bytes" => @w_rbytes,
      "lost" => @w_lost, "reorder" => @w_reorder, "dup" => @w_dup, "rtt" => @w_rtt,
      "loop_avg" => loop_avg, "loop_max" => @w_loop_max, "work_max" => @w_work_max,
      "poll_avg" => @w_loop_n > 0 ? @w_poll_sum / @w_loop_n : 0, "loops" => @w_loop_n,
      "poll_ms" => [@w_ast_sum, @w_zs_sum, @w_take_sum, @w_send_sum],
      "draw_avg" => @w_draw_n > 0 ? @w_draw_sum / @w_draw_n : 0, "draws" => @w_draw_n, "draw" => @draw,
      "iram_free" => iram, "iram_min" => @iram_min,
      "t_sent" => @t_sent, "t_recv" => @t_recv, "t_lost" => @t_lost, "t_reorder" => @t_reorder,
      "t_dup" => @t_dup, "t_rtt" => @t_rtt_n }
    begin
      @zs.put(@stats_key, ::Asterism::Codec.pack(@last_stats)) if @zs
    rescue => e
      Log.info("rt_probe: stats: #{e.message}")
    end
    @shown = "rtt med #{med || '-'} max #{mx || '-'} ms (#{r.size}/s)"
    @shown2 = "tx #{@w_sent}/s #{@w_bytes} B/s rx #{@w_recv}/s lost #{@t_lost} reord #{@t_reorder}"
    @shown3 = "loop #{loop_avg}/#{@w_loop_max} ms work #{@w_work_max} draw #{@w_draw_n > 0 ? @w_draw_sum / @w_draw_n : '-'} iram #{iram ? iram / 1024 : '-'}K"
    Log.info("rt_probe: #{@id}<->#{@peer} #{@shown} | #{@shown2} | #{@shown3}")
    reset_window
    @win_at = now
    draw_text_lines
  end

  # ------------------------------------------------------------ the screen

  def draw_screen
    clear_user_area
    @gfx.fill_rect(@fx, @fy, @fw, @fh, C_FIELD)
    @dot_old = []
    draw_text_lines
  end

  def draw_text_lines
    x = @user_area_x0 + 2
    y = @ty
    w = @user_area_width - 4
    n = w / 6
    @gfx.fill_rect(x, y, w, @user_area_y0 + @user_area_height - y, theme_bg)
    peer = @peer ? @peer : "none"
    head = @small ? "#{RATES[@rate_i]}Hz #{peer}" : "#{@id} <-> #{peer} #{RATES[@rate_i]}/s #{@interp ? 'interp' : 'raw'} (+/- i)"
    @gfx.draw_text(x, y, head[0, n], theme_fg)
    @gfx.draw_text(x, y + 11, @shown.to_s[0, n], theme_fg)
    @gfx.draw_text(x, y + 22, @shown2.to_s[0, n], theme_fg)
    @gfx.draw_text(x, y + 33, @shown3.to_s[0, n], theme_fg)
    @gfx.draw_text(x, y + 44, @net == :up ? @locator[0, n] : "net #{@net}", theme_border) unless @small
  end

  def dot(list, x, y, c)
    return if x.nil?
    x = 0 if x < 0
    y = 0 if y < 0
    x = @fw - 4 if x > @fw - 4
    y = @fh - 4 if y > @fh - 4
    @gfx.fill_rect(@fx + x, @fy + y, 4, 4, c)
    list << [x, y]
  end

  def draw_dots(now)
    i = 0
    while i < @dot_old.size
      d = @dot_old[i]
      @gfx.fill_rect(@fx + d[0], @fy + d[1], 4, 4, C_FIELD)
      i += 1
    end
    list = []
    dot(list, @me_x, @me_y, C_ME)
    dot(list, @px, @py, C_PEER)
    if @interp
      ip = interpolated(now)
      dot(list, ip[0], ip[1], C_INTERP) if ip
    end
    @dot_old = list
    @gfx.present
  end

  # ------------------------------------------------------------ input, loop

  def on_event(ev)
    return unless ev[:type] == :key_down
    ch = ev[:character] || 0
    if ch == 43 || ch == 61 # + =
      @rate_i += 1 if @rate_i < RATES.size - 1
    elsif ch == 45 # -
      @rate_i -= 1 if @rate_i > 0
    elsif ch == 105 # i
      @interp = !@interp
    elsif ch == 100 # d: stop / start drawing the dots (to see what drawing costs)
      @draw = !@draw
    else
      return
    end
    @next_send = 0
    draw_text_lines
  end

  def on_update
    now = Machine.board_millis
    if @prev_tick
      dt = now - @prev_tick
      @w_loop_n += 1
      @w_loop_sum += dt
      @w_loop_max = dt if dt > @w_loop_max
    end
    @prev_tick = now
    if @net == :connecting
      connect if network_ready?
    elsif @net == :down
      connect if now >= @retry_at
    end
    if @net == :up
      t0 = Machine.board_millis
      begin
        ok = true
        if t0 - @ast_at >= OBJ_POLL_MS
          @ast_at = t0
          ok = ::Asterism.poll
        end
        t1 = Machine.board_millis
        @w_ast_sum += t1 - t0
        if !ok
          lost("asterism: #{::Asterism.lost_reason}")
        elsif !@zs.poll
          lost("zenoh closed")
        else
          now = Machine.board_millis
          @w_zs_sum += now - t1
          take(now)
          t2 = Machine.board_millis
          @w_take_sum += t2 - now
          send_pos(t2)
          @w_send_sum += Machine.board_millis - t2
        end
      rescue => e
        lost("#{e.class}: #{e.message}")
      end
      @w_poll_sum += Machine.board_millis - t0
    end
    @peer = nil if @peer && now - @peer_seen > PEER_SILENT_MS
    if @draw && now - @drawn_at >= DRAW_EVERY_MS
      @drawn_at = now
      t1 = Machine.board_millis
      draw_dots(now)
      @w_draw_sum += Machine.board_millis - t1
      @w_draw_n += 1
    end
    work = Machine.board_millis - now
    @w_work_max = work if work > @w_work_max
    close_window(Machine.board_millis)
    # Wake for the next send, at most 10 ms away.
    wait = @next_send - Machine.board_millis
    wait = 10 if wait > 10
    wait = 1 if wait < 1
    wait
  end

  def on_destroy
    @zs.close if @zs
    ::Asterism.close
    Log.info("rt_probe: closed")
  rescue => e
    Log.info("rt_probe: close: #{e.message}")
  end
end

begin
  app = RtProbeApp.new
  app.start
rescue => e
  puts "rt_probe: #{e.message}"
end
