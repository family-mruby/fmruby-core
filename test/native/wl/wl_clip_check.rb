#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_clip_check.rb - the machine's clipboard as the compositor's selection
# (doc/linux_native/, L12a), in the Ubuntu 26.04 image with the program
# built by rake native:wl, headless (WLR_BACKENDS=headless, a 1280x800
# output: a 640x400 screen at 2x), --machine linux_pc_wl.
#
#   ruby test/native/wl/wl_clip_check.rb [--engine standard|mruby]
#                                        [--renderer pixman|auto] [--keep]
#
# foot is started on it running a script that prints one line of Japanese
# and then copies what it is given (`cat > typed`). XDG_DATA_HOME is a
# directory of the run, so Family mruby's /home is there. Nothing is
# written outside the run directory and the container.
#
#   1. editor -> foot: the Shell's `edit /home/src.txt`, Ctrl+A and Ctrl+C
#      in the editor (the compositor owns the selection), then foot focused
#      and its paste key (Ctrl+Shift+V) and Enter: foot got the editor's
#      text
#   2. foot -> editor: foot's line selected by a drag and foot's copy key
#      (Ctrl+Shift+C): the compositor reads it; the editor focused, Ctrl+A,
#      Ctrl+V, Ctrl+S: the file holds foot's line
#   3. the Shell: Ctrl+V at the prompt and Enter runs the pasted line
#      ("Command: <text>" in the log); a Linux program started with "!"
#      gets the text from Ctrl+Shift+V (`!cat > shell_typed`)
#   4. X11 through Xwayland: an X11 program takes CLIPBOARD, the Shell's
#      paste gets its text; an app sets the clipboard, the X11 program asks
#      CLIPBOARD and gets it
#   5. foot exits: the selection is kept (the compositor takes it over), and
#      the counters (comp clip) agree
#
# Shots, logs and a summary go to tmp/wl_check/clip-<engine>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
DEBUG_PORT = 15_567
TITLE_RGB = [222, 36, 82].freeze
# Linux key codes (input-event-codes.h), US positions; the punctuation used
# here sits on the same keys on a JIS keyboard.
KEYS = { "a" => 30, "b" => 48, "c" => 46, "d" => 32, "e" => 18, "f" => 33, "g" => 34, "h" => 35, "i" => 23,
         "j" => 36, "k" => 37, "l" => 38, "m" => 50, "n" => 49, "o" => 24, "p" => 25, "q" => 16, "r" => 19,
         "s" => 31, "t" => 20, "u" => 22, "v" => 47, "w" => 17, "x" => 45, "y" => 21, "z" => 44, " " => 57,
         "." => 52, "/" => 53, "\n" => 28 }.freeze
SHIFTED = { "!" => 2, ">" => 52 }.freeze
KEY_LEFTSHIFT = 42
KEY_LEFTCTRL = 29
# The texts (UTF-8, Japanese in each).
SRC_TEXT = "editor: 日本語のコピー"
FOOT_TEXT = "foot: かなと漢字をコピー"
X11_TEXT = "x11: エックス窓の文字"
APP_TEXT = "app: アプリから X11 へ"

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

class ClipRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-clip-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-clip-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "clip-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "clip #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} clip #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  # ------------------------------------------------------------ the run

  def write(rel, text, mode = 0o644)
    path = File.join(@run, rel)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, text)
    File.chmod(mode, path)
  end

  def home(rel) = File.join(@run, "data/family-mruby-dev/home", rel)

  def prepare
    FileUtils.rm_rf(@run)
    write("data/family-mruby-dev/home/src.txt", "#{SRC_TEXT}\n")
    # foot's program: one line to copy, then what it is given into a file.
    write("foot.sh", "#!/bin/sh\nprintf '%s\\n' '#{FOOT_TEXT}'\nexec cat > /run/fmrb/typed\n", 0o755)
    write("data/family-mruby-dev/home/clipset.app.rb", <<~RUBY)
      class ClipSetApp < FmrbApp
        def on_create
          Log.info("clipset: " + FmrbApp.set_clipboard(#{APP_TEXT.inspect}).to_s)
        end

        def on_update
          stop
          100
        end
      end
      ClipSetApp.new.start
    RUBY
  end

  def start
    sh!("docker", "rm", "-f", @name, ok: false)
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    build = "cd /run/fmrb/bin && gcc -O1 -Wall -o x11_clip /src/test/native/wl/clients/x11_clip.c -lX11"
    prog = "#{BIN} --machine linux_pc_wl --set display.mode=1280x800 " \
           "--set 'compositor.autostart=foot -e /run/fmrb/foot.sh'"
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

  def kv(line) = line.split.drop(1).to_h { |x| x.split("=", 2) }

  def clip = kv(comp("clip"))

  def info
    r = comp("info")
    h = kv(r)
    re = %r{(\d+):(\d+)x(\d+)\+(-?\d+)\+(-?\d+)/(-?\d+)x(-?\d+)\+(\d+)x(\d+)/(\d)}
    h["windows"] = h["windows"].to_s.scan(re).map do |v|
      id, ww, wh, x, y, fx, fy, fw, fh, vis = v.map(&:to_i)
      { id: id, w: ww, h: wh, x: x, y: y, fx: fx, fy: fy, fw: fw, fh: fh, vis: vis == 1 }
    end
    h
  end

  def to_out(fx, fy)
    s = @info["scale"].to_i
    x0, y0 = @info["at"].split(",").map(&:to_i)
    [x0 + fx * s + s / 2, y0 + fy * s + s / 2]
  end

  def click(fx, fy) = comp("click", *to_out(fx, fy))

  def key(code, down) = comp("key", code, down ? 1 : 0)

  def tap(code, shift: false, ctrl: false)
    key(KEY_LEFTCTRL, true) if ctrl
    key(KEY_LEFTSHIFT, true) if shift
    key(code, true)
    key(code, false)
    key(KEY_LEFTSHIFT, false) if shift
    key(KEY_LEFTCTRL, false) if ctrl
    sleep 0.05
  end

  def type(text)
    text.each_char do |c|
      if SHIFTED[c]
        tap(SHIFTED[c], shift: true)
      else
        tap(KEYS.fetch(c))
      end
    end
  end

  def shot(name)
    path = File.join(@out, name)
    sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, "shot", path)
    Regress::Screens::Image.load(path)
  end

  def native_tool(*args, timeout: 30)
    cmd = ["timeout", timeout.to_s, "ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, *args.map(&:to_s)]
    sh!({ "FMRB_DEBUG_TCP_PORT" => DEBUG_PORT.to_s }, *cmd, ok: false)
  end

  def log_text
    File.exist?(File.join(@run, "core.log")) ? File.read(File.join(@run, "core.log")).force_encoding("UTF-8").scrub : ""
  end

  def file(rel) = File.exist?(File.join(@run, rel)) ? File.read(File.join(@run, rel)).force_encoding("UTF-8") : ""

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.2
    end
  end

  def client_log = file("x11_clip.log")

  def client
    fifo = File.join(@run, "x11_clip.fifo")
    File.delete(fifo) if File.exist?(fifo)
    File.mkfifo(fifo)
    env = ["-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", "-e", "DISPLAY=#{@x_display}",
           "-e", "LANG=C.UTF-8"]
    sh!("docker", "exec", "-d", *env, @name, "/run/fmrb/bin/x11_clip", "/run/fmrb/x11_clip.fifo",
        "/run/fmrb/x11_clip.log")
    wait_for("x11_clip started", 30) { client_log.include?("started") || client_log[/error.*/] }
    raise "x11_clip: #{client_log[/error.*/]}" if client_log.include?("error")
  end

  def tell(line)
    File.open(File.join(@run, "x11_clip.fifo"), File::WRONLY | File::NONBLOCK) { |f| f.write("#{line}\n") }
  end

  # A Family mruby window's title bar on the picture: the first run of the
  # title colour, scanning rows from the top, left of x_max (outside the
  # Linux window's rectangle). Its middle, to click on.
  def title_point(img, x_max)
    (14...img.h).each do |y|
      xs = (0...x_max).select { |x| img.pixel(x, y) == TITLE_RGB }
      next if xs.size < 20
      return [xs[0] + 30, y]
    end
    nil
  end

  def focus_fmrb_window(label)
    img = shot("#{label}.png")
    pt = title_point(img, @foot_fx - 2) or raise "no Family mruby window to click (#{label})"
    click(*pt)
    sleep 0.6
    pt
  end

  # Foot's first line on the output: dragged over to select it.
  def select_foot_line
    w = info["windows"].find { |x| x[:id] == @foot }
    y = w[:y] + 10
    comp("move", w[:x] + 3, y)
    comp("button", 1, 1)
    6.times do |i|
      comp("move", w[:x] + 3 + (w[:w] - 10) * (i + 1) / 6, y)
      sleep 0.05
    end
    comp("button", 1, 0)
    sleep 0.3
  end

  def click_foot
    w = info["windows"].find { |x| x[:id] == @foot }
    comp("click", w[:x] + w[:w] / 2, w[:y] + w[:h] - 10)
    sleep 0.6
  end

  # ------------------------------------------------------------ checks

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    prepare
    start
    wait_for("Boot complete", 120) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    wait_for("foot's stand-in", 60) { log_text[/wl_bridge: window \d+ \(Foot .*stand-in pid \d+/] }
    @socket = log_text[/WAYLAND_DISPLAY=(wayland-\d+)/, 1]
    @x_display = log_text[/X11 programs on DISPLAY=(:\d+)/, 1] || ":0"
    @info = wait_for("foot placed", 20) { i = info; i["windows"].any? { |w| w[:vis] } && i }
    @foot = @info["windows"].first[:id]
    @foot_fx = @info["windows"].first[:fx]
    sleep 1.5

    # 1. editor -> foot
    native_tool("app", "spawn", "default/shell")
    wait_for("the Shell", 30) { log_text.include?("FM-Shell: on_create called") }
    sleep 2.0
    type("edit /home/src.txt\n")
    wait_for("the editor", 30) { log_text =~ /edit_doc: lines=\d+ .*file=\/home\/src\.txt/ }
    sleep 1.5
    shot("editor.png")
    tap(KEYS["a"], ctrl: true)
    tap(KEYS["c"], ctrl: true)
    c1 = wait_for("the selection Family mruby's", 10) { c = clip; c["owner"] == "fmrb" && c } rescue clip
    click_foot
    tap(KEYS["v"], ctrl: true, shift: true)
    sleep 0.3
    tap(KEYS["\n"])
    want = "#{SRC_TEXT}\n"
    got = wait_for("foot's paste", 10) { file("typed") == want && want } rescue file("typed")
    result("editor -> foot", got == want && c1["owner"] == "fmrb" && c1["last_gave"] == SRC_TEXT.bytesize.to_s,
           "Ctrl+A, Ctrl+C in the editor: the selection #{c1['owner']}'s (#{c1['last_gave']} bytes); " \
           "Ctrl+Shift+V and Enter in foot: it got #{got.inspect}")

    # 2. foot -> editor
    took0 = clip["took"].to_i
    select_foot_line
    tap(KEYS["c"], ctrl: true, shift: true)
    c2 = wait_for("foot's text read", 10) { c = clip; c["took"].to_i > took0 && c } rescue clip
    shot("foot_selected.png")
    focus_fmrb_window("before_editor_paste")
    tap(KEYS["a"], ctrl: true)
    tap(KEYS["v"], ctrl: true)
    sleep 0.5
    tap(KEYS["s"], ctrl: true)
    saved = wait_for("the file saved", 10) do
      t = File.exist?(home("src.txt")) ? File.read(home("src.txt")).force_encoding("UTF-8") : ""
      t.include?(FOOT_TEXT) && t
    end rescue (File.exist?(home("src.txt")) ? File.read(home("src.txt")).force_encoding("UTF-8") : "")
    shot("editor_pasted.png")
    result("foot -> editor", saved.start_with?(FOOT_TEXT) && c2["owner"] == "linux",
           "foot's line selected and Ctrl+Shift+C: the selection #{c2['owner']}'s, #{c2['last_took']} bytes read " \
           "(#{log_text[/native: clipboard: took \d+ bytes from a Linux program \([^)]*\)/]}); Ctrl+A, Ctrl+V, " \
           "Ctrl+S in the editor: src.txt is #{saved.inspect}")

    # 3. the Shell
    tap(KEYS["x"], ctrl: true)
    sleep 1.5
    focus_fmrb_window("before_shell_paste")
    tap(KEYS["v"], ctrl: true)
    sleep 0.5
    shot("shell_pasted.png")
    tap(KEYS["\n"])
    line = wait_for("the pasted command", 10) { log_text[/Command: (#{Regexp.escape(FOOT_TEXT)}[^\n]*)/, 1] } rescue nil
    type("!cat > /run/fmrb/shell.typed\n")
    sleep 1.5
    tap(KEYS["v"], ctrl: true, shift: true)
    sleep 0.3
    tap(KEYS["\n"])
    tap(KEYS["d"], ctrl: true)
    lx = wait_for("the Linux program's input", 10) { file("shell.typed").include?(FOOT_TEXT) && file("shell.typed") } rescue file("shell.typed")
    result("the Shell", line == FOOT_TEXT && lx.start_with?(FOOT_TEXT),
           "Ctrl+V at the prompt and Enter: #{line ? "Command: #{line}" : 'no command line'}; " \
           "`!cat > shell.typed`, Ctrl+Shift+V, Enter, Ctrl+D: it got #{lx.inspect}")

    # 4. X11 through Xwayland
    before = info["windows"].map { |w| w[:id] }
    client
    xw = wait_for("x11_clip's window", 30) { info["windows"].find { |w| !before.include?(w[:id]) && w[:vis] } } rescue nil
    if xw
      comp("click", xw[:x] + xw[:w] / 2, xw[:y] + xw[:h] / 2)
      sleep 0.5
    end
    took0 = clip["took"].to_i
    tell("set #{X11_TEXT}")
    c4 = wait_for("the X11 text read", 15) { c = clip; c["took"].to_i > took0 && c } rescue clip
    focus_fmrb_window("before_x11_paste")
    tap(KEYS["v"], ctrl: true)
    sleep 0.5
    tap(KEYS["\n"])
    xline = wait_for("the X11 text pasted", 10) { log_text[/Command: (#{Regexp.escape(X11_TEXT)}[^\n]*)/, 1] } rescue nil
    native_tool("app", "spawn", "/home/clipset.app.rb")
    wait_for("clipset", 20) { log_text.include?("clipset: true") } rescue nil
    wait_for("the selection Family mruby's again", 10) { clip["owner"] == "fmrb" } rescue nil
    # Xwayland lets an X11 program read the clipboard only while an X11
    # window has the keyboard (wlroots' rule), as when one pastes.
    if xw
      comp("click", xw[:x] + xw[:w] / 2, xw[:y] + xw[:h] / 2)
      sleep 0.5
    end
    tell("get")
    xgot = wait_for("the X11 program's answer", 15) { client_log[/^got (.*)$/, 1] } rescue nil
    result("X11 through Xwayland", xw && xline == X11_TEXT && xgot == APP_TEXT,
           "an X11 program's CLIPBOARD (#{c4['last_took']} bytes, #{log_text.scan(/took \d+ bytes from a Linux program \(([^)]*)\)/).flatten.last}): " \
           "the Shell pasted #{xline.inspect}; an app's set_clipboard: the X11 program got #{xgot.inspect} " \
           "(#{client_log.lines.grep(/^served/).map(&:strip).uniq.join(', ')})")
    tell("quit")
    wait_for("x11_clip's window gone", 15) { info["windows"].none? { |w| xw && w[:id] == xw[:id] } } rescue nil

    # 5. the owner goes: the text stays
    took0 = clip["took"].to_i
    click_foot
    select_foot_line
    tap(KEYS["c"], ctrl: true, shift: true)
    wait_for("foot's text again", 10) { clip["took"].to_i > took0 } rescue nil
    sh!("docker", "exec", @name, "pkill", "-x", "foot", ok: false)
    c5 = wait_for("the selection kept", 15) { c = clip; c["owner"] == "fmrb" && c["kept_again"].to_i >= 1 && c } rescue clip
    result("kept when the owner goes", c5["owner"] == "fmrb" && c5["kept_again"].to_i >= 1,
           "foot copied its line again and ended: the selection #{c5['owner']}'s (kept #{c5['kept_again']}x, " \
           "#{c5['kept']} bytes); #{comp('clip').strip}")
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
  ensure
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = ClipRun.new(opts[:renderer], opts[:keep])
run.go
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-clip-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(run.results))
failed = run.results.reject { |r| r[:pass] }
puts "wl_clip_check #{ENGINE} #{opts[:renderer]}: #{run.results.length - failed.length}/#{run.results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
