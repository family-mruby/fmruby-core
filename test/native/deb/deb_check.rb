#!/usr/bin/env ruby
# frozen_string_literal: true
#
# deb_check.rb - install a native package into a plain Debian trixie
# container and check it the way a user would meet it (doc/linux_native/
# report/l3a.md, acceptance 2-4).
#
#   ruby test/native/deb/deb_check.rb --arch arm64     # the Cardputer's (qemu)
#   ruby test/native/deb/deb_check.rb --arch amd64
#   options: --deb FILE (default: the newest native/build-deb/family-mruby_*_<arch>.deb)
#            --timeout SEC (each wait, default 240; qemu is slow) --keep (leave the container)
#
# What it does, in a container of the pinned base (no build tools, nothing
# but the package and what apt pulls in for it):
#
#   1. apt install ./family-mruby_*.deb  (the Depends have to resolve)
#   2. the per-machine launcher (family-mruby-hdmi) is run: in a container
#      there is no DRM device, so it must be refused for that reason, with
#      the machine the launcher named
#   3. the program as root, headless (SDL dummy), --machine sdl: the time to
#      "Boot complete", the desktop against the x86_64 native baseline
#      (test/regress/screens/baseline/native/desktop.png, clock and pointer
#      masked as rake regress:screens masks them)
#   4. the editor from the desktop's menu and one key, against
#      baseline/native/editor_type.png
#   5. where it wrote: nothing in /usr/share/family-mruby/root changed, the
#      user's files under /root/.local/share/family-mruby and
#      /root/.cache/family-mruby, nothing else outside /tmp and the run dir
#   6. the /etc update: the user's system_conf.toml and services.toml put
#      back to an earlier release's (with values of the user's own in them)
#      and associations.toml removed; the next start must add what is
#      missing, log it, and keep every line the user had
#   7. the package's rules (L3c): the release system_conf (debug_mode false,
#      no "Development mode", zenoh_router empty), debugd not listening; with
#      debugd = true it listens on 127.0.0.1:5565 only
#
# The run directory (control and inject sockets) is a host directory mounted
# into the container, so the root tools drive the program from outside
# (tools/fmrb_native.rb shot, tools/fmrb_input.rb --socket). The program's
# pid is the container's: nothing here signals a pid from it.
#
# Shots, logs and a summary go to tmp/deb_check/<arch>/.

require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
BASE_IMAGE = "debian:trixie-20261020"
BASELINE = File.join(CORE, "test/regress/screens/baseline/native")
TITLE_RGB = [222, 36, 82]    # a focused window's title bar (test/regress/screens/helpers.rb)
MENU_BAR_H = 13
MENU_ITEM_H = 12
# The last commit whose native /etc lacked what L2n added (zenoh_router, net, timesync).
OLD_ETC_COMMIT = "e22bfb7f"

opts = { arch: "arm64", timeout: 240, keep: false }
OptionParser.new do |o|
  o.on("--arch ARCH") { |v| opts[:arch] = v }
  o.on("--deb FILE") { |v| opts[:deb] = File.expand_path(v) }
  o.on("--timeout SEC", Integer) { |v| opts[:timeout] = v }
  o.on("--keep") { opts[:keep] = true }
end.parse!
arch = opts[:arch]
abort "--arch is arm64 or amd64" unless %w[arm64 amd64].include?(arch)
deb = opts[:deb] || Dir[File.join(CORE, "native/build-deb/family-mruby_*_#{arch}.deb")].max_by { |f| File.mtime(f) }
abort "no package for #{arch}; rake native:deb ARCH=#{arch}" unless deb && File.exist?(deb)
bin = arch == "arm64" ? "/usr/share/APPLaunch/bin/family-mruby" : "/usr/bin/family-mruby"

OUT = File.join(CORE, "tmp/deb_check", arch)
FileUtils.rm_rf(OUT)
FileUtils.mkdir_p(OUT)
NAME = "fmrb-deb-check-#{arch}"
RUN = "/tmp/fmrb-deb-#{arch}-#{Process.uid}"
$results = []

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def sh!(*cmd, ok: true)
  out, st = Open3.capture2e(*cmd)
  raise "#{cmd.join(' ')}:\n#{out.lines.last(15).join}" if ok && !st.success?
  [out, st]
end

def dexec(script, ok: true) = sh!("docker", "exec", NAME, "bash", "-c", script, ok: ok)

def result(name, pass, detail)
  $results << { name: name, pass: pass, detail: detail }
  puts "#{pass ? 'ok  ' : 'FAIL'} #{name}: #{detail}"
end

def ctl(request, timeout: 30)
  sock = UNIXSocket.new(File.join(RUN, "ctl"))
  sock.write("#{request}\n")
  data = +""
  deadline = now + timeout
  loop do
    raise "control socket: no answer in #{timeout}s" if now > deadline
    next unless IO.select([sock], nil, nil, 0.5)
    chunk = sock.read_nonblock(65_536, exception: false)
    break if chunk.nil?
    next if chunk == :wait_readable
    data << chunk
  end
  sock.close
  data
end

DEBUGD_PORT = 5565

# Listening TCP sockets in the container, as "a.b.c.d:port" (from
# /proc/net/tcp: the base image has no ss or netstat).
def tcp_listeners
  out, = dexec("cat /proc/net/tcp")
  out.lines.drop(1).map(&:split).select { |f| f[3] == "0A" }.map do |f|
    hex, port = f[1].split(":")
    "#{[hex].pack('H*').bytes.reverse.join('.')}:#{port.to_i(16)}"
  end
end

SHOT = File.join(OUT, "capture.png")
def capture
  FileUtils.rm_f(SHOT)
  sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", RUN, "shot", SHOT)
  Regress::Screens::Image.load(SHOT)
end

def input(*words)
  sh!("ruby", File.join(TOOLS, "fmrb_input.rb"), "--socket", File.join(RUN, "inject"), *words.map(&:to_s))
end

def masks(img) = [[img.w - 90, 0, 90, MENU_BAR_H], [img.w - 6, 60, 6, 16]]

# Until two captures 1.5 s apart agree outside the masks (and, with a
# reference, until it is matched), up to the timeout. The last capture.
def settle(timeout, ref: nil)
  deadline = now + timeout
  prev = capture
  loop do
    sleep 1.5
    cur = capture
    quiet = cur.same?(prev, masks(cur))
    return cur if quiet && (ref.nil? || cur.same?(ref, masks(cur)))
    return cur if now > deadline
    prev = cur
  end
end

def wait_until(what, timeout)
  deadline = now + timeout
  loop do
    img = capture
    return img if yield(img)
    raise "#{what}: not within #{timeout}s" if now > deadline
    sleep 1
  end
end

def compare(name, img, baseline_file)
  base = Regress::Screens::Image.load(baseline_file)
  count, box = img.diff(base, masks(img))
  FileUtils.cp(SHOT, File.join(OUT, "#{name}.png"))
  result("#{name} vs #{File.basename(baseline_file)}", count.zero?,
         "#{img.w}x#{img.h}, #{count} differing pixels outside the clock and pointer#{box ? " (box #{box.inspect})" : ''}")
end

begin
  sh!("docker", "rm", "-f", NAME, ok: false)
  FileUtils.rm_rf(RUN)
  FileUtils.mkdir_p(RUN)
  File.chmod(0o755, RUN)
  sh!("docker", "run", "-d", "--name", NAME, "--platform", "linux/#{arch}",
      "-v", "#{File.dirname(deb)}:/deb:ro", "-v", "#{RUN}:/run/fmrb",
      BASE_IMAGE, "sleep", "infinity")
  uname, = dexec("uname -m; . /etc/os-release; echo $PRETTY_NAME")
  puts "container: #{uname.split("\n").join(', ')}"

  # 1. install
  t0 = now
  out, st = dexec("apt-get update -qq && apt-get install -y --no-install-recommends /deb/#{File.basename(deb)}", ok: false)
  File.write(File.join(OUT, "apt.log"), out)
  pulled = out[/(\d+) newly installed/, 1]
  result("apt install", st.success? && out.include?("Setting up family-mruby"),
         "#{File.basename(deb)}: #{pulled || '?'} packages newly installed in #{(now - t0).round} s")
  raise "the package did not install (#{File.join(OUT, 'apt.log')})" unless st.success?
  dexec("find /usr/share/family-mruby/root -type f -print0 | sort -z | xargs -0 md5sum > /tmp/root.before; " \
        "touch /tmp/marker; sleep 1")

  # 2. the launcher
  if arch == "arm64"
    out, st = dexec("cd /root && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy /usr/share/APPLaunch/bin/family-mruby-hdmi 2>&1", ok: false)
    File.write(File.join(OUT, "launcher.log"), out)
    refusal = out[/native: refused: .*/].to_s
    result("launcher family-mruby-hdmi", st.exitstatus == 3 && refusal.include?("machine cardputer_zero_hdmi"),
           "exit #{st.exitstatus}: #{refusal}")
  end

  # 3. boot to the desktop
  t0 = now
  sh!("docker", "exec", "-d", NAME, "bash", "-c",
      "umask 0; cd /root; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy FMRB_NATIVE_RUN_DIR=/run/fmrb " \
      "exec #{bin} --machine sdl > /run/fmrb/core.log 2>&1")
  log = File.join(RUN, "core.log")
  booted = false
  deadline = now + opts[:timeout]
  until booted || now > deadline
    sleep 0.5
    text = File.exist?(log) ? File.read(log) : ""
    booted = text.include?("Boot complete")
    break if text.include?("native: refused") || text =~ /native: .*(cannot|bad|not at)/
  end
  boot_s = (now - t0).round(1)
  FileUtils.cp(log, File.join(OUT, "core.boot.log")) if File.exist?(log)
  result("boot as root, --machine sdl", booted, booted ? "Boot complete in #{boot_s} s" : "no Boot complete (see core.boot.log)")
  raise "no boot" unless booted
  paths = File.read(log)[/native: system=.*/].to_s
  puts "  #{paths}"

  # 3b. the package's rules (L3c): the release system_conf, no debugd.
  boot_text = File.read(log)
  conf_now, = dexec("cat /root/.local/share/family-mruby/etc/system_conf.toml")
  result("release settings", !boot_text.include?("Development mode") && conf_now =~ /^debug_mode = false$/ &&
         conf_now =~ /^zenoh_router = ""$/ && conf_now =~ /^debugd = false$/,
         "boot line: #{boot_text[/Running as a native Linux program.*/]}; debug_mode = false, zenoh_router = \"\", debugd = false in the user's system_conf")
  listen = tcp_listeners
  result("debugd off by default", listen.none? { |a| a.end_with?(":#{DEBUGD_PORT}") } && boot_text.include?("debugd off"),
         "TCP listeners: #{listen.empty? ? 'none' : listen.join(' ')}")

  input("move", 420, 60)
  sleep 1
  input("move", 420, 60)
  base_desktop = Regress::Screens::Image.load(File.join(BASELINE, "desktop.png"))
  t0 = now
  img = settle(opts[:timeout], ref: base_desktop)
  compare("desktop", img, File.join(BASELINE, "desktop.png"))
  puts "  desktop settled #{(now - t0).round(1)} s after the boot line"

  # 4. the editor and one key
  t0 = now
  input("click", 30, 5)
  sleep 2
  input("click", 30, MENU_BAR_H + 1 * MENU_ITEM_H + MENU_ITEM_H / 2)
  wait_until("the editor's window", opts[:timeout]) { |i| i.pixel(150, 20) == TITLE_RGB }
  editor_s = (now - t0).round(1)
  input("move", 420, 60)
  before = settle(opts[:timeout])
  t1 = now
  input("key", "a")
  wait_until("the key on the screen", opts[:timeout]) { |i| !i.same?(before, masks(i)) }
  key_s = (now - t1).round(1)
  input("move", 420, 60)
  img = settle(opts[:timeout], ref: Regress::Screens::Image.load(File.join(BASELINE, "editor_type.png")))
  result("editor opened", true, "window up #{editor_s} s after the menu click")
  compare("editor_type", img, File.join(BASELINE, "editor_type.png"))
  puts "  the key reached the screen #{key_s} s after it was sent"

  # 5. where it wrote
  out, = dexec("find /usr/share/family-mruby/root -type f -print0 | sort -z | xargs -0 md5sum > /tmp/root.after; " \
               "cmp -s /tmp/root.before /tmp/root.after && echo same || echo changed; " \
               "find /usr/share/family-mruby/root -newer /tmp/marker | wc -l")
  same, newer = out.split("\n")
  result("distribution tree untouched", same == "same" && newer.to_i.zero?,
         "md5 of every file #{same}, #{newer} entries newer than the start")
  out, = dexec("ls -d /root/.local/share/family-mruby/* /root/.cache/family-mruby/* 2>&1; " \
               "ls /root/.local/share/family-mruby/etc")
  File.write(File.join(OUT, "user_dirs.txt"), out)
  user_ok = out.include?("/root/.local/share/family-mruby/etc") && out.include?("/root/.local/share/family-mruby/home") &&
            out.include?("/root/.cache/family-mruby/data") && out.include?("system_conf.toml")
  result("user files under root's XDG dirs", user_ok, out.split("\n").first(12).join(" "))
  out, = dexec("find / -xdev -newer /tmp/marker -not -type d " \
               "-not -path '/proc/*' -not -path '/sys/*' -not -path '/tmp/*' -not -path '/run/fmrb/*' " \
               "-not -path '/root/.local/share/family-mruby/*' -not -path '/root/.cache/family-mruby/*' 2>/dev/null")
  File.write(File.join(OUT, "other_writes.txt"), out)
  result("nothing written elsewhere", out.strip.empty?, out.strip.empty? ? "none" : out.split("\n").first(5).join(" "))

  ctl("quit", timeout: 30)
  sleep 2
  FileUtils.cp(log, File.join(OUT, "core.log"))

  # 6. an /etc from before L2n (no zenoh_router, the browser's service list
  #    without net and timesync), with the user's own values in it: the next
  #    start adds what is missing and changes nothing the user has.
  etc_dir = File.join(OUT, "etc_old")
  FileUtils.mkdir_p(etc_dir)
  conf = sh!("git", "-C", CORE, "show", "#{OLD_ETC_COMMIT}:config/system_conf_native.toml")[0]
  conf = conf.sub(/^audio_mute = false$/, "audio_mute = true # the user's").sub(/^language = "en".*$/, 'language = "ja"')
  services = sh!("git", "-C", CORE, "show", "#{OLD_ETC_COMMIT}:config/services_wasm.toml")[0]
  services = services.sub(/(\[clock\][^\[]*?enable = )true/m, '\\1false')
  File.write(File.join(etc_dir, "system_conf.toml"), conf)
  File.write(File.join(etc_dir, "services.toml"), services)
  user_etc = "/root/.local/share/family-mruby/etc"
  dexec("rm -f #{user_etc}/system_conf.toml #{user_etc}/services.toml #{user_etc}/associations.toml")
  sh!("docker", "cp", File.join(etc_dir, "system_conf.toml"), "#{NAME}:#{user_etc}/system_conf.toml")
  sh!("docker", "cp", File.join(etc_dir, "services.toml"), "#{NAME}:#{user_etc}/services.toml")
  File.delete(log)
  sh!("docker", "exec", "-d", NAME, "bash", "-c",
      "umask 0; cd /root; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy FMRB_NATIVE_RUN_DIR=/run/fmrb " \
      "exec #{bin} --machine sdl > /run/fmrb/core.log 2>&1")
  deadline = now + opts[:timeout]
  sleep 0.5 until (File.exist?(log) && File.read(log).include?("Boot complete")) || now > deadline
  text = File.read(log)
  FileUtils.cp(log, File.join(OUT, "core.etc.log"))
  ctl("quit", timeout: 30)
  sleep 2
  new_dir = File.join(OUT, "etc_new")
  FileUtils.mkdir_p(new_dir)
  %w[system_conf.toml services.toml associations.toml].each do |f|
    sh!("docker", "cp", "#{NAME}:#{user_etc}/#{f}", File.join(new_dir, f), ok: false)
  end
  added = text.scan(%r{native: /etc: (.*)}).flatten
  File.write(File.join(OUT, "etc_added.txt"), added.join("\n") + "\n")
  new_conf = File.read(File.join(new_dir, "system_conf.toml"))
  new_services = File.read(File.join(new_dir, "services.toml"))
  # Only additions: the old text is still there, in one piece, line for line.
  kept = [[conf, new_conf], [services, new_services]].all? do |old, new|
    rest = new.lines
    old.lines.all? { |l| (i = rest.index(l)) && (rest = rest[(i + 1)..]) }
  end
  values = new_conf.include?("audio_mute = true # the user's") && new_conf.include?('language = "ja"') &&
           new_conf =~ /^zenoh_router = / && new_services =~ /^\[net\]/ && new_services =~ /^\[timesync\.config\]/ &&
           new_services[/\[clock\][^\[]*/].include?("enable = false") && File.exist?(File.join(new_dir, "associations.toml"))
  result("/etc update", text.include?("Boot complete") && kept && values && !added.empty?,
         "#{added.length} log line(s): #{added.join('; ')}; old lines all kept in order: #{kept}; user values kept and new items present: #{!!values}")

  # 7. debugd turned on in system_conf: 127.0.0.1:5565 only. A client in the
  #    container reaches it; the container's own address does not, from the
  #    host or from inside.
  dexec("sed -i 's/^debugd = false$/debugd = true/' #{user_etc}/system_conf.toml")
  File.delete(log)
  sh!("docker", "exec", "-d", NAME, "bash", "-c",
      "umask 0; cd /root; SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy FMRB_NATIVE_RUN_DIR=/run/fmrb " \
      "exec #{bin} --machine sdl > /run/fmrb/core.log 2>&1")
  deadline = now + opts[:timeout]
  sleep 0.5 until (File.exist?(log) && File.read(log).include?("Boot complete")) || now > deadline
  text = File.read(log)
  FileUtils.cp(log, File.join(OUT, "core.debugd.log"))
  listen = tcp_listeners
  ip, = sh!("docker", "inspect", "-f", "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}", NAME)
  ip = ip.strip
  inside, = dexec("timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/#{DEBUGD_PORT}' 2>/dev/null && echo open || echo closed", ok: false)
  via_ip, = dexec("timeout 3 bash -c 'exec 3<>/dev/tcp/#{ip}/#{DEBUGD_PORT}' 2>/dev/null && echo open || echo closed", ok: false)
  from_host = begin
    Socket.tcp(ip, DEBUGD_PORT, connect_timeout: 3).close
    "open"
  rescue StandardError => e
    "closed (#{e.class.name.split('::').last})"
  end
  ok = listen == ["127.0.0.1:#{DEBUGD_PORT}"] && inside.strip == "open" && via_ip.strip == "closed" && from_host.start_with?("closed")
  result("debugd on: loopback only", ok,
         "listeners #{listen.join(' ')}; 127.0.0.1 in the container #{inside.strip}, #{ip} in the container #{via_ip.strip}, " \
         "#{ip} from the host #{from_host}; log: #{text[/listening on .*/]}")
  ctl("quit", timeout: 30)
  sleep 2
rescue StandardError => e
  result("run", false, e.message)
ensure
  sh!("docker", "rm", "-f", NAME, ok: false) unless opts[:keep]
  # The sockets are root's files in our directory: ours to unlink.
  FileUtils.rm_rf(RUN) unless opts[:keep]
  File.write(File.join(OUT, "summary.json"), JSON.pretty_generate($results))
  failed = $results.reject { |r| r[:pass] }
  puts "deb_check #{arch}: #{$results.length - failed.length}/#{$results.length} passed (#{OUT.sub("#{CORE}/", '')})"
  exit(failed.empty? ? 0 : 1)
end
