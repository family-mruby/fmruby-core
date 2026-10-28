#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_winops_check.rb - the PC's windows and the screen lock
# (doc/linux_native/, L13), in the Ubuntu 26.04 image with the program built
# by rake native:wl, headless, --machine linux_pc_wl, no IME.
#
#   ruby test/native/wl/wl_winops_check.rb [--engine standard|mruby]
#                                          [--renderer pixman|auto] [--keep]
#
# Beside the program, in the container: a D-Bus daemon of the check's own
# as the system bus (DBUS_SYSTEM_BUS_ADDRESS) with a logind stand-in on it
# (clients/fake_logind.c: Lock, PrepareForSleep, an inhibitor), and a lock
# program of the check's own (clients/wl_lock.c, compositor.locker). Never
# the machine's logind or lock.
#
#   1. maximize: a double-click on the editor's title gives it the work area
#      (below the menu bar), another puts it back; the same on foot's
#      stand-in (foot asked the work area's size), and back; the Wayland
#      client's own maximize request (wl_dialog "max"/"unmax") with the
#      state told back (configure: maximized)
#   2. Super + left / right / up / down on the window in front: the halves
#      (tiled left / right), maximized, back; the arrows never reach the
#      window; the same for the editor
#   3. the screen lock: logind's Lock starts the lock program, which covers
#      the screen; keys and clicks reach it and nothing else (Family mruby
#      gets no input event, the window no key); the program dying leaves
#      the screen locked (the cover), a key starts it again, it unlocks and
#      the screen and the input come back; PrepareForSleep locks too, the
#      sleep inhibitor is let go once locked and taken again on waking; the
#      power menu's Lock entry; swaylock itself covers the screen, and
#      killed leaves it locked
#
# Shots, logs and a summary go to tmp/wl_check/winops-<engine>-<renderer>/.

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
MENU_ITEM_H = 12
FRAME_TOP = 11
OUTPUT = "1280x800"
GREEN = [0, 192, 0].freeze
TITLE_RGB = [222, 36, 82].freeze
MAGENTA = [255, 0, 255].freeze
# Linux key codes (input-event-codes.h).
KEY_LEFTMETA = 125
KEY_LEFT = 105
KEY_RIGHT = 106
KEY_UP = 103
KEY_DOWN = 108
KEY_A = 30
ARROWS = [KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN].freeze

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

class WinopsRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-winops-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-winops-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "winops-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "winops #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} winops #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  # ------------------------------------------------------------ the run

  def start
    sh!("docker", "rm", "-f", @name, ok: false)
    %w[logind.fifo locker.fifo].each { |f| File.mkfifo(File.join(@run, f)) }
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy",
         "-e", "DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/fmrb/sysbus", "-e", "XDG_SESSION_ID=1"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    src = "/src/test/native/wl/clients"
    prot = "/usr/share/wayland-protocols"
    build = "cd /run/fmrb/bin && " \
            "wayland-scanner client-header #{prot}/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h && " \
            "wayland-scanner private-code #{prot}/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c && " \
            "wayland-scanner client-header #{prot}/staging/ext-session-lock/ext-session-lock-v1.xml " \
            "ext-session-lock-v1-client-protocol.h && " \
            "wayland-scanner private-code #{prot}/staging/ext-session-lock/ext-session-lock-v1.xml " \
            "ext-session-lock-v1-protocol.c && " \
            "gcc -O1 -Wall -I. -o wl_dialog #{src}/wl_dialog.c xdg-shell-protocol.c -lwayland-client && " \
            "gcc -O1 -Wall -I. -o wl_lock #{src}/wl_lock.c ext-session-lock-v1-protocol.c -lwayland-client && " \
            "gcc -O1 -Wall -o fake_logind #{src}/fake_logind.c $(pkg-config --cflags --libs libsystemd)"
    locker = "/run/fmrb/bin/wl_lock --now /run/fmrb/locker.fifo /run/fmrb/locker.log"
    prog = "#{BIN} --machine linux_pc_wl --set display.mode=#{OUTPUT} --set compositor.ime= " \
           "--set 'compositor.locker=#{locker}' --set system.power_log_only=true"
    inner = "mkdir -p -m 700 /run/fmrb/xdg /tmp/home /run/fmrb/bin && (#{build}) > /run/fmrb/bin/build.log 2>&1; " \
            "dbus-daemon --session --address=unix:path=/run/fmrb/sysbus --nofork --nopidfile " \
            "> /run/fmrb/dbus.log 2>&1 & " \
            "for i in $(seq 50); do [ -S /run/fmrb/sysbus ] && break; sleep 0.1; done; " \
            "/run/fmrb/bin/fake_logind /run/fmrb/logind.fifo /run/fmrb/logind.log & " \
            "for i in $(seq 50); do grep -q ready /run/fmrb/logind.log 2>/dev/null && break; sleep 0.1; done; " \
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
    h["state_of"] = h["states"].to_s.split(",").to_h { |p| i, st = p.split(":"); [i.to_i, st] }
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

  def tap(code)
    key(code, true)
    key(code, false)
  end

  def super_tap(code)
    key(KEY_LEFTMETA, true)
    tap(code)
    key(KEY_LEFTMETA, false)
  end

  def double_click(fx, fy)
    click(fx, fy)
    click(fx, fy)
  end

  def shot(path)
    sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, "shot", path)
    Regress::Screens::Image.load(path)
  end

  def grim(label, output = @primary)
    sh!("docker", "exec", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "grim", "-o", output, "/run/fmrb/#{label}.png")
    path = File.join(@out, "#{label}.png")
    FileUtils.cp(File.join(@run, "#{label}.png"), path)
    Regress::Screens::Image.load(path)
  end

  def read_run(name) = File.exist?(File.join(@run, name)) ? File.read(File.join(@run, name)).force_encoding("UTF-8").scrub : ""
  def log_text = read_run("core.log")
  def client_log(which) = read_run("#{which}.log")

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.2
    end
  end

  def try_wait(timeout, &blk)
    wait_for("", timeout, &blk)
  rescue StandardError
    nil
  end

  def tell(which, line)
    File.open(File.join(@run, "#{which}.fifo"), File::WRONLY | File::NONBLOCK) { |f| f.write("#{line}\n") }
  rescue Errno::ENXIO
    raise "#{which} is not running (#{client_log(which).lines.last.to_s.strip})"
  end

  def client(which)
    fifo = File.join(@run, "#{which}.fifo")
    File.delete(fifo) if File.exist?(fifo)
    File.mkfifo(fifo)
    env = ["-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}"]
    sh!("docker", "exec", "-d", *env, @name, "/run/fmrb/bin/#{which}", "/run/fmrb/#{which}.fifo",
        "/run/fmrb/#{which}.log")
    wait_for("#{which} started", 20) { client_log(which).include?("started") || client_log(which)[/error.*/] }
    raise "#{which}: #{client_log(which)[/error.*/]}" if client_log(which).include?("error")
  end

  def new_window(before, timeout = 30)
    wait_for("a new window placed", timeout) do
      info["windows"].find { |x| !before.include?(x[:id]) && x[:vis] }
    end
  end

  def count(img, rgb)
    n = 0
    img.h.times { |y| img.w.times { |x| n += 1 if img.pixel(x, y) == rgb } }
    n
  end

  def frame_size(inf = @info) = inf["frame"].split("x").map(&:to_i)

  # The last window operation the log tells for a pid.
  def winop_lines = log_text.scan(/winops: PID (\d+) '([^']*)' (maximized|on the left half|on the right half|back): (\d+)x(\d+) at \((\d+),(\d+)\) \(([^)]*)\)/)

  def winop_count = winop_lines.size

  def wait_winop(n0, timeout = 10)
    try_wait(timeout) { winop_count > n0 && winop_lines.last }
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
    wait_for("the desktop", 60) { log_text.include?("Linux windows are Family mruby windows") }
    unless %w[wl_dialog wl_lock fake_logind].all? { |b| File.exist?(File.join(@run, "bin", b)) }
      raise "the clients did not build: #{read_run('bin/build.log').lines.last(8).join}"
    end
    @info = info
    @primary = log_text[/compositor: output (\S+) \d+x\d+, rotate/, 1]
    sleep 1.5
    fw, fh = frame_size
    work = [0, MENU_BAR_H, fw, fh - MENU_BAR_H]

    # ---- 1. maximize: the editor (Family mruby's window)
    click(30, 5)
    sleep 1.0
    click(30, MENU_BAR_H + MENU_ITEM_H + MENU_ITEM_H / 2)
    wait_for("the editor's window", 60) { shot(File.join(@out, "editor.png")).pixel(150, 20) == TITLE_RGB }
    sleep 1.0
    n0 = winop_count
    double_click(100, 20)
    l = wait_winop(n0)
    ed_pid = l && l[0].to_i
    maxed = l && l[2] == "maximized" && [l[5], l[6], l[3], l[4]].map(&:to_i) == work
    sleep 0.8
    pic = shot(File.join(@out, "editor_max.png"))
    title_wide = pic.pixel(fw - 40, MENU_BAR_H + 5) == pic.pixel(100, MENU_BAR_H + 5)
    n0 = winop_count
    double_click(100, MENU_BAR_H + 5)
    l2 = wait_winop(n0)
    back = l2 && l2[2] == "back" && l2[0].to_i == ed_pid
    ed_rect = l2 ? l2[3..6].map(&:to_i) : [240, 200, 5, 15]   # w, h, x, y
    result("a double-click maximizes the editor and puts it back",
           maxed && title_wide && back,
           "#{l ? "#{l[1]} #{l[2]} #{l[3]}x#{l[4]} at (#{l[5]},#{l[6]})" : 'no operation'} (the work area " \
           "#{work[2]}x#{work[3]} at (#{work[0]},#{work[1]})); its title bar across the screen #{title_wide}; " \
           "again: #{l2 ? "#{l2[2]} #{l2[3]}x#{l2[4]} at (#{l2[5]},#{l2[6]})" : 'nothing'}")

    # ---- 2 (Family mruby). Super + arrows on the editor
    click(ed_rect[2] + 40, ed_rect[3] + 40)
    sleep 0.5
    seq = []
    [[KEY_LEFT, "on the left half", [0, MENU_BAR_H, fw / 2, fh - MENU_BAR_H]],
     [KEY_RIGHT, "on the right half", [fw / 2, MENU_BAR_H, fw - fw / 2, fh - MENU_BAR_H]],
     [KEY_UP, "maximized", work], [KEY_DOWN, "back", [ed_rect[2], ed_rect[3], ed_rect[0], ed_rect[1]]]].each do |code, want, rect|
      n0 = winop_count
      super_tap(code)
      x = wait_winop(n0)
      got = x && [x[5], x[6], x[3], x[4]].map(&:to_i)
      ok = x && x[2] == want && x[0].to_i == ed_pid && got == rect
      seq << [want, ok ? true : false, x ? "#{x[3]}x#{x[4]} at (#{x[5]},#{x[6]})" : "none"]
      sleep 0.4
    end
    result("Super and the arrows lay the editor out", seq.all? { |s| s[1] },
           seq.map { |s| "#{s[0]}: #{s[1]} (#{s[2]})" }.join("; "))

    # ---- 1. maximize: foot's stand-in
    before = info["windows"].map { |w| w[:id] }
    sh!("docker", "exec", "-d", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "foot", "--app-id=footmax")
    foot = new_window(before)[:id]
    sleep 1.5
    f0 = win(info, foot)
    n0 = winop_count
    double_click(f0[:fx] + 30, f0[:fy] - 5)
    l = wait_winop(n0)
    sleep 1.5
    inf = info
    f1 = win(inf, foot)
    want_content = [1, MENU_BAR_H + FRAME_TOP, fw - 2, fh - MENU_BAR_H - 12]
    sc = inf["scale"].to_i
    fits = f1[:fx] == want_content[0] && f1[:fy] == want_content[1] && f1[:fw] == want_content[2] &&
           f1[:fh] == want_content[3] && f1[:w] <= f1[:fw] * sc && f1[:w] > f1[:fw] * sc - 30 &&
           f1[:h] <= f1[:fh] * sc && f1[:h] > f1[:fh] * sc - 40
    told = inf["state_of"][foot] == "m"
    n0 = winop_count
    double_click(f1[:fx] + 30, f1[:fy] - 5)
    l2 = wait_winop(n0)
    sleep 1.5
    f2 = win(info, foot)
    back = l2 && l2[2] == "back" && f2[:fx] == f0[:fx] && f2[:fy] == f0[:fy] && f2[:fw] == f0[:fw] &&
           f2[:fh] == f0[:fh] && info["state_of"][foot] == "-"
    result("a double-click maximizes foot (its stand-in) and puts it back", l && l[2] == "maximized" && fits && told && back,
           "content #{f0[:fw]}x#{f0[:fh]} at (#{f0[:fx]},#{f0[:fy]}) -> #{f1[:fw]}x#{f1[:fh]} at (#{f1[:fx]},#{f1[:fy]}) " \
           "(want #{want_content[2]}x#{want_content[3]} at (#{want_content[0]},#{want_content[1]})), foot drew " \
           "#{f1[:w]}x#{f1[:h]} of #{f1[:fw] * sc}x#{f1[:fh] * sc}; state told #{inf['state_of'][foot]}; again -> " \
           "#{f2[:fw]}x#{f2[:fh]} at (#{f2[:fx]},#{f2[:fy]}), state #{info['state_of'][foot]}")

    # ---- the client's own requests (wl_dialog)
    before = info["windows"].map { |w| w[:id] }
    client("wl_dialog")
    dlg = new_window(before)[:id]
    sleep 1.2
    d0 = win(info, dlg)
    lines0 = client_log("wl_dialog").lines.size
    tell("wl_dialog", "max")
    st = try_wait(10) { (i = info)["state_of"][dlg] == "m" && i }
    sleep 1.0
    cfg = client_log("wl_dialog").lines.drop(lines0)
    told_max = cfg.any? { |x| x.start_with?("states parent") && x.include?(" max") }
    size_max = cfg.grep(/^configure parent/).last.to_s.split.drop(2).map(&:to_i)
    want_sz = [(fw - 2) * @info["scale"].to_i, (fh - MENU_BAR_H - 12) * @info["scale"].to_i]
    lines1 = client_log("wl_dialog").lines.size
    tell("wl_dialog", "unmax")
    un = try_wait(10) { (i = info)["state_of"][dlg] == "-" && i }
    sleep 1.0
    d1 = win(info, dlg)
    cfg2 = client_log("wl_dialog").lines.drop(lines1)
    told_un = cfg2.any? { |x| x.start_with?("states parent") && !x.include?(" max") }
    result("the client's maximize request, and its unmaximize",
           st && told_max && size_max == want_sz && un && told_un && d1[:fw] == d0[:fw] && d1[:fh] == d0[:fh],
           "maximized #{st ? true : false} (configure #{size_max.join('x')}, want #{want_sz.join('x')}; maximized told " \
           "#{told_max}); unmaximized #{un ? true : false} (told #{told_un}), back to #{d1[:fw]}x#{d1[:fh]} " \
           "(was #{d0[:fw]}x#{d0[:fh]})")

    # ---- 2 (Linux window). Super + arrows on the client, arrows never reach it
    click(d1[:fx] + d1[:fw] / 2, d1[:fy] + d1[:fh] / 2)
    sleep 0.6
    keys0 = client_log("wl_dialog").lines.size
    seq = []
    [[KEY_LEFT, "l", [1, fw / 2 - 2]], [KEY_RIGHT, "r", [fw / 2 + 1, fw - fw / 2 - 2]], [KEY_UP, "m", [1, fw - 2]],
     [KEY_DOWN, "-", [d1[:fx], d1[:fw]]]].each do |code, want, (x, w)|
      super_tap(code)
      i = try_wait(10) { (q = info)["state_of"][dlg] == want && q }
      sleep 0.6
      q = win(info, dlg)
      ok = i && q[:fx] == x && q[:fw] == w
      seq << "#{want}: #{ok ? true : false} (content x #{q[:fx]} width #{q[:fw]}, want #{x} #{w})"
    end
    sleep 0.5
    got = client_log("wl_dialog").lines.drop(keys0)
    arrows = got.grep(/^key parent (\d+) 1/).map { |x| x[/\d+/].to_i }.select { |c| ARROWS.include?(c) }
    tiles = got.select { |x| x.start_with?("states parent") }
    tl = tiles.any? { |x| x.include?(" tl") }
    tr = tiles.any? { |x| x.include?(" tr") }
    result("Super and the arrows lay the Linux window out",
           seq.all? { |x| x.include?(": true") } && arrows.empty? && tl && tr,
           "#{seq.join('; ')}; the arrows that reached the window: #{arrows.inspect}; told tiled left #{tl}, right #{tr}")

    # ---- 3. the screen lock: logind's Lock
    tell("wl_dialog", "quit")
    try_wait(10) { win(info, dlg).nil? }
    before = info["windows"].map { |w| w[:id] }
    client("wl_dialog")
    dlg = new_window(before)[:id]
    sleep 1.0
    d = win(info, dlg)
    click(d[:fx] + d[:fw] / 2, d[:fy] + d[:fh] / 2)
    sleep 0.5
    tell("logind", "lock")
    locked = try_wait(15) { client_log("locker").include?("locked") && info["lock"] == "locked" }
    sleep 1.0
    img = grim("locked")
    mag = count(img, MAGENTA)
    st0 = stats
    k0 = client_log("wl_dialog").lines.size
    tap(KEY_A)
    click(100, 100)
    sleep 0.6
    st1 = stats
    lk_key = client_log("locker").include?("key #{KEY_A} 1")
    lk_btn = client_log("locker").include?("button 272 1")
    dlg_keys = client_log("wl_dialog").lines.drop(k0).grep(/^key /).size
    result("logind's Lock: the lock program covers the screen and takes the input",
           locked && mag == 1280 * 800 && st1["to_fmrb"] == st0["to_fmrb"] && lk_key && lk_btn && dlg_keys.zero?,
           "locked #{locked ? true : false} (#{log_text[/started the lock program[^\n]*/]}); #{mag} of #{1280 * 800} " \
           "pixels the lock's; Family mruby got #{st1['to_fmrb'] - st0['to_fmrb']} input events; the lock got the key " \
           "#{lk_key}, the click #{lk_btn}; the window got #{dlg_keys} keys")
    # The program dies: still locked.
    tell("locker", "die")
    dead = try_wait(10) { info["lock"] == "dead" }
    sleep 0.8
    img = grim("lock_dead")
    covered = img.pixel(640, 400)
    pic_px = count(img, GREEN)
    st2 = stats
    sleep 2.2
    tap(KEY_A)
    restarted = try_wait(15) { client_log("locker").scan("locked").size >= 2 && info["lock"] == "locked" }
    st3 = stats
    result("the lock program dying leaves the screen locked; a key starts it again",
           dead && pic_px.zero? && covered != GREEN && st3["to_fmrb"] == st2["to_fmrb"] && restarted,
           "dead #{dead ? true : false} (#{log_text[/the lock went away[^\n]*/] ? 'logged' : 'not logged'}); the cover " \
           "#{covered.inspect}, #{pic_px} of the window's pixels; Family mruby got #{st3['to_fmrb'] - st2['to_fmrb']} " \
           "events; started again and locked #{restarted ? true : false} (#{st3['lock_starts']} starts)")
    tell("locker", "unlock")
    unlocked = try_wait(10) { info["lock"] == "none" }
    sleep 1.0
    img = grim("unlocked")
    k1 = client_log("wl_dialog").lines.size
    d = win(info, dlg)
    click(d[:fx] + d[:fw] / 2, d[:fy] + d[:fh] / 2)
    sleep 0.4
    tap(KEY_A)
    sleep 0.6
    back_key = client_log("wl_dialog").lines.drop(k1).any? { |x| x.start_with?("key parent #{KEY_A} 1") }
    result("unlocked: the screen and the input are back", unlocked && count(img, GREEN).positive? && back_key,
           "unlocked #{unlocked ? true : false}; #{count(img, GREEN)} pixels of the window; a key reaches it #{back_key}")

    # PrepareForSleep: locked before the sleep, the inhibitor let go then.
    tell("locker", "quit") rescue nil
    sleep 0.5
    inhib0 = client_log("logind").scan("inhibitor released").size
    tell("logind", "sleep")
    slept = try_wait(15) { info["lock"] == "locked" && client_log("logind").scan("inhibitor released").size > inhib0 }
    released_after = slept && log_text.include?("the sleep may go on (the screen is locked)")
    taken0 = client_log("logind").scan("Inhibit sleep delay").size
    tell("logind", "wake")
    retaken = try_wait(10) { client_log("logind").scan("Inhibit sleep delay").size > taken0 }
    tell("locker", "unlock")
    try_wait(10) { info["lock"] == "none" }
    result("PrepareForSleep locks first, then lets the sleep go on; waking takes the inhibitor again",
           slept && released_after && retaken,
           "locked and let go #{slept ? true : false} (#{released_after ? 'once locked' : 'not after the lock'}); " \
           "the inhibitor taken again on waking #{retaken ? true : false}")

    # The power menu's Lock (the last entry).
    tell("locker", "quit") rescue nil
    sleep 1.0
    starts0 = stats["lock_starts"]
    click(fw - 11, 5)
    sleep 1.0
    click(fw - 71, MENU_BAR_H + 1 + 4 * MENU_ITEM_H + MENU_ITEM_H / 2)
    via_menu = try_wait(15) { info["lock"] == "locked" && stats["lock_starts"] > starts0 }
    result("the power menu's Lock", via_menu && log_text.include?("Desktop: lock the screen"),
           "locked #{via_menu ? true : false}; #{log_text[/started the lock program[^\n]*the power menu[^\n]*/] || 'not from the menu'}")
    tell("locker", "unlock")
    try_wait(10) { info["lock"] == "none" }
    sleep 0.8

    # ---- 3b. swaylock itself: it covers the screen; killed, the screen
    #      stays locked (it cannot be unlocked here: it asks for the
    #      password, so this is the end of the run)
    sh!("docker", "exec", "-d", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "sh", "-c", "swaylock -c 3366cc > /run/fmrb/swaylock.log 2>&1")
    sw = try_wait(15) { info["lock"] == "locked" }
    sleep 1.0
    img = grim("swaylock")
    blue = count(img, [0x33, 0x66, 0xcc])
    sh!("docker", "exec", @name, "pkill", "-9", "-x", "swaylock", ok: false)
    sw_dead = try_wait(10) { info["lock"] == "dead" }
    sleep 0.8
    img = grim("swaylock_killed")
    st0 = stats
    tap(KEY_A)
    st1 = stats
    result("swaylock covers the screen; killed, the screen stays locked",
           sw && blue > 1280 * 800 * 9 / 10 && sw_dead && count(img, GREEN).zero? && st1["to_fmrb"] == st0["to_fmrb"],
           "locked #{sw ? true : false}; #{blue} of #{1280 * 800} pixels swaylock's colour; killed: #{sw_dead ? 'dead' : info['lock']}, " \
           "the cover #{img.pixel(640, 400).inspect}; Family mruby got #{st1['to_fmrb'] - st0['to_fmrb']} events")

    tell("wl_dialog", "quit") rescue nil
    ctl("quit")
    wait_for("the container to end", 30) { !system("docker inspect #{@name} > /dev/null 2>&1") } rescue nil
  rescue StandardError => e
    result("run", false, "#{e.message.lines.first(3).join.strip} (#{e.backtrace&.first})")
  ensure
    %w[core.log wl_dialog.log locker.log logind.log dbus.log].each do |c|
      FileUtils.cp(File.join(@run, c), File.join(@out, c)) if File.exist?(File.join(@run, c))
    end
    FileUtils.cp(File.join(@run, "bin/build.log"), File.join(@out, "build.log")) if File.exist?(File.join(@run, "bin/build.log"))
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = WinopsRun.new(opts[:renderer], opts[:keep])
run.go
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-winops-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(run.results))
failed = run.results.reject { |r| r[:pass] }
puts "wl_winops_check #{ENGINE} #{opts[:renderer]}: #{run.results.length - failed.length}/#{run.results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
