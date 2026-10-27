#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_check.rb - the compositor (doc/linux_native/, L8) in the Ubuntu 26.04
# image, where wlroots 0.19 is: the program built by rake native:wl
# (native/build-wl), --machine linux_pc_wl, with foot started on it.
#
#   ruby test/native/wl/wl_check.rb             headless (WLR_BACKENDS=headless,
#                                               a 1280x720 output), the renderer
#                                               wlroots picks and pixman
#   ruby test/native/wl/wl_check.rb --nested    the same, nested in WSLg's Wayland
#                                               (a window opens on the desktop for
#                                               the run and closes after it)
#   ruby test/native/wl/wl_check.rb --run [-- ARGS...]
#                                               nested in WSLg for a person to use,
#                                               in the foreground until the window
#                                               or Family mruby's Exit closes it;
#                                               ARGS go to the program (default
#                                               --set compositor.autostart=foot)
#   options: --renderer auto|pixman|gles2 (one only; auto is wlroots' own
#            choice), --keep (leave the container)
#
# Per renderer ("auto": what wlroots picks -- pixman in a WSL2 container,
# which has no GPU Mesa can use; WLR_RENDERER=pixman named outright):
#
#   1. boot: "Boot complete", foot's window mapped, the renderer used
#   2. the composited screen (grim through wlr-screencopy) against the
#      picture Family mruby made (its own "shot") scaled by the compositor's
#      scale and placed where it says, black around: outside foot's window
#      every pixel equal (clock and pointer masked); inside it, foot at its
#      own size and not the picture
#   3. keys to foot: a click on its window, then "hello" and Enter through
#      the compositor's seat; foot runs `cat > typed`, so the file must hold
#      "hello\n" -- and Family mruby's picture must not have changed
#   4. keys to Family mruby: a click on the picture (its menu), the editor
#      from the menu, one "a"; against the native baseline editor_type.png
#   5. timings: CPU of the program and the picture's update gaps, idle,
#      with an app animating (shooter) and with one holding the CPU
#      (spin_hog); 30 keys to foot in each case and the time from each key
#      to foot's next commit
#   6. quit: the program ends, and foot with it
#
# The run directory is a host directory mounted into the container
# (/run/fmrb): the control socket, the inject socket, the log, the
# compositor's Wayland socket (xdg/), and the shots. The debugger's port is
# published on the host's loopback (DEBUG_PORT) for tools/fmrb_native.rb app.
# Shots, logs and a summary go to tmp/wl_check/<mode>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
BIN = "native/build-wl/cmake/fmruby-native"
BASELINE = File.join(CORE, "test/regress/screens/baseline/native")
WSLG = "/mnt/wslg/runtime-dir/wayland-0"
# The debugger's port in the container (5565) as the host sees it, apart
# from a native program running on the host.
DEBUG_PORT = 15_565
RENDER_NODE = "/dev/dri/renderD128"
TITLE_RGB = [222, 36, 82]
MENU_BAR_H = 13
MENU_ITEM_H = 12
# Linux key codes (input-event-codes.h) of the letters typed here.
KEYS = { "a" => 30, "e" => 18, "h" => 35, "l" => 38, "o" => 24, "\n" => 28 }.freeze

opts = { mode: :headless, renderers: %w[auto pixman], keep: false }
args = ARGV.dup
pass = (i = args.index("--")) ? args.slice!(i..)[1..] : nil
OptionParser.new do |o|
  o.on("--nested") { opts[:mode] = :nested }
  o.on("--run") { opts[:mode] = :run }
  o.on("--renderer R") { |v| opts[:renderers] = [v] }
  o.on("--keep") { opts[:keep] = true }
end.parse!(args)

def image_name
  file = File.join(CORE, "native/docker/Dockerfile.ubuntu")
  from = File.read(file)[/^FROM\s+ubuntu:(\S+)/, 1]
  "fmrb-native-build:#{from}-#{Digest::SHA256.file(file).hexdigest[0, 8]}"
end

IMAGE = image_name
abort "#{File.join(CORE, BIN)} not found; rake native:wl" unless File.exist?(File.join(CORE, BIN))
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
# log in the run directory.
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
  inner = "mkdir -p -m 700 /run/fmrb/xdg /tmp/home && exec #{BIN} --machine linux_pc_wl " +
          program_args.map { |x| "'" + x.gsub("'") { %q('\\'') } + "'" }.join(" ") + log
  inner = inner.sub("exec ", "") unless detach
  a + [IMAGE, "bash", "-c", inner]
end

# ------------------------------------------------------------------ --run

if opts[:mode] == :run
  run = "/tmp/fmrb-wl-run-#{Process.uid}"
  FileUtils.rm_rf(run)
  FileUtils.mkdir_p(run)
  prog = pass && !pass.empty? ? pass : ["--set", "compositor.autostart=foot"]
  puts "wl_check --run: #{prog.join(' ')} (run dir #{run}; Family mruby's Exit or closing the window ends it)"
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
    @name = "fmrb-wl-check-#{mode}-#{renderer}"
    @run = "/tmp/fmrb-wl-#{mode}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "#{mode}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "#{@mode}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} #{@mode}/#{@renderer} #{name}: #{detail}"
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

  def info
    r = comp("info")
    h = r.split.drop(1).to_h { |kv| kv.split("=", 2) }
    h["windows"] = h["windows"].to_s.scan(/(\d+):(\d+)x(\d+)\+(-?\d+)\+(-?\d+)/).map do |id, ww, wh, x, y|
      { id: id.to_i, w: ww.to_i, h: wh.to_i, x: x.to_i, y: y.to_i }
    end
    h
  end

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

  def type(text)
    text.each_char do |c|
      code = KEYS.fetch(c)
      comp("key", code, 1)
      comp("key", code, 0)
    end
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

  # The picture as the compositor should show it: scaled, placed, black around.
  def expected_composite(pic, inf)
    ow, oh = inf["out"].split("x").map(&:to_i)
    s = inf["scale"].to_i
    x0, y0 = inf["at"].split(",").map(&:to_i)
    black = "\0\0\0".b
    rows = Array.new(oh) do |y|
      fy = (y - y0) / s
      next black * ow if y < y0 || fy >= pic.h
      src = pic.rows[fy]
      line = +"".b
      line << black * x0
      pic.w.times { |fx| line << src.byteslice(fx * 3, 3) * s }
      line << black * (ow - x0 - pic.w * s) if ow > x0 + pic.w * s
      line.byteslice(0, ow * 3)
    end
    Regress::Screens::Image.new(ow, oh, rows)
  end

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.5
    end
  end

  def log_text = File.exist?(File.join(@run, "core.log")) ? File.read(File.join(@run, "core.log")) : ""

  def cpu_ticks(pid)
    out, = sh!("docker", "exec", @name, "cat", "/proc/#{pid}/stat")
    f = out.sub(/\A.*\) /m, "").split
    f[11].to_i + f[12].to_i   # utime + stime (fields 14 and 15)
  end

  # CPU (% of one core), the picture's gaps and 30 keys to foot over `secs`.
  def measure(label, pid, secs)
    stats(reset: true)
    t0 = now
    c0 = cpu_ticks(pid)
    keys_at = []
    30.times do
      keys_at << now
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
    wait_for("foot's window", 60) { log_text[/compositor: window \d+ "foot"/] }
    boot_s = (now - t0).round(1)
    @socket = log_text[/WAYLAND_DISPLAY=(wayland-\d+)/, 1]
    used = log_text[/renderer (\w+)\)/, 1]
    @info = info
    @pid = ctl("info").split[4].to_i
    win = @info["windows"].first
    result("boot", !win.nil? && (@renderer == "auto" || used == @renderer),
           "Boot complete and foot mapped in #{boot_s} s; renderer #{used}; output #{@info['out']}, " \
           "picture #{@info['frame']} at #{@info['scale']}x at (#{@info['at']}), foot #{win[:w]}x#{win[:h]} at (#{win[:x]},#{win[:y]})")

    # The pointer where the native baselines have it (frame 420,60).
    comp("move", *to_out(420, 60))
    sleep 2

    # 2. the composited screen against the picture
    ok = false
    detail = ""
    3.times do
      a = shot(File.join(@out, "picture.png"))
      g = grim(File.join(@out, "composite.png"))
      b = shot(File.join(@out, "picture2.png"))
      next unless a.same?(b, frame_masks(a))
      exp = expected_composite(a, @info)
      exp.write(File.join(@out, "expected.png"))
      s = @info["scale"].to_i
      x0, y0 = @info["at"].split(",").map(&:to_i)
      masks = frame_masks(a).map { |mx, my, mw, mh| [x0 + mx * s, y0 + my * s, mw * s, mh * s] }
      wrect = [win[:x], win[:y], win[:w], win[:h]]
      outside, box = g.diff(exp, masks + [wrect])
      inside, = g.diff(exp, [], rect: wrect)
      area = win[:w] * win[:h]
      ok = g.w == exp.w && g.h == exp.h && outside.zero? && inside > area / 2
      detail = "grim #{g.w}x#{g.h}: #{outside} pixels differ outside foot's window#{box ? " (box #{box.inspect})" : ''}; " \
               "inside it #{inside} of #{area} differ from the picture (foot at #{win[:w]}x#{win[:h]}, its own size)"
      break
    end
    result("composite vs the picture", ok, detail.empty? ? "the picture kept changing" : detail)

    # 3. keys to foot
    before = shot(File.join(@out, "before_foot_keys.png"))
    comp("click", win[:x] + win[:w] / 2, win[:y] + win[:h] / 2)
    type("hello\n")
    typed = wait_for("foot's output", 10) { File.exist?(File.join(@run, "typed")) && File.read(File.join(@run, "typed")).include?("\n") && File.read(File.join(@run, "typed")) }
    after = shot(File.join(@out, "after_foot_keys.png"))
    unchanged = after.same?(before, frame_masks(after))
    result("keys to foot", typed == "hello\n" && info["focus"] == win[:id].to_s && unchanged,
           "typed #{typed.inspect}; keyboard on window #{info['focus']}; Family mruby's picture unchanged: #{unchanged}")

    # 4. keys to Family mruby: its menu (a click on the picture takes the
    #    keyboard back), the editor, one key.
    comp("click", *to_out(30, 5))
    sleep 1.5
    focus_back = info["focus"] == "0"
    comp("click", *to_out(30, MENU_BAR_H + MENU_ITEM_H + MENU_ITEM_H / 2))
    wait_for("the editor's window", 60) { shot(File.join(@out, "editor.png")).pixel(150, 20) == TITLE_RGB }
    comp("move", *to_out(420, 60))
    sleep 1.5
    ed_before = shot(File.join(@out, "editor_before.png"))
    type("a")
    img = wait_for("the key on the screen", 20) do
      i = shot(File.join(@out, "editor_type.png"))
      !i.same?(ed_before, frame_masks(i)) && i
    end
    sleep 1.5
    img = shot(File.join(@out, "editor_type.png"))
    base = Regress::Screens::Image.load(File.join(BASELINE, "editor_type.png"))
    count, box = img.diff(base, frame_masks(img))
    typed_after = File.read(File.join(@run, "typed"))
    result("keys to Family mruby", focus_back && count.zero? && typed_after == "hello\n",
           "keyboard back on Family mruby after the click on its menu: #{focus_back}; editor_type vs baseline: " \
           "#{count} differing pixels#{box ? " (box #{box.inspect})" : ''}; foot got nothing more: #{typed_after == "hello\n"}")
    grim(File.join(@out, "composite_editor.png"))

    # 5. timings: idle; with an app animating on its own (shooter, about 20
    #    pictures a second); with one holding the CPU (spin_hog, 20 s
    #    without yielding). foot has the keyboard for the 30 keys of each.
    comp("click", win[:x] + win[:w] / 2, win[:y] + win[:h] / 2)
    loads = [["idle", nil], ["animating", "/app/game/shooter.app.rb"], ["spinning", "/app/test/spin_hog.app.rb"]]
    ms = loads.map do |label, app|
      if app
        out, st = sh!({ "FMRB_DEBUG_TCP_PORT" => DEBUG_PORT.to_s }, "ruby", File.join(TOOLS, "fmrb_native.rb"),
                      "--run-dir", @run, "app", "spawn", app, ok: false)
        raise "app spawn #{app}: #{out.strip}" unless st.success? && out.include?("pid")
        sleep 2
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
    typed_final = File.read(File.join(@run, "typed"))
    want = "hello\n" + ("a" * 30 + "\n") * loads.size
    result("foot got every key", typed_final == want,
           "foot's file #{typed_final.bytesize} bytes, #{typed_final == want ? 'as typed' : 'NOT as typed'}")

    # 6. quit: the program and foot end.
    ctl("quit")
    gone = begin
      wait_for("the container to end", 20) { !system("docker inspect #{@name} > /dev/null 2>&1") }
      true
    rescue StandardError
      false
    end
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log"))
    ended = log_text[/compositor: "foot[^\n]*" (stopped|killed) with the program/]
    result("quit", gone && !ended.nil?, "the container ended: #{gone}; #{ended || 'no line about foot stopping'}")
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
File.write(File.join(out, "summary-#{opts[:mode]}.json"), JSON.pretty_generate(all))
failed = all.reject { |r| r[:pass] }
puts "wl_check #{opts[:mode]}: #{all.length - failed.length}/#{all.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
