#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_settings_check.rb - the PC's settings and the session's robustness
# (doc/linux_native/, L14), in the Ubuntu 26.04 image with the program built
# by rake native:wl, headless, --machine linux_pc_wl, started by the login
# screen's session script (native/package/family-mruby-session) as on the
# PC, no IME, no lock program.
#
#   ruby test/native/wl/wl_settings_check.rb [--engine standard|mruby]
#                                            [--renderer pixman|auto] [--keep]
#
# Beside the program, in the container: a D-Bus daemon of the check's own as
# the system bus with stand-ins for NetworkManager (clients/fake_nm.c) and
# BlueZ (clients/fake_bluez.c) on it, another as the session bus (the
# notifications, the desktop portal), and PipeWire with WirePlumber and a
# null output of the check's own. Never the machine's services.
#
#   1. Wi-Fi: the network dialog's "Wi-Fi..." opens the settings window; the
#      list (one entry per name, no hidden one); an open network connects
#      and disconnects; a new secured one with a wrong password fails (and
#      the connection made for it is deleted), with the right one connects;
#      a saved one whose password NetworkManager does not keep asks for it
#      (the secret agent says "none"), then connects with it (the agent
#      gives it); a saved one with its password kept connects without one;
#      no password ever reaches the log
#   2. Bluetooth: the menu's Bluetooth turns the window's page; the paired
#      devices only; off and on; connect and disconnect; a failing connect
#      says why
#   3. the volume: PipeWire's output moves with the volume keys (also while
#      a Linux window has the keyboard, which gets none of them) and with
#      the desktop's Mute; the keys' level shows as a notice
#   4. notifications: notify-send's notice shows at the top right and goes
#      after its time; one with --wait gets its NotificationClosed
#   5. the screen share: the desktop portal (with the package's
#      family-mruby-portals.conf and xdg-desktop-portal-wlr's settings)
#      gives a ScreenCast stream of the output
#   6. robustness: SIGSEGV -> the session starts it again and the new one
#      says so; the Exit (the control socket's quit, the same call as the
#      power menu's Exit) is not started again; the 4th abnormal end within
#      a minute ends the session
#
# Shots, logs and a summary go to tmp/wl_check/settings-<engine>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
MENU_BAR_H = 13
MENU_ITEM_H = 12
OUTPUT = "1280x800"
SECRETS = %w[cafe1234 walletpw nope1234].freeze
# Linux key codes (input-event-codes.h).
KEY_ENTER = 28
KEY_BACKSPACE = 14
KEY_MUTE = 113
KEY_VOLUMEDOWN = 114
KEY_VOLUMEUP = 115
KEY_OF = {
  "1" => 2, "2" => 3, "3" => 4, "4" => 5, "5" => 6, "6" => 7, "7" => 8, "8" => 9, "9" => 10, "0" => 11,
  "q" => 16, "w" => 17, "e" => 18, "r" => 19, "t" => 20, "y" => 21, "u" => 22, "i" => 23, "o" => 24, "p" => 25,
  "a" => 30, "s" => 31, "d" => 32, "f" => 33, "g" => 34, "h" => 35, "j" => 36, "k" => 37, "l" => 38,
  "z" => 44, "x" => 45, "c" => 46, "v" => 47, "b" => 48, "n" => 49, "m" => 50
}.freeze
VOLUME_KEYS = [KEY_MUTE, KEY_VOLUMEDOWN, KEY_VOLUMEUP].freeze

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

class SettingsRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-settings-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-settings-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "settings-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "settings #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} settings #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  # ------------------------------------------------------------ the container

  def start
    sh!("docker", "rm", "-f", @name, ok: false)
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "XDG_CONFIG_HOME=/run/fmrb/cfg", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy",
         "-e", "DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/fmrb/sysbus",
         "-e", "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/fmrb/xdg/bus",
         "-e", "FMRB_COMPOSITOR_ACTIVATION_ENV=1", "-e", "FMRB_SESSION_CONF=/run/fmrb/session.conf"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    src = "/src/test/native/wl/clients"
    prot = "/usr/share/wayland-protocols"
    sd = "$(pkg-config --cflags --libs libsystemd)"
    build = "cd /run/fmrb/bin && " \
            "wayland-scanner client-header #{prot}/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h && " \
            "wayland-scanner private-code #{prot}/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c && " \
            "gcc -O1 -Wall -I. -o wl_dialog #{src}/wl_dialog.c xdg-shell-protocol.c -lwayland-client && " \
            "gcc -O1 -Wall -o fake_nm #{src}/fake_nm.c #{sd} && " \
            "gcc -O1 -Wall -o fake_bluez #{src}/fake_bluez.c #{sd} && " \
            "gcc -O1 -Wall -o portal_cast #{src}/portal_cast.c #{sd}"
    # The package's files, at the places the user's own would be.
    File.write(File.join(@run, "session.conf"),
               "FMRB_ARGS=\"--set display.mode=#{OUTPUT} --set compositor.ime= --set compositor.locker= " \
               "--set system.power_log_only=true\"\n")
    FileUtils.mkdir_p(File.join(@run, "cfg/xdg-desktop-portal"))
    FileUtils.mkdir_p(File.join(@run, "cfg/xdg-desktop-portal-wlr"))
    FileUtils.cp(File.join(CORE, "native/package/family-mruby-portals.conf"),
                 File.join(@run, "cfg/xdg-desktop-portal/family-mruby-portals.conf"))
    FileUtils.cp(File.join(CORE, "native/package/xdg-desktop-portal-wlr.conf"),
                 File.join(@run, "cfg/xdg-desktop-portal-wlr/family-mruby"))
    FileUtils.mkdir_p(File.join(@run, "bin"))
    FileUtils.cp(File.join(CORE, "native/package/family-mruby-session"), File.join(@run, "bin/family-mruby-session"))
    # RLIMIT_CORE 1: no core when it is ended with SIGSEGV (on WSL a core
    # goes to the host's crash capture whatever the limit, but for 1).
    File.write(File.join(@run, "bin/family-mruby"), "#!/bin/sh\nexec prlimit --core=1:1 /src/#{BIN} \"$@\"\n")
    File.chmod(0o755, File.join(@run, "bin/family-mruby"), File.join(@run, "bin/family-mruby-session"))
    inner = "mkdir -p -m 700 /run/fmrb/xdg /tmp/home && (#{build}) > /run/fmrb/bin/build.log 2>&1; " \
            "dbus-daemon --session --address=unix:path=/run/fmrb/sysbus --nofork --nopidfile " \
            "> /run/fmrb/sysbus.log 2>&1 & " \
            "dbus-daemon --session --address=unix:path=/run/fmrb/xdg/bus --nofork --nopidfile " \
            "> /run/fmrb/sessbus.log 2>&1 & " \
            "for i in $(seq 50); do [ -S /run/fmrb/sysbus ] && [ -S /run/fmrb/xdg/bus ] && break; sleep 0.1; done; " \
            "/run/fmrb/bin/fake_nm /run/fmrb/nm.log & /run/fmrb/bin/fake_bluez /run/fmrb/bluez.log & " \
            "pipewire > /run/fmrb/pw.log 2>&1 & sleep 0.5; wireplumber > /run/fmrb/wp.log 2>&1 & sleep 1; " \
            "pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=fmrb-check " \
            "node.description=fmrb-check media.class=Audio/Sink object.linger=true audio.position=[FL FR] }' " \
            "> /run/fmrb/sink.log 2>&1; " \
            "for i in $(seq 50); do wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null | grep -q Volume: && break; sleep 0.2; done; " \
            "echo setup-done > /run/fmrb/setup.log; sleep 3600"
    sh!(*a, IMAGE, "bash", "-c", inner)
    wait_for("the container's setup", 120) { read_run("setup.log").include?("setup-done") }
    unless %w[wl_dialog fake_nm fake_bluez portal_cast].all? { |b| File.exist?(File.join(@run, "bin", b)) }
      raise "the clients did not build: #{read_run('bin/build.log').lines.last(8).join}"
    end
  end

  # A session: the script, in the background, its output (and the
  # program's) to core.log.
  def session_start
    path = File.join(@run, "core.log")
    File.open(path, "a") { |f| f.write("---- a session\n") }
    @log_from = File.size(path)
    FileUtils.rm_f(File.join(@run, "pid"))
    sh!("docker", "exec", "-d", @name, "sh", "-c",
        "/run/fmrb/bin/family-mruby-session >> /run/fmrb/core.log 2>&1; echo \"session exit=$?\" >> /run/fmrb/core.log")
  end

  def boot(after = 0)
    wait_for("Boot complete", 180) { log_text.scan("Boot complete").size > after || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    @socket = log_text.scan(/WAYLAND_DISPLAY=(wayland-\d+)/).last&.first
    wait_for("the desktop", 60) { log_text.scan("Linux windows are Family mruby windows").size > after }
    sleep 1.5
    @info = info
  end

  def program_pid = read_run("pid").to_i

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

  def type(str)
    str.each_char do |c|
      tap(KEY_OF.fetch(c))
      sleep 0.05
    end
  end

  def frame_size = @info["frame"].split("x").map(&:to_i)

  def shot(label)
    path = File.join(@out, "#{label}.png")
    sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, "shot", path)
    Regress::Screens::Image.load(path)
  end

  def dexec(*cmd, ok: true) = sh!("docker", "exec", @name, *cmd, ok: ok)

  def read_run(name) = File.exist?(File.join(@run, name)) ? File.read(File.join(@run, name)).force_encoding("UTF-8").scrub : ""
  # The log of the session started last.
  def log_text = read_run("core.log").byteslice(@log_from || 0..).to_s.scrub
  def nm_log = read_run("nm.log")
  def bluez_log = read_run("bluez.log")

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

  # The settings window's last state line for a page.
  def state(what)
    l = log_text.scan(/settings: #{what} (\S+) \[([^\]]*)\] \[([^\]]*)\] \[([^\]]*)\] \[([^\]]*)\] ?(.*)$/).last
    return nil unless l
    { status: l[0], f2: l[1], f3: l[2], code: l[3], arg: l[4], names: l[5].to_s.split(",") }
  end

  def wait_state(what, timeout = 15, &cond)
    try_wait(timeout) { (s = state(what)) && cond.call(s) && s }
  end

  def volume
    out, = dexec("wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", ok: false)
    v = out[/Volume:\s*([\d.]+)/, 1]
    [v ? v.to_f.round(2) : nil, out.include?("[MUTED]")]
  end

  # ------------------------------------------------------------ the settings window

  # Where the window's user area is on the frame, from its log lines.
  def settings_area
    win = log_text.scan(/the settings window: pid (\d+), (\d+)x(\d+) at \((\d+),(\d+)\)/).last
    ua = log_text.scan(/settings: the \S+ page, user area (\d+)x(\d+) at \((\d+),(\d+)\)/).last
    raise "no settings window in the log" unless win && ua
    x, y = win[3].to_i, win[4].to_i
    { x: x + ua[2].to_i, y: y + ua[3].to_i, w: ua[0].to_i, h: ua[1].to_i, pid: win[0].to_i }
  end

  def sclick(ux, uy)
    a = @area
    click(a[:x] + ux, a[:y] + uy)
  end

  def row(i) = sclick(40, 32 + i * 12 + 6)
  def button(n) = sclick(4 + n * 78 + 36, @area[:h] - 34 + 8)    # 0 Connect, 1 Disconnect, 2 Scan

  def select_network(name)
    s = state("wifi")
    i = s[:names].index { |n| n.delete("*") == name }
    raise "#{name} is not listed (#{s[:names].inspect})" unless i
    row(i)
    sleep 0.4
  end

  # ------------------------------------------------------------ the run

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    FileUtils.rm_rf(@run)
    FileUtils.mkdir_p(@run)
    start
    session_start
    boot
    fw, fh = frame_size
    started = log_text
    result("the services at start", started.include?("native: volume: PipeWire (wpctl) on the default output") &&
           started.include?("notifications: serving org.freedesktop.Notifications") &&
           started.include?("the session's activation environment: WAYLAND_DISPLAY and XDG_CURRENT_DESKTOP"),
           [started[/native: volume: [^\n]*/], started[/notifications: [^\n]*/],
            started[/activation environment: [^\n]*/]].join("; "))

    # ---- 1. Wi-Fi from the network dialog
    click(30, 5)
    sleep 0.8
    click(30, MENU_BAR_H + 1 + 9 * MENU_ITEM_H + MENU_ITEM_H / 2)     # Network
    sleep 1.0
    shot("network_dialog")
    # The dialog's buttons: Refresh, Wi-Fi..., Close in the middle of its
    # bottom (network_dialog.rb).
    nx = (fw - 240) / 2
    ny = (fh - 104) / 2
    widths = [7 * 6 + 12, 8 * 6 + 12, 5 * 6 + 12]
    bx = nx + (240 - (widths.sum + 16)) / 2
    click(bx + widths[0] + 8 + widths[1] / 2, ny + 104 - 22 + 8)
    wait_for("the settings window", 20) { log_text.include?("settings: the Wi-Fi page") }
    @area = settings_area
    s = wait_state("wifi") { |x| x[:status] == "ok" && x[:names].size >= 5 }
    names = s ? s[:names] : []
    shot("wifi_list")
    result("the network dialog opens the Wi-Fi page; the list",
           names == %w[fmrb-home fmrb-open fmrb-wallet fmrb-cafe fmrb-corp] && nm_log.include?("Register family-mruby"),
           "networks #{names.inspect} (one fmrb-cafe, no hidden one); the secret agent #{nm_log.include?('Register family-mruby') ? 'registered' : 'not registered'}")

    # open network: connect, disconnect
    select_network("fmrb-open")
    button(0)
    s = wait_state("wifi") { |x| x[:code] == "connected" && x[:arg] == "fmrb-open" && x[:f2] == "fmrb-open" }
    ok1 = s && nm_log.include?("AddAndActivate fmrb-open key-mgmt=none psk=none")
    button(1)
    s2 = wait_state("wifi") { |x| x[:code] == "disconnected" && x[:f2] == "" }
    result("an open network connects and disconnects", ok1 && s2 && nm_log.include?("Disconnect"),
           "connected #{s ? true : false} (#{nm_log[/AddAndActivate fmrb-open[^\n]*/]}); disconnected #{s2 ? true : false}")

    # a new secured network: wrong, then right
    select_network("fmrb-cafe")
    shot("wifi_password_field")
    type("nope1234")
    button(0)
    s = wait_state("wifi", 20) { |x| x[:code] == "auth_failed" && x[:arg] == "fmrb-cafe" }
    deleted = try_wait(5) { nm_log.include?("Delete fmrb-cafe") }
    shot("wifi_wrong_password")
    type("cafe1234")
    button(0)
    s2 = wait_state("wifi", 20) { |x| x[:code] == "connected" && x[:f2] == "fmrb-cafe" }
    result("a new secured network: a wrong password fails, the right one connects",
           s && deleted && s2 && nm_log.include?("AddAndActivate fmrb-cafe key-mgmt=wpa-psk psk=wrong") &&
           nm_log.include?("AddAndActivate fmrb-cafe key-mgmt=wpa-psk psk=right"),
           "wrong: #{s ? s[:code] : 'no outcome'}, the connection made for it deleted #{deleted ? true : false}; " \
           "right: #{s2 ? 'connected' : 'not connected'} (#{nm_log.scan(/AddAndActivate fmrb-cafe[^\n]*/).join(', ')})")

    # a saved network whose password the agent has to give
    select_network("fmrb-wallet")
    button(0)
    s = wait_state("wifi", 20) { |x| x[:code] == "need_password" && x[:arg] == "fmrb-wallet" }
    asked = nm_log.include?("GetSecrets fmrb-wallet: none")
    sleep 0.5
    shot("wifi_need_password")
    type("walletpw")
    button(0)
    s2 = wait_state("wifi", 20) { |x| x[:code] == "connected" && x[:f2] == "fmrb-wallet" }
    result("a saved network without its password: asked for, then given through the secret agent",
           s && asked && s2 && nm_log.include?("GetSecrets fmrb-wallet: right"),
           "first: #{s ? s[:code] : 'no outcome'} (the agent said none #{asked}); with the password: " \
           "#{s2 ? 'connected' : 'not connected'} (#{nm_log.scan(/GetSecrets fmrb-wallet[^\n]*/).join(', ')})")

    # a saved network with its password kept
    select_network("fmrb-home")
    button(0)
    s = wait_state("wifi", 20) { |x| x[:code] == "connected" && x[:f2] == "fmrb-home" }
    leaked = SECRETS.select { |p| log_text.include?(p) || nm_log.include?(p) }
    result("a saved network with its password kept; no password in a log",
           s && nm_log.include?("Activate fmrb-home") && !nm_log.include?("GetSecrets fmrb-home") && leaked.empty?,
           "#{s ? 'connected' : 'not connected'} without being asked; passwords in the logs: #{leaked.inspect}")
    button(2)
    scanned = try_wait(10) { nm_log.include?("RequestScan") }
    result("Scan asks NetworkManager for a scan", scanned ? true : false, "RequestScan #{scanned ? 'asked' : 'not asked'}")

    # ---- 2. Bluetooth from the menu
    click(30, 5)
    sleep 0.8
    click(30, MENU_BAR_H + 1 + 10 * MENU_ITEM_H + MENU_ITEM_H / 2)    # Bluetooth
    s = wait_state("bt") { |x| x[:status] == "ok" }
    shot("bt_list")
    devs = s ? s[:names] : []
    result("the menu's Bluetooth turns the page; the paired devices only",
           s && devs == ["FM Keyboard*", "FM Headphones", "FM Speaker"] && s[:f2] == "1",
           "devices #{devs.inspect}, powered #{s && s[:f2]}; the window #{settings_area[:pid] == @area[:pid] ? 'the same' : 'another'}")
    sclick(@area[:w] - 34, 24)    # Turn off
    off = wait_state("bt") { |x| x[:f2] == "0" && x[:code] == "powered_off" }
    sclick(@area[:w] - 34, 24)    # Turn on
    on = wait_state("bt") { |x| x[:f2] == "1" && x[:code] == "powered_on" }
    result("Bluetooth off and on", off && on && bluez_log.include?("Powered 0") && bluez_log.include?("Powered 1"),
           "off #{off ? true : false}, on #{on ? true : false} (#{bluez_log.scan(/Powered \d/).join(', ')})")
    s = wait_state("bt") { |x| x[:names].include?("FM Headphones") }
    i = s[:names].index("FM Headphones")
    row(i)
    sleep 0.4
    button(0)
    c = wait_state("bt", 15) { |x| x[:code] == "connected" && x[:names].include?("FM Headphones*") }
    s = state("bt")
    row(s[:names].index("FM Headphones*"))
    sleep 0.4
    button(1)
    d = wait_state("bt", 15) { |x| x[:code] == "disconnected" && x[:names].include?("FM Headphones") }
    s = state("bt")
    row(s[:names].index("FM Speaker"))
    sleep 0.4
    button(0)
    f = wait_state("bt", 15) { |x| x[:code] == "failed" }
    shot("bt_failed")
    result("connect, disconnect, and a failing connect says why",
           c && d && f && f[:arg].include?("br-connection-page-timeout") && bluez_log.include?("Connect FM Headphones: ok") &&
           bluez_log.include?("Disconnect FM Headphones"),
           "connected #{c ? true : false}, disconnected #{d ? true : false}, failed: #{f ? f[:arg] : 'no outcome'}")
    # Close the settings window (its close button: the top right of its frame).
    win = log_text.scan(/the settings window: pid (\d+), (\d+)x(\d+) at \((\d+),(\d+)\)/).last
    click(win[3].to_i + win[1].to_i - 6, win[4].to_i + 5)
    sleep 1.0

    # ---- 3. the volume
    v0 = volume
    n0 = log_text.scan("notice(s) on screen").size
    tap(KEY_VOLUMEDOWN)
    v1 = try_wait(5) { (v = volume) && v[0] && v[0] < v0[0] && v }
    shown = try_wait(3) { log_text.scan("notice(s) on screen").size > n0 }
    img = shot("volume_notice")
    tap(KEY_MUTE)
    v2 = try_wait(5) { (v = volume) && v[1] && v }
    tap(KEY_VOLUMEUP)
    v3 = try_wait(5) { (v = volume) && !v[1] && v[0] && v[0] > (v1 ? v1[0] : 0) && v }
    result("the volume keys move PipeWire's output; the level shows",
           v1 && v2 && v3 && shown && log_text.include?("volume key:"),
           "#{v0.inspect} -> down #{v1.inspect} -> mute #{v2.inspect} -> up #{v3.inspect}; the notice #{shown ? 'shown' : 'not shown'}")
    # A Linux window with the keyboard does not keep them.
    before = info["windows"].map { |w| w[:id] }
    FileUtils.rm_f(File.join(@run, "wl_dialog.log"))
    File.mkfifo(File.join(@run, "wl_dialog.fifo")) unless File.exist?(File.join(@run, "wl_dialog.fifo"))
    sh!("docker", "exec", "-d", "-e", "WAYLAND_DISPLAY=#{@socket}", @name, "/run/fmrb/bin/wl_dialog",
        "/run/fmrb/wl_dialog.fifo", "/run/fmrb/wl_dialog.log")
    dlg = wait_for("the Linux window", 30) { info["windows"].find { |x| !before.include?(x[:id]) && x[:vis] } }
    sleep 1.0
    click(dlg[:fx] + dlg[:fw] / 2, dlg[:fy] + dlg[:fh] / 2)
    sleep 0.5
    va = volume
    tap(KEY_VOLUMEDOWN)
    vb = try_wait(5) { (v = volume) && v[0] && v[0] < va[0] && v }
    sleep 0.5
    got = read_run("wl_dialog.log").lines.grep(/^key /).map { |l| l.split[2].to_i }
    result("the volume keys while a Linux window has the keyboard",
           vb && (got & VOLUME_KEYS).empty?,
           "#{va.inspect} -> #{vb.inspect}; the volume keys the window got: #{(got & VOLUME_KEYS).inspect}")
    begin
      File.open(File.join(@run, "wl_dialog.fifo"), File::WRONLY | File::NONBLOCK) { |f| f.write("quit\n") }
    rescue StandardError
      nil
    end
    sleep 1.0
    # The desktop's Mute moves PipeWire's.
    click(30, 5)
    sleep 0.8
    click(30, MENU_BAR_H + 1 + 7 * MENU_ITEM_H + MENU_ITEM_H / 2)     # Mute
    m1 = try_wait(5) { volume[1] }
    click(30, 5)
    sleep 0.8
    click(30, MENU_BAR_H + 1 + 7 * MENU_ITEM_H + MENU_ITEM_H / 2)     # Unmute
    m2 = try_wait(5) { !volume[1] }
    result("the desktop's Mute and Unmute move PipeWire's", m1 && m2 && log_text.include?("(the OS mixer)"),
           "muted #{m1 ? true : false}, unmuted #{m2 ? true : false}")

    # ---- 4. notifications
    sleep 2.5    # the volume notice has gone
    base = shot("before_notify")
    n0 = log_text.scan(/Desktop: 1 notice\(s\) on screen/).size
    t0 = now
    dexec("notify-send", "-a", "Check", "Build done", "All 12 tests passed")
    shown = try_wait(5) { log_text.scan(/Desktop: 1 notice\(s\) on screen/).size > n0 }
    sleep 0.6
    img = shot("notice")
    nx0 = fw - 184 - 4
    diff = 0
    (MENU_BAR_H + 4...MENU_BAR_H + 40).each do |y|
      (nx0...fw - 4).each { |x| diff += 1 if img.pixel(x, y) != base.pixel(x, y) }
    end
    gone = try_wait(10) { log_text.include?("Desktop: 0 notice(s) on screen") && log_text.rindex("0 notice(s)") > log_text.rindex("1 notice(s)") }
    secs = (now - t0).round(1)
    after = shot("after_notice")
    back = 0
    (MENU_BAR_H + 4...MENU_BAR_H + 40).each do |y|
      (nx0...fw - 4).each { |x| back += 1 if after.pixel(x, y) != base.pixel(x, y) }
    end
    served = log_text.include?("notifications: #1 from \"Check\": Build done") ||
             log_text =~ /notifications: #\d+ from "Check": Build done/
    result("notify-send's notice at the top right, gone after its time",
           served && shown && diff > 500 && gone && back.zero?,
           "served #{served ? true : false}; #{diff} pixels changed at the top right; gone after #{secs}s " \
           "(#{back} pixels still differ)")
    out, st = dexec("timeout", "10", "notify-send", "--wait", "-t", "2000", "Waiting", "for the close", ok: false)
    result("notify-send --wait gets NotificationClosed", st.success?, "exit #{st.exitstatus} #{out.strip}")

    # ---- 5. the screen share through the desktop portal
    FileUtils.rm_f(File.join(@run, "cast.log"))
    dexec("timeout", "60", "/run/fmrb/bin/portal_cast", "/run/fmrb/cast.log", ok: false)
    cast = read_run("cast.log")
    node = cast[/stream (\d+) (\d+)x(\d+)/, 1]
    size = cast[/stream \d+ (\d+x\d+)/, 1]
    result("the desktop portal's ScreenCast gives a stream of the output",
           node && size == OUTPUT && cast.include?("remote fd"),
           "#{cast.lines.map(&:strip).join(' | ')}")

    # ---- 6. robustness: a crash, started again, the notice; then the Exit
    pid0 = program_pid
    booted = log_text.scan("Boot complete").size
    dexec("kill", "-SEGV", pid0.to_s)
    again = try_wait(20) { log_text.include?("Family mruby ended by SIGSEGV; starting it again (1 of 3 within 60s)") }
    boot(booted)
    told = log_text.include?("the session started Family mruby again (the last run ended abnormally: SIGSEGV)")
    shown = try_wait(10) { log_text.scan(/Desktop: 1 notice\(s\) on screen/).size.positive? && log_text.rindex("Desktop: 1 notice") > log_text.rindex("starting it again") }
    sleep 0.6
    shot("restarted_notice")
    result("SIGSEGV: the session starts it again, and the new one says so",
           again && told && shown && program_pid != pid0,
           "#{log_text[/Family mruby ended by SIGSEGV[^\n]*/]}; told #{told}; the notice #{shown ? 'shown' : 'not shown'}")
    ctl("quit")
    ended = try_wait(30) { log_text.include?("session exit=0") }
    result("the Exit ends the session (no new start)",
           ended && log_text.include?("Family mruby ended (its Exit entry)") && log_text.scan("Boot complete").size == booted + 1,
           "#{log_text[/Family mruby ended \(its Exit entry\)/] || 'no line'}; #{log_text[/session exit=\d+/]}")

    # The 4th abnormal end within a minute ends the session.
    session_start
    pids = []
    4.times do |k|
      pid = wait_for("run #{k + 1}", 60) { (p = program_pid).positive? && !pids.include?(p) && p }
      pids << pid
      sleep 2.0
      dexec("kill", "-SEGV", pid.to_s, ok: false)
      try_wait(10) { log_text.include?("ended by SIGSEGV; that is 4") || log_text.scan("ended by SIGSEGV").size > k }
    end
    stopped = try_wait(20) { log_text.include?("session exit=1") }
    result("the 4th abnormal end within a minute ends the session",
           stopped && log_text.include?("that is 4 abnormal ends within 60s: not starting it again") &&
           log_text.scan(/starting it again \(\d of/).size == 3,
           "#{log_text[/that is \d+ abnormal ends[^\n]*/] || 'no line'}; #{log_text[/session exit=\d+/]}; runs #{pids.size}")
  rescue StandardError => e
    result("run", false, "#{e.message.lines.first(3).join.strip} (#{e.backtrace&.first})")
  ensure
    %w[core.log nm.log bluez.log sysbus.log sessbus.log pw.log wp.log cast.log wl_dialog.log].each do |c|
      FileUtils.cp(File.join(@run, c), File.join(@out, c)) if File.exist?(File.join(@run, c))
    end
    FileUtils.cp(File.join(@run, "bin/build.log"), File.join(@out, "build.log")) if File.exist?(File.join(@run, "bin/build.log"))
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = SettingsRun.new(opts[:renderer], opts[:keep])
run.go
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-settings-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(run.results))
failed = run.results.reject { |r| r[:pass] }
puts "wl_settings_check #{ENGINE} #{opts[:renderer]}: #{run.results.length - failed.length}/#{run.results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)
