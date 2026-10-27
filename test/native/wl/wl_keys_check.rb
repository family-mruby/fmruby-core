#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_keys_check.rb - the keyboard layout from Linux's settings (doc/
# linux_native/, L9d), in the Ubuntu 26.04 image with the program built by
# rake native:wl, headless, --machine linux_pc_wl, foot started on it.
#
#   ruby test/native/wl/wl_keys_check.rb [--engine standard|mruby]
#                                        [--renderer pixman|auto] [--keep]
#
# Each case boots the program with a made-up /etc/default/keyboard (a file
# of the run directory mounted read-only over the container's) and the
# user's system_conf seeded with a keyboard_layout (a file of the run
# directory, XDG_DATA_HOME). Nothing is written outside the run directory
# and the container. Then the same keys -- the two right of P and the one
# left of Backspace -- go to foot (the Linux window's keymap) and to Family
# mruby's Shell (its own key table), each writing what it got to a file:
# "@[^" on jp, "[]=" on us.
#
#   1. Linux jp, keyboard_layout auto (not chosen): jp for both, and the
#      log's one line says where it came from
#   2. Linux us, auto: us for both
#   3. Linux jp, keyboard_layout us (chosen in Config): us for both
#
# Shots, logs and a summary go to tmp/wl_check/keys-<engine>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
DEBUG_PORT = 15_565
# Linux key codes (input-event-codes.h).
KEY = { "1" => 2, "e" => 18, "c" => 46, "h" => 35, "o" => 24, " " => 57, "." => 52, "/" => 53, "r" => 19,
        "u" => 22, "n" => 49, "f" => 33, "m" => 50, "b" => 48, "t" => 20, "y" => 21, "p" => 25, "d" => 32,
        "\n" => 28 }.freeze
KEY_LEFTSHIFT = 42
# The keys whose symbols tell the layouts apart: right of P, the next one,
# left of Backspace.
PROBE = [26, 27, 13].freeze
WANT = { "jp" => "@[^", "us" => "[]=" }.freeze

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

class KeysRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-keys-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-keys-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "keys-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "keys #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} keys #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  def prepare(linux, conf_layout)
    sh!("docker", "rm", "-f", @name, ok: false)
    FileUtils.rm_rf(@run)
    FileUtils.mkdir_p(File.join(@run, "data/family-mruby-dev/etc"))
    File.write(File.join(@run, "keyboard"), "XKBMODEL=\"pc105\"\nXKBLAYOUT=\"#{linux}\"\nXKBVARIANT=\"\"\n")
    conf = File.read(File.join(CORE, "native/build/root/etc/system_conf.toml"))
    conf = conf.sub(/^keyboard_layout = .*$/, "keyboard_layout = \"#{conf_layout}\"")
    File.write(File.join(@run, "data/family-mruby-dev/etc/system_conf.toml"), conf)
  end

  def start
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-v", "#{File.join(@run, 'keyboard')}:/etc/default/keyboard:ro",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/tmp/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    prog = "#{BIN} --machine linux_pc_wl --set display.mode=1280x800 " \
           "--set \"compositor.autostart=foot -e sh -c 'cat > /run/fmrb/typed'\""
    inner = "mkdir -p -m 700 /run/fmrb/xdg /tmp/home && #{prog} > /run/fmrb/core.log 2>&1; " \
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
    data
  end

  def comp(*words)
    r = ctl("comp #{words.join(' ')}")
    raise "comp #{words.join(' ')}: #{r}" unless r.start_with?("OK")
    r
  end

  def info
    h = comp("info").split.drop(1).to_h { |kv| kv.split("=", 2) }
    re = %r{(\d+):(\d+)x(\d+)\+(-?\d+)\+(-?\d+)/(-?\d+)x(-?\d+)\+(\d+)x(\d+)/(\d)}
    h["windows"] = h["windows"].to_s.scan(re).map do |v|
      id, ww, wh, x, y, = v.map(&:to_i)
      { id: id, w: ww, h: wh, x: x, y: y, vis: v[9] == "1" }
    end
    h
  end

  def tap(code, shift: false)
    comp("key", KEY_LEFTSHIFT, 1) if shift
    comp("key", code, 1)
    comp("key", code, 0)
    comp("key", KEY_LEFTSHIFT, 0) if shift
  end

  def type(text) = text.each_char { |c| tap(KEY.fetch(c)) }

  def probe = PROBE.each { |k| tap(k) }

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
  def file(name) = File.exist?(File.join(@run, name)) ? File.read(File.join(@run, name)) : ""

  def native_tool(*args, timeout: 30)
    cmd = ["timeout", timeout.to_s, "ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, *args.map(&:to_s)]
    sh!({ "FMRB_DEBUG_TCP_PORT" => DEBUG_PORT.to_s }, *cmd, ok: false)
  end

  # One case: what foot and Family mruby's Shell make of the probe keys.
  def one(label, linux, conf_layout, want)
    tag = label.tr(" ,", "_").squeeze("_")
    prepare(linux, conf_layout)
    start
    wait_for("Boot complete", 120) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    foot = wait_for("foot placed", 60) { info["windows"].find { |w| w[:vis] } }
    sleep 1.0
    # foot: a click on it (its middle is a hole), then the keys.
    comp("click", foot[:x] + foot[:w] / 2, foot[:y] + foot[:h] / 2)
    sleep 0.6
    probe
    tap(KEY["\n"])
    got_foot = wait_for("foot's file", 10) { file("typed").end_with?("\n") && file("typed") } rescue file("typed")
    keymap = info["keymap"]
    # Family mruby: its Shell, a Linux command that writes what it got.
    out, = native_tool("app", "spawn", "default/shell")
    raise "spawn the Shell: #{out.strip}" unless out.include?("pid")
    sleep 2.5
    tap(KEY["1"], shift: true)
    type("echo ")
    probe
    type(" ")
    tap(KEY["."], shift: true)
    type(" /run/fmrb/fmtyped\n")
    got_fm = wait_for("the Shell's file", 15) { file("fmtyped").end_with?("\n") && file("fmtyped") } rescue file("fmtyped")
    native_tool("shot", File.join(@out, "#{tag}_shell.png"))
    line = log_text[/kernel: Keyboard layout.*/].to_s.strip
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "#{tag}_core.log"))
    ok = got_foot == "#{WANT[want]}\n" && got_fm == "#{WANT[want]}\n" && keymap == want
    ok &&= line.include?("auto -> #{want}") if conf_layout == "auto"
    result(label, ok,
           "Linux #{linux}, keyboard_layout #{conf_layout}: foot got #{got_foot.inspect}, the Shell #{got_fm.inspect} " \
           "(#{want}: #{WANT[want].inspect}); the windows' keymap #{keymap}; log: #{line}")
  ensure
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
  end

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    [["Linux jp, not chosen", "jp", "auto", "jp"],
     ["Linux us, not chosen", "us", "auto", "us"],
     ["Linux jp, us chosen", "jp", "us", "us"]].each do |label, linux, conf, want|
      one(label, linux, conf, want)
    rescue StandardError => e
      result(label, false, e.message.lines.first(3).join.strip)
    end
  ensure
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = KeysRun.new(opts[:renderer], opts[:keep])
run.go
all = run.results
FileUtils.mkdir_p(File.join(CORE, "tmp/wl_check"))
File.write(File.join(CORE, "tmp/wl_check", "summary-keys-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(all))
passed = all.count { |r| r[:pass] }
puts "wl_keys_check #{ENGINE} #{opts[:renderer]}: #{passed}/#{all.size} passed (tmp/wl_check/)"
exit(passed == all.size && !all.empty? ? 0 : 1)
