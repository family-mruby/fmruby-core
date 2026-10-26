# The native program's network check (doc/linux_native/report/l2n.md).
#
# Not shipped (it lives in test/, not flash/): test/native/net/l2n_check.rb
# copies it into the user's /home/l2n/ and starts it there, against servers
# it runs on the PC. The steps to run are the lines of /home/l2n/steps.txt,
# one per line, in order:
#
#   tcp          TCPSocket to 127.0.0.1:7601, write a line, readpartial the
#                answer (the server answers after a few seconds)
#   tcp_hang     the same against 127.0.0.1:7605, which never answers (the
#                kill check)
#   tcp_stubborn the same against 127.0.0.1:7605, over and over, without
#                ever returning to the event loop: an app that does not
#                answer a stop, so a kill has to delete its task while it
#                waits (the forced path)
#   http         Net::HTTP GET http://127.0.0.1:7602/l2n.txt
#   https        Net::HTTP GET https://example.com/ (a name lookup, a TLS
#                handshake with the system CA store)
#   https_bad    Net::HTTP GET https://localhost:7603/ (self-signed): must
#                raise, the certificate does not verify
#   ws           Net::WebSocket echo against ws://127.0.0.1:7604/
#   lookup       TCPSocket to example.com:80 (a name lookup that is not
#                localhost, then a connect), closed at once
#
# Every step logs one line, "l2n: <step> ok|ng <ms> ms <detail>", which the
# check reads from the program's log; the window shows the same.

require 'net/http'
require 'net/websocket'
require 'socket'

class NetL2nApp < FmrbApp
  STEPS_FILE = "/home/l2n/steps.txt"

  def on_create
    @steps = read_steps
    @lines = []
    @running = nil
    note("steps: #{@steps.join(' ')}")
    draw_screen
  end

  def read_steps
    text = File.open(STEPS_FILE, "r") { |f| f.read }
    text.to_s.split("\n").map { |l| l.strip }.reject { |l| l.empty? }
  rescue => e
    Log.info("l2n: no #{STEPS_FILE}: #{e.message}")
    []
  end

  def note(line)
    @lines << line
    @lines.shift while @lines.size > 9
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @gfx.draw_text(x, y, "now: #{@running || '-'}", theme_fg)
    i = 0
    while i < @lines.size
      @gfx.draw_text(x, y + 12 + i * 10, @lines[i][0, 60], theme_fg)
      i += 1
    end
    draw_window_frame
    @gfx.present
  end

  def on_update
    step = @steps.shift
    return 200 if step.nil?
    @running = step
    draw_screen
    run_step(step)
    @running = nil
    draw_screen
    50
  end

  def run_step(step)
    t0 = Machine.board_millis
    ok, detail = begin
      send("step_#{step}")
    rescue => e
      [false, "#{e.class}: #{e.message}"]
    end
    ms = Machine.board_millis - t0
    line = "#{step} #{ok ? 'ok' : 'ng'} #{ms} ms #{detail}"
    Log.info("l2n: #{line}")
    note(line)
  end

  def step_tcp
    tcp_round_trip(7601)
  end

  def step_tcp_hang
    tcp_round_trip(7605)
  end

  def step_tcp_stubborn
    n = 0
    while true
      n += 1
      sock = nil
      begin
        sock = TCPSocket.new("127.0.0.1", 7605)
        sock.write("ping #{n}\n")
        Log.info("l2n: tcp sent, waiting for 127.0.0.1:7605 (#{n})") if n == 1
        sock.readpartial(128)
      rescue => e
        Log.info("l2n: stubborn #{n}: #{e.class}") if n <= 3
      ensure
        sock.close if sock && !sock.closed?
      end
    end
  end

  def tcp_round_trip(port)
    sock = TCPSocket.new("127.0.0.1", port)
    sock.write("ping from native\n")
    Log.info("l2n: tcp sent, waiting for 127.0.0.1:#{port}")
    got = sock.readpartial(128)
    sock.close
    [got.to_s.start_with?("pong"), got.to_s.strip]
  end

  def step_http
    res = Net::HTTP.get_response(URI.parse("http://127.0.0.1:7602/l2n.txt"))
    body = res.body.to_s
    [res.code == "200" && body.start_with?("l2n"), "#{res.code} #{body.bytesize}B #{body.strip[0, 24]}"]
  end

  def step_https
    res = Net::HTTP.get_response(URI.parse("https://example.com/"))
    body = res.body.to_s
    [res.code == "200", "#{res.code} #{body.bytesize}B"]
  end

  def step_https_bad
    res = Net::HTTP.get_response(URI.parse("https://localhost:7603/"))
    [false, "accepted a self-signed certificate (#{res.code})"]
  rescue IOError => e
    [true, "rejected: #{e.message[0, 60]}"]
  end

  def step_ws
    got = nil
    Net::WebSocket::Client.connect("ws://127.0.0.1:7604/") do |ws|
      ws.send_text("hello l2n")
      got = ws.receive(timeout: 5)
    end
    [got == "hello l2n", got.inspect]
  end

  def step_lookup
    sock = TCPSocket.new("example.com", 80)
    sock.close
    [true, "connected"]
  end
end

begin
  app = NetL2nApp.new
  app.start
rescue => e
  puts "net_l2n: #{e.message}"
end
