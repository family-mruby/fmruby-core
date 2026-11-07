#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_touch_check.rb - the touch screen and the finger scroll (doc/
# linux_native/, L17), in the
# Ubuntu 26.04 image with the program built by rake native:wl, headless,
# --machine linux_pc_wl, no autostart.
#
#   ruby test/native/wl/wl_touch_check.rb [--engine standard|mruby]
#                                         [--renderer pixman|auto] [--keep]
#
# The headless backend has no touch screen, so the check uses the tools'
# own (comp touch: a wlr_touch made on first use, attached to the cursor and
# mapped to the output like a real one; its points are given as seen and
# turned back to the device's own, so wlr_cursor's turn is what places
# them). The output is the GPD's: a 1080x1920 panel turned by 270 to
# 1920x1080, display.scale 3, so that the turn shows.
# Two copies of the check's client wl_dialog (test/native/wl/clients/), one
# that binds wl_touch ("touchy") and one that does not ("plain"):
#
#   1. the seat says it has touch once the touch screen is there, and the
#      touchy client binds wl_touch
#   2. a tap on the touchy window: wl_touch down and up at the point
#      (surface coordinates, within a pixel), the window gets the keyboard
#      from the other one
#   3. a finger moved on it: the motion in surface coordinates follows the
#      finger as seen (the turn applied once)
#   4. a tap on the plain window: the pointer is emulated (enter at the
#      point, the left button down and up)
#   5. a finger on the plain window's Family mruby title bar, moved and
#      lifted: the window is dragged (pointer emulation on Family mruby's
#      own picture)
#   6. a second finger while the first is emulating is ignored
#   7. a finger's scroll (the touchpad's two fingers, L17c) on Family
#      mruby: one wheel notch per 15 units with the rest kept, the scroll's
#      stop starting afresh; a mouse wheel click still one notch
#
# Not here: the CRT look's curve (the headless container has no GLES); it
# is checked on the GPD (doc/linux_native/report/l17.md).
#
# Shots, logs and a summary go to tmp/wl_check/touch-<engine>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"

CORE = File.expand_path("../../..", __dir__)
# the root tools: family-mruby, or FMRB_ROOT= from a worktree (tools/regress/tree.rb)
require_relative "../../../tools/regress/tree"
Regress::Tree.need_root!
TOOLS = Regress::Tree::TOOLS
# The all-mruby configuration's checks publish theirs 100 above, so that
# rake regress:wl can run the two configurations side by side.
DEBUG_PORT = 15_565 + (ARGV.each_cons(2).include?(["--engine", "mruby"]) ? 100 : 0)
BTN_LEFT = 272

opts = { engine: "standard", renderer: "pixman", keep: false }
OptionParser.new do |o|
  o.on("--engine E") { |v| opts[:engine] = v }
  o.on("--renderer R") { |v| opts[:renderer] = v }
  o.on("--keep") { opts[:keep] = true }
end.parse!
abort "--engine standard|mruby" unless %w[standard mruby].include?(opts[:engine])
ENGINE = opts[:engine]
BIN = "native/build-wl/#{ENGINE == 'mruby' ? 'cmake-mruby' : 'cmake'}/fmruby-native"

def image_name
  file = File.join(CORE, "native/docker/Dockerfile.ubuntu")
  from = File.read(file)[/^FROM\s+ubuntu:(\S+)/, 1]
  "fmrb-native-build:#{from}-#{Digest::SHA256.file(file).hexdigest[0, 8]}"
end

IMAGE = image_name
abort "#{BIN} not found; rake native:wl" unless File.exist?(File.join(CORE, BIN))
abort "image #{IMAGE} not found; rake native:wl builds it" unless system("docker image inspect #{IMAGE} > /dev/null 2>&1")

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def sh!(*cmd, ok: true)
  out, st = Open3.capture2e(*cmd)
  raise "#{cmd.join(' ')}:\n#{out.lines.last(15).join}" if ok && !st.success?
  [out, st]
end

class TouchRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-touch-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-touch-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "touch-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "touch #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} touch #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  def start
    sh!("docker", "rm", "-f", @name, ok: false)
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    src = "/src/test/native/wl/clients"
    build = "cd /run/fmrb/bin && " \
            "wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml " \
            "xdg-shell-client-protocol.h && " \
            "wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml " \
            "xdg-shell-protocol.c && " \
            "gcc -O1 -Wall -I. -o wl_dialog #{src}/wl_dialog.c xdg-shell-protocol.c -lwayland-client"
    prog = "#{BIN} --machine linux_pc_wl --set display.mode=1080x1920 --set display.rotate=270 " \
           "--set display.scale=3"
    inner = "mkdir -p -m 700 /run/fmrb/xdg /tmp/home /run/fmrb/bin && (#{build}) > /run/fmrb/bin/build.log 2>&1; " \
            "#{prog} > /run/fmrb/core.log 2>&1; echo \"exit=$?\" >> /run/fmrb/core.log"
    sh!(*a, IMAGE, "bash", "-c", inner)
  end

  def ctl(request, timeout: 30)
    sock = UNIXSocket.new(File.join(@run, "ctl"))
    sock.write("#{request}\n")
    data = +""
    deadline = now + timeout
    loop do
      raise "control socket: no answer to #{request.inspect} in #{timeout}s" if now > deadline
      next unless IO.select([sock], nil, nil, 0.5)
      chunk = sock.read_nonblock(65_536, exception: false)
      break if chunk.nil?
      next if chunk == :wait_readable
      data << chunk
    end
    sock.close
    data.force_encoding("UTF-8")
  end

  def comp(*words)
    r = ctl("comp #{words.join(' ')}")
    raise "comp #{words.join(' ')}: #{r}" unless r.start_with?("OK")
    r
  end

  def kv(r) = r.split.drop(1).to_h { |x| x.split("=", 2) }

  def info
    h = kv(comp("info"))
    re = %r{(\d+):(\d+)x(\d+)\+(-?\d+)\+(-?\d+)/(-?\d+)x(-?\d+)\+(\d+)x(\d+)/(\d)}
    h["windows"] = h["windows"].to_s.scan(re).map do |v|
      id, ww, wh, x, y, fx, fy, fw, fh, vis = v.map(&:to_i)
      { id: id, w: ww, h: wh, x: x, y: y, fx: fx, fy: fy, fw: fw, fh: fh, vis: vis == 1 }
    end
    h
  end

  def win(inf, id) = inf["windows"].find { |w| w[:id] == id }
  def touch_stats = kv(comp("touch")).transform_values(&:to_i)

  def to_out(fx, fy, inf = @info)
    s = inf["scale"].to_i
    x0, y0 = inf["at"].split(",").map(&:to_i)
    [x0 + fx * s + s / 2, y0 + fy * s + s / 2]
  end

  def log_text = File.exist?(File.join(@run, "core.log")) ? File.read(File.join(@run, "core.log")).force_encoding("UTF-8").scrub : ""
  def client_log(which) = File.exist?(File.join(@run, "#{which}.log")) ? File.read(File.join(@run, "#{which}.log")) : ""

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.2
    end
  end

  # wl_dialog under a name of its own (its FIFO and log), with touch or not.
  def client(which, touch)
    fifo = File.join(@run, "#{which}.fifo")
    File.delete(fifo) if File.exist?(fifo)
    File.mkfifo(fifo)
    env = ["-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}"]
    sh!("docker", "exec", "-d", *env, @name, "/run/fmrb/bin/wl_dialog", "/run/fmrb/#{which}.fifo",
        "/run/fmrb/#{which}.log", *(touch ? ["touch"] : []))
    wait_for("#{which} started", 20) { client_log(which).include?("started") || client_log(which)[/error.*/] }
    raise "#{which}: #{client_log(which)[/error.*/]}" if client_log(which).include?("error")
  end

  def new_window(before, timeout = 30)
    wait_for("a new window placed", timeout) do
      info["windows"].find { |x| !before.include?(x[:id]) && x[:vis] }
    end
  end

  # A point of a window's surface on the output (the window's place plus
  # the surface coordinates; the output's scale is 1).
  def surface_point(w, sx, sy) = [w[:x] + sx, w[:y] + sy]

  def lines(which, re) = client_log(which).lines.grep(re)

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    FileUtils.rm_rf(@run)
    FileUtils.mkdir_p(@run)
    start
    wait_for("Boot complete", 180) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    @socket = log_text[/WAYLAND_DISPLAY=(wayland-\d+)/, 1]
    wait_for("the desktop", 60) { log_text.include?("Linux windows are Family mruby windows") }
    raise "the client did not build: #{File.read(File.join(@run, 'bin/build.log')).lines.last(5).join}" unless
      File.exist?(File.join(@run, "bin/wl_dialog"))
    @info = info
    sleep 1.0

    # 1. the touch screen, then the clients.
    ts = touch_stats
    client("touchy", true)
    touchy = new_window([])
    client("plain", false)
    plain = new_window([touchy[:id]])
    sleep 1.0
    bound = wait_for("touchy binds wl_touch", 10) { client_log("touchy").include?("touch bound") } rescue false
    result("touch screen", ts["devices"] == 1 && bound && !client_log("plain").include?("touch bound"),
           "devices=#{ts['devices']}, touchy bound wl_touch: #{bound ? 'yes' : 'no'}, plain: " \
           "#{client_log('plain').include?('touch bound') ? 'yes' : 'no'}; #{log_text[/touch screen fmrb.*/]}")

    # The two windows apart: touchy is dragged by its title bar (the mouse)
    # below plain, so neither covers the other.
    inf = info
    touchy = win(inf, touchy[:id])
    plain = win(inf, plain[:id])
    fh = inf["frame"].split("x").map(&:to_i)[1]
    comp("move", *to_out(touchy[:fx] + 30, touchy[:fy] - 5))
    comp("button", 1, 1)
    target_fy = [plain[:fy] + plain[:fh] + 20, fh - touchy[:fh] - 4].min
    4.times do |i|
      comp("move", *to_out(touchy[:fx] + 30, touchy[:fy] - 5 + (target_fy - touchy[:fy]) * (i + 1) / 4))
      sleep 0.08
    end
    comp("button", 1, 0)
    comp("move", *to_out(2, fh - 2))
    sleep 1.0
    inf = info
    touchy = win(inf, touchy[:id])
    plain = win(inf, plain[:id])

    # 2. a tap on touchy: wl_touch at the point, the keyboard to it (plain
    # has it first, by a click).
    comp("click", *surface_point(plain, 150, 75))
    comp("move", *to_out(2, fh - 2))
    sleep 0.6
    focus0 = info["focus"].to_i
    sx, sy = 100, 60
    comp("touch", "tap", *surface_point(touchy, sx, sy))
    sleep 0.6
    down = lines("touchy", /^touch down/).last.to_s.split
    up = lines("touchy", /^touch up/).size
    dx = down[4].to_f - sx
    dy = down[5].to_f - sy
    focus = info["focus"].to_i
    result("tap on a touch client", down[2] == "parent" && dx.abs <= 1 && dy.abs <= 1 && up == 1 &&
           focus0 == plain[:id] && focus == touchy[:id] && lines("touchy", /^button/).empty?,
           "#{down.join(' ')} (wanted #{sx},#{sy}), ups #{up}, focus #{focus0} -> #{focus} (touchy #{touchy[:id]}), " \
           "pointer buttons #{lines('touchy', /^button/).size}")

    # 3. a finger moved on it: the motion follows as seen.
    comp("touch", "down", 3, *surface_point(touchy, 40, 40))
    comp("touch", "move", 3, *surface_point(touchy, 80, 50))
    comp("touch", "move", 3, *surface_point(touchy, 120, 60))
    comp("touch", "up", 3)
    sleep 0.6
    mv = lines("touchy", /^touch motion 3 /).last.to_s.split
    mx = mv[3].to_f
    my = mv[4].to_f
    result("finger moved on a touch client", (mx - 120).abs <= 1 && (my - 60).abs <= 1,
           "last motion #{mv.join(' ')} (wanted 120,60), motions #{lines('touchy', /^touch motion 3 /).size}")

    # 4. a tap on plain: the pointer emulated.
    before = lines("plain", /^button parent #{BTN_LEFT}/).size
    comp("touch", "tap", *surface_point(plain, 90, 50))
    sleep 0.6
    btn = lines("plain", /^button parent #{BTN_LEFT}/)[before..] || []
    ent = btn.first.to_s.split
    ok = btn.size == 2 && ent[3] == "1" && (ent[4].to_f - 90).abs <= 1 && (ent[5].to_f - 50).abs <= 1
    result("tap on a client without touch", ok && info["focus"].to_i == plain[:id],
           "#{btn.map(&:strip).join(' | ')} (wanted down and up at 90,50); focus #{info['focus']} " \
           "(plain #{plain[:id]})")

    # 5. a finger on plain's Family mruby title bar: dragged.
    inf = info
    plain = win(inf, plain[:id])
    x0, y0 = plain[:x], plain[:y]
    comp("touch", "down", 5, *to_out(plain[:fx] + 30, plain[:fy] - 5, inf))
    6.times do |i|
      comp("touch", "move", 5, *to_out(plain[:fx] + 30 + 5 * (i + 1), plain[:fy] - 5 + 2 * (i + 1), inf))
      sleep 0.08
    end
    # 6. a second finger meanwhile: ignored.
    ign = touch_stats["ignored"]
    comp("touch", "tap", *to_out(2, fh - 2, inf))
    ign2 = touch_stats["ignored"]
    comp("touch", "up", 5)
    sleep 1.0
    plain2 = win(info, plain[:id])
    s = inf["scale"].to_i
    moved_x = plain2[:x] - x0
    moved_y = plain2[:y] - y0
    result("title bar dragged by a finger", moved_x == 30 * s && moved_y == 12 * s,
           "the window moved #{moved_x},#{moved_y} output pixels (wanted #{30 * s},#{12 * s})")
    result("a second finger while dragging", ign2 == ign + 1, "ignored #{ign} -> #{ign2}")

    # 7. a finger's scroll on Family mruby (L17c): one notch per 15 units,
    # the rest kept, the stop starting afresh; a wheel click stays one.
    comp("move", *to_out(320, 5))
    sleep 0.3
    comp("stats", "reset")
    notches = ->(r = comp("stats")) { kv(r)["wheel_notches"].to_i }
    7.times { comp("scroll", 3) }
    a = notches.call
    comp("scroll", 0)
    3.times { comp("scroll", 3) }
    b = notches.call
    comp("scroll", 0)
    comp("wheel", 1)
    c = notches.call
    5.times { comp("scroll", -6) }
    comp("scroll", 0)
    d = notches.call
    result("finger scroll on Family mruby", [a, b, c, d] == [1, 1, 2, 4],
           "notches after 7x3: #{a} (1), then a stop and 3x3: #{b} (1), a wheel click: #{c} (2), 5x-6: #{d} (4)")
    st = touch_stats
    File.write(File.join(@out, "touch_stats.txt"), comp("touch"))
    puts "  touch: #{st}"
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
  ensure
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
    %w[touchy plain].each do |c|
      f = File.join(@run, "#{c}.log")
      FileUtils.cp(f, File.join(@out, "#{c}.log")) if File.exist?(f)
    end
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = TouchRun.new(opts[:renderer], opts[:keep])
run.go
all = run.results
FileUtils.mkdir_p(File.join(CORE, "tmp/wl_check"))
File.write(File.join(CORE, "tmp/wl_check", "summary-touch-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(all))
passed = all.count { |r| r[:pass] }
puts "wl_touch_check #{ENGINE} #{opts[:renderer]}: #{passed}/#{all.size} passed (tmp/wl_check/)"
exit(passed == all.size && !all.empty? ? 0 : 1)
