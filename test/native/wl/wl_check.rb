#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_check.rb - the compositor (doc/linux_native/, L8, L9a) in the Ubuntu
# 26.04 image, where wlroots 0.19 is: the program built by rake native:wl
# (native/build-wl), --machine linux_pc_wl, with foot started on it.
#
#   ruby test/native/wl/wl_check.rb             headless (WLR_BACKENDS=headless,
#                                               a 1280x720 output), the renderer
#                                               wlroots picks and pixman
#   ruby test/native/wl/wl_check.rb --nested    the same, nested in WSLg's Wayland
#                                               (a window opens on the desktop for
#                                               the run and closes after it; the
#                                               desktop's own pointer moves the
#                                               same cursor, so keep the mouse off
#                                               the window or the drags go astray)
#   ruby test/native/wl/wl_check.rb --run [-- ARGS...]
#                                               nested in WSLg for a person to use,
#                                               in the foreground until the window
#                                               or Family mruby's Exit closes it;
#                                               ARGS go to the program (default
#                                               --set compositor.autostart=foot)
#   options: --renderer auto|pixman|gles2 (one only; auto is wlroots' own
#            choice), --engine standard|mruby (native/build-wl/cmake or
#            cmake-mruby), --keep (leave the container)
#
# Per renderer ("auto": what wlroots picks -- pixman in a WSL2 container,
# which has no GPU Mesa can use; WLR_RENDERER=pixman named outright). foot
# runs `cat > typed`, so what it was typed can be read back. Linux windows
# are Family mruby windows (L9a): each has a stand-in app whose window has
# the Family mruby frame and whose content is a hole the compositor shows
# the Linux window through.
#
#   1. boot: "Boot complete", foot's window mapped with a server-side
#      decoration, its stand-in started, the renderer used
#   2. the frame: the composited screen (grim) against the picture Family
#      mruby made (its own "shot") scaled and placed: every pixel equal
#      except the holes (the stand-in's content where it is on screen), which
#      show foot, not the hole colour; the stand-in's title bar over foot
#   3. move and resize: the title bar dragged, the grip dragged; foot follows
#      to the place times the scale and draws itself at the new size (to its
#      cell grid), and the stand-in fits what foot took (L9b)
#   4. keys and the pointer to foot: a click on its window, "hello" and
#      Enter, then symbols on the jp layout (the windows' keymap follows
#      keyboard_layout); pointer moves of one output pixel reach foot one
#      pixel apart (finer than Family mruby's pixel); Family mruby's picture
#      unchanged
#   5. keys to Family mruby: the editor from the menu, one "a", the editor's
#      window against the native baseline editor_type.png; foot got nothing
#   6. stacking: the editor over foot (only the uncovered part of foot shows),
#      a click on foot brings it in front (all of it shows); the desktop's
#      menu and the exit question over foot; each a grim against the picture
#   7. Ctrl+Tab with foot focused switches Family mruby's windows (the key is
#      Family mruby's, not foot's)
#   8. timings: CPU and the picture's update gaps, idle, with an app
#      animating (stackchan) and with one holding the CPU (spin_hog); 30 keys
#      to foot in each case and the time from each key to foot's next commit
#   9. the close button: a second foot closes by it and its stand-in ends
#  10. the end: a third foot that ignores the request to close (stopped),
#      then Family mruby's Exit from the power menu: the windows are asked
#      to close, the stopped one is ended within seconds, the program ends,
#      and no process is left in the container
#
# The run directory is a host directory mounted into the container
# (/run/fmrb): the control socket, the inject socket, the log, the
# compositor's Wayland socket (xdg/), and the shots. The debugger's port is
# published on the host's loopback (DEBUG_PORT) for tools/fmrb_native.rb app.
# Shots, logs and a summary go to tmp/wl_check/<engine>-<mode>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
BASELINE = File.join(CORE, "test/regress/screens/baseline/native")
WSLG = "/mnt/wslg/runtime-dir/wayland-0"
# The debugger's port in the container (5565) as the host sees it, apart
# from a native program running on the host.
DEBUG_PORT = 15_565
RENDER_NODE = "/dev/dri/renderD128"
TITLE_RGB = [222, 36, 82]
# RGB332 0x01, the stand-in's content (a hole), as the shot shows it.
HOLE_RGB = [0, 0, 82].freeze
MENU_BAR_H = 13
MENU_ITEM_H = 12
# A window's frame around its content (FmrbApp's user area).
FRAME_TOP = 11
# Linux key codes (input-event-codes.h).
KEYS = { "a" => 30, "e" => 18, "h" => 35, "l" => 38, "o" => 24, "\n" => 28 }.freeze
KEY_LEFTSHIFT = 42
KEY_LEFTCTRL = 29
KEY_TAB = 15
KEY_2 = 3
KEY_7 = 8
KEY_LEFTBRACE = 26
# Shift+2, the key right of P, Shift+7 on a JIS keyboard.
JP_SYMBOLS = "\"@'"

opts = { mode: :headless, renderers: %w[auto pixman], keep: false, engine: "standard" }
args = ARGV.dup
pass = (i = args.index("--")) ? args.slice!(i..)[1..] : nil
OptionParser.new do |o|
  o.on("--nested") { opts[:mode] = :nested }
  o.on("--run") { opts[:mode] = :run }
  o.on("--renderer R") { |v| opts[:renderers] = [v] }
  o.on("--engine E") { |v| opts[:engine] = v }
  o.on("--keep") { opts[:keep] = true }
end.parse!(args)
abort "--engine standard|mruby" unless %w[standard mruby].include?(opts[:engine])
BIN = "native/build-wl/#{opts[:engine] == 'mruby' ? 'cmake-mruby' : 'cmake'}/fmruby-native"
ENGINE = opts[:engine]

def image_name
  file = File.join(CORE, "native/docker/Dockerfile.ubuntu")
  from = File.read(file)[/^FROM\s+ubuntu:(\S+)/, 1]
  "fmrb-native-build:#{from}-#{Digest::SHA256.file(file).hexdigest[0, 8]}"
end

IMAGE = image_name
abort "#{File.join(CORE, BIN)} not found; rake native:wl#{ENGINE == 'mruby' ? ' NATIVE_ENGINE=mruby' : ''}" unless File.exist?(File.join(CORE, BIN))
abort "image #{IMAGE} not found; rake native:wl builds it" unless system("docker image inspect #{IMAGE} > /dev/null 2>&1")
if opts[:mode] != :headless && !File.socket?(WSLG)
  abort "#{WSLG} is not there: --nested and --run need WSLg"
end

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def sh!(*cmd, ok: true)
  out, st = Open3.capture2e(*cmd)
  raise "#{cmd.join(' ')}:\n#{out.lines.last(15).join}" if ok && !st.success?
  [out, st]
end

# The container's command line: the program on the compositor machine, its
# log in the run directory. For the checks the program is not the shell's
# last command: after it ends, the processes left in the container are
# listed (ps_after).
def docker_args(name, run, mode, renderer, program_args, detach: true)
  a = ["docker", "run", "--rm", "--init", "--name", name, "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
       "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{run}:/run/fmrb",
       "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
       "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb"]
  a << "-d" if detach
  if mode == :headless
    a += ["-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
  else
    a += ["-v", "/mnt/wslg:/mnt/wslg", "-e", "WAYLAND_DISPLAY=#{WSLG}"]
    a += mode == :run ? ["-e", "PULSE_SERVER=unix:/mnt/wslg/PulseServer"] : ["-e", "SDL_AUDIODRIVER=dummy"]
  end
  a += ["-e", "WLR_RENDERER=#{renderer}"] if renderer != "auto"
  if renderer == "gles2" && File.exist?(RENDER_NODE)
    # GLES needs a DRM render node, and on a machine without a GPU driver
    # for Mesa it runs on the software rasteriser, which wlroots only
    # accepts when told to. (WSL2's node is vgem: GLES starts, but its
    # buffers cannot be allocated -- see doc/linux_native/report/l8.md.)
    a += ["--device", RENDER_NODE, "--group-add", File.stat(RENDER_NODE).gid.to_s,
          "-e", "WLR_RENDERER_ALLOW_SOFTWARE=1"]
  end
  a << "-t" if !detach && $stdin.tty?
  log = detach ? " > /run/fmrb/core.log 2>&1" : " 2>&1 | tee /run/fmrb/core.log"
  prog = "#{BIN} --machine linux_pc_wl " + program_args.map { |x| "'" + x.gsub("'") { %q('\\'') } + "'" }.join(" ")
  inner = if detach
            "mkdir -p -m 700 /run/fmrb/xdg /tmp/home && #{prog}#{log}; " \
              "echo \"exit=$?\" >> /run/fmrb/core.log; ps -e -o pid=,comm= > /run/fmrb/ps_after"
          else
            "mkdir -p -m 700 /run/fmrb/xdg /tmp/home && #{prog}#{log}"
          end
  a + [IMAGE, "bash", "-c", inner]
end

# ------------------------------------------------------------------ --run

if opts[:mode] == :run
  run = "/tmp/fmrb-wl-run-#{Process.uid}"
  FileUtils.rm_rf(run)
  FileUtils.mkdir_p(run)
  prog = pass && !pass.empty? ? pass : ["--set", "compositor.autostart=foot"]
  puts "wl_check --run (#{ENGINE}): #{prog.join(' ')} (run dir #{run}; Family mruby's Exit or closing the window ends it)"
  system(*docker_args("fmrb-wl-run", run, :run, opts[:renderers].size == 1 ? opts[:renderers][0] : "auto",
                      prog, detach: false))
  exit($?.exitstatus || 0)
end

# ------------------------------------------------------------- the checks

class Run
  attr_reader :results

  def initialize(mode, renderer, keep)
    @mode = mode
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-check-#{ENGINE}-#{mode}-#{renderer}"
    @run = "/tmp/fmrb-wl-#{ENGINE}-#{mode}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "#{ENGINE}-#{mode}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "#{ENGINE}/#{@mode}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} #{ENGINE}/#{@mode}/#{@renderer} #{name}: #{detail}"
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
    data
  end

  def comp(*words)
    r = ctl("comp #{words.join(' ')}")
    raise "comp #{words.join(' ')}: #{r}" unless r.start_with?("OK")
    r
  end

  # comp info: windows bottom to top, each with its output place, its
  # content rectangle in frame coordinates and whether it shows.
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
    h
  end

  def win(inf, id) = inf["windows"].find { |w| w[:id] == id }

  def stats(reset: false)
    r = comp(reset ? "stats reset" : "stats")
    r.split.drop(1).to_h { |kv| k, v = kv.split("=", 2); [k, v.include?(".") ? v.to_f : v.to_i] }
  end

  # Frame coordinates to the output's (the middle of the scaled pixel).
  def to_out(fx, fy, inf = @info)
    s = inf["scale"].to_i
    x0, y0 = inf["at"].split(",").map(&:to_i)
    [x0 + fx * s + s / 2, y0 + fy * s + s / 2]
  end

  def click(fx, fy)
    comp("click", *to_out(fx, fy))
  end

  # A press at one frame point, the pointer moved in steps, the release.
  def drag(fx, fy, dx, dy, steps: 6)
    comp("move", *to_out(fx, fy))
    comp("button", 1, 1)
    steps.times do |i|
      comp("move", *to_out(fx + dx * (i + 1) / steps, fy + dy * (i + 1) / steps))
      sleep 0.08
    end
    comp("button", 1, 0)
  end

  def key(code, down) = comp("key", code, down ? 1 : 0)

  def tap(code, shift: false, ctrl: false)
    key(KEY_LEFTSHIFT, true) if shift
    key(KEY_LEFTCTRL, true) if ctrl
    key(code, true)
    key(code, false)
    key(KEY_LEFTCTRL, false) if ctrl
    key(KEY_LEFTSHIFT, false) if shift
  end

  def type(text)
    text.each_char { |c| tap(KEYS.fetch(c)) }
  end

  def shot(path)
    sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, "shot", path)
    Regress::Screens::Image.load(path)
  end

  def grim(path)
    sh!("docker", "exec", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "grim", "/run/fmrb/#{File.basename(path)}")
    FileUtils.cp(File.join(@run, File.basename(path)), path) unless File.dirname(path) == @run
    Regress::Screens::Image.load(path)
  end

  def frame_masks(img) = [[img.w - 90, 0, 90, MENU_BAR_H], [img.w - 6, 60, 6, 16]]

  def typed = File.exist?(File.join(@run, "typed")) ? File.read(File.join(@run, "typed")) : ""

  # The frame pixels the windows show through: the hole colour inside a
  # shown window's content rectangle.
  def holes(pic, inf)
    map = Array.new(pic.h) { nil }
    n = 0
    inf["windows"].each do |w|
      next unless w[:vis]
      ([w[:fy], 0].max...[w[:fy] + w[:fh], pic.h].min).each do |fy|
        row = pic.rows[fy]
        ([w[:fx], 0].max...[w[:fx] + w[:fw], pic.w].min).each do |fx|
          o = fx * 3
          next unless row.getbyte(o) == HOLE_RGB[0] && row.getbyte(o + 1) == HOLE_RGB[1] &&
                      row.getbyte(o + 2) == HOLE_RGB[2]
          map[fy] ||= Array.new(pic.w, false)
          next if map[fy][fx]
          map[fy][fx] = true
          n += 1
        end
      end
    end
    [map, n]
  end

  # A hole pixel of window w near the middle of its content (a point to click
  # foot at), in frame coordinates.
  def hole_point(pic, w)
    cx = w[:fx] + w[:fw] / 2
    cy = w[:fy] + w[:fh] / 2
    best = nil
    (w[:fy]...w[:fy] + w[:fh]).each do |fy|
      (w[:fx]...w[:fx] + w[:fw]).each do |fx|
        next unless pic.pixel(fx, fy) == HOLE_RGB
        d = (fx - cx).abs + (fy - cy).abs
        best = [fx, fy, d] if best.nil? || d < best[2]
      end
    end
    best && best[0, 2]
  end

  # The composited screen against the picture: scaled, placed, black
  # around, the windows through the holes. Returns [ok, detail, holes].
  def composite_check(label, inf = info)
    3.times do
      a = shot(File.join(@out, "#{label}_picture.png"))
      g = grim(File.join(@out, "#{label}_composite.png"))
      b = shot(File.join(@out, "#{label}_picture2.png"))
      next unless a.same?(b, frame_masks(a))
      ow, oh = inf["out"].split("x").map(&:to_i)
      s = inf["scale"].to_i
      x0, y0 = inf["at"].split(",").map(&:to_i)
      map, nholes = holes(a, inf)
      masks = Regress::Screens::Image.mask_rows(a.w, a.h, frame_masks(a))
      differ = 0
      box = nil
      through = 0
      hole_coloured = 0
      return [false, "grim is #{g.w}x#{g.h}, the output #{ow}x#{oh}", nholes] if g.w != ow || g.h != oh
      oh.times do |y|
        grow = g.rows[y]
        fy = y >= y0 ? (y - y0) / s : -1
        prow = fy >= 0 && fy < a.h ? a.rows[fy] : nil
        hrow = prow ? map[fy] : nil
        mrow = prow ? masks[fy] : nil
        ow.times do |x|
          o = x * 3
          fx = x >= x0 ? (x - x0) / s : -1
          if prow.nil? || fx >= a.w || fx.negative?
            exp = [0, 0, 0]
          else
            next if mrow && mrow[fx]
            if hrow && hrow[fx]
              through += 1
              hole_coloured += 1 if grow.getbyte(o) == HOLE_RGB[0] && grow.getbyte(o + 1) == HOLE_RGB[1] &&
                                    grow.getbyte(o + 2) == HOLE_RGB[2]
              next
            end
            exp = [prow.getbyte(fx * 3), prow.getbyte(fx * 3 + 1), prow.getbyte(fx * 3 + 2)]
          end
          next if grow.getbyte(o) == exp[0] && grow.getbyte(o + 1) == exp[1] && grow.getbyte(o + 2) == exp[2]
          differ += 1
          box = box ? [[box[0], x].min, [box[1], y].min, [box[2], x].max, [box[3], y].max] : [x, y, x, y]
        end
      end
      ok = differ.zero? && hole_coloured.zero?
      detail = "#{differ} pixels differ from the picture outside the holes#{box ? " (box #{box.inspect})" : ''}; " \
               "#{nholes} frame pixels are holes (#{through} output pixels through them, " \
               "#{hole_coloured} of them still the hole colour)"
      return [ok, detail, nholes]
    end
    [false, "the picture kept changing", 0]
  end

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.3
    end
  end

  def log_text = File.exist?(File.join(@run, "core.log")) ? File.read(File.join(@run, "core.log")) : ""

  def cpu_ticks(pid)
    out, = sh!("docker", "exec", @name, "cat", "/proc/#{pid}/stat")
    f = out.sub(/\A.*\) /m, "").split
    f[11].to_i + f[12].to_i   # utime + stime (fields 14 and 15)
  end

  def ps
    out, = sh!("docker", "exec", @name, "ps", "-e", "-o", "pid=,stat=,args=")
    out.lines.map(&:strip)
  end

  # Another foot, started in the container beside the program (its app id
  # tells it apart). Returns the new window's id once its stand-in places it.
  def start_foot(app_id)
    before = info["windows"].map { |w| w[:id] }
    sh!("docker", "exec", "-d", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "foot", "--app-id=#{app_id}")
    wait_for("#{app_id}'s window placed", 30) do
      w = info["windows"].find { |x| !before.include?(x[:id]) && x[:vis] }
      w && w[:id]
    end
  end

  # CPU (% of one core), the picture's gaps and 30 keys to foot over `secs`.
  def measure(label, pid, secs)
    stats(reset: true)
    t0 = now
    c0 = cpu_ticks(pid)
    30.times do
      type("a")
      sleep(secs / 30.0)
    end
    type("\n")
    sleep 0.3
    c1 = cpu_ticks(pid)
    wall = now - t0
    st = stats
    cpu = ((c1 - c0) / 100.0) / wall * 100
    m = { label: label, cpu_percent: cpu.round(1), wall_s: wall.round(1), pictures: st["pictures"],
          gap_mean_ms: st["gap_mean_ms"], gap_max_ms: st["gap_max_ms"], commits: st["commits"],
          keys: st["keys"], key_commit_n: st["key_commit_n"], key_commit_mean_ms: st["key_commit_mean_ms"],
          key_commit_max_ms: st["key_commit_max_ms"] }
    File.write(File.join(@out, "measure-#{label}.json"), JSON.pretty_generate(m))
    m
  end

  def native_tool(*args, timeout: 30)
    cmd = ["timeout", timeout.to_s, "ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, *args.map(&:to_s)]
    sh!({ "FMRB_DEBUG_TCP_PORT" => DEBUG_PORT.to_s }, *cmd, ok: false)
  end

  # Click foot where it shows (a hole of its window).
  def click_foot(id)
    inf = info
    w = win(inf, id)
    pic = shot(File.join(@out, "foot_click.png"))
    pt = hole_point(pic, w) or raise "no part of window #{id} shows"
    click(*pt)
    sleep 0.6
    pt
  end

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    sh!("docker", "rm", "-f", @name, ok: false)
    FileUtils.rm_rf(@run)
    FileUtils.mkdir_p(@run)

    # 1. boot, with foot running `cat > typed`
    t0 = now
    sh!(*docker_args(@name, @run, @mode, @renderer,
                     ["--set", "compositor.autostart=foot -e sh -c 'cat > /run/fmrb/typed'"]))
    wait_for("Boot complete", 120) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    # The stand-in carries the launcher entry's name (foot.desktop: "Foot", L9b).
    wait_for("foot's stand-in", 60) { log_text[/wl_bridge: window \d+ \(Foot .*stand-in pid \d+/] }
    @socket = log_text[/WAYLAND_DISPLAY=(wayland-\d+)/, 1]
    used = log_text[/renderer (\w+)\)/, 1]
    @info = wait_for("foot placed", 20) { i = info; i["windows"].any? { |w| w[:vis] } && i }
    boot_s = (now - t0).round(1)
    @pid = ctl("info").split[4].to_i
    foot = @info["windows"].first
    @foot = foot[:id]
    deco = log_text[/window #{@foot} "foot" \d+x\d+ \(decoration: ([^;)]+)/, 1]
    result("boot", (@renderer == "auto" || used == @renderer) && deco == "server side",
           "Boot complete and foot placed in #{boot_s} s; renderer #{used}; output #{@info['out']}, " \
           "picture #{@info['frame']} at #{@info['scale']}x at (#{@info['at']}); foot #{foot[:w]}x#{foot[:h]} " \
           "at (#{foot[:x]},#{foot[:y]}), its content #{foot[:fw]}x#{foot[:fh]} at (#{foot[:fx]},#{foot[:fy]}) in " \
           "the frame; decoration: #{deco}")

    # The pointer where the native baselines have it (frame 420,60).
    comp("move", *to_out(420, 60))
    sleep 1.5

    # 2. the frame: Family mruby's title bar and border over foot, foot
    #    through the hole, every other pixel the picture's.
    ok, detail, nholes = composite_check("frame", @info)
    pic = Regress::Screens::Image.load(File.join(@out, "frame_picture.png"))
    title_ok = pic.pixel(foot[:fx] + foot[:fw] / 2, foot[:fy] - FRAME_TOP + 1) == TITLE_RGB
    full = foot[:fw] * foot[:fh]
    result("frame", ok && title_ok && nholes > full * 9 / 10,
           "#{detail}; the stand-in's title bar over foot: #{title_ok}; holes #{nholes} of the content's #{full}")

    # 3. move and resize. The grip (the window's bottom-right corner) first:
    #    content 232x165 -> 168x128, foot takes its cell grid's size of that
    #    and the stand-in fits it (the content: foot's size over the scale,
    #    rounded up); then the title bar, to the right.
    wx = foot[:fx] - 1
    wy = foot[:fy] - FRAME_TOP
    ww = foot[:fw] + 2
    wh = foot[:fh] + 12
    sc = @info["scale"].to_i
    fits = ->(w) { w[:fw] == (w[:w] + sc - 1) / sc && w[:fh] == (w[:h] + sc - 1) / sc }
    drag(wx + ww - 4, wy + wh - 4, 168 - foot[:fw], 128 - foot[:fh])
    sized = begin
      wait_for("foot resized and fitted", 10) do
        w = win(info, @foot)
        (w[:w] - 168 * sc).abs < 24 && (w[:h] - 128 * sc).abs < 40 && fits.call(w) && w
      end
    rescue StandardError
      win(info, @foot)
    end
    @area = sized[:fw] * sized[:fh]
    result("resize by the frame", (sized[:w] - 504).abs < 24 && (sized[:h] - 384).abs < 40 && fits.call(sized),
           "content #{foot[:fw]}x#{foot[:fh]} -> 168x128 dragged (asked 504x384 of foot); " \
           "foot draws itself #{foot[:w]}x#{foot[:h]} -> #{sized[:w]}x#{sized[:h]} (its cell grid); " \
           "the stand-in fitted it: content #{sized[:fw]}x#{sized[:fh]}")
    drag(wx + 40, wy + 5, 250 - wx, 48 - wy)
    moved = begin
      wait_for("foot moved", 10) { w = win(info, @foot); w[:fx] == 251 && w[:fy] == 59 && w }
    rescue StandardError
      win(info, @foot)
    end
    @info = info
    s = @info["scale"].to_i
    x0, y0 = @info["at"].split(",").map(&:to_i)
    result("move by the title bar", moved[:x] == x0 + 251 * s && moved[:y] == y0 + 59 * s,
           "content at (#{sized[:fx]},#{sized[:fy]}) -> (#{moved[:fx]},#{moved[:fy]}); foot on the output at " \
           "(#{moved[:x]},#{moved[:y]})")
    ok, detail, = composite_check("moved")
    result("frame after move and resize", ok, detail)

    # 4. keys and the pointer to foot (Family mruby's cursor parked where the
    #    masks are first: over foot it is hidden)
    comp("move", *to_out(420, 60))
    sleep 0.5
    before = shot(File.join(@out, "before_foot_keys.png"))
    pt = click_foot(@foot)
    type("hello\n")
    tap(KEY_2, shift: true)
    tap(KEY_LEFTBRACE)
    tap(KEY_7, shift: true)
    type("\n")
    want = "hello\n#{JP_SYMBOLS}\n"
    got = wait_for("foot's output", 10) { typed == want && typed } rescue typed
    after = shot(File.join(@out, "after_foot_keys.png"))
    unchanged = after.same?(before, frame_masks(after) + [[moved[:fx], moved[:fy], moved[:fw], moved[:fh]]])
    inf = info
    result("keys to foot", got == want && inf["focus"] == @foot.to_s && unchanged && inf["keymap"] == "jp",
           "typed #{got.inspect} (keymap #{inf['keymap']}: Shift+2, the key right of P, Shift+7 are #{JP_SYMBOLS}); " \
           "keyboard on window #{inf['focus']}; Family mruby's picture unchanged outside foot: #{unchanged}")
    ox, oy = to_out(*pt)
    seen = [0, 1, 2].map do |d|
      comp("move", ox - 1 + d, oy)
      info["ptr"]
    end
    fine = seen.all? { |p| p[:id] == @foot } && seen.each_cons(2).all? { |p, q| (q[:sx] - p[:sx] - 1.0).abs < 0.01 }
    result("pointer to foot at the output's pixels", fine && info["cursor"] == "window",
           "moves of one output pixel inside one frame pixel reach foot at x #{seen.map { |p| p[:sx] }.join(', ')}; " \
           "Family mruby's cursor hidden over foot: #{info['cursor'] == 'window'}")

    # 5. keys to Family mruby: its menu (the click takes the keyboard back),
    #    the editor, one key, against the native baseline (the editor's
    #    window; foot is beside it).
    click(30, 5)
    sleep 1.2
    focus_back = info["focus"] == "0"
    click(30, MENU_BAR_H + MENU_ITEM_H + MENU_ITEM_H / 2)
    wait_for("the editor's window", 60) { shot(File.join(@out, "editor.png")).pixel(150, 20) == TITLE_RGB }
    comp("move", *to_out(420, 60))
    sleep 1.5
    ed_before = shot(File.join(@out, "editor_before.png"))
    type("a")
    wait_for("the key on the screen", 20) do
      i = shot(File.join(@out, "editor_type.png"))
      !i.same?(ed_before, frame_masks(i)) && i
    end
    sleep 1.5
    img = shot(File.join(@out, "editor_type.png"))
    base = Regress::Screens::Image.load(File.join(BASELINE, "editor_type.png"))
    # The baseline is the standard configuration's (the Spinel editor); the
    # mruby editor's menu bar has one more entry (Debug), so that row is left
    # out for it.
    ed_masks = frame_masks(img)
    ed_masks += [[5, 25, 240, 10]] if ENGINE == "mruby"
    count, box = img.diff(base, ed_masks, rect: [5, 15, 240, 200])
    result("keys to Family mruby", focus_back && count.zero? && typed == want,
           "keyboard back on Family mruby after the click on its menu: #{focus_back}; the editor's window vs " \
           "editor_type: #{count} differing pixels#{box ? " (box #{box.inspect})" : ''}; foot got nothing more: " \
           "#{typed == want}")

    # 6. stacking. foot dragged half over the editor (the press on its title
    #    bar brings it in front), then the editor clicked in front of it.
    drag(250 + 40, 48 + 5, -100, 0)
    wait_for("foot moved over the editor", 10) { win(info, @foot)[:fx] == 151 }
    click(20, 120)
    sleep 1.2
    inf = info
    ok, detail, partly = composite_check("editor_over_foot", inf)
    result("the editor over foot", ok && partly < @area * 7 / 10 && inf["focus"] == "0",
           "#{detail}; keyboard on #{inf['focus']} (Family mruby)")
    click_foot(@foot)
    inf = info
    ok, detail, all = composite_check("foot_over_editor", inf)
    result("a click brings foot in front", ok && all > partly && all >= @area - 100 && inf["focus"] == @foot.to_s,
           "#{detail} (#{partly} while covered); keyboard on window #{inf['focus']}")
    # The desktop's menu and the exit question, foot under them.
    drag(151 + 40, 48 + 5, -110, -8)
    wait_for("foot under the menu", 10) { win(info, @foot)[:fx] == 41 }
    click(30, 5)
    sleep 1.2
    ok, detail, under_menu = composite_check("menu_over_foot")
    result("the desktop's menu over foot", ok && under_menu < all, "#{detail} (#{all} without it)")
    click(300, 220)
    sleep 1.0
    click(415, 5)
    sleep 1.0
    click(355, 19)
    sleep 1.2
    ok, detail, under_q = composite_check("question_over_foot")
    result("the exit question over foot", ok && under_q < all, "#{detail} (#{all} without it)")
    click(243, 143)    # "No"
    sleep 1.0
    # Back beside the editor, so that the editor in front leaves it visible.
    drag(41 + 40, 40 + 5, 209, 8)
    wait_for("foot beside the editor", 10) { win(info, @foot)[:fx] == 250 }

    # 7. Ctrl+Tab with foot focused: Family mruby's, not foot's.
    click_foot(@foot)
    f0 = info["focus"]
    tap(KEY_TAB, ctrl: true)
    switched = wait_for("Ctrl+Tab", 5) { info["focus"] != @foot.to_s } rescue false
    result("Ctrl+Tab is Family mruby's", f0 == @foot.to_s && switched && typed == want,
           "focus on window #{f0}, after Ctrl+Tab on #{info['focus']}; foot got nothing: #{typed == want}")

    # 8. timings: idle; with an app animating on its own (stackchan, about 25
    #    pictures a second); with one holding the CPU (spin_hog, 20 s
    #    without yielding). foot has the keyboard for the 30 keys of each
    #    (clicked again after an app starts, since the new app takes it).
    click_foot(@foot)
    # (The animating app is a windowed one: a fullscreen app such as shooter
    # takes the whole screen, Linux windows included.)
    loads = [["idle", nil], ["animating", "/app/demo/stackchan.app.rb"], ["spinning", "/app/test/spin_hog.app.rb"]]
    spawned = {}
    ms = loads.map do |label, app|
      if app
        out, st = native_tool("app", "spawn", app)
        raise "app spawn #{app}: #{out.strip}" unless st.success? && out.include?("pid")
        spawned[label] = out[/"pid":\s*(\d+)/, 1]
        sleep 2
        click_foot(@foot)
      end
      measure(label, @pid, 6)
    end
    grim(File.join(@out, "composite_busy.png"))
    ms.each do |m|
      pictures_ok = m[:label] != "animating" || (m[:pictures] >= 6 * 10 && m[:gap_max_ms] < 500)
      result("timing #{m[:label]}", m[:key_commit_n] >= 25 && m[:key_commit_max_ms] < 200 && pictures_ok,
             "CPU #{m[:cpu_percent]}% of a core; #{m[:pictures]} pictures in #{m[:wall_s]} s " \
             "(gap mean #{m[:gap_mean_ms]} ms, max #{m[:gap_max_ms]} ms), #{m[:commits]} output commits; " \
             "#{m[:keys]} keys to foot, key to foot's commit mean #{m[:key_commit_mean_ms]} ms, max #{m[:key_commit_max_ms]} ms")
    end
    final_want = want + ("a" * 30 + "\n") * loads.size
    result("foot got every key", typed == final_want,
           "foot's file #{typed.bytesize} bytes, #{typed == final_want ? 'as typed' : "NOT as typed: #{typed.inspect}"}")
    # spin_hog spins 20 s at a time with a second's rest between: ended in
    # one of the rests, and the animating app with it.
    wait_for("spin_hog ended", 90) do
      native_tool("app", "kill", spawned["spinning"], timeout: 25)
      log_text =~ /Spin Hog gen=\d+\] Reaped/
    end
    native_tool("app", "kill", spawned["animating"])

    # 9. the close button of a second foot
    second = start_foot("closeme")
    w2 = win(info, second)
    pid2 = log_text[/wl_bridge: window #{second} \(closeme .*stand-in pid (\d+)/, 1]
    click(w2[:fx] - 1 + w2[:fw] + 2 - 6, w2[:fy] - FRAME_TOP + 5)
    gone = wait_for("closeme gone", 10) { info["windows"].none? { |w| w[:id] == second } } rescue false
    ended = wait_for("its stand-in ended", 10) { log_text.include?("stand-in pid #{pid2} ended") } rescue false
    left = ps.grep(/--app-id=closeme/)
    result("the close button", gone && ended && left.empty?,
           "window #{second} asked to close and gone: #{gone}; stand-in pid #{pid2} ended: #{ended}; " \
           "foot processes left: #{left.size}")

    # 10. the end: a foot that ignores the request to close (stopped), then
    #     Family mruby's Exit from the power menu.
    stuck = start_foot("stuck")
    stuck_pid = ps.grep(/--app-id=stuck/).first.to_s.split.first
    sh!("docker", "exec", @name, "kill", "-STOP", stuck_pid)
    click(415, 5)
    sleep 1.0
    click(355, 19)
    sleep 1.2
    grim(File.join(@out, "exit_question.png"))
    t_exit = now
    click(182, 143)    # "Yes"
    gone = begin
      wait_for("the container to end", 30) { !system("docker inspect #{@name} > /dev/null 2>&1") }
      true
    rescue StandardError
      false
    end
    took = (now - t_exit).round(1)
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log"))
    lines = log_text.lines.grep(/compositor: (asked \d+ window|every window|windows still|SIGTERM|SIGKILL|"foot)/)
                    .map(&:strip)
    left = File.exist?(File.join(@run, "ps_after")) ? File.read(File.join(@run, "ps_after")).lines.map(&:strip) : ["?"]
    left = left.reject { |l| l =~ /\A\d+ (docker-init|bash|ps)\z/ }
    exited = log_text[/^exit=(\d+)/, 1]
    killed = lines.any? { |l| l.include?("SIGKILL") || l.include?("SIGTERM") }
    result("the end", gone && exited == "0" && killed && left.empty? && took < 15,
           "Exit to the program's end #{took} s; #{lines.join(' / ')}; exit=#{exited}; processes left: " \
           "#{left.empty? ? 'none' : left.join(', ')}")
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
  ensure
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

all = []
opts[:renderers].each do |r|
  run = Run.new(opts[:mode], r, opts[:keep])
  run.go
  all.concat(run.results)
end
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-#{ENGINE}-#{opts[:mode]}.json"), JSON.pretty_generate(all))
failed = all.reject { |r| r[:pass] }
puts "wl_check #{ENGINE} #{opts[:mode]}: #{all.length - failed.length}/#{all.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
