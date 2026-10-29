#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_size_check.rb - a Linux window that keeps a size of its own (doc/
# linux_native/, L15: Firefox restoring its session drew wider than its
# stand-in, and the part beyond showed through another window's hole), in
# the Ubuntu 26.04 image with the program built by rake native:wl,
# headless, --machine linux_pc_wl, no autostart.
#
#   ruby test/native/wl/wl_size_check.rb [--engine standard|mruby]
#                                        [--renderer pixman|auto] [--keep]
#
# Two instances of the test client wl_dialog (test/native/wl/clients/): A
# in front, B beside it. A is told to keep a size of its own larger than
# its stand-in (the "own" line: magenta, larger than the screen).
#
# Family mruby decides a Linux window's size: a Wayland window that draws
# larger than asked, without answering the size asked with its own (the
# configure that asked it acked, then its own size committed: foot's cell
# grid, L9a), keeps its stand-in and is cut to the stand-in's content.
#
#   0. a subsurface larger than the window (a browser's page; the window's
#      geometry stays): its stand-in keeps its size, and nothing of A shows
#      outside the stand-in's content, also not through B's hole
#   1. a size of its own drawn without acking the configures since: the
#      same, and the stand-in's title bar across the stand-in
#   2. a size of its own drawn after later configures (activated) were
#      acked: the same
#   3. unmapped and mapped again at a size of its own (a toolkit hiding
#      and showing its window): the same
#   4. an X11 window asking for a size of its own larger than the screen:
#      its stand-in fits it as far as the screen allows (the rule since
#      L9a), moved back so that it stays on the screen (L15), its title bar
#      redrawn across it, nothing of it outside its content
#
# Shots, logs and a summary go to tmp/wl_check/size-<engine>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
DEBUG_PORT = 15_565
MENU_BAR_H = 13
FRAME_TOP = 11
TITLE_RGB = [222, 36, 82].freeze
GREEN = [0, 192, 0].freeze
MAGENTA = [255, 0, 255].freeze
KEYS = {}.freeze

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

class SizeRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-size-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-size-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "size-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "size #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} size #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  # ------------------------------------------------------------ the run

  def start
    sh!("docker", "rm", "-f", @name, ok: false)
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    # The clients first (gcc and the protocol headers are in the image).
    src = "/src/test/native/wl/clients"
    build = "cd /run/fmrb/bin && " \
            "wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml " \
            "xdg-shell-client-protocol.h && " \
            "wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml " \
            "xdg-shell-protocol.c && " \
            "gcc -O1 -Wall -I. -o wl_dialog #{src}/wl_dialog.c xdg-shell-protocol.c -lwayland-client && " \
            "gcc -O1 -Wall -o x11_dialog #{src}/x11_dialog.c -lX11"
    prog = "#{BIN} --machine linux_pc_wl"
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

  def info
    r = comp("info")
    h = r.split.drop(1).to_h { |kv| kv.split("=", 2) }
    re = %r{(\d+):(\d+)x(\d+)\+(-?\d+)\+(-?\d+)/(-?\d+)x(-?\d+)\+(\d+)x(\d+)/(\d)}
    h["windows"] = h["windows"].to_s.scan(re).map do |v|
      id, ww, wh, x, y, fx, fy, fw, fh, vis = v.map(&:to_i)
      { id: id, w: ww, h: wh, x: x, y: y, fx: fx, fy: fy, fw: fw, fh: fh, vis: vis == 1 }
    end
    h
  end

  def win(inf, id) = inf["windows"].find { |w| w[:id] == id }

  def stats
    r = comp("stats")
    r.split.drop(1).to_h { |kv| k, v = kv.split("=", 2); [k, v.include?(".") ? v.to_f : v.to_i] }
  end

  def to_out(fx, fy, inf = @info)
    s = inf["scale"].to_i
    x0, y0 = inf["at"].split(",").map(&:to_i)
    [x0 + fx * s + s / 2, y0 + fy * s + s / 2]
  end

  def click(fx, fy) = comp("click", *to_out(fx, fy))
  def key(code, down) = comp("key", code, down ? 1 : 0)

  def type(text)
    text.each_char do |c|
      key(KEYS.fetch(c), true)
      key(KEYS.fetch(c), false)
    end
  end

  def drag(fx, fy, dx, dy, steps: 6)
    comp("move", *to_out(fx, fy))
    comp("button", 1, 1)
    steps.times do |i|
      comp("move", *to_out(fx + dx * (i + 1) / steps, fy + dy * (i + 1) / steps))
      sleep 0.08
    end
    comp("button", 1, 0)
  end

  def shot(path)
    sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, "shot", path)
    Regress::Screens::Image.load(path)
  end

  def grim(label)
    sh!("docker", "exec", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "grim", "/run/fmrb/#{label}.png")
    path = File.join(@out, "#{label}.png")
    FileUtils.cp(File.join(@run, "#{label}.png"), path)
    Regress::Screens::Image.load(path)
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

  # A test client in the container, its lines through a FIFO in the run
  # directory, its log beside it.
  def client(which, bin = which)
    fifo = File.join(@run, "#{which}.fifo")
    File.delete(fifo) if File.exist?(fifo)
    File.mkfifo(fifo)
    env = ["-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", "-e", "DISPLAY=#{@x_display}"]
    sh!("docker", "exec", "-d", *env, @name, "/run/fmrb/bin/#{bin}", "/run/fmrb/#{which}.fifo",
        "/run/fmrb/#{which}.log")
    wait_for("#{which} started", 20) { client_log(which).include?("started") || client_log(which)[/error.*/] }
    raise "#{which}: #{client_log(which)[/error.*/]}" if client_log(which).include?("error")
  end

  def tell(which, line)
    # Not blocking: a client that has gone is an error, not a hang.
    File.open(File.join(@run, "#{which}.fifo"), File::WRONLY | File::NONBLOCK) { |f| f.write("#{line}\n") }
  rescue Errno::ENXIO
    raise "#{which} is not running (#{client_log(which).lines.last.to_s.strip})"
  end

  def new_window(before, timeout = 30)
    wait_for("a new window placed", timeout) do
      info["windows"].find { |x| !before.include?(x[:id]) && x[:vis] }
    end
  end

  # The picture's rectangle on the output.
  def picture_rect(inf = @info)
    x0, y0 = inf["at"].split(",").map(&:to_i)
    fw, fh = inf["frame"].split("x").map(&:to_i)
    s = inf["scale"].to_i
    [x0, y0, fw * s, fh * s]
  end

  # Pixels outside the picture's rectangle that are not black.
  def outside(img, inf = @info)
    x0, y0, w, h = picture_rect(inf)
    n = 0
    img.h.times do |y|
      inside_y = y >= y0 && y < y0 + h
      img.w.times do |x|
        next if inside_y && x >= x0 && x < x0 + w
        n += 1 unless img.pixel(x, y) == [0, 0, 0]
      end
    end
    n
  end

  def count(img, rgb, rect = nil)
    x0, y0, w, h = rect || [0, 0, img.w, img.h]
    n = 0
    ([y0, 0].max...[y0 + h, img.h].min).each do |y|
      ([x0, 0].max...[x0 + w, img.w].min).each { |x| n += 1 if img.pixel(x, y) == rgb }
    end
    n
  end

  # A window's rectangle on the output (its size, at its place).
  def out_rect(w) = [w[:x], w[:y], w[:w], w[:h]]

  def centre(w) = [w[:fx] + w[:fw] / 2.0, w[:fy] + w[:fh] / 2.0]

  def stand_in?(id) = log_text.match?(/wl_bridge: window #{id} \([^)]*\): stand-in/)

  # ------------------------------------------------------------ checks

  # A click on a Linux window: the pointer first goes somewhere else on the
  # desktop for a frame, so that Family mruby's own cursor (drawn into the
  # picture where the pointer rests) is not over the point.
  def wclick(fx, fy)
    comp("move", *to_out(2, 236))
    sleep 0.15
    click(fx, fy)
  end

  # The window gone, and its stand-in ended (until then its frame still
  # takes the clicks where it was).
  def gone?(id, timeout = 10)
    wait_for("window #{id} gone", timeout) do
      win(info, id).nil? && log_text.match?(/wl_bridge: window #{id}: stand-in pid \d+ ended/)
    end
    true
  rescue StandardError
    false
  end

  # Pixels of rgb on the output outside the rectangle (x, y, w, h).
  def count_outside(img, rgb, rect)
    x0, y0, w, h = rect
    n = 0
    img.h.times do |y|
      inside_y = y >= y0 && y < y0 + h
      img.w.times do |x|
        next if inside_y && x >= x0 && x < x0 + w
        n += 1 if img.pixel(x, y) == rgb
      end
    end
    n
  end

  # The stand-in's title bar drawn to its full width: the title colour in
  # its middle and just left of the close button at its current right end.
  # A stand-in's content on the output: where the window may show.
  def content_rect(w, inf = @info) = [w[:x], w[:y], w[:fw] * inf["scale"].to_i, w[:fh] * inf["scale"].to_i]

  def title_bar_ok(pic, w)
    ty = w[:fy] - FRAME_TOP + 2
    right = w[:fx] + w[:fw] - 14
    pic.pixel(right, ty) == TITLE_RGB && pic.pixel(w[:fx] + w[:fw] / 2, ty) == TITLE_RGB
  end

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    FileUtils.rm_rf(@run)
    FileUtils.mkdir_p(@run)
    start
    wait_for("Boot complete", 180) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    @socket = log_text[/WAYLAND_DISPLAY=(wayland-\d+)/, 1]
    @x_display = log_text[/X11 programs on DISPLAY=(:\d+)/, 1] || ":0"
    wait_for("the desktop", 60) { log_text.include?("Linux windows are Family mruby windows") }
    raise "the client did not build: #{File.read(File.join(@run, 'bin/build.log')).lines.last(5).join}" unless
      File.exist?(File.join(@run, "bin/wl_dialog"))
    @info = info
    sleep 1.0

    client("a", "wl_dialog")
    a = new_window([])
    client("b", "wl_dialog")
    b = new_window([a[:id]])
    sleep 1.0
    # B to the right of A, then A in front (by its title bar): where A
    # reaches beyond its stand-in, B's hole would show it.
    b = win(info, b[:id])
    fw = @info["frame"].split("x")[0].to_i
    comp("move", *to_out(2, 236))
    sleep 0.15
    drag(b[:fx] + 30, b[:fy] - 5, fw - (b[:fx] + b[:fw]) - 20, 60)
    sleep 0.8
    a = win(info, a[:id])
    comp("move", *to_out(2, 236))
    sleep 0.15
    click(a[:fx] + 40, a[:fy] - 5)
    sleep 0.8
    inf = info
    a0 = win(inf, a[:id])
    b0 = win(inf, b[:id])
    raise "A is not in front (#{inf['windows'].map { |w| w[:id] }.inspect})" unless inf["windows"].last[:id] == a[:id]
    grim("start")

    # 0. a subsurface larger than the window (a browser's page)
    tell("a", "sub 1100 700")
    wait_for("A's subsurface", 10) { client_log("a").include?("sub 1100 700") }
    sleep 1.5
    inf = info
    a_s = win(inf, a[:id])
    img = grim("sub")
    shot(File.join(@out, "sub_picture.png"))
    out0 = count_outside(img, MAGENTA, content_rect(a_s, inf))
    inb0 = count(img, MAGENTA, out_rect(b0))
    inside0 = count(img, MAGENTA, content_rect(a_s, inf))
    result("a subsurface larger than the window", out0.zero? && inside0.positive? && a_s[:fw] == a0[:fw],
           "A's stand-in content #{a_s[:fw]}x#{a_s[:fh]} (window #{a_s[:w]}x#{a_s[:h]}); #{inside0} magenta " \
           "pixels inside it, #{out0} outside (#{inb0} in B's hole at #{out_rect(b0).inspect})")

    # 1. a size of its own drawn without acking the configures since
    tell("a", "own 1100 700 noack")
    wait_for("A drew its own size", 10) { client_log("a").include?("own 1100 700 noack") }
    sleep 1.5
    inf = info
    a1 = win(inf, a[:id])
    img = grim("own_noack")
    pic = shot(File.join(@out, "own_noack_picture.png"))
    out1 = count_outside(img, MAGENTA, content_rect(a1, inf))
    inside1 = count(img, MAGENTA, content_rect(a1, inf))
    inb1 = count(img, MAGENTA, out_rect(b0))
    bar1 = title_bar_ok(pic, a1)
    result("a size of its own, not acked", out1.zero? && inside1.positive? && a1[:fw] == a0[:fw] && bar1,
           "A's stand-in content #{a0[:fw]}x#{a0[:fh]} -> #{a1[:fw]}x#{a1[:fh]} (window #{a1[:w]}x#{a1[:h]}); " \
           "#{inside1} magenta pixels inside it, #{out1} outside (#{inb1} in B's hole); its title bar across " \
           "it #{bar1}")

    # 2. the same after acking the configures that followed the size asked
    tell("a", "own 1000 640")
    wait_for("A drew its own size", 10) { client_log("a").lines.grep(/^own 1000 640$/).any? }
    sleep 0.5
    comp("move", *to_out(2, 236))
    sleep 0.15
    click(a[:fx] + 40, a[:fy] - 5)
    sleep 1.5
    inf = info
    a2 = win(inf, a[:id])
    img = grim("own_ack")
    pic = shot(File.join(@out, "own_ack_picture.png"))
    out2 = count_outside(img, MAGENTA, content_rect(a2, inf))
    inside2 = count(img, MAGENTA, content_rect(a2, inf))
    bar2 = title_bar_ok(pic, a2)
    result("a size of its own, later configures acked", out2.zero? && inside2.positive? && a2[:fw] == a0[:fw] &&
           bar2,
           "A's stand-in content #{a2[:fw]}x#{a2[:fh]} (window #{a2[:w]}x#{a2[:h]}); #{inside2} magenta pixels " \
           "inside it, #{out2} outside; its title bar across it #{bar2}")

    # 3. unmapped and mapped again at its own size
    tell("a", "own 1200 760")
    sleep 0.5
    tell("a", "remap")
    wait_for("A mapped again", 10) { client_log("a").include?("remap") }
    sleep 2.0
    inf = info
    a4 = win(inf, a[:id]) || inf["windows"].last
    img = grim("remap")
    out4 = count_outside(img, MAGENTA, content_rect(a4, inf))
    inside4 = count(img, MAGENTA, content_rect(a4, inf))
    result("mapped again at a size of its own", out4.zero? && inside4.positive?,
           "window #{a4[:id]} content #{a4[:fw]}x#{a4[:fh]} (window #{a4[:w]}x#{a4[:h]}); #{inside4} magenta " \
           "pixels inside, #{out4} outside")
    tell("a", "quit")
    gone?(a4[:id])

    # 4. an X11 window that asks for a size of its own larger than the
    #    screen, in front of B
    before = info["windows"].map { |w| w[:id] }
    client("x11_dialog")
    x = new_window(before)
    sleep 1.0
    x = win(info, x[:id])
    comp("move", *to_out(2, 236))
    sleep 0.15
    click(x[:fx] + 40, x[:fy] - 5)
    sleep 0.5
    x0 = win(info, x[:id])
    tell("x11_dialog", "own 1100 700")
    wait_for("the X11 window's own size", 10) { client_log("x11_dialog").include?("own 1100 700") }
    sleep 2.5
    inf = info
    x3 = win(inf, x[:id])
    img = grim("x11_own")
    pic = shot(File.join(@out, "x11_own_picture.png"))
    out3 = count_outside(img, MAGENTA, content_rect(x3, inf))
    inside3 = count(img, MAGENTA, content_rect(x3, inf))
    inb3 = count(img, MAGENTA, out_rect(b0))
    fw, fh = inf["frame"].split("x").map(&:to_i)
    bar3 = title_bar_ok(pic, x3)
    on3 = x3[:fx] >= 1 && x3[:fy] >= MENU_BAR_H + FRAME_TOP && x3[:fx] + x3[:fw] <= fw - 1 &&
          x3[:fy] + x3[:fh] <= fh - 1
    result("an X11 window's own size", out3.zero? && inside3.positive? && x3[:fw] > x0[:fw] && bar3 && on3,
           "content #{x0[:fw]}x#{x0[:fh]} -> #{x3[:fw]}x#{x3[:fh]} at (#{x3[:fx]},#{x3[:fy]}) (window " \
           "#{x3[:w]}x#{x3[:h]}); #{inside3} magenta pixels inside, #{out3} outside (#{inb3} in B's hole); its " \
           "title bar redrawn across it #{bar3}; on the screen #{on3}")
    tell("x11_dialog", "quit")
    tell("b", "quit")
    ctl("quit")
    wait_for("the container to end", 30) { !system("docker inspect #{@name} > /dev/null 2>&1") } rescue nil
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
  ensure
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
    %w[a b x11_dialog].each do |c|
      FileUtils.cp(File.join(@run, "#{c}.log"), File.join(@out, "#{c}.log")) if File.exist?(File.join(@run, "#{c}.log"))
    end
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = SizeRun.new(opts[:renderer], opts[:keep])
run.go
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-size-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(run.results))
failed = run.results.reject { |r| r[:pass] }
puts "wl_size_check #{ENGINE} #{opts[:renderer]}: #{run.results.length - failed.length}/#{run.results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
