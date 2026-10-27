#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_ime_check.rb - the PC's IME in the compositor (doc/linux_native/,
# L12b), in the Ubuntu 26.04 image with the program built by rake native:wl,
# headless (WLR_BACKENDS=headless, a 1280x800 output: a 640x400 screen at
# 2x), --machine linux_pc_wl, which starts fcitx5 (mozc) itself. The whole
# run is inside dbus-run-session (fcitx5 wants a session bus); HOME (fcitx5's
# and mozc's settings) and the XDG places are the run's own.
#
#   ruby test/native/wl/wl_ime_check.rb [--engine standard|mruby]
#                                       [--renderer pixman|auto] [--keep]
#
#   1. foot (a Linux window, text-input-v3): Ctrl+Space (mozc on), "nihongo",
#      Space Space (converted, the candidate window out), Up, Enter, Enter:
#      foot's program got "日本語"; the candidates were near foot's cursor
#      and inside the picture
#   2. Family mruby's editor (the compositor stands in as its text input):
#      the Shell's `edit /home/ime.txt` typed through the IME's virtual
#      keyboard (mozc off), then mozc on, "nihongo", Space Space: the text
#      being composed is drawn at the editor's cursor and the candidates
#      under it, inside the picture (grim); Up, Enter, Ctrl+S: the file holds
#      "日本語"; the kana indicators are hidden (mode 3)
#   3. the "sdl" machine (L12b's SDL path): a second program, --machine
#      sdl, as a Wayland client of this compositor (SDL_VIDEODRIVER=wayland:
#      SDL's text input is text-input-v3, relayed to fcitx5 like any Linux
#      window's). Its Shell's `edit /home/sdl.txt`, mozc on, "nihongo",
#      Space, Enter, Ctrl+S: its file holds "日本語" (SDL_TEXTINPUT to the
#      editor), and its log says the desktop's IME was seen
#   4. the IME goes (fcitx5 killed): the kana input is back; Ctrl+Space,
#      "nihongo", Ctrl+S in the editor: the file gets "にほんご"
#   5. the IME program not installed (a second, short run with
#      --set compositor.ime=no-such-ime): one line says so, and the kana
#      input works in the editor
#
# Shots, logs and a summary go to tmp/wl_check/ime-<engine>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
DEBUG_PORT = 15_569
# Linux key codes (input-event-codes.h), US positions.
KEYS = { "a" => 30, "b" => 48, "c" => 46, "d" => 32, "e" => 18, "f" => 33, "g" => 34, "h" => 35, "i" => 23,
         "j" => 36, "k" => 37, "l" => 38, "m" => 50, "n" => 49, "o" => 24, "p" => 25, "q" => 16, "r" => 19,
         "s" => 31, "t" => 20, "u" => 22, "v" => 47, "w" => 17, "x" => 45, "y" => 21, "z" => 44, " " => 57,
         "." => 52, "/" => 53, "\n" => 28 }.freeze
KEY_LEFTSHIFT = 42
KEY_LEFTCTRL = 29
KEY_UP = 103
WORD = "日本語"
KANA = "にほんご"

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

FCITX_PROFILE = <<~INI
  [Groups/0]
  Name=Default
  Default Layout=us
  DefaultIM=mozc

  [Groups/0/Items/0]
  Name=keyboard-us
  Layout=

  [Groups/0/Items/1]
  Name=mozc
  Layout=

  [GroupOrder]
  0=Default
INI

class ImeRun
  attr_reader :results

  def initialize(renderer, keep, label: "ime", ime: nil)
    @renderer = renderer
    @keep = keep
    @label = label
    @ime = ime
    @name = "fmrb-wl-#{label}-#{ENGINE}-#{renderer}"
    @port = DEBUG_PORT + (ime ? 1 : 0)
    @run = "/tmp/fmrb-wl-#{label}-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "#{label}-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "#{@label} #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} #{@label} #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  def write(rel, text, mode = 0o644)
    path = File.join(@run, rel)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, text)
    File.chmod(mode, path)
  end

  def home(rel) = File.join(@run, "data/family-mruby-dev/home", rel)

  def prepare
    FileUtils.rm_rf(@run)
    write("home/.config/fcitx5/profile", FCITX_PROFILE)
    write("foot.sh", "#!/bin/sh\nprintf '%s\\n' 'type here'\nexec cat > /run/fmrb/typed\n", 0o755)
    FileUtils.mkdir_p(File.join(@run, "data/family-mruby-dev/home"))
  end

  def start
    sh!("docker", "rm", "-f", @name, ok: false)
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{@port}:5565", "-p", "127.0.0.1:#{@port + 10}:5575",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/run/fmrb/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_CONFIG_HOME=/run/fmrb/home/.config",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    sets = "--set display.mode=1280x800 --set 'compositor.autostart=foot -e /run/fmrb/foot.sh'"
    sets += " --set compositor.ime=#{@ime}" if @ime
    prog = "#{BIN} --machine linux_pc_wl #{sets}"
    inner = "mkdir -p -m 700 /run/fmrb/xdg; " \
            "dbus-run-session -- sh -c \"#{prog.gsub('"', '\"')} > /run/fmrb/core.log 2>&1\"; " \
            "echo \"exit=$?\" >> /run/fmrb/core.log"
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

  def ime = kv(comp("ime"))

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

  def key(code, down) = comp("key", code, down ? 1 : 0)

  def tap(code, shift: false, ctrl: false)
    key(KEY_LEFTCTRL, true) if ctrl
    key(KEY_LEFTSHIFT, true) if shift
    key(code, true)
    key(code, false)
    key(KEY_LEFTSHIFT, false) if shift
    key(KEY_LEFTCTRL, false) if ctrl
    sleep 0.08
  end

  def type(text) = text.each_char { |c| tap(KEYS.fetch(c)) }

  def native_tool(*args, timeout: 30)
    cmd = ["timeout", timeout.to_s, "ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, *args.map(&:to_s)]
    sh!({ "FMRB_DEBUG_TCP_PORT" => @port.to_s }, *cmd, ok: false)
  end

  def grim(name)
    sh!("docker", "exec", "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", @name,
        "grim", "/run/fmrb/#{name}")
    FileUtils.cp(File.join(@run, name), File.join(@out, name))
    Regress::Screens::Image.load(File.join(@out, name))
  end

  def log_text
    File.exist?(File.join(@run, "core.log")) ? File.read(File.join(@run, "core.log")).force_encoding("UTF-8").scrub : ""
  end

  def file(rel) = File.exist?(File.join(@run, rel)) ? File.read(File.join(@run, rel)).force_encoding("UTF-8") : ""

  def home_file(rel) = File.exist?(home(rel)) ? File.read(home(rel)).force_encoding("UTF-8") : ""

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.2
    end
  end

  def click_foot
    w = info["windows"].find { |x| x[:id] == @foot }
    comp("click", w[:x] + w[:w] / 2, w[:y] + w[:h] - 10)
    sleep 0.6
  end

  def picture_box
    i = info
    s = i["scale"].to_i
    x0, y0 = i["at"].split(",").map(&:to_i)
    fw, fh = i["frame"].split("x").map(&:to_i)
    [x0, y0, fw * s, fh * s]
  end

  # "WxH+X+Y" of the shown candidate window, or nil.
  def popup_rect(h)
    m = h["popup"].to_s.match(/(\d+)x(\d+)\+(-?\d+)\+(-?\d+)/) or return nil
    m.captures.map(&:to_i)
  end

  def inside?(r, box)
    w, h, x, y = r
    bx, by, bw, bh = box
    x >= bx && y >= by && x + w <= bx + bw && y + h <= by + bh
  end

  def boot
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    prepare
    start
    wait_for("Boot complete", 120) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    wait_for("foot's stand-in", 60) { log_text[/wl_bridge: window \d+ \(Foot .*stand-in pid \d+/] }
    @socket = log_text[/WAYLAND_DISPLAY=(wayland-\d+)/, 1]
    @info = wait_for("foot placed", 20) { i = info; i["windows"].any? { |w| w[:vis] } && i }
    @foot = @info["windows"].first[:id]
    sleep 1.5
  end

  def open_editor(path)
    native_tool("app", "spawn", "default/shell")
    wait_for("the Shell", 30) { log_text.include?("FM-Shell: on_create called") }
    sleep 2.0
    type("edit #{path}\n")
    wait_for("the editor", 30) { log_text =~ /edit_doc: lines=\d+ .*file=#{Regexp.escape(path)}/ }
    sleep 1.5
  end

  # ------------------------------------------------------------ checks

  def go
    boot
    present = wait_for("the IME", 30) { ime["present"] == "1" } rescue false
    result("IME up", present && log_text.include?("IME: started \"fcitx5\""),
           "#{log_text[/native: compositor: IME: started[^\n]*/]}; #{log_text[/native: IME: [^\n]*/]}; " \
           "#{log_text[/the PC's IME types for the apps[^\n]*/]}")

    # 1. foot
    click_foot
    tap(KEYS[" "], ctrl: true)
    sleep 0.5
    type("nihongo")
    sleep 0.5
    tap(KEYS[" "])
    sleep 0.5
    tap(KEYS[" "])
    sleep 0.8
    h1 = ime
    grim("foot_candidates.png")
    pr = popup_rect(h1)
    box = picture_box
    fw = info["windows"].find { |x| x[:id] == @foot }
    tap(KEY_UP)
    sleep 0.3
    tap(KEYS["\n"])
    sleep 0.3
    tap(KEYS["\n"])
    got = wait_for("foot's text", 10) { file("typed").include?(WORD) && file("typed") } rescue file("typed")
    near = pr && fw && pr[2] >= fw[:x] && pr[2] < fw[:x] + fw[:w] && pr[3] >= fw[:y] && pr[3] < fw[:y] + fw[:h] + 40
    result("foot", got == "#{WORD}\n" && h1["serves"] == "linux" && pr && inside?(pr, box) && near,
           "Ctrl+Space, nihongo, Space Space: the IME served #{h1['serves']}, candidates #{h1['popup']} " \
           "(foot at #{fw && "#{fw[:w]}x#{fw[:h]}+#{fw[:x]}+#{fw[:y]}"}, picture #{box.join(',')}); Up, Enter, Enter: " \
           "foot got #{got.inspect}")
    tap(KEYS[" "], ctrl: true)   # mozc off (the input state is shared)
    sleep 0.5

    # 2. the editor
    open_editor("/home/ime.txt")
    typed_ok = log_text.include?("edit_doc") # the path reached the Shell through the IME's virtual keyboard
    h2a = ime
    tap(KEYS[" "], ctrl: true)
    sleep 0.5
    type("nihongo")
    sleep 0.6
    h2b = ime
    img_c = grim("editor_composing.png")
    tap(KEYS[" "])
    sleep 0.5
    tap(KEYS[" "])
    sleep 0.8
    h2 = ime
    grim("editor_candidates.png")
    pr2 = popup_rect(h2)
    tap(KEY_UP)
    sleep 0.3
    tap(KEYS["\n"])
    sleep 0.5
    tap(KEYS["s"], ctrl: true)
    saved = wait_for("the file saved", 10) { home_file("ime.txt").include?(WORD) && home_file("ime.txt") } rescue home_file("ime.txt")
    cur = h2["cursor_out"].to_s.match(/(-?\d+),(-?\d+),(\d+)x(\d+)/)&.captures&.map(&:to_i)
    pre = h2b["preedit"].to_s.match(/(\d+)x(\d+)\+(-?\d+)\+(-?\d+)/)&.captures&.map(&:to_i)
    near2 = pr2 && cur && (pr2[2] - cur[0]).abs <= 200 && pr2[3] >= cur[1] - pr2[1] - 4 && pr2[3] <= cur[1] + cur[3] + 40
    white = pre ? img_white(img_c, pre) : 0
    result("editor", saved.start_with?(WORD) && h2a["serves"] == "fmrb" && pr2 && inside?(pr2, picture_box) &&
           near2 && pre && pre[0] > 0 && white.positive? && typed_ok,
           "the Shell's edit line through the IME (serves #{h2a['serves']}); mozc on, nihongo: composing text " \
           "#{h2b['preedit_bytes']} bytes drawn #{h2b['preedit']} (#{white} white pixels in it), cursor " \
           "#{h2['cursor']}; Space Space: candidates #{h2['popup']}; Up, Enter, Ctrl+S: ime.txt is #{saved.inspect} " \
           "(#{log_text.scan(/IME committed \d+ bytes to Family mruby/).last})")
    hidden = log_text.scan(/kana mode=\d+ asked while/).size
    result("kana indicators", log_text.include?("kana input steps aside"),
           "#{log_text[/the PC's IME types for the apps[^\n]*/]} (indicator clicks refused: #{hidden})")
    tap(KEYS[" "], ctrl: true)   # mozc off
    sleep 0.5

    # 3. the SDL window's IME, nested
    sdl_step

    # 4. the IME goes: the kana input is back
    focus_fmrb_editor
    sh!("docker", "exec", @name, "pkill", "-x", "fcitx5", ok: false)
    back = wait_for("the kana input back", 15) { log_text.include?("kana input is back") } rescue false
    tap(KEYS["a"], ctrl: true)
    tap(KEYS[" "], ctrl: true)   # Family mruby's kana on
    sleep 0.3
    type("nihongo")
    tap(KEYS[" "], ctrl: true)
    sleep 0.3
    tap(KEYS["s"], ctrl: true)
    kana = wait_for("kana saved", 10) { home_file("ime.txt").include?(KANA) && home_file("ime.txt") } rescue home_file("ime.txt")
    result("no IME: kana", back && kana.include?(KANA) && ime["present"] == "0",
           "fcitx5 ended: #{log_text[/no IME on the PC any more[^\n]*/]}; Ctrl+A, Ctrl+Space, nihongo, Ctrl+S: " \
           "ime.txt is #{kana.inspect}")
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
  ensure
    finish
  end

  def sdl_tool(*args, timeout: 30)
    cmd = ["timeout", timeout.to_s, "ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", File.join(@run, "sdl"),
           *args.map(&:to_s)]
    sh!({ "FMRB_DEBUG_TCP_PORT" => (@port + 10).to_s }, *cmd, ok: false)
  end

  def sdl_log = file("sdl/core.log")

  def sdl_home(rel)
    path = File.join(@run, "sdl/data/family-mruby-dev/home", rel)
    File.exist?(path) ? File.read(path).force_encoding("UTF-8") : ""
  end

  def sdl_step
    FileUtils.mkdir_p(File.join(@run, "sdl/data/family-mruby-dev/home"))
    before = info["windows"].map { |w| w[:id] }
    env = ["-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", "-e", "SDL_VIDEODRIVER=wayland",
           "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb/sdl", "-e", "XDG_DATA_HOME=/run/fmrb/sdl/data",
           "-e", "FMRB_DEBUG_TCP_PORT=5575", "-e", "SDL_AUDIODRIVER=dummy", "-e", "LANG=C.UTF-8"]
    sh!("docker", "exec", "-d", *env, @name, "sh", "-c", "#{BIN} --machine sdl > /run/fmrb/sdl/core.log 2>&1")
    wait_for("the SDL program's boot", 120) { sdl_log.include?("Boot complete") }
    sw = wait_for("its window", 30) { info["windows"].find { |w| !before.include?(w[:id]) && w[:vis] } }
    sleep 2.0
    comp("click", sw[:x] + sw[:w] / 2, sw[:y] + sw[:h] / 2)
    sleep 0.6
    sdl_tool("app", "spawn", "default/shell")
    wait_for("its Shell", 30) { sdl_log.include?("FM-Shell: on_create called") }
    sleep 2.0
    type("edit /home/sdl.txt\n")
    wait_for("its editor", 30) { sdl_log =~ %r{edit_doc: lines=\d+ .*file=/home/sdl\.txt} }
    sleep 1.5
    served = ime["serves"]
    tap(KEYS[" "], ctrl: true)
    sleep 0.5
    type("nihongo")
    sleep 0.5
    tap(KEYS[" "])
    sleep 0.6
    grim("sdl_converting.png")
    tap(KEYS["\n"])
    sleep 0.5
    tap(KEYS[" "], ctrl: true)   # mozc off
    sleep 0.3
    tap(KEYS["s"], ctrl: true)
    got = wait_for("its file", 10) { sdl_home("sdl.txt").include?(WORD) && sdl_home("sdl.txt") } rescue sdl_home("sdl.txt")
    seen = sdl_log[/native: IME: [^\n]*/]
    result("sdl window", got.start_with?(WORD) && served == "linux" && seen,
           "--machine sdl on this compositor (SDL's text-input-v3): the IME served #{served}; its editor, nihongo, " \
           "Space, Enter, Ctrl+S: sdl.txt is #{got.inspect}; #{seen}")
    begin
      sock = UNIXSocket.new(File.join(@run, "sdl/ctl"))
      sock.write("quit\n")
      sock.close
    rescue StandardError
      nil
    end
    wait_for("its window gone", 15) { info["windows"].none? { |w| w[:id] == sw[:id] } } rescue nil
  rescue StandardError => e
    result("sdl window", false, e.message.lines.first(3).join.strip)
  end

  # Family mruby's editor back in front (the SDL program gone): its title
  # bar (the editor's window is at frame (5,15)).
  def focus_fmrb_editor
    sleep 0.5
    i = info
    s = i["scale"].to_i
    x0, y0 = i["at"].split(",").map(&:to_i)
    comp("click", x0 + 120 * s, y0 + 20 * s)
    sleep 0.6
  end

  def img_white(img, pre)
    w, h, x, y = pre
    n = 0
    (y...(y + h)).step(2) do |yy|
      (x...(x + w)).step(2) { |xx| n += 1 if xx < img.w && yy < img.h && img.pixel(xx, yy) == [255, 255, 255] }
    end
    n
  end

  # 4. the IME program is not installed.
  def go_missing
    boot
    line = log_text[/IME: no-such-ime is not installed[^\n]*/]
    open_editor("/home/kana.txt")
    tap(KEYS[" "], ctrl: true)
    sleep 0.3
    type("nihongo")
    tap(KEYS[" "], ctrl: true)
    sleep 0.3
    tap(KEYS["s"], ctrl: true)
    kana = wait_for("kana saved", 10) { home_file("kana.txt").include?(KANA) && home_file("kana.txt") } rescue home_file("kana.txt")
    result("not installed", line && kana.include?(KANA) && ime["present"] == "0",
           "#{line}; the editor's Ctrl+Space, nihongo, Ctrl+S: kana.txt is #{kana.inspect}")
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
  ensure
    finish
  end

  def finish
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

results = []
run = ImeRun.new(opts[:renderer], opts[:keep])
run.go
results.concat(run.results)
run2 = ImeRun.new(opts[:renderer], opts[:keep], label: "ime-missing", ime: "no-such-ime")
run2.go_missing
results.concat(run2.results)
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-ime-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(results))
failed = results.reject { |r| r[:pass] }
puts "wl_ime_check #{ENGINE} #{opts[:renderer]}: #{results.length - failed.length}/#{results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
