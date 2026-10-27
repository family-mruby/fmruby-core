#!/usr/bin/env ruby
# frozen_string_literal: true
#
# sdl_clip_check.rb - the machine's clipboard joined to SDL's on the "sdl"
# machine (doc/linux_native/, L12a), with the program rake native:core
# built, on an X server of its own (Xvfb, nothing on the screen) and
# another SDL program (clip_peer.c) standing for a desktop application.
#
#   ruby test/native/sdl/sdl_clip_check.rb [--engine standard|mruby]
#
# HOME, the XDG places, the user and cache directories and the run
# directory are all under one temporary directory, removed at the end.
#
#   1. an app's FmrbApp.set_clipboard: the peer reads the text (the desktop
#      clipboard); a text over 64 KB is refused (false)
#   2. the peer copies: the app sees the generation change and reads the
#      peer's text (SDL tells the change when the window had owned the
#      clipboard; see output_sdl.c)
#   3. the control socket's clip counters agree
#
# Needs Xvfb and the SDL2 development files (sdl2-config).

require "fileutils"
require "open3"
require "optparse"
require "socket"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
DEBUG_PORT = 15_568
FM_TEXT = "fm: アプリからデスクトップへ"
PEER_TEXT = "peer: デスクトップから日本語"

opts = { engine: "standard" }
OptionParser.new { |o| o.on("--engine E") { |v| opts[:engine] = v } }.parse!
abort "--engine standard|mruby" unless %w[standard mruby].include?(opts[:engine])
BIN = File.join(CORE, "native", opts[:engine] == "mruby" ? "build-mruby" : "build", "fmruby-native")
abort "#{BIN} not found; rake native:core" unless File.exist?(BIN)
abort "Xvfb not found" unless system("which Xvfb > /dev/null 2>&1")

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def wait_for(what, timeout)
  deadline = now + timeout
  loop do
    v = yield
    return v if v
    raise "#{what}: not within #{timeout}s" if now > deadline
    sleep 0.2
  end
end

tmp = "/tmp/fmrb-sdlclip-#{Process.uid}"
FileUtils.rm_rf(tmp)
FileUtils.mkdir_p(%W[#{tmp}/home #{tmp}/user/home #{tmp}/cache #{tmp}/run])
File.chmod(0o700, "#{tmp}/run")
peer = "#{tmp}/clip_peer"
out, st = Open3.capture2e("sh", "-c", "cc -O1 #{File.join(__dir__, 'clip_peer.c')} $(sdl2-config --cflags --libs) -o #{peer}")
abort "clip_peer: #{out}" unless st.success?
File.write("#{tmp}/user/home/cliptest.app.rb", <<~RUBY)
  class ClipTest < FmrbApp
    def on_create
      big = "x" * 70000
      Log.info("cliptest: big " + FmrbApp.set_clipboard(big).to_s)
      Log.info("cliptest: set " + FmrbApp.set_clipboard(#{FM_TEXT.inspect}).to_s)
      @g = FmrbApp.clipboard_gen
    end

    def on_update
      g = FmrbApp.clipboard_gen
      if g != @g
        @g = g
        Log.info("cliptest: changed " + FmrbApp.clipboard)
      end
      100
    end
  end
  ClipTest.new.start
RUBY

# An X server of its own: the first display number free (Xvfb ends at once
# on one in use), ready when its (abstract) socket answers.
def x_ready?(n)
  UNIXSocket.new("\0/tmp/.X11-unix/X#{n}").close
  true
rescue StandardError
  false
end

display = nil
xvfb = nil
(90..120).each do |n|
  next if x_ready?(n)
  pid = Process.spawn("Xvfb", ":#{n}", "-screen", "0", "1600x1000x24", out: "#{tmp}/xvfb.log", err: [:child, :out])
  ok = begin
    wait_for("Xvfb :#{n}", 10) { Process.wait(pid, Process::WNOHANG) ? :gone : (x_ready?(n) && :up) }
  rescue StandardError
    :gone
  end
  if ok == :up
    display = n
    xvfb = pid
    break
  end
  Process.kill("TERM", pid) rescue nil
end
abort "Xvfb did not start (#{File.read("#{tmp}/xvfb.log") rescue ''})" if display.nil?
env = { "HOME" => "#{tmp}/home", "XDG_DATA_HOME" => "#{tmp}/xdgdata", "XDG_CONFIG_HOME" => "#{tmp}/xdgconf",
        "XDG_CACHE_HOME" => "#{tmp}/xdgcache", "FMRB_NATIVE_RUN_DIR" => "#{tmp}/run",
        "FMRB_DEBUG_TCP_PORT" => DEBUG_PORT.to_s, "DISPLAY" => ":#{display}", "SDL_VIDEODRIVER" => "x11",
        "SDL_AUDIODRIVER" => "dummy", "WAYLAND_DISPLAY" => nil }
# The tools themselves (the debug client is Python, with its packages under
# the real home) only need the run directory and the port.
tool_env = { "FMRB_NATIVE_RUN_DIR" => "#{tmp}/run", "FMRB_DEBUG_TCP_PORT" => DEBUG_PORT.to_s }
results = []
log = "#{tmp}/run/core.log"
log_text = -> { File.exist?(log) ? File.read(log).force_encoding("UTF-8").scrub : "" }
begin
  up, = Open3.capture2e(env, "ruby", File.join(TOOLS, "fmrb_native.rb"), "up", "--gui", "--engine", opts[:engine],
                        "--user-dir", "#{tmp}/user", "--cache-dir", "#{tmp}/cache", "--timeout", "90")
  raise "up: #{up}" unless up.include?("boot complete")
  sp = ""
  wait_for("the app set its text", 30) do
    if sp !~ /"pid"/
      sp, = Open3.capture2e(tool_env, "ruby", File.join(TOOLS, "fmrb_native.rb"), "app", "spawn", "/home/cliptest.app.rb")
    end
    log_text.call.include?("cliptest: set")
  end rescue raise("#{$!.message} (spawn: #{sp.strip})")
  sleep 0.5
  got, = Open3.capture2e(env, peer, "get")
  got = got.force_encoding("UTF-8").strip
  big = log_text.call[/cliptest: big (\w+)/, 1]
  results << ["app -> desktop", got == FM_TEXT && big == "false",
              "set_clipboard: the peer read #{got.inspect}; 70000 bytes: #{big}"]

  pid = Process.spawn(env, peer, "set", PEER_TEXT, "3")
  seen = wait_for("the app saw the peer's text", 10) { log_text.call[/cliptest: changed ([^\n]*)/, 1] } rescue nil
  Process.wait(pid)
  results << ["desktop -> app", seen == PEER_TEXT,
              "the peer copied: the app read #{seen.inspect} (#{log_text.call[/native: clipboard: took[^\n]*/]})"]

  sock = UNIXSocket.new("#{tmp}/run/ctl")
  sock.write("clip\n")
  counts = sock.read.to_s.strip
  sock.close
  kv = counts.split.drop(1).to_h { |x| x.split("=", 2) }
  results << ["counts", kv["joined"] == "1" && kv["out"] == "1" && kv["in"] == "1" && kv["taken"] == "1", counts]
rescue StandardError => e
  results << ["run", false, e.message.lines.first(3).join.strip]
ensure
  Open3.capture2e(tool_env, "ruby", File.join(TOOLS, "fmrb_native.rb"), "down")
  Process.kill("TERM", xvfb) rescue nil
  Process.wait(xvfb) rescue nil
  FileUtils.cp(log, File.join(CORE, "tmp", "sdl_clip_check.log")) if File.exist?(log) && Dir.exist?(File.join(CORE, "tmp"))
  FileUtils.rm_rf(tmp)
end

results.each { |name, pass, detail| puts "#{pass ? 'ok  ' : 'FAIL'} sdl clip #{opts[:engine]} #{name}: #{detail}" }
failed = results.reject { |r| r[1] }
puts "sdl_clip_check #{opts[:engine]}: #{results.size - failed.size}/#{results.size} passed"
exit(failed.empty? ? 0 : 1)
