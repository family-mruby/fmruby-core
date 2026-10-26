#!/usr/bin/env ruby
# frozen_string_literal: true
#
# The native program's network check (doc/linux_native/report/l2n.md): the
# acceptance criteria of L2n that a machine can check by itself.
#
#   ruby test/native/net/l2n_check.rb [--engine mruby] [--only a,b,...]
#                                     [--no-external] [--no-gate] [--keep]
#
# Parts (all by default, in this order):
#
#   wait    a TCP round trip whose answer comes after 6 s. While the task
#           waits: two screenshots 2 s apart must show the menu bar clock
#           move, a drag injected on the window's title bar must move the
#           window, and burst_ms (the longest time any task kept the others
#           waiting) must stay small.
#   http    Net::HTTP to a local WEBrick, https://example.com/ (name lookup
#           and TLS against the system CA store; --no-external skips it),
#           a self-signed local HTTPS that must be refused, a connect to
#           example.com:80 (lookup), a WebSocket echo; burst_ms again.
#   kill    an app waiting on a peer that never answers is killed: it must
#           end on request (not by force), the peer must see the connection
#           close, and no socket and no gate helper thread may remain.
#   forcekill  the same with an app that never returns to its event loop
#           (it retries the wait for ever), so the kill deletes its task
#           while it waits: the gate's delete hook must close the socket.
#   zenoh   /app/test/zenoh_echo.app.rb against the zenohd of the parent
#           repository's docker compose on 127.0.0.1:7447 (started with
#           `docker compose up -d zenohd` when it is not running): its puts
#           are read on the PC through the REST plugin (tools/fmrb_zenoh.rb),
#           and a put from the PC must reach the app.
#   net     the net service published net/state ("svc[net] up: ...") and
#           timesync did not start.
#
# --no-gate starts the program with FMRB_NATIVE_GATE=off (the waiting calls
# are made on the task's own thread, as before L2n), to see what the gate is
# for: "wait" then fails on burst_ms, which comes out as long as the wait.
#
# The program runs with a run directory, a debug port and user/cache
# directories of its own (under the system temp directory), so it does not
# disturb a native program somebody has open. It needs the native build
# (rake native:core, or NATIVE_ENGINE=mruby with --engine mruby).

require "fileutils"
require "open3"
require "optparse"
require "socket"
require "tmpdir"
require "base64"
require "digest/sha1"
require "webrick"
require "webrick/https"

HERE = File.dirname(File.expand_path(__FILE__))
CORE = File.expand_path("../../..", HERE)
ROOT = File.expand_path("..", CORE)
NATIVE_TOOL = File.join(ROOT, "tools", "fmrb_native.rb")
ZENOH_TOOL = File.join(ROOT, "tools", "fmrb_zenoh.rb")
require File.join(ROOT, "tools", "fmrb_png.rb")

PARTS = %w[wait http kill forcekill zenoh net].freeze

opts = { engine: nil, only: PARTS, external: true, keep: false, gate: true }
OptionParser.new do |o|
  o.on("--engine ENGINE") { |v| opts[:engine] = v }
  o.on("--only LIST") { |v| opts[:only] = v.split(",") }
  o.on("--no-external") { opts[:external] = false }
  o.on("--no-gate") { opts[:gate] = false }
  o.on("--keep", "leave the program running at the end") { opts[:keep] = true }
end.parse!

WORK = File.join(Dir.tmpdir, "fmrb-native-l2n-#{Process.uid}")
RUN_DIR = File.join(WORK, "run")
USER_DIR = File.join(WORK, "user")
CACHE_DIR = File.join(WORK, "cache")
DEBUG_PORT = "5585"
ENV["FMRB_DEBUG_TCP_PORT"] = DEBUG_PORT

$failures = []
$results = []

def say(msg) = puts(msg)

def check(name, ok, detail)
  $results << [name, ok, detail]
  say "#{ok ? 'PASS' : 'FAIL'} #{name}: #{detail}"
  $failures << name unless ok
  ok
end

def native(*args)
  out, st = Open3.capture2e("ruby", NATIVE_TOOL, "--run-dir", RUN_DIR, *args)
  [out, st.success?]
end

def native!(*args)
  out, ok = native(*args)
  abort "fmrb_native.rb #{args.join(' ')} failed:\n#{out}" unless ok
  out
end

def log_text
  File.exist?(File.join(RUN_DIR, "core.log")) ? File.read(File.join(RUN_DIR, "core.log"), mode: "rb").scrub : ""
end

# Wait until the log matches re (after offset bytes); the match or nil.
def wait_log(re, timeout, from: 0)
  deadline = Time.now + timeout
  while Time.now < deadline
    m = log_text[from..].to_s.match(re)
    return m if m
    sleep 0.2
  end
  nil
end

def stats
  out = native!("stats").strip
  out.scan(/(\w+)=(\d+)/).to_h { |k, v| [k, v.to_i] }
end

def shot(name)
  path = File.join(WORK, "#{name}.png")
  native!("shot", path)
  FmrbPng.read_rgb(path)
end

def region(img, x0, y0, x1, y1)
  _w, _h, rows = img
  (y0...y1).map { |y| rows[y].byteslice(x0 * 3, (x1 - x0) * 3) }.join
end

def pid_of_program
  File.read(File.join(RUN_DIR, "pid")).to_i
end

# The program's sockets. Settles first: the main thread closes the control
# connection of a `stats` just after answering it.
def sockets_of(pid)
  sleep 0.3
  Dir.glob("/proc/#{pid}/fd/*").count do |fd|
    File.readlink(fd).start_with?("socket:")
  rescue SystemCallError
    false
  end
end

# TCP sockets of pid connected to 127.0.0.1:port (ss -tanp).
def peer_sockets_of(pid, port)
  `ss -tanp 2>/dev/null`.lines.count { |l| l.include?("pid=#{pid},") && l.include?("127.0.0.1:#{port} ") }
end

# What the sockets of pid are, for a failure message.
def describe_sockets(pid)
  inodes = Dir.glob("/proc/#{pid}/fd/*").filter_map { |fd| File.readlink(fd)[/socket:\[(\d+)\]/, 1] rescue nil }
  lines = `ss -tuxanpe 2>/dev/null`.lines.select { |l| inodes.any? { |i| l.include?("ino:#{i}") || l.include?(" #{i} ") } }
  lines.map { |l| l.split.first(6).join(" ") }.join(" | ")
end

def gate_threads_of(pid)
  Dir.glob("/proc/#{pid}/task/*/comm").count { |f| File.read(f).strip == "fmrb-gate" rescue false }
end

def spawn_app(path)
  out = native!("app", "spawn", path)
  out[/"pid":\s*(\d+)/, 1].to_i
end

def app_alive?(app_pid)
  out, = native("app", "ps")
  out.include?("\"pid\": #{app_pid},") || out.match?(/"pid":\s*#{app_pid}\b/)
end

def write_steps(*steps)
  dir = File.join(USER_DIR, "home", "l2n")
  FileUtils.mkdir_p(dir)
  File.write(File.join(dir, "steps.txt"), steps.join("\n") + "\n")
  FileUtils.cp(File.join(HERE, "net_l2n.app.rb"), dir)
  FileUtils.cp(File.join(HERE, "net_l2n.app.toml"), dir)
end

# ------------------------------------------------------------- servers

class Servers
  attr_reader :events

  def initialize
    @events = Queue.new
    @threads = []
    @closers = []
  end

  def log(msg)
    @events << msg
  end

  # 7601: reads a line, answers "pong <line>" after delay seconds.
  def delayed_echo(port, delay)
    srv = TCPServer.new("127.0.0.1", port)
    @closers << srv
    @threads << Thread.new do
      loop do
        c = srv.accept
        Thread.new(c) do |s|
          line = s.gets
          sleep delay
          s.write("pong #{line}")
          s.close
        rescue StandardError
          nil
        end
      end
    rescue StandardError
      nil
    end
  end

  # 7605: reads a line and never answers; reports when the peer closes.
  def silent(port)
    srv = TCPServer.new("127.0.0.1", port)
    @closers << srv
    @threads << Thread.new do
      loop do
        c = srv.accept
        Thread.new(c) do |s|
          s.gets
          log("silent: got a line")
          r = s.read # until the peer closes
          log("silent: peer closed (#{r.to_s.bytesize} more bytes)")
          s.close
        rescue StandardError => e
          log("silent: peer closed (#{e.class})")
        end
      end
    rescue StandardError
      nil
    end
  end

  def http(port, dir)
    s = WEBrick::HTTPServer.new(BindAddress: "127.0.0.1", Port: port, DocumentRoot: dir,
                                Logger: WEBrick::Log.new(File::NULL), AccessLog: [])
    @closers << s
    @threads << Thread.new { s.start }
  end

  def https_self_signed(port, dir)
    s = WEBrick::HTTPServer.new(BindAddress: "127.0.0.1", Port: port, DocumentRoot: dir,
                                SSLEnable: true, SSLCertName: [%w[CN localhost]],
                                Logger: WEBrick::Log.new(File::NULL), AccessLog: [])
    @closers << s
    @threads << Thread.new { s.start }
  end

  # 7604: a WebSocket echo (one text frame back per text frame).
  def ws_echo(port)
    srv = TCPServer.new("127.0.0.1", port)
    @closers << srv
    @threads << Thread.new do
      loop do
        c = srv.accept
        Thread.new(c) { |s| ws_session(s) }
      end
    rescue StandardError
      nil
    end
  end

  def ws_session(s)
    key = nil
    while (line = s.gets) && line != "\r\n"
      key = line.split(":", 2)[1].strip if line.downcase.start_with?("sec-websocket-key:")
    end
    accept = Base64.strict_encode64(Digest::SHA1.digest("#{key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
    s.write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" \
            "Sec-WebSocket-Accept: #{accept}\r\n\r\n")
    loop do
      b0, b1 = s.read(2).bytes
      len = b1 & 0x7f
      len = s.read(2).unpack1("n") if len == 126
      len = s.read(8).unpack1("Q>") if len == 127
      mask = (b1 & 0x80).zero? ? [0, 0, 0, 0] : s.read(4).bytes
      data = s.read(len).bytes.each_with_index.map { |b, i| b ^ mask[i % 4] }.pack("C*")
      op = b0 & 0x0f
      if op == 8
        s.write([0x88, 0].pack("CC"))
        break
      end
      next unless op == 1
      log("ws: echo #{data.inspect}")
      s.write([0x81, data.bytesize].pack("CC") + data) if data.bytesize < 126
    end
    s.close
  rescue StandardError
    nil
  end

  def stop
    @closers.each { |c| c.respond_to?(:shutdown) ? c.shutdown : c.close rescue nil }
    @threads.each { |t| t.kill rescue nil }
  end
end

# --------------------------------------------------------------- parts

def part_wait
  say "-- wait: a TCP answer that takes 6 s"
  write_steps("tcp")
  native!("stats", "reset")
  from = log_text.bytesize
  app = spawn_app("/home/l2n/net_l2n.app.rb")
  return check("wait", false, "the app did not send") unless wait_log(/l2n: tcp sent/, 15, from: from)
  sleep 0.5
  a = shot("wait_1")
  sleep 2.2
  b = shot("wait_2")
  clock_moved = region(a, 340, 0, 426, 11) != region(b, 340, 0, 426, 11)
  # The window's title bar (the window opens at y=30, net_l2n.app.toml):
  # take its colour at (260, 35), drag it at x=200 70 px down, look again at
  # x=260 (the pointer ends up over x=200).
  _w, _h, rows = b
  title = FmrbPng.pixel(rows, 260, 35)
  native!("input", "move", "200", "35", "sleep", "100", "down", "200", "35", "sleep", "100",
          "move", "200", "70", "sleep", "100", "move", "200", "105", "sleep", "100", "up", "200", "105")
  sleep 0.6
  c = shot("wait_3")
  _w, _h, rows_c = c
  moved = FmrbPng.pixel(rows_c, 260, 105) == title && FmrbPng.pixel(rows_c, 260, 35) != title
  answered = wait_log(/l2n: tcp (ok|ng) (\d+) ms ([^\n]*)/, 15, from: from)
  # After the answer: a stretch is counted when it ends.
  st = stats
  check("wait/clock", clock_moved, "menu bar clock changed between two shots 2.2 s apart")
  check("wait/window", moved, "title bar colour #{title.inspect} moved from y=35 to y=105 by the injected drag")
  check("wait/burst", st["burst_ms"] < 100, "burst_ms=#{st['burst_ms']} from the start of the wait to its answer (gate calls=#{st['calls']})")
  check("wait/answer", answered && answered[1] == "ok",
        answered ? "#{answered[1]} after #{answered[2]} ms: #{answered[3]}" : "no answer logged")
  native("app", "kill", app.to_s)
end

def part_http(external)
  say "-- http: local HTTP, HTTPS (example.com), a self-signed HTTPS, a lookup, a WebSocket"
  steps = %w[http https_bad ws]
  steps += %w[https lookup] if external
  write_steps(*steps)
  native!("stats", "reset")
  from = log_text.bytesize
  app = spawn_app("/home/l2n/net_l2n.app.rb")
  steps.each do |step|
    m = wait_log(/l2n: #{step} (ok|ng) (\d+) ms ([^\n]*)/, 40, from: from)
    check("http/#{step}", m && m[1] == "ok", m ? "#{m[1]} #{m[2]} ms: #{m[3]}" : "no result logged")
  end
  st = stats
  check("http/burst", st["burst_ms"] < 150, "burst_ms=#{st['burst_ms']} over the requests (gate calls=#{st['calls']})")
  native("app", "kill", app.to_s)
end

def part_kill(servers)
  say "-- kill: an app waiting on a peer that never answers"
  write_steps("tcp_hang")
  sleep 6 # let idle helpers of earlier parts end
  pid = pid_of_program
  base_sockets = sockets_of(pid)
  from = log_text.bytesize
  app = spawn_app("/home/l2n/net_l2n.app.rb")
  return check("kill", false, "the app did not send") unless wait_log(/l2n: tcp sent, waiting for 127.0.0.1:7605/, 15, from: from)
  sleep 1
  before = stats
  waiting_sockets = sockets_of(pid)
  t0 = Time.now
  out, = native("app", "kill", app.to_s)
  took = Time.now - t0
  sleep 0.5
  closed = []
  closed << servers.events.pop until servers.events.empty?
  ended = log_text[from..].to_s
  graceful = ended.match?(/Exited on request/) && !ended.match?(/forcing termination/)
  after = stats
  sleep 6 # the helper threads' idle time
  later = stats
  check("kill/waiting", before["busy"] == 1 && before["helpers"] >= 1,
        "before the kill: busy=#{before['busy']} helpers=#{before['helpers']} sockets=#{waiting_sockets} (baseline #{base_sockets})")
  check("kill/ended", out.match?(/true|ok|killed/i) && graceful && !app_alive?(app),
        "kill answered in #{format('%.2f', took)} s, #{graceful ? 'ended on request' : 'forced or not ended'}; #{out.strip[0, 80]}")
  check("kill/peer", closed.any? { |e| e.start_with?("silent: peer closed") }, "server saw: #{closed.join('; ')}")
  peer = peer_sockets_of(pid, 7605)
  check("kill/cleanup", after["busy"].zero? && peer.zero? && sockets_of(pid) == base_sockets &&
        later["helpers"].zero? && gate_threads_of(pid).zero?,
        "after: busy=#{after['busy']} cancels=#{after['cancels']} deleted=#{after['deleted']} sockets to the peer=#{peer}; " \
        "6 s later sockets=#{sockets_of(pid)} (baseline #{base_sockets}) helpers=#{later['helpers']} " \
        "threads named fmrb-gate=#{gate_threads_of(pid)}")
  # The rest of the machine still answers.
  shot("kill_after")
  out2, ok2 = native("app", "ps")
  check("kill/alive", ok2 && out2.include?("desktop"), "app ps answers after the kill")
end

def part_forcekill
  say "-- forcekill: an app that does not answer the stop, waiting on a silent peer"
  write_steps("tcp_stubborn")
  sleep 6 # let idle helpers of earlier parts end
  pid = pid_of_program
  base_sockets = sockets_of(pid)
  base = stats
  from = log_text.bytesize
  app = spawn_app("/home/l2n/net_l2n.app.rb")
  return check("forcekill", false, "the app did not send") unless wait_log(/l2n: tcp sent, waiting for 127.0.0.1:7605/, 15, from: from)
  sleep 1
  t0 = Time.now
  native("app", "kill", app.to_s)
  took = Time.now - t0
  sleep 0.5
  ended = log_text[from..].to_s
  forced = ended.match?(/forcing termination/)
  after = stats
  peer = peer_sockets_of(pid, 7605)
  sleep 6
  later = stats
  check("forcekill/forced", forced && !app_alive?(app), "kill took #{format('%.2f', took)} s, #{forced ? 'forced' : 'not forced'}")
  check("forcekill/cleanup", after["deleted"] > base["deleted"] && after["busy"].zero? && peer.zero? &&
        sockets_of(pid) == base_sockets && later["helpers"].zero? && gate_threads_of(pid).zero?,
        "deleted while waiting=#{after['deleted'] - base['deleted']} busy=#{after['busy']} sockets to the peer=#{peer}; " \
        "6 s later sockets=#{sockets_of(pid)} (baseline #{base_sockets}) helpers=#{later['helpers']} " \
        "threads named fmrb-gate=#{gate_threads_of(pid)}")
  say "  sockets: #{describe_sockets(pid)}" if sockets_of(pid) != base_sockets
  out2, ok2 = native("app", "ps")
  check("forcekill/alive", ok2 && out2.include?("desktop"), "app ps answers after the kill")
end

def zenoh_value(key)
  out, st = Open3.capture2e("ruby", ZENOH_TOOL, "get", key)
  st.success? ? out.strip : nil
end

def part_zenoh
  say "-- zenoh: zenoh_echo against zenohd on 127.0.0.1:7447"
  up = Dir.chdir(ROOT) { `docker compose ps --status running --services 2>/dev/null` }.split.include?("zenohd")
  unless up
    Dir.chdir(ROOT) { system("docker", "compose", "up", "-d", "zenohd", out: File::NULL, err: File::NULL) }
    $started_zenohd = true
  end
  deadline = Time.now + 20
  sleep 0.5 until (TCPSocket.new("127.0.0.1", 7447).close rescue false) || Time.now > deadline
  native!("stats", "reset")
  from = log_text.bytesize
  app = spawn_app("/app/test/zenoh_echo.app.rb")
  m = wait_log(/zenoh_echo: (connected|failed[^\n]*) \(([^,]+), (\d+) ms\)/, 20, from: from)
  check("zenoh/open", m && m[1] == "connected", m ? "#{m[1]} to #{m[2]} in #{m[3]} ms" : "no result logged")
  return native("app", "kill", app.to_s) unless m && m[1] == "connected"

  sleep 2.5
  v1 = zenoh_value("fmrb/test/out")
  sleep 2.2
  v2 = zenoh_value("fmrb/test/out")
  check("zenoh/put", v1 && v2 && v1 != v2, "fmrb/test/out read on the PC: #{v1.inspect} then #{v2.inspect}")
  before = shot("zenoh_1")
  word = "l2n-#{Time.now.to_i % 100000}"
  system("ruby", ZENOH_TOOL, "put", "fmrb/test/in", word, out: File::NULL)
  sleep 1.5
  after = shot("zenoh_2")
  # The "in:" line of the window changed (the screenshot is kept to look at).
  changed = region(before, 0, 50, 426, 110) != region(after, 0, 50, 426, 110)
  check("zenoh/get", changed, "after a put of #{word.inspect} on fmrb/test/in the app's window changed " \
                              "(#{File.join(WORK, 'zenoh_2.png')})")
  st = stats
  check("zenoh/burst", st["burst_ms"] < 150, "burst_ms=#{st['burst_ms']} (gate calls=#{st['calls']})")
  native("app", "kill", app.to_s)
end

def part_net
  say "-- net: the services"
  text = log_text
  up = text[/svc\[net\] up: ([^\n]*)/, 1]
  check("net/state", !up.nil?, up ? "net service: up: #{up}" : "no net/state logged")
  check("net/timesync", text.match?(/timesync disabled/) && !text.match?(/timesync started/),
        "timesync #{text[/services: timesync [a-z]+/] || '?'}")
end

# ---------------------------------------------------------------- main

# One left over from an earlier --keep would take the debug port.
native("down") if File.exist?(File.join(RUN_DIR, "pid"))
FileUtils.rm_rf(WORK)
FileUtils.mkdir_p([RUN_DIR, USER_DIR, CACHE_DIR])
www = File.join(WORK, "www")
FileUtils.mkdir_p(www)
File.write(File.join(www, "l2n.txt"), "l2n says hello\n")

servers = Servers.new
servers.delayed_echo(7601, 6)
servers.http(7602, www)
servers.https_self_signed(7603, www)
servers.ws_echo(7604)
servers.silent(7605)

ENV["FMRB_NATIVE_GATE"] = "off" unless opts[:gate]
up_args = ["up", "--user-dir", USER_DIR, "--cache-dir", CACHE_DIR]
up_args += ["--engine", opts[:engine]] if opts[:engine]
say native!(*up_args).lines.first.strip

begin
  part_wait if opts[:only].include?("wait")
  part_http(opts[:external]) if opts[:only].include?("http")
  part_kill(servers) if opts[:only].include?("kill")
  part_forcekill if opts[:only].include?("forcekill")
  part_zenoh if opts[:only].include?("zenoh")
  part_net if opts[:only].include?("net")
ensure
  native("down") unless opts[:keep]
  servers.stop
  if $started_zenohd && !opts[:keep]
    Dir.chdir(ROOT) { system("docker", "compose", "stop", "zenohd", out: File::NULL, err: File::NULL) }
  end
end

say "l2n_check: #{$results.count { |r| r[1] }}/#{$results.size} passed#{$failures.empty? ? '' : " (failed: #{$failures.join(', ')})"}"
exit($failures.empty? ? 0 : 1)
