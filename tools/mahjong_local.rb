#!/usr/bin/env ruby
# frozen_string_literal: true
#
# mahjong_local.rb - bring up everything a mahjong game in the browser build
# needs on this PC, or take it down again (doc/ruby_asterism/report/mj_layout.md).
#
#   ruby tools/mahjong_local.rb up       # zenohd, the game host, the page server
#   ruby tools/mahjong_local.rb status
#   ruby tools/mahjong_local.rb down
#
# (also rake mahjong:up / mahjong:status / mahjong:down)
#
# up starts, in order, and leaves running:
#   1. zenohd with its WebSocket listener (docker compose in the repository
#      root above fmruby-core: docker-compose.yml + docker/asterism/
#      docker-compose.zenoh-ws.yml; tcp 7447 and ws 7449 on loopback).
#   2. the game host (asterism-games next to fmruby-core, or MAHJONG_GAMES_DIR),
#      config/example.yml: two tables, mahjong1 and mahjong2. The MAHJONG_*
#      settings of the environment pass through (MAHJONG_CPU_MS and the rest).
#   3. the page server (rake wasm:serve DIST=1, PORT or 8006) for the
#      self-contained bundle rake wasm:dist made (wasm/build/dist: the page and
#      the machine of the branch it was built on, so the working copy can move
#      to another branch while it is served).
# Each part that is already running is left as it is. Then it prints the
# address to open. down stops the host and the server this tool started and
# stops zenohd (KEEP_ZENOHD=1 leaves it).
#
# The browser reaches all of it through localhost (the page server and the
# WebSocket of zenohd); a Windows browser reaches WSL's localhost the same way.

require "fileutils"
require "net/http"
require "socket"

# Run from a file, fmruby-core is one up; run as `git show <branch>:tools/
# mahjong_local.rb | ruby - down` (a working copy on another branch), it is
# the current directory.
CORE = __FILE__ == "-" ? Dir.pwd : File.expand_path("..", __dir__)
ROOT = File.expand_path("..", CORE)
GAMES = File.expand_path(ENV["MAHJONG_GAMES_DIR"] || File.join(ROOT, "asterism-games"))
RUN = File.join(CORE, "wasm", "build", "mahjong_local")
PORT = (ENV["PORT"] || 8006).to_i
WS_PORT = 7449
COMPOSE = ["docker", "compose", "-f", "docker-compose.yml",
           "-f", "docker/asterism/docker-compose.zenoh-ws.yml"].freeze
QUERY = "?w=1280&h=720&app=/app/game/mahjong/mahjong.app.rb"
DIST = File.join(CORE, "wasm", "build", "dist")

# The page's address on the server listening on PORT: the dist bundle is at
# the root, the development tree under wasm/web/.
def url
  root = Net::HTTP.start("127.0.0.1", PORT, open_timeout: 1, read_timeout: 2) { |h| h.head("/index.html").code } rescue nil
  path = root == "200" ? "/index.html" : "/wasm/web/index.html"
  "http://localhost:#{PORT}#{path}#{QUERY}"
end

def listening?(port)
  Socket.tcp("127.0.0.1", port, connect_timeout: 0.5).close
  true
rescue StandardError
  false
end

def pid_alive?(name)
  f = File.join(RUN, "#{name}.pid")
  return nil unless File.exist?(f)
  pid = File.read(f).to_i
  Process.kill(0, pid)
  pid
rescue Errno::ESRCH, Errno::EPERM
  nil
end

def spawn_logged(name, env, cmd, dir)
  FileUtils.mkdir_p(RUN)
  log = File.join(RUN, "#{name}.log")
  pid = Process.spawn(env, *cmd, chdir: dir, out: [log, "a"], err: [:child, :out], pgroup: true)
  Process.detach(pid)
  File.write(File.join(RUN, "#{name}.pid"), "#{pid}\n")
  pid
end

# What a log has after byte offset start (File.size before the process
# started). Read as bytes: the log has names in Japanese, and a character
# index at a byte offset lands past the end once there is one.
def log_since(log, start)
  return "" unless File.exist?(log)
  File.open(log, "rb") do |f|
    f.seek(start)
    f.read.to_s
  end
end

def wait_for(what, seconds)
  t = Time.now
  until yield
    abort "#{what}: not up after #{seconds} s" if Time.now - t > seconds
    sleep 0.5
  end
end

def up
  abort "#{GAMES}: no asterism-games here (set MAHJONG_GAMES_DIR)" unless File.exist?(File.join(GAMES, "bin", "asterism-games"))
  abort "no browser bundle: run `rake wasm:dist` in fmruby-core first" unless File.exist?(File.join(DIST, "index.html"))

  # 1. zenohd
  if listening?(WS_PORT)
    puts "zenohd: already listening on ws #{WS_PORT}"
  else
    system(*COMPOSE, "up", "-d", "zenohd", chdir: ROOT) || abort("zenohd: docker compose failed")
    wait_for("zenohd", 30) { listening?(WS_PORT) && listening?(7447) }
    puts "zenohd: up (tcp 7447, ws #{WS_PORT})"
  end

  # 2. the game host
  if (pid = pid_alive?("host"))
    puts "game host: already running (pid #{pid})"
  else
    log = File.join(RUN, "host.log")
    start = File.exist?(log) ? File.size(log) : 0
    env = { "ASTERISM_GAMES_CONFIG" => ENV["ASTERISM_GAMES_CONFIG"] || "config/example.yml" }
    pid = spawn_logged("host", env, %w[bundle exec bin/asterism-games], GAMES)
    wait_for("game host (#{log})", 60) { log_since(log, start).include?(" up (") }
    puts "game host: up (pid #{pid}, log #{log})"
  end

  # 3. the page server
  if listening?(PORT)
    puts "page server: already listening on #{PORT}"
  else
    pid = spawn_logged("serve", {}, ["rake", "wasm:serve", "PORT=#{PORT}", "DIST=1"], CORE)
    wait_for("page server", 30) { listening?(PORT) }
    puts "page server: up (pid #{pid}, port #{PORT})"
  end

  puts
  puts "Open (Chrome or Edge on this PC, Windows included):"
  puts "  #{url}"
  puts "Stop everything: ruby tools/mahjong_local.rb down (or rake mahjong:down)"
end

def stop(name)
  pid = pid_alive?(name)
  File.delete(File.join(RUN, "#{name}.pid")) if File.exist?(File.join(RUN, "#{name}.pid"))
  return puts("#{name}: not running (not started by this tool)") unless pid
  Process.kill("TERM", -pid) # the whole group (bundle/rake and what they started)
  20.times do
    break unless (Process.kill(0, pid) rescue nil)
    sleep 0.25
  end
  puts "#{name}: stopped"
rescue Errno::ESRCH
  puts "#{name}: stopped"
end

def down
  stop("host")
  stop("serve")
  if ENV["KEEP_ZENOHD"]
    puts "zenohd: left running (KEEP_ZENOHD)"
  else
    system(*COMPOSE, "stop", "zenohd", chdir: ROOT)
    puts "zenohd: stopped"
  end
end

def status
  puts "zenohd:      #{listening?(WS_PORT) ? "listening (ws #{WS_PORT})" : 'down'}"
  h = pid_alive?("host")
  puts "game host:   #{h ? "running (pid #{h})" : 'not started by this tool'}"
  s = pid_alive?("serve")
  puts "page server: #{listening?(PORT) ? "listening on #{PORT}" : 'down'}#{s ? " (pid #{s})" : ''}"
  puts "open:        #{url}" if listening?(PORT)
end

case ARGV[0]
when "up" then up
when "down" then down
when "status" then status
else
  puts File.read(__FILE__).lines.drop(2).take_while { |l| l.start_with?("#") }.map { |l| l.sub(/^# ?/, "") }.join
end
