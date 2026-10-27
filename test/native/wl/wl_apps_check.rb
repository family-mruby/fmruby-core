#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_apps_check.rb - the Linux PC's applications in the launcher, Xwayland
# and the end (doc/linux_native/, L9b), in the Ubuntu 26.04 image with the
# program built by rake native:wl, headless (WLR_BACKENDS=headless, a
# 1280x800 output by default, --output WxH: a 640x400 screen at 2x),
# --machine linux_pc_wl, no autostart.
#
#   ruby test/native/wl/wl_apps_check.rb [--engine standard|mruby]
#                                        [--renderer pixman|auto] [--keep]
#                                        [--output WxH]
#
# XDG_DATA_HOME is a directory of the run (the run directory, a host
# directory mounted at /run/fmrb), with .desktop entries of the check's own
# beside what the image has in /usr/share/applications (foot, xterm), and
# the user's system_conf seeded with language = "ja". Nothing is written
# outside the run directory and the container.
#
#   1. the list: the launcher offers the Linux group; NoDisplay=true is not
#      listed, Name[ja] is used, foot is listed with an icon (comp app); an
#      entry whose icon only Breeze has, as an SVG, gets it (L9e)
#   2. from the launcher (keys: the group, then the entry): foot starts, its
#      stand-in carries the list's name ("Foot", where foot's app id is
#      "foot"), and what the scale leaves between foot and its frame is the
#      frame's colour, not black
#   3. X11: before any X11 program, no Xwayland process and Xwayland "lazy";
#      xeyes from the list starts it, its window comes with a stand-in named
#      by the list (Name[ja]); an xterm from the list gets keys ("hello" to a
#      file) and the pointer (at the output's pixels), and resizes itself
#      (the xterm window-ops sequence): the stand-in fits it, no black
#   4. many entries: 300 more .desktop files; the launcher's open time with
#      and without them, the group's list read again (Reload) and its count
#   5. the end: a windowless program from the list (sleep), xeyes stopped
#      (SIGSTOP: it cannot answer), then Family mruby's Exit: the Wayland and
#      X11 windows are asked to close, xeyes is ended after the wait, the
#      sleep is ended, Xwayland ends, the program ends, and no process is
#      left in the container
#   6. no Xwayland (WLR_XWAYLAND naming no file): one line says X11 programs
#      cannot run, and foot (Wayland) still comes with its stand-in
#
# Shots, logs and a summary go to tmp/wl_check/apps-<engine>-<renderer>/.

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
# Linux key codes (input-event-codes.h).
KEYS = { "e" => 18, "h" => 35, "l" => 38, "o" => 24, "\n" => 28 }.freeze
KEY_RIGHT = 106
KEY_ENTER = 28
KEY_ESC = 1
KEY_F5 = 63
# The built-ins before the Linux group (shell, editor, the store).
BUILTINS = 3
BULK = 300

opts = { engine: "standard", renderer: "pixman", keep: false, output: "1280x800" }
OptionParser.new do |o|
  o.on("--engine E") { |v| opts[:engine] = v }
  o.on("--renderer R") { |v| opts[:renderer] = v }
  o.on("--keep") { opts[:keep] = true }
  o.on("--output WxH") { |v| opts[:output] = v }
end.parse!
abort "--engine standard|mruby" unless %w[standard mruby].include?(opts[:engine])
abort "--output WxH" unless opts[:output] =~ /\A\d+x\d+\z/
ENGINE = opts[:engine]
# The headless output's size: the screen is it over the scale, 2 (L9d).
OUTPUT = opts[:output]
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

class AppsRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-apps-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-apps-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "apps-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "apps #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} apps #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  # ------------------------------------------------------------ the run

  def desktop(file, text)
    File.write(File.join(@run, "data/applications", file), "[Desktop Entry]\nType=Application\n#{text}")
  end

  def prepare
    FileUtils.rm_rf(@run)
    FileUtils.mkdir_p(File.join(@run, "data/applications"))
    FileUtils.mkdir_p(File.join(@run, "data/family-mruby-dev/etc"))
    conf = File.read(File.join(CORE, "native/build/root/etc/system_conf.toml"))
    File.write(File.join(@run, "data/family-mruby-dev/etc/system_conf.toml"),
               conf.sub(/^language = "en"/, 'language = "ja"'))
    desktop("fmrb-hidden.desktop", "Name=Hidden Test\nExec=foot\nNoDisplay=true\n")
    desktop("fmrb-eyes.desktop", "Name=Eyes\nName[ja]=\u76EE\u7389\nExec=xeyes\nIcon=xterm\nStartupWMClass=XEyes\n")
    desktop("fmrb-xtyper.desktop", "Name=X Typer\nExec=xterm -xrm XTerm*allowWindowOps:true -e /run/fmrb/xterm.sh\n")
    desktop("fmrb-sleeper.desktop", "Name=Sleeper\nExec=sleep 1000\n")
    # An icon only the Breeze theme has, and only as SVG (L9e).
    desktop("fmrb-calc.desktop", "Name=Calc Icon\nExec=foot\nIcon=accessories-calculator\n")
    # The xterm resizes itself after a moment (12 rows, 50 columns: the
    # window-ops sequence), then writes what it is typed to a file.
    File.write(File.join(@run, "xterm.sh"), "#!/bin/sh\nsleep 3\nprintf '\\033[8;12;50t'\nexec cat > /run/fmrb/xtyped\n")
    File.chmod(0o755, File.join(@run, "xterm.sh"))
  end

  def start(env = {})
    sh!("docker", "rm", "-f", @name, ok: false)
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    env.each { |k, v| a += ["-e", "#{k}=#{v}"] }
    prog = "#{BIN} --machine linux_pc_wl --set display.mode=#{OUTPUT} #{env.key?('ARGS') ? env['ARGS'] : ''}"
    inner = "mkdir -p -m 700 /run/fmrb/xdg /tmp/home && #{prog} > /run/fmrb/core.log 2>&1; " \
            "echo \"exit=$?\" >> /run/fmrb/core.log; ps -e -o pid=,comm= > /run/fmrb/ps_after"
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
    pid, sx, sy = h["ptr"].to_s.split(/[:,]/)
    h["ptr"] = { id: pid.to_i, sx: sx.to_f, sy: sy.to_f }
    h["x11"] = h["x11"].to_s == "-" ? [] : h["x11"].to_s.split(",")
    h
  end

  def app(id)
    r = comp("app", id, "ja")
    { index: r[/index=(-?\d+)/, 1].to_i, name: r[/name=(.*) icon=/, 1], icon: r[/icon=(\S+)/, 1] }
  end

  def win(inf, id) = inf["windows"].find { |w| w[:id] == id }

  def to_out(fx, fy, inf = @info)
    s = inf["scale"].to_i
    x0, y0 = inf["at"].split(",").map(&:to_i)
    [x0 + fx * s + s / 2, y0 + fy * s + s / 2]
  end

  def click(fx, fy) = comp("click", *to_out(fx, fy))

  # The frame's size (640x400 on the default output, L9d), and where the
  # menu bar's power button, its Exit entry and the exit question's buttons
  # are on it (laid out from the right edge, and centred).
  def frame_size(inf = @info) = inf["frame"].split("x").map(&:to_i)
  def power_button = [frame_size[0] - 11, 5]
  def power_exit = [frame_size[0] - 71, 19]

  def question(button)
    w, h = frame_size
    [(button == :yes ? 182 : 243) + (w - 426) / 2, 143 + (h - 240) / 2]
  end
  def key(code, down) = comp("key", code, down ? 1 : 0)

  def tap(code)
    key(code, true)
    key(code, false)
  end

  def type(text) = text.each_char { |c| tap(KEYS.fetch(c)) }

  def shot(path)
    sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, "shot", path)
    Regress::Screens::Image.load(path)
  end

  def grim(path)
    sh!("docker", "exec", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "grim", "/run/fmrb/#{File.basename(path)}")
    FileUtils.cp(File.join(@run, File.basename(path)), path)
    Regress::Screens::Image.load(path)
  end

  def log_text = File.exist?(File.join(@run, "core.log")) ? File.read(File.join(@run, "core.log")).force_encoding("UTF-8").scrub : ""

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.2
    end
  end

  def ps
    out, = sh!("docker", "exec", @name, "ps", "-e", "-o", "pid=,comm=,args=")
    out.lines.map(&:strip)
  end

  # ------------------------------------------------- the launcher by keys

  def open_launcher
    click(30, 5)
    sleep 0.8
    click(30, MENU_BAR_H + MENU_ITEM_H / 2)
  end

  # The launcher's rectangle (launcher.rb: 300x190, centred, 8 below the bar).
  LAUNCHER = [63, 21, 300, 190].freeze

  def frame_seq = ctl("info").split[3].to_i

  # From a key or click (the block) to the first new frame showing the
  # launcher's rectangle changed against `before`, ms. Frames are polled by
  # their number (cheap), then the frame is looked at.
  def time_to_frame(label, before, least)
    seq = frame_seq
    t0 = now
    yield
    loop do
      raise "#{label}: no new frame in 10 s" if now - t0 > 10
      cur = frame_seq
      next sleep(0.002) if cur == seq
      t = ((now - t0) * 1000).round
      i = shot(File.join(@out, "#{label}.png"))
      count, = i.diff(before, [], rect: LAUNCHER)
      return t if count > least
      seq = cur
    end
  end

  # The time from the menu's item to the launcher on screen, ms.
  def time_open_launcher(label)
    click(30, 5)
    sleep 0.8
    before = shot(File.join(@out, "#{label}_before.png"))
    ms = time_to_frame(label, before, 20_000) { click(30, MENU_BAR_H + MENU_ITEM_H / 2) }
    tap(KEY_ESC)
    sleep 0.6
    ms
  end

  # The time from Enter on the group to its list on screen, ms (the
  # launcher left closed).
  def time_enter_linux(label)
    open_launcher
    sleep 1.0
    BUILTINS.times { tap(KEY_RIGHT) }
    sleep 0.5
    before = shot(File.join(@out, "#{label}_before.png"))
    ms = time_to_frame(label, before, 2_000) { tap(KEY_ENTER) }
    tap(KEY_ESC)
    sleep 0.6
    ms
  end

  def lists_shown = log_text.scan(/Launcher: Linux: (\d+) apps/).flatten.map(&:to_i)

  # The launcher open, the group entered: returns the list's size.
  def enter_linux
    n0 = lists_shown.size
    open_launcher
    sleep 1.0
    BUILTINS.times { tap(KEY_RIGHT) }
    tap(KEY_ENTER)
    wait_for("the Linux list", 15) { lists_shown.size > n0 && lists_shown.last }
  end

  # An entry started from the launcher by its id.
  def launch(id)
    idx = app(id)[:index]
    raise "#{id} is not listed" if idx.negative?
    enter_linux
    sleep 0.5
    (idx + 1).times { tap(KEY_RIGHT) }
    tap(KEY_ENTER)
  end

  def new_window(before, timeout = 30)
    wait_for("a new window placed", timeout) do
      w = info["windows"].find { |x| !before.include?(x[:id]) && x[:vis] }
      w
    end
  end

  # What lies between the window and its stand-in's content (output pixels)
  # on the composited screen: [pixels, black ones, a colour seen there].
  def strip(img, w, inf)
    s = inf["scale"].to_i
    x1 = w[:x] + w[:fw] * s
    y1 = w[:y] + w[:fh] * s
    n = 0
    black = 0
    colour = nil
    (w[:y]...y1).each do |y|
      (w[:x]...x1).each do |x|
        next if x < w[:x] + w[:w] && y < w[:y] + w[:h]
        px = img.pixel(x, y)
        n += 1
        black += 1 if px == [0, 0, 0]
        colour ||= px
      end
    end
    [n, black, colour]
  end

  # ------------------------------------------------------------ checks

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    prepare
    start
    wait_for("Boot complete", 120) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    @socket = log_text[/WAYLAND_DISPLAY=(wayland-\d+)/, 1]
    wait_for("the desktop", 60) { log_text.include?("Linux windows are Family mruby windows") }
    @info = info
    sleep 1.5

    # 1. the list
    apps = comp("apps", "ja")
    count = apps[/count=(\d+)/, 1].to_i
    foot = app("foot.desktop")
    eyes = app("fmrb-eyes.desktop")
    hidden = app("fmrb-hidden.desktop")
    offered = log_text.include?("Launcher: the Linux group is offered")
    result("the list", offered && apps.include?("ready=1") && hidden[:index] == -1 && eyes[:name] == "\u76EE\u7389" &&
           foot[:index] >= 0 && foot[:name] == "Foot" && foot[:icon].start_with?("/var/cache/linux/"),
           "the group offered: #{offered}; #{count} entries (#{log_text[/linux apps: \d+ entries[^\n]*/]}); " \
           "NoDisplay=true listed: #{hidden[:index] >= 0}; Name[ja]: #{eyes[:name]}; foot at #{foot[:index]} " \
           "named #{foot[:name]} with icon #{foot[:icon]}")

    # 1b. a Breeze SVG icon (L9e): no theme set, so Plasma's default
    #     (breeze, then hicolor); the SVG made a 24x24 icon with pixels in it.
    calc = app("fmrb-calc.desktop")
    themes = log_text[/linux apps: icon themes ([^\n]*)/, 1].to_s
    bmp = calc[:icon].to_s.start_with?("/var/cache/linux/") ? calc[:icon].sub("/var/cache/linux/", "") : nil
    drawn = 0
    if bmp
      out, = sh!("docker", "exec", @name, "bash", "-c",
                 "f=$(find / -xdev -name #{bmp} 2>/dev/null | head -1); od -An -v -tu1 -j 1078 \"$f\"", ok: false)
      drawn = out.split.count { |v| v.to_i != 0 }
    end
    result("a Breeze SVG icon", calc[:index] >= 0 && !bmp.nil? && drawn > 24 && themes.start_with?("breeze > hicolor") &&
           themes =~ /[1-9]\d* SVGs? converted/,
           "Icon=accessories-calculator (an SVG of Breeze's alone): #{calc[:icon]}, #{drawn} of 576 pixels drawn; " \
           "icon themes #{themes}")

    # 2. foot from the launcher
    t_open = 3.times.map { |i| time_open_launcher("open#{i}") }.sort[1]
    before = info["windows"].map { |w| w[:id] }
    n = enter_linux
    shot(File.join(@out, "linux_list.png"))
    tap(KEY_ESC)
    sleep 0.6
    launch("foot.desktop")
    fw = new_window(before)
    @info = info
    named = log_text[/wl_bridge: window #{fw[:id]} \(([^ ]+) \d+x\d+\): stand-in/, 1]
    sleep 1.0
    g = grim(File.join(@out, "foot.png"))
    sn, sblack, scol = strip(g, win(info, fw[:id]), @info)
    result("foot from the launcher", n == count && named == "Foot" && log_text.include?('started "Foot"') && sblack.zero?,
           "the Linux list shown with #{n} entries; foot started (#{log_text[/linux apps: started "Foot"[^\n]*/]}); " \
           "its stand-in is named #{named}; foot #{fw[:w]}x#{fw[:h]} in a content of #{fw[:fw]}x#{fw[:fh]} at " \
           "#{@info['scale']}x: #{sn} output pixels between them, #{sblack} black, colour #{scol.inspect}")

    # 3. X11
    before_ps = ps
    xw_before = info["xwayland"]
    no_xw = before_ps.none? { |l| l.include?("Xwayland") }
    before = info["windows"].map { |w| w[:id] }
    launch("fmrb-eyes.desktop")
    ew = new_window(before)
    wait_for("Xwayland started", 20) { log_text.include?("Xwayland started") }
    inf = info
    xw_up = ps.any? { |l| l.include?("Xwayland") }
    ename = log_text[/wl_bridge: window #{ew[:id]} \(([^ ]+) \d+x\d+\): stand-in/, 1]
    result("Xwayland only on demand", no_xw && xw_before == "lazy" && xw_up && inf["xwayland"] == "up" &&
           inf["x11"].include?(ew[:id].to_s) && ename == "\u76EE\u7389",
           "before: no Xwayland process #{no_xw}, xwayland=#{xw_before}; after xeyes from the list: Xwayland " \
           "running #{xw_up}, xwayland=#{inf['xwayland']}, X11 windows #{inf['x11'].join(',')}; xeyes " \
           "#{ew[:w]}x#{ew[:h]} with a stand-in named #{ename} (#{log_text[/window #{ew[:id]} "[^"]*" \d+x\d+ \(X11[^)]*\)/]})")

    before = info["windows"].map { |w| w[:id] }
    launch("fmrb-xtyper.desktop")
    xt = new_window(before)
    xname = log_text[/wl_bridge: window #{xt[:id]} \(([^)]+?) \d+x\d+\): stand-in/, 1]
    first = win(info, xt[:id])
    asked = wait_for("the xterm's own size", 20) { log_text[/window #{xt[:id]} \(X11\) asked to be (\d+)x(\d+)/] && [$1.to_i, $2.to_i] }
    sc = @info["scale"].to_i
    fitted = wait_for("its stand-in fitted", 10) do
      w = win(info, xt[:id])
      w[:fw] == (asked[0] + sc - 1) / sc && w[:fh] == (asked[1] + sc - 1) / sc && w[:w] == w[:fw] * sc &&
        w[:h] == w[:fh] * sc && w
    end rescue win(info, xt[:id])
    sleep 0.8
    @info = info
    g = grim(File.join(@out, "xterm_fitted.png"))
    xn, xblack, = strip(g, win(@info, xt[:id]), @info)
    ok_fit = fitted[:fw] == (asked[0] + sc - 1) / sc && fitted[:fh] == (asked[1] + sc - 1) / sc
    result("an X11 program resizing itself", ok_fit && xblack.zero?,
           "xterm #{first[:w]}x#{first[:h]} (content #{first[:fw]}x#{first[:fh]}) asked for #{asked.join('x')}; " \
           "the stand-in's content is now #{fitted[:fw]}x#{fitted[:fh]}, the window configured " \
           "#{fitted[:w]}x#{fitted[:h]}; #{xn} output pixels between them, #{xblack} black")

    # Keys and the pointer to the xterm (its window's middle).
    w = win(info, xt[:id])
    cx = w[:fx] + w[:fw] / 2
    cy = w[:fy] + w[:fh] / 2
    click(cx, cy)
    sleep 0.6
    type("hello\n")
    typed = wait_for("the xterm's file", 10) { (t = File.read(File.join(@run, "xtyped")) rescue "") == "hello\n" && t } rescue (File.read(File.join(@run, "xtyped")) rescue "")
    ox, oy = to_out(cx, cy)
    seen = [0, 1, 2].map do |d|
      comp("move", ox - 1 + d, oy)
      info["ptr"]
    end
    fine = seen.all? { |p| p[:id] == xt[:id] } && seen.each_cons(2).all? { |p, q| (q[:sx] - p[:sx] - 1.0).abs < 0.01 }
    result("keys and the pointer to an X11 program", typed == "hello\n" && fine && info["focus"] == xt[:id].to_s &&
           xname == "X Typer",
           "its stand-in named #{xname} (the started entry's, not debian-xterm's); typed #{typed.inspect}; " \
           "keyboard on #{info['focus']}; pointer moves of one output pixel reach it at x " \
           "#{seen.map { |p| p[:sx] }.join(', ')}")

    # A windowless program from the list, for the end.
    launch("fmrb-sleeper.desktop")
    wait_for("sleep started", 10) { ps.any? { |l| l.include?("sleep 1000") } }

    # 4. many entries: the launcher's open time and the group's list shown
    #    (Enter to the grid on screen), with the windows above on screen,
    #    before and after 300 more entries have been read (Reload).
    t_open_few = 3.times.map { |i| time_open_launcher("open_few#{i}") }.sort[1]
    t_enter_few = 3.times.map { |i| time_enter_linux("enter_few#{i}") }.sort[1]
    BULK.times do |i|
      desktop(format("fmrb-bulk-%03d.desktop", i), "Name=Bulk #{format('%03d', i)}\nExec=sleep 1\nIcon=foot\n")
    end
    n0 = lists_shown.size
    open_launcher
    sleep 1.0
    BUILTINS.times { tap(KEY_RIGHT) }
    tap(KEY_ENTER)
    wait_for("the Linux list", 15) { lists_shown.size > n0 }
    t0 = now
    tap(KEY_F5)
    n1 = lists_shown.size
    many = wait_for("the list read again", 30) { lists_shown.size > n1 && lists_shown.last }
    t_reload = ((now - t0) * 1000).round
    read = log_text.scan(/linux apps: (\d+) entries of (\d+) desktop files \(\w+\), (\d+) with icons; read in (\d+) ms, icons in (\d+) ms/).last
    sleep 0.5
    shot(File.join(@out, "linux_list_many.png"))
    tap(KEY_ESC)
    sleep 0.6
    t_open_many = 3.times.map { |i| time_open_launcher("open_many#{i}") }.sort[1]
    t_enter_many = 3.times.map { |i| time_enter_linux("enter_many#{i}") }.sort[1]
    result("many entries", many == count + BULK && t_open_many < t_open_few + 50 && t_enter_many < t_enter_few + 200,
           "launcher open #{t_open} ms (no Linux window), with the windows #{t_open_few} ms with #{count} entries " \
           "and #{t_open_many} ms with #{many}; the Linux list shown #{t_enter_few} ms / #{t_enter_many} ms " \
           "(medians of 3, from the click or Enter to the first frame showing it); Reload to #{many} entries " \
           "#{t_reload} ms (read #{read&.at(3)} ms, icons #{read&.at(4)} ms, #{read&.at(2)} with icons)")

    # 5. the end, with xeyes stopped (it cannot answer the request to close)
    eyes_pid = ps.find { |l| l.split[1] == "xeyes" }.to_s.split.first
    sh!("docker", "exec", @name, "kill", "-STOP", eyes_pid) if eyes_pid
    click(*power_button)
    sleep 1.0
    click(*power_exit)
    sleep 1.2
    t_exit = now
    click(*question(:yes))
    gone = begin
      wait_for("the container to end", 40) { !system("docker inspect #{@name} > /dev/null 2>&1") }
      true
    rescue StandardError
      false
    end
    took = (now - t_exit).round(1)
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log"))
    lines = log_text.lines.grep(/compositor: (asked \d+ window|every window|windows still|SIGTERM|SIGKILL|ended \d+ process)/)
                    .map(&:strip)
    left = File.exist?(File.join(@run, "ps_after")) ? File.read(File.join(@run, "ps_after")).lines.map(&:strip) : ["?"]
    left = left.reject { |l| l =~ /\A\d+ (docker-init|bash|ps)\z/ }
    exited = log_text[/^exit=(\d+)/, 1]
    asked = log_text[/asked (\d+) windows? to close/, 1].to_i
    sleep_ended = log_text.include?("ended 1 process started from the launcher")
    result("the end", gone && exited == "0" && left.empty? && asked == 3 && sleep_ended && took < 15 &&
           lines.any? { |l| l.include?("SIGTERM") } && !eyes_pid.nil?,
           "Exit to the program's end #{took} s; #{lines.join(' / ')}; exit=#{exited}; processes left: " \
           "#{left.empty? ? 'none' : left.join(', ')}")

    # 6. no Xwayland
    prepare
    start("WLR_XWAYLAND" => "/nonexistent/Xwayland", "ARGS" => "--set compositor.autostart=foot")
    wait_for("Boot complete", 120) { log_text.include?("Boot complete") }
    line = wait_for("the line", 10) { log_text[/native: compositor: X11 programs cannot run here[^\n]*/] }
    footw = wait_for("foot placed", 30) { info["windows"].find { |x| x[:vis] } }
    inf = info
    ctl("quit")
    wait_for("the container to end", 30) { !system("docker inspect #{@name} > /dev/null 2>&1") } rescue nil
    result("no Xwayland", line.include?("not installed") && footw && inf["xwayland"] == "off",
           "#{line}; foot #{footw[:w]}x#{footw[:h]} placed with its stand-in; xwayland=#{inf['xwayland']}")
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
  ensure
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = AppsRun.new(opts[:renderer], opts[:keep])
run.go
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-apps-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(run.results))
failed = run.results.reject { |r| r[:pass] }
puts "wl_apps_check #{ENGINE} #{opts[:renderer]}: #{run.results.length - failed.length}/#{run.results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
