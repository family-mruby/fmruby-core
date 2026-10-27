#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_dialog_check.rb - dialogs and menus of the Linux windows (doc/
# linux_native/, L9c), in the Ubuntu 26.04 image with the program built by
# rake native:wl, headless, --machine linux_pc_wl, no autostart.
#
#   ruby test/native/wl/wl_dialog_check.rb [--engine standard|mruby]
#                                          [--renderer pixman|auto] [--keep]
#
# The output is turned (display.rotate = 90: 720x1280, the picture at 1x
# with wide black bands above and below), so whatever leaves the picture's
# rectangle shows on grim's screen. Two test clients of the check's own
# (test/native/wl/clients/, compiled in the container) make the windows:
# wl_dialog (Wayland: a parent, a dialog with the parent set, popups) and
# x11_dialog (X11: a parent, a transient dialog, an override-redirect menu).
#
#   1. a Wayland dialog: it comes with a stand-in (the Family mruby frame),
#      in the middle of its parent's content, in front, with the keyboard;
#      keys and a click reach it; nothing outside the picture
#   2. the dialog's button closes it: the window and its stand-in go
#   3. a dialog larger than the room around its parent stays in the picture
#   4. a menu (popup) opened at the parent's corner by the picture's edge:
#      with slide/flip allowed it is moved inside the picture; with no
#      adjustment allowed it is cut at the picture's edge; nothing outside
#   5. a press on Family mruby (not on the menu or its window) closes the
#      menu (popup_done)
#   6. stacking: the editor brought in front hides the dialog where it
#      overlaps it; a click on the parent brings the parent in front and the
#      dialog with it, over the parent
#   7. an X11 transient dialog: a stand-in, in the middle of its parent,
#      the keyboard and keys, closed by its button
#   8. an X11 menu (override-redirect) asked for across the picture's edge
#      is moved inside it
#   L9e: the parent clicked gives the dialog the keyboard as well (6); a
#   window coming under a pointer that stands still gets the pointer (7);
#   an X11 menu holding the pointer closes on a press on Family mruby (9)
#
# Shots, logs and a summary go to tmp/wl_check/dialog-<engine>-<renderer>/.

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
TITLE_RGB = [222, 36, 82]
BLUE = [0, 0, 255].freeze
YELLOW = [255, 255, 0].freeze
GREEN = [0, 192, 0].freeze
RED = [255, 0, 0].freeze
# Linux key codes (input-event-codes.h); X key codes are these + 8.
KEYS = { "e" => 18, "h" => 35, "l" => 38, "o" => 24 }.freeze
BTN_W = 40
BTN_H = 24
# The stand-in's rounded bottom-left corner covers this many pixels of the
# content (its frame colour over the window).
CORNER = 3

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

class DialogRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-dialog-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-dialog-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "dialog-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "dialog #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} dialog #{ENGINE}/#{@renderer} #{name}: #{detail}"
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
    prog = "#{BIN} --machine linux_pc_wl --set display.rotate=90"
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
  def client(which)
    fifo = File.join(@run, "#{which}.fifo")
    File.delete(fifo) if File.exist?(fifo)
    File.mkfifo(fifo)
    env = ["-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "WAYLAND_DISPLAY=#{@socket}", "-e", "DISPLAY=#{@x_display}"]
    sh!("docker", "exec", "-d", *env, @name, "/run/fmrb/bin/#{which}", "/run/fmrb/#{which}.fifo",
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
    raise "the clients did not build: #{File.read(File.join(@run, 'bin/build.log')).lines.last(5).join}" unless
      File.exist?(File.join(@run, "bin/wl_dialog")) && File.exist?(File.join(@run, "bin/x11_dialog"))
    @info = info
    sleep 1.0
    rect = picture_rect
    fw, fh = @info["frame"].split("x").map(&:to_i)

    # 1. a Wayland dialog
    client("wl_dialog")
    parent = new_window([])
    sleep 0.8
    parent = win(info, parent[:id])
    wclick(*centre(parent).map(&:to_i))
    sleep 0.5
    before = info["windows"].map { |w| w[:id] }
    tell("wl_dialog", "dialog 160 90")
    dlg = new_window(before)
    sleep 1.0
    inf = info
    dlg = win(inf, dlg[:id])
    parent = win(inf, parent[:id])
    pc = centre(parent)
    dc = centre(dlg)
    centred = (pc[0] - dc[0]).abs <= 1 && (pc[1] - dc[1]).abs <= 1
    type("hello")
    sleep 0.5
    keys = client_log("wl_dialog").lines.grep(/^key dialog \d+ 1/).size
    img = grim("wl_dialog")
    pic = shot(File.join(@out, "wl_dialog_picture.png"))
    full = dlg[:w] * dlg[:h] - BTN_W * BTN_H - CORNER
    blue = count(img, BLUE)
    title = pic.pixel(dlg[:fx] + 20, dlg[:fy] - FRAME_TOP + 5)
    out1 = outside(img, inf)
    wclick(dlg[:fx] + 10, dlg[:fy] + 10)
    sleep 0.5
    clicked = client_log("wl_dialog").include?("button dialog 272 1")
    top = inf["windows"].last[:id] == dlg[:id]
    result("a Wayland dialog", stand_in?(dlg[:id]) && centred && inf["focus"] == dlg[:id].to_s && keys == 5 &&
           clicked && title == TITLE_RGB && blue == full && out1.zero? && top,
           "stand-in #{stand_in?(dlg[:id])}; the parent's content centre #{pc.inspect}, the dialog's #{dc.inspect}; " \
           "keyboard on #{inf['focus']} (the dialog #{dlg[:id]}), #{keys} of 5 keys reached it, a click #{clicked}; " \
           "its stand-in's title bar #{title.inspect}; #{blue} of #{full} blue pixels on screen; on top #{top}; " \
           "#{out1} pixels outside the picture")

    # 2. the dialog's button
    wclick(dlg[:fx] + dlg[:fw] - BTN_W / 2, dlg[:fy] + dlg[:fh] - BTN_H / 2)
    closed = begin
      wait_for("closed by its button", 10) { client_log("wl_dialog").include?("dialog closed by its button") }
    rescue StandardError
      false
    end
    gone = gone?(dlg[:id])
    ended = begin
      wait_for("its stand-in ended", 10) { log_text.match?(/wl_bridge: window #{dlg[:id]}: stand-in pid \d+ ended/) }
    rescue StandardError
      false
    end
    result("the dialog's button closes it", closed && gone && ended,
           "the client: closed by its button #{closed}; the window gone #{gone}; its stand-in ended #{ended}")

    # 3. a dialog larger than the room around its parent
    before = info["windows"].map { |w| w[:id] }
    tell("wl_dialog", "dialog 400 200")
    big = new_window(before)
    sleep 1.0
    inf = info
    big = win(inf, big[:id])
    img = grim("big_dialog")
    inside = big[:fx] >= 1 && big[:fy] >= MENU_BAR_H + FRAME_TOP && big[:fx] + big[:fw] <= fw - 1 &&
             big[:fy] + big[:fh] <= fh - 1
    out3 = outside(img, inf)
    result("a large dialog stays in the picture", inside && out3.zero?,
           "content #{big[:fw]}x#{big[:fh]} at (#{big[:fx]},#{big[:fy]}) in a frame of #{fw}x#{fh}; " \
           "#{out3} pixels outside the picture")
    wclick(big[:fx] + big[:fw] - BTN_W / 2, big[:fy] + big[:fh] - BTN_H / 2)
    raise "the large dialog did not close" unless gone?(big[:id])

    # 4. menus at the parent's corner, by the picture's right edge: the
    #    parent's stand-in dragged right (its title bar).
    parent = win(info, parent[:id])
    room = fw - (parent[:fx] + parent[:fw]) - 2
    comp("move", *to_out(2, 236))
    sleep 0.15
    drag(parent[:fx] + 30, parent[:fy] - 5, room, 0)
    wait_for("the parent by the edge", 10) { (q = win(info, parent[:id])) && q[:fx] + q[:fw] >= fw - 3 }
    sleep 0.5
    inf = info
    parent = win(inf, parent[:id])
    wclick(*centre(parent).map(&:to_i))
    sleep 0.3
    tell("wl_dialog", "popup #{parent[:w] - 10} #{parent[:h] - 10} 150 100 slide")
    wait_for("the popup", 10) { client_log("wl_dialog").include?("popup configure") }
    sleep 0.8
    conf = client_log("wl_dialog").lines.grep(/^popup configure/).last.split[2..].map(&:to_i)
    img = grim("popup_slide")
    px0 = parent[:x] + conf[0]
    py0 = parent[:y] + conf[1]
    in_slide = px0 >= rect[0] && py0 >= rect[1] && px0 + conf[2] <= rect[0] + rect[2] && py0 + conf[3] <= rect[1] + rect[3]
    yel = count(img, YELLOW)
    out4a = outside(img, inf)
    tell("wl_dialog", "unpopup")
    sleep 0.3
    wclick(*centre(parent).map(&:to_i))
    sleep 0.3
    tell("wl_dialog", "popup #{parent[:w] - 10} #{parent[:h] - 10} 150 100 none")
    wait_for("the second popup", 10) { client_log("wl_dialog").lines.grep(/^popup configure/).size >= 2 }
    sleep 0.8
    conf2 = client_log("wl_dialog").lines.grep(/^popup configure/).last.split[2..].map(&:to_i)
    img = grim("popup_none")
    yel2 = count(img, YELLOW)
    out4b = outside(img, inf)
    result("menus by the picture's edge", in_slide && yel == 150 * 100 && out4a.zero? && yel2 < 150 * 100 &&
           yel2.positive? && out4b.zero?,
           "slide/flip allowed: configured at #{conf[0..1].inspect} of the parent = (#{px0},#{py0}) " \
           "#{conf[2]}x#{conf[3]}, inside the picture #{rect.inspect}: #{in_slide}, #{yel} yellow pixels, " \
           "#{out4a} outside; none allowed: configured at #{conf2[0..1].inspect}, #{yel2} yellow pixels on " \
           "screen (cut at the edge), #{out4b} outside")

    # 5. a press on Family mruby closes the menu
    done0 = client_log("wl_dialog").scan("popup done").size
    comp("click", *to_out(2, 236))
    sleep 0.8
    done = client_log("wl_dialog").scan("popup done").size > done0
    img = grim("popup_closed")
    result("a press elsewhere closes the menu", done && count(img, YELLOW).zero?,
           "popup_done #{done}; #{count(img, YELLOW)} yellow pixels left (#{log_text[/\d+ menus? closed \(a press elsewhere\)/]})")

    # 6. stacking: a dialog, the editor in front, then the parent clicked.
    before = info["windows"].map { |w| w[:id] }
    tell("wl_dialog", "dialog 160 90")
    dlg = new_window(before)
    sleep 1.0
    dlg = win(info, dlg[:id])
    full = dlg[:w] * dlg[:h] - BTN_W * BTN_H - CORNER
    comp("click", *to_out(30, 5))
    sleep 1.2
    comp("click", *to_out(30, MENU_BAR_H + MENU_ITEM_H + MENU_ITEM_H / 2))
    wait_for("the editor's window", 60) { shot(File.join(@out, "editor.png")).pixel(150, 20) == TITLE_RGB }
    sleep 1.0
    inf = info
    img = grim("editor_over_dialog")
    under = count(img, BLUE)
    still_above = inf["windows"].map { |w| w[:id] }
    # A point of the parent the editor and the dialog leave uncovered.
    pic = shot(File.join(@out, "editor_picture.png"))
    parent = win(inf, parent[:id])
    pt = nil
    (parent[:fy]...(parent[:fy] + parent[:fh])).step(3) do |fy|
      (parent[:fx]...(parent[:fx] + parent[:fw])).step(3) do |fx|
        next unless pic.pixel(fx, fy) == [0, 0, 82]
        next if fx >= dlg[:fx] - 2 && fx < dlg[:fx] + dlg[:fw] + 2 && fy >= dlg[:fy] - 13 && fy < dlg[:fy] + dlg[:fh] + 2
        pt = [fx, fy]
        break
      end
      break if pt
    end
    raise "no part of the parent left uncovered by the editor" unless pt
    wclick(*pt)
    sleep 1.0
    inf = info
    img = grim("parent_clicked")
    again = count(img, BLUE)
    order = inf["windows"].map { |w| w[:id] }
    above = order.index(dlg[:id]).to_i > order.index(parent[:id]).to_i
    raised = log_text.include?("in front of its parent's")
    result("stacking", under < full && again == full && above && raised && outside(img, inf).zero?,
           "the dialog shows #{full} blue pixels in full; with the editor in front #{under} (Linux windows bottom " \
           "to top #{still_above.inspect}); after a click on the parent at #{pt.inspect} #{again}, bottom to top " \
           "#{order.inspect} (the dialog above its parent: #{above}; raised by the bridge: #{raised}); keyboard on " \
           "#{inf['focus']}")
    # ... and the keyboard went to the dialog with it (L9e).
    dkeys0 = client_log("wl_dialog").lines.grep(/^key dialog \d+ 1/).size
    pkeys0 = client_log("wl_dialog").lines.grep(/^key parent \d+ 1/).size
    type("hello")
    sleep 0.5
    dkeys = client_log("wl_dialog").lines.grep(/^key dialog \d+ 1/).size - dkeys0
    pkeys = client_log("wl_dialog").lines.grep(/^key parent \d+ 1/).size - pkeys0
    result("the parent clicked: the keyboard to the dialog", inf["focus"] == dlg[:id].to_s && dkeys == 5 && pkeys.zero?,
           "keyboard on #{inf['focus']} (the dialog #{dlg[:id]}, its parent #{parent[:id]}); 5 keys typed: " \
           "#{dkeys} reached the dialog, #{pkeys} the parent (#{log_text[/stand-in pid \d+ in front of its parent's[^\n]*/]})")
    tell("wl_dialog", "quit")
    gone?(parent[:id])

    # 7. X11: a transient dialog. Before it, the pointer is left resting in
    #    the middle of the screen, where the parent will come (L9e): without
    #    a move, the pointer is found over the new window (Family mruby's
    #    own arrow covers the hole under it).
    comp("move", *to_out(fw / 2, fh / 2 + 5))
    sleep 0.8
    rc0 = stats["pointer_rechecks"]
    before = info["windows"].map { |w| w[:id] }
    client("x11_dialog")
    xp = new_window(before)
    sleep 0.8
    xp = win(info, xp[:id])
    still = wait_for("the pointer over the new window", 5) { i = info; i["ptr"].to_s.start_with?("#{xp[:id]}:") && i } rescue info
    rc = stats["pointer_rechecks"] - rc0
    result("a window coming under a still pointer", still["ptr"].to_s.start_with?("#{xp[:id]}:") &&
           still["cursor"] == "window" && rc.positive?,
           "the pointer at frame (#{fw / 2},#{fh / 2 + 5}) not moved; window #{xp[:id]} came under it at " \
           "(#{xp[:fx]},#{xp[:fy]}) #{xp[:fw]}x#{xp[:fh]}; the pointer now on #{still['ptr']}, cursor " \
           "#{still['cursor']}; #{rc} recheck(s)")
    before = info["windows"].map { |w| w[:id] }
    tell("x11_dialog", "dialog 160 90")
    xd = new_window(before)
    sleep 1.0
    inf = info
    xd = win(inf, xd[:id])
    xp = win(inf, xp[:id])
    pc = centre(xp)
    dc = centre(xd)
    xcentred = (pc[0] - dc[0]).abs <= 1 && (pc[1] - dc[1]).abs <= 1
    type("hello")
    sleep 0.5
    xkeys = client_log("x11_dialog").lines.grep(/^key dialog \d+ 1/).size
    img = grim("x11_dialog")
    xblue = count(img, BLUE)
    xfull = xd[:w] * xd[:h] - BTN_W * BTN_H - CORNER
    xout = outside(img, inf)
    wclick(xd[:fx] + xd[:fw] - BTN_W / 2, xd[:fy] + xd[:fh] - BTN_H / 2)
    xgone = gone?(xd[:id])
    xclosed = client_log("x11_dialog").include?("dialog closed by its button")
    result("an X11 transient dialog", stand_in?(xd[:id]) && xcentred && inf["focus"] == xd[:id].to_s && xkeys == 5 &&
           xclosed && xgone && xblue == xfull && xout.zero?,
           "stand-in #{stand_in?(xd[:id])}; the parent's content centre #{pc.inspect}, the dialog's #{dc.inspect}; " \
           "keyboard on #{inf['focus']} (the dialog #{xd[:id]}), #{xkeys} of 5 keys reached it; #{xblue} of " \
           "#{xfull} blue pixels; closed by its button #{xclosed}, gone #{xgone}; #{xout} pixels outside the picture")

    # 8. an X11 menu across the picture's right edge
    mx = rect[0] + rect[2] - 60
    my = rect[1] + 40
    tell("x11_dialog", "menu #{mx} #{my} 150 100")
    wait_for("the X11 menu", 10) { client_log("x11_dialog").include?("configure menu") }
    sleep 1.0
    img = grim("x11_menu")
    mconf = client_log("x11_dialog").lines.grep(/^configure menu/).last.split[2..].map(&:to_i)
    xin = mconf[0] >= rect[0] && mconf[1] >= rect[1] && mconf[0] + mconf[2] <= rect[0] + rect[2] &&
          mconf[1] + mconf[3] <= rect[1] + rect[3]
    yel = count(img, YELLOW)
    mout = outside(img)
    result("an X11 menu by the picture's edge", xin && yel == 150 * 100 && mout.zero?,
           "asked at (#{mx},#{my}) 150x100, now at (#{mconf[0]},#{mconf[1]}) #{mconf[2]}x#{mconf[3]}; inside the " \
           "picture #{rect.inspect}: #{xin}; #{yel} yellow pixels, #{mout} outside")

    # 9. an X11 menu holding the pointer (a toolkit's menu: typed a popup
    #    menu, a grab) closes on a press on Family mruby (L9e)
    tell("x11_dialog", "unmenu")
    sleep 0.3
    tell("x11_dialog", "grabmenu #{rect[0] + 20} #{rect[1] + 60} 150 100")
    grabbed = wait_for("the menu's grab", 10) { client_log("x11_dialog")[/^menu grab (\d+)/, 1] } rescue nil
    sleep 0.8
    img = grim("x11_grab_menu")
    shown = count(img, YELLOW)
    p0 = stats["x11_menu_presses"]
    comp("click", *to_out(fw - 30, fh - 20))
    closed = wait_for("the menu closed", 10) { client_log("x11_dialog").include?("menu closed by a press outside") } rescue false
    sleep 0.5
    img = grim("x11_grab_menu_closed")
    left = count(img, YELLOW)
    passed = stats["x11_menu_presses"] - p0
    result("a press on Family mruby closes an X11 menu", grabbed == "0" && shown == 150 * 100 && closed && left.zero? &&
           passed == 1,
           "the menu's grab #{grabbed.inspect} (0 = taken); #{shown} yellow pixels; a press on the desktop: passed " \
           "to the menu #{passed} time(s), the program closed it #{closed ? true : false}, #{left} yellow pixels left " \
           "(#{log_text[/a press elsewhere passed to X11 menu[^\n]*/]})")
    tell("x11_dialog", "quit")
    ctl("quit")
    wait_for("the container to end", 30) { !system("docker inspect #{@name} > /dev/null 2>&1") } rescue nil
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
  ensure
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
    %w[wl_dialog x11_dialog].each do |c|
      FileUtils.cp(File.join(@run, "#{c}.log"), File.join(@out, "#{c}.log")) if File.exist?(File.join(@run, "#{c}.log"))
    end
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = DialogRun.new(opts[:renderer], opts[:keep])
run.go
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-dialog-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(run.results))
failed = run.results.reject { |r| r[:pass] }
puts "wl_dialog_check #{ENGINE} #{opts[:renderer]}: #{run.results.length - failed.length}/#{run.results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
