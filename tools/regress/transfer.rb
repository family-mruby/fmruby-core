#!/usr/bin/env ruby
# frozen_string_literal: true
#
# transfer.rb - a file carried between two native programs over Asterism with
# faults put in on purpose (rake regress:transfer, doc/asterism_file_transfer
# T1). Asterism::Transfer (the asterism gem's data/transfer, which the
# transfer app uses) under the test app /app/test/xfer_probe.app.rb on both
# programs: the first sends a 1 MiB file of random bytes, the second
# receives it as the kind "probe".
#
#   ruby tools/regress/transfer.rb               # the native builds there are
#   ruby tools/regress/transfer.rb --only cut    # one case (a part of its name)
#   ruby tools/regress/transfer.rb --engine mruby
#   ruby tools/regress/transfer.rb --bench       # T0's numbers instead (below)
#   ruby tools/regress/transfer.rb --bench --web # T0's numbers with the browser build
#   ruby tools/regress/transfer.rb --peer NODE --router tcp/HOST:7447
#                                                # a board as the receiver (below)
#
# A Zenoh router of the run's own (as regress:checkpoint's, in a container on
# loopback ports), the first program straight to it, the second through a
# relay in this process that can cut the connection (CutRelay). The cases:
#
#   straight   nothing broken: whole, the sha256 matches
#   drop       every 10th piece left out (a tenth): each missing piece is
#              asked for at the position the receiver stands
#   twice      every 5th piece sent two times: the second is read past
#   swap       every 7th piece sent after the one that follows it
#   cut        the second program's connection cut twice on the way (at 256
#              and 640 KB, down for 1.5 s each): the sender asks again and
#              goes on from where the receiver stood each time
#   restart    the receiver's app stopped on the way and started again: it
#              takes up the piece file it kept and the sender goes on from
#              there, not from 0
#   mismatch   the sender declares a wrong sha256: everything arrives, nothing
#              is placed, and the sender is told why
#   kept       the sender stopped on the way: the receiver deletes the piece
#              file once its time (3 s here) has gone by
#   file       the kind "file" through the transfer apps (T2): the first
#              program's probe asks its transfer app to send (FmrbTransfer)
#              to the second's, three times: placed in /home/inbox/, then
#              beside it under name(1), then over the first (overwrite)
#   big        a 4 MiB "file" from the first program's probe to the second's
#              transfer app (T3), the second's connection cut twice on the
#              way (at 1 and 3 MiB): the pieces are written straight to a
#              hidden file in /home/inbox/ (not to /tmp, looked at while it
#              comes), it goes on from where the receiver stood, the sha256
#              matches, and the piece size and window are the receiver's
#
# --bench (doc/asterism_file_transfer/report/t0.md): instead of the cases,
# one call of each size (the largest that goes through), the file at each
# piece size and number of pieces on their way, and SHA-256 / CRC32 of it,
# printed as the probe logs them. --size N (bytes; the file is in /home
# and the receiver writes to /home past 1 MiB, /tmp being smaller),
# --pieces LIST and --windows LIST change the speed runs; --speed only
# those; --file adds one run of the kind "file" to the transfer app there,
# with the piece and window it names (report/t3.md).
#
# --bench --web: the browser build (wasm/build/dist, rake wasm:dist; emsdk is
# not needed to serve it) on a development server and a headless Linux
# Chromium of the run's own (as regress:checkpoint's browser rounds), opened
# with ?app= the probe: SHA-256 and CRC32 there first, then the probe there
# receives the native program's calls of each size and the file at each
# piece size and window.
#
# --peer NODE --router LOCATOR (a board, doc/asterism_file_transfer/report/t1.md
# 7): the receiver is the probe on the board NODE, which a person starts
# (its /home/xfer_probe.job {"keep_ms": 3000}, its receive.toml taking
# rg-xfer-a), on the board's router; only the first program is started
# here, and it is the one connected through the relay (cut cuts it). The
# cases are judged from the sender's side (commit there checked the
# sha256): straight, drop, twice, swap, cut, mismatch, big (the board's
# transfer app: its receive.toml's [file] takes rg-xfer-a, and /home has
# 4 MiB free; the piece file's place is not looked at); --bench too.
# restart and kept need the board's app stopped by hand (the report's
# steps).
#
# Every case that arrives checks the file there byte for byte (its sha256),
# and the positions the sending went on from are read from both logs. The
# summary is one line; the programs' logs are kept in tmp/regress/transfer/.
require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "tree"

module Regress
  module Transfer
    CORE = File.expand_path("../..", __dir__)
    ROOT = Tree::ROOT
    NATIVE_TOOL = File.join(ROOT, "tools", "fmrb_native.rb")
    OUT = File.join(CORE, "tmp", "regress", "transfer")
    # this tree's own (tree.rb): the shared checkout's are below; another
    # tree's take regress:checkpoint's ports of its block (the two runs are
    # not run side by side in one tree; a port in use stops the run)
    RUN_DIR_A = Tree.run_dir("fmrb-native-xfer")
    RUN_DIR_B = Tree.run_dir("fmrb-native-xfer-b")
    PORT_A = Tree.port(5595, :ckpt_a)
    PORT_B = Tree.port(5596, :ckpt_b)
    ROUTER_NAME = Tree.name("fmrb_regress_xfer_zenohd")
    ROUTER_IMAGE = "eclipse/zenoh:1.10.1"
    ROUTER_PORT = Tree.port(17_467, :ckpt_router)
    RELAY_PORT = Tree.port(17_468, :ckpt_c)
    ROUTER_WS_PORT = Tree.port(17_469, :ckpt_router_ws)
    WEB_PORT = Tree.port(8077, :ckpt_web)
    WEB_DIST = File.join(CORE, "wasm", "build", "dist")
    NODE_WEB = "rg-xfer-web"
    NODE_A = "rg-xfer-a"
    NODE_B = "rg-xfer-b"
    PROBE = "/app/test/xfer_probe.app.rb"
    JOB = "/tmp/xfer_probe.job"
    SRC = "/tmp/xfer_src.bin"
    SIZE = 1_048_576
    # the big case's file, and where the sending is cut
    BIG = 4_194_304
    BIG_CUTS = [1_048_576, 3_145_728].freeze
    BIG_PACE_MS = 40
    # what the cases of T1 send: 3 KB pieces, one at a time (their counts
    # are of that)
    T1_SENDER = { "piece" => 3072, "window" => 1 }.freeze
    KEEP_MS = 3000
    # the sender's pace in the cases that break things on the way (a step at
    # most every 10 ms: about 4 s for the file, so that the break lands in
    # the middle)
    PACE_MS = 10
    # how long the connection stays cut
    CUT_DOWN_S = 1.5

    class Failure < StandardError; end

    # A TCP relay between a program and the router whose connections can be
    # cut: both ends closed at once, and for down seconds a new connection is
    # closed as soon as it comes (zenoh-pico reconnects within milliseconds,
    # and a cut between two calls would otherwise go unnoticed by them).
    class CutRelay
      def initialize(port, to_port, host: "127.0.0.1")
        @server = TCPServer.new("127.0.0.1", port)
        @to_host = host
        @to_port = to_port
        @pairs = []
        @lock = Mutex.new
        @cuts = 0
        @down_until = 0
        @thread = Thread.new { accept_loop }
      end

      attr_reader :cuts

      def accept_loop
        loop do
          c = @server.accept
          if Process.clock_gettime(Process::CLOCK_MONOTONIC) < @down_until
            c.close
            next
          end
          r = TCPSocket.new(@to_host, @to_port)
          # no Nagle: a call's last segment would wait for the ACK of the
          # one before (about 20 ms a call here)
          [c, r].each { |x| x.setsockopt(Socket::IPPROTO_TCP, Socket::TCP_NODELAY, 1) }
          @lock.synchronize { @pairs << [c, r] }
          [[c, r], [r, c]].each { |a, b| Thread.new { pump(a, b) } }
        end
      rescue IOError, SystemCallError
        nil
      end

      def pump(from, to)
        loop { to.write(from.readpartial(65_536)) }
      rescue IOError, SystemCallError
        [from, to].each { |s| s.close rescue nil } # rubocop:disable Style/RescueModifier
      end

      def cut!(down = 0)
        @down_until = Process.clock_gettime(Process::CLOCK_MONOTONIC) + down
        @lock.synchronize do
          @pairs.each { |p| p.each { |s| s.close rescue nil } } # rubocop:disable Style/RescueModifier
          @pairs.clear
          @cuts += 1
        end
      end

      def close
        @server.close rescue nil # rubocop:disable Style/RescueModifier
        cut!
      end
    end

    class Runner
      def initialize(opts)
        @opts = opts
        @engine = opts[:engine] || "standard"
        @to = opts[:peer] || NODE_B
      end

      def peer? = !@opts[:peer].nil?

      def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

      def run
        t0 = now
        FileUtils.mkdir_p(OUT)
        @src = File.join(OUT, "src.bin")
        File.binwrite(@src, Random.new(20_261_108).bytes(SIZE))
        @sha = Digest::SHA256.file(@src).hexdigest
        notes = []
        ok = true
        begin
          if @opts[:web]
            raise Failure, "--web is for --bench" unless @opts[:bench]
            router_up
            up(RUN_DIR_A, PORT_A, NODE_A, NODE_WEB, "tcp/127.0.0.1:#{ROUTER_PORT}")
            native(RUN_DIR_A, PORT_A, "file", "put", @src, SRC, timeout: 120)
            return web_bench
          elsif peer?
            host, port = @opts[:router].sub(%r{\Atcp/}, "").split(":")
            @relay = CutRelay.new(RELAY_PORT, port.to_i, host: host)
            up(RUN_DIR_A, PORT_A, NODE_A, @to, "tcp/127.0.0.1:#{RELAY_PORT}")
          else
            router_up
            @relay = CutRelay.new(RELAY_PORT, ROUTER_PORT)
            up(RUN_DIR_A, PORT_A, NODE_A, NODE_B, "tcp/127.0.0.1:#{ROUTER_PORT}")
            up(RUN_DIR_B, PORT_B, NODE_B, NODE_A, "tcp/127.0.0.1:#{RELAY_PORT}")
          end
          native(RUN_DIR_A, PORT_A, "file", "put", @src, SRC, timeout: 120)
          return bench if @opts[:bench]
          cases.each do |name|
            next if @opts[:only] && !name.include?(@opts[:only])
            next if peer? && %w[restart kept file].include?(name)
            notes << "#{name}: #{send("case_#{name}")}"
          rescue Failure => e
            ok = false
            notes << "#{name}: FAILED #{e.message}"
            kill_probes
          end
        rescue Failure => e
          ok = false
          notes << "FAILED #{e.message}"
        ensure
          keep_logs
          [[RUN_DIR_A, PORT_A], [RUN_DIR_B, PORT_B]].each { |d, p| down(d, p) }
          @relay&.close
          web_down
          router_down unless peer?
        end
        notes.each { |n| puts "  #{n}" }
        puts format("transfer: %s (%s, %.1f min)", ok ? "ok" : "FAILED", @engine, (now - t0) / 60.0)
        ok
      end

      def cases = %w[straight drop twice swap cut restart mismatch kept file big]

      BENCH_SIZES = [1024, 2048, 3072, 3584, 3840, 3968, 4000, 4032, 4064, 4096, 4608, 8192, 16_384, 65_536].freeze
      BENCH_PIECES = [512, 1024, 2048, 3072, 3840].freeze
      BENCH_WINDOWS = [1, 2, 4, 8].freeze

      # The receiver's job of the bench: it names the largest piece and 16 at
      # once, so that the sender's own most (each run's) is what counts.
      def bench_recv
        h = { "recv_piece" => 65_536, "recv_window" => 16 }
        h["recv_dir"] = "/home/xp/" if bench_size > SIZE
        h
      end

      def bench_size = @opts[:size] || SIZE

      # The bench's file in the first program: /tmp up to 1 MiB, else /home.
      def bench_src
        return SRC if bench_size <= SIZE
        home_a = File.join(OUT, "#{File.basename(RUN_DIR_A)}_home", "user", "home")
        File.binwrite(File.join(home_a, "xfer_src.bin"), Random.new(20_261_108).bytes(bench_size))
        "/home/xfer_src.bin"
      end

      def bench
        lines = []
        kill_probes
        unless peer?
          put_job(RUN_DIR_B, PORT_B, bench_recv)
          spawn_probe(RUN_DIR_B, PORT_B, NODE_B)
        end
        sizes = @opts[:sizes] || BENCH_SIZES
        src = bench_src
        [{ "bench" => "sizes", "to" => @to, "sizes" => sizes },
         { "bench" => "speed", "to" => @to, "path" => src, "pieces" => @opts[:pieces] || BENCH_PIECES,
           "windows" => @opts[:windows] || BENCH_WINDOWS },
         { "bench" => "hash", "path" => src }].each do |job|
          next if @opts[:sizes] && job["bench"] != "sizes"
          next if @opts[:speed] && job["bench"] != "speed"
          put_job(RUN_DIR_A, PORT_A, job)
          from = log_lines(RUN_DIR_A).size
          spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
          wait_line(RUN_DIR_A, /xfer_probe: bench done/, from, "the bench's end", timeout: 1800)
          lines.concat(log_lines(RUN_DIR_A)[from..].grep(/xfer_probe: (bench|done)/).map { |l| l[/xfer_probe: .*/] })
          pid = probe_pid(RUN_DIR_A, PORT_A)
          app(RUN_DIR_A, PORT_A, "kill", pid.to_s) if pid
          sleep 0.5
        end
        kill_probes
        lines.concat(file_bench(@to, RUN_DIR_B, PORT_B, src)) if @opts[:file] && !peer?
        lines.each { |l| puts l }
        true
      end

      # One file of the kind "file" from the first program's probe to the
      # transfer app of to, at the piece and window it names.
      def file_bench(to, dir, port, src)
        kill_probes
        if dir
          home = File.join(OUT, "#{File.basename(dir)}_home", "user", "home")
          File.write(File.join(home, "asterism", "receive.toml"), "enabled = true\nfrom = [\"#{NODE_A}\"]\n\n[file]\nfrom = [\"#{NODE_A}\"]\n")
        end
        put_job(RUN_DIR_A, PORT_A, { "to" => to, "app" => "transfer", "kind" => "file", "path" => src,
                                     "name" => "bench.bin", "overwrite" => true })
        from = log_lines(RUN_DIR_A).size
        spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
        i = wait_line(RUN_DIR_A, /xfer_probe: done /, from, "the file's end", timeout: 600)
        kill_probes
        ["file to #{to}'s transfer app: #{log_lines(RUN_DIR_A)[i][/xfer_probe: .*/]}"]
      end

      # ---- the browser build (--web) ----

      def web_bench
        raise Failure, "no browser bundle in #{WEB_DIST} (rake wasm:dist)" unless File.exist?(File.join(WEB_DIST, "index.html"))
        chrome = web_chrome or raise Failure, "no Linux Chromium (FMRB_CHROME, or npx playwright install chromium)"
        lines = []
        web_up(chrome)
        dir = File.join(OUT, "web_home")
        FileUtils.mkdir_p(dir)
        File.write(File.join(dir, "config.toml"), "router = \"ws/127.0.0.1:#{ROUTER_WS_PORT}\"\nnode = \"#{NODE_WEB}\"\n")
        File.write(File.join(dir, "receive.toml"), "enabled = true\nfrom = [\"#{NODE_A}\"]\n\n[file]\nfrom = [\"#{NODE_A}\"]\n")
        File.write(File.join(dir, "hash.json"), JSON.generate({ "bench" => "hash", "path" => "/tmp/xfer_hash.bin",
                                                                "make" => SIZE }))
        File.write(File.join(dir, "recv.json"), JSON.generate(bench_recv))
        web_tool("put", File.join(dir, "config.toml"), "/flash/home/asterism/config.toml")
        web_tool("put", File.join(dir, "receive.toml"), "/flash/home/asterism/receive.toml")
        unless @opts[:speed]
          web_tool("put", File.join(dir, "hash.json"), "/flash/home/xfer_probe.job")
          mark = web_lines.size
          web_tool("reload", timeout: 90)
          web_wait(/xfer_probe: bench done/, mark, "the browser's hash bench", timeout: 300)
          lines.concat(web_lines[mark..].grep(/xfer_probe: (bench|made)/).map { |l| "web: #{l[/xfer_probe: .*/]}" })
        end
        src = bench_src
        web_tool("put", File.join(dir, "recv.json"), "/flash/home/xfer_probe.job")
        mark = web_lines.size
        web_tool("reload", timeout: 90)
        web_wait(/xfer_probe: ready as #{NODE_WEB}/, mark, "the browser's probe ready", timeout: 90)
        [{ "bench" => "sizes", "to" => NODE_WEB, "sizes" => @opts[:sizes] || BENCH_SIZES },
         { "bench" => "speed", "to" => NODE_WEB, "path" => src, "pieces" => @opts[:pieces] || BENCH_PIECES,
           "windows" => @opts[:windows] || BENCH_WINDOWS }].each do |job|
          next if @opts[:speed] && job["bench"] != "speed"
          put_job(RUN_DIR_A, PORT_A, job)
          from = log_lines(RUN_DIR_A).size
          spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
          wait_line(RUN_DIR_A, /xfer_probe: bench done/, from, "the bench's end", timeout: 1800)
          lines.concat(log_lines(RUN_DIR_A)[from..].grep(/xfer_probe: (bench|done)/).map { |l| l[/xfer_probe: .*/] })
          pid = probe_pid(RUN_DIR_A, PORT_A)
          app(RUN_DIR_A, PORT_A, "kill", pid.to_s) if pid
          sleep 0.5
        end
        lines.concat(web_lines.grep(/transfer: .* is whole/).map { |l| "web: #{l[/transfer: .*/]}" })
        lines.concat(file_bench(NODE_WEB, nil, nil, src)) if @opts[:file]
        lines.concat(web_send_bench(dir)) if @opts[:web_send]
        lines.each { |l| puts l }
        true
      end

      # The other way: the browser's probe sends (its file made there) to
      # the first program's probe.
      def web_send_bench(dir)
        kill_probes
        put_job(RUN_DIR_A, PORT_A, bench_recv)
        spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
        path = bench_size > SIZE ? "/home/xfer_src.bin" : "/tmp/xfer_src.bin"
        File.write(File.join(dir, "send.json"), JSON.generate({ "bench" => "speed", "to" => NODE_A, "path" => path,
                                                                "make" => bench_size,
                                                                "pieces" => @opts[:pieces] || BENCH_PIECES,
                                                                "windows" => @opts[:windows] || BENCH_WINDOWS }))
        web_tool("put", File.join(dir, "send.json"), "/flash/home/xfer_probe.job")
        mark = web_lines.size
        web_tool("reload", timeout: 90)
        web_wait(/xfer_probe: bench done/, mark, "the browser's sending bench", timeout: 1800)
        web_lines[mark..].grep(/xfer_probe: (bench|done)/).map { |l| "web sends: #{l[/xfer_probe: .*/]}" }
      end

      def web_chrome
        return ENV["FMRB_CHROME"] if ENV["FMRB_CHROME"] && File.executable?(ENV["FMRB_CHROME"])
        found = %w[/usr/bin/google-chrome /usr/bin/chromium /usr/bin/chromium-browser].find { |p| File.executable?(p) }
        found || Dir.glob(File.expand_path("~/.cache/ms-playwright/chromium-*/chrome-linux*/chrome"))
                    .max_by { |p| p[/chromium-(\d+)/, 1].to_i }
      end

      def web_console = File.join(OUT, "web.chrome.log")

      def web_up(chrome)
        Tree.need_free_port!(WEB_PORT, "the development server")
        serve_log = File.join(OUT, "web.serve.log")
        @web_serve = Process.spawn({ "DIST" => "1", "PORT" => WEB_PORT.to_s }, "rake", "wasm:serve", chdir: CORE,
                                   in: File::NULL, out: serve_log, err: serve_log, pgroup: true)
        deadline = now + 30
        loop do
          Socket.tcp("127.0.0.1", WEB_PORT, connect_timeout: 0.5).close
          break
        rescue StandardError
          raise Failure, "the development server did not come up on #{WEB_PORT}" if now > deadline
          sleep 0.3
        end
        profile = File.join(OUT, "web_profile")
        FileUtils.rm_rf(profile)
        FileUtils.rm_f(web_console)
        url = "http://localhost:#{WEB_PORT}/?autostart=1&drive=1&app=#{PROBE}&zenoh=ws/127.0.0.1:#{ROUTER_WS_PORT}"
        @web_chrome = Process.spawn(chrome, "--headless=new", "--user-data-dir=#{profile}", "--no-first-run",
                                    "--no-default-browser-check", "--disable-gpu", "--mute-audio",
                                    "--enable-logging=stderr", "--v=0", url,
                                    in: File::NULL, out: File::NULL, err: web_console, pgroup: true)
        web_wait(/Boot complete/, 0, "the browser build's boot", timeout: 90)
      rescue Tree::Busy => e
        raise Failure, e.message
      end

      def web_down
        [@web_chrome, @web_serve].compact.each do |pid|
          Process.kill("TERM", -pid)
        rescue SystemCallError
          nil
        end
        [@web_chrome, @web_serve].compact.each do |pid|
          Process.wait(pid)
        rescue SystemCallError
          nil
        end
        @web_chrome = @web_serve = nil
      end

      def web_tool(*args, timeout: 30)
        out, st = Open3.capture2e(Gem.ruby, File.join(ROOT, "tools", "fmrb_web.rb"), "--port", WEB_PORT.to_s,
                                  "--timeout", timeout.to_s, *args, chdir: ROOT)
        raise Failure, "fmrb_web.rb #{args.first}: #{out.strip.lines.last(2).join}" unless st.success?
        out
      end

      def web_lines
        return [] unless File.exist?(web_console)
        File.readlines(web_console, chomp: true).filter_map { |l| l[/INFO:CONSOLE[^\]]*\] "(.*)", source: /, 1] }
      end

      def web_wait(re, from, what, timeout: 15)
        deadline = now + timeout
        loop do
          lines = web_lines
          i = (from...lines.size).find { |k| lines[k] =~ re }
          return i if i
          raise Failure, "no #{what} in the page's console within #{timeout}s" if now > deadline
          sleep 0.2
        end
      end

      # ---- the cases ----

      def case_straight
        r = send_file("straight")
        "whole in #{r[:ms]} ms (#{r[:calls]} calls)"
      end

      def case_drop
        r = send_file("drop", faults: "drop")
        lost = (0...(SIZE / 3072.0).ceil).count { |i| i % 10 == 3 }
        raise Failure, "#{r[:again]} pieces asked for again, #{lost} left out" unless r[:again] == lost
        unless peer?
          gaps = r[:b_whole][/(\d+) gap\(s\)/, 1].to_i
          raise Failure, "the receiver saw #{gaps} gaps, #{lost} expected" unless gaps == lost
        end
        "whole, #{lost} pieces left out, each sent again from the receiver's position (#{r[:ms]} ms)"
      end

      def case_twice
        r = send_file("twice", faults: "twice")
        twice = (0...(SIZE / 3072.0).ceil).count { |i| i % 5 == 1 }
        unless peer?
          again = r[:b_whole][/(\d+) piece\(s\) again/, 1].to_i
          raise Failure, "the receiver read past #{again} pieces, #{twice} sent twice" unless again == twice
        end
        "whole, #{twice} pieces sent twice and read past there (#{r[:ms]} ms)"
      end

      def case_swap
        r = send_file("swap", faults: "swap")
        raise Failure, "no piece was asked for again" unless r[:again].positive?
        "whole, #{r[:again]} swapped pairs put in order from the receiver's position (#{r[:ms]} ms)"
      end

      def case_cut
        cut_at = [262_144, 655_360]
        held = []
        r = send_file("cut", pace_ms: PACE_MS) do |a_from|
          cut_at.each_with_index do |at, k|
            wait_line(RUN_DIR_A, /xfer_probe: held #{at}\b/, a_from, "held #{at}", timeout: 60)
            @relay.cut!(CUT_DOWN_S)
            held << k
          end
        end
        resumed = r[:resumed]
        raise Failure, "went on #{resumed.size} times (#{resumed.inspect}), 2 expected" unless resumed.size == 2
        raise Failure, "went on from 0 (#{resumed.inspect})" unless resumed.all?(&:positive?)
        unless peer?
          b_on = r[:b_lines].grep(/goes on with probe cut at (\d+)/).map { |l| l[/ at (\d+) of/, 1].to_i }
          raise Failure, "the receiver went on at #{b_on.inspect}, the sender from #{resumed.inspect}" unless b_on == resumed
        end
        "whole, cut twice, went on from #{resumed.join(' and ')} (of #{SIZE}) (#{r[:ms]} ms, #{r[:retries]} retries)"
      end

      def case_restart
        r = send_file("restart", pace_ms: PACE_MS) do |a_from|
          wait_line(RUN_DIR_A, /xfer_probe: held 262144\b/, a_from, "held 262144", timeout: 60)
          pid = probe_pid(RUN_DIR_B, PORT_B) or raise Failure, "no probe in the second program"
          mark = log_lines(RUN_DIR_B).size
          app(RUN_DIR_B, PORT_B, "kill", pid.to_s)
          wait_line(RUN_DIR_B, /Transfer Probe.*(Task exiting|exit)|xfer_probe.*ended/i, mark, "the probe ending",
                    timeout: 10, quiet: true)
          spawn_probe(RUN_DIR_B, PORT_B, NODE_B)
        end
        resumed = r[:resumed]
        held = r[:b_lines].grep(/transfer: held probe restart from #{NODE_A} \((\d+) of/).map { |l| l[/\((\d+) of/, 1].to_i }
        raise Failure, "the new receiver took up nothing (#{held.inspect})" unless held.size == 1 && held[0].positive?
        raise Failure, "went on from #{resumed.inspect}, the receiver held #{held[0]}" unless resumed == held
        "whole, receiver started again holding #{held[0]}, went on from there (#{r[:ms]} ms)"
      end

      def case_mismatch
        r = send_file("mismatch", sha256: "0" * 64, expect: :refused)
        "not placed, the sender told: #{r[:why][0, 60]}"
      end

      def case_kept
        prepare("kept", { "pace_ms" => PACE_MS })
        a_from = log_lines(RUN_DIR_A).size
        b_from = log_lines(RUN_DIR_B).size
        spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
        wait_line(RUN_DIR_A, /xfer_probe: held 262144\b/, a_from, "held 262144", timeout: 60)
        pid = probe_pid(RUN_DIR_A, PORT_A) or raise Failure, "no probe in the first program"
        app(RUN_DIR_A, PORT_A, "kill", pid.to_s)
        t = now
        i = wait_line(RUN_DIR_B, /probe kept from #{NODE_A} dropped at (\d+) of #{SIZE} bytes: no piece for #{KEEP_MS} ms/,
                      b_from, "the piece file dropped", timeout: 20)
        took = ((now - t) * 1000).round
        at = log_lines(RUN_DIR_B)[i][/dropped at (\d+)/, 1].to_i
        gone = !file_there?(RUN_DIR_B, PORT_B, "/tmp/kept.probe.part") && !file_there?(RUN_DIR_B, PORT_B, "/tmp/kept.probe.xfer")
        raise Failure, "the piece file is still there" unless gone
        kill_probes
        "the piece file (#{at} bytes) deleted #{took} ms after the sender stopped"
      end

      def case_file
        kill_probes
        home_a = File.join(OUT, "#{File.basename(RUN_DIR_A)}_home", "user", "home")
        home_b = File.join(OUT, "#{File.basename(RUN_DIR_B)}_home", "user", "home")
        File.write(File.join(home_b, "asterism", "receive.toml"), "enabled = true\nfrom = [\"#{NODE_A}\"]\n\n[file]\nfrom = [\"#{NODE_A}\"]\n")
        FileUtils.cp(@src, File.join(home_a, "xfer_file.bin"))
        FileUtils.rm_rf(File.join(home_b, "inbox"))
        placed = [false, false, true].map do |over|
          put_job(RUN_DIR_A, PORT_A, { "api" => { "to" => NODE_B, "path" => "/home/xfer_file.bin", "overwrite" => over } })
          from = log_lines(RUN_DIR_A).size
          spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
          i = wait_line(RUN_DIR_A, /xfer_probe: api done/, from, "the probe's api end", timeout: 60)
          line = log_lines(RUN_DIR_A)[i]
          raise Failure, line[/xfer_probe: .*/] unless line =~ /api done ok (\S+)/
          kill_probes
          ::Regexp.last_match(1)
        end
        raise Failure, "placed as #{placed.inspect}" unless placed == ["xfer_file.bin", "xfer_file(1).bin", "xfer_file.bin"]
        got = Dir.children(File.join(home_b, "inbox")).sort
        raise Failure, "the inbox has #{got.inspect}" unless got == ["xfer_file(1).bin", "xfer_file.bin"]
        got.each do |n|
          sha = Digest::SHA256.file(File.join(home_b, "inbox", n)).hexdigest
          raise Failure, "#{n} has sha256 #{sha[0, 16]}" unless sha == @sha
        end
        "#{placed.join(', ')} in the second program's /home/inbox (the third overwrote the first)"
      end

      # A 4 MiB "file" from the first program's probe to the second's
      # transfer app, cut twice on the way; the piece file is looked at in
      # the second program's /home/inbox/ (hidden) and /tmp while it comes.
      def case_big
        kill_probes
        home_a = File.join(OUT, "#{File.basename(RUN_DIR_A)}_home", "user", "home")
        home_b = File.join(OUT, "#{File.basename(RUN_DIR_B)}_home", "user", "home")
        inbox = File.join(home_b, "inbox")
        bytes = Random.new(20_261_109).bytes(BIG)
        sha = Digest::SHA256.hexdigest(bytes)
        File.binwrite(File.join(home_a, "xfer_big.bin"), bytes)
        unless peer?
          File.write(File.join(home_b, "asterism", "receive.toml"), "enabled = true\nfrom = [\"#{NODE_A}\"]\n\n[file]\nfrom = [\"#{NODE_A}\"]\n")
          FileUtils.rm_rf(inbox)
        end
        put_job(RUN_DIR_A, PORT_A, { "to" => @to, "app" => "transfer", "kind" => "file", "path" => "/home/xfer_big.bin",
                                     "name" => "big.bin", "pace_ms" => BIG_PACE_MS, "overwrite" => true })
        a_from = log_lines(RUN_DIR_A).size
        b_from = log_lines(RUN_DIR_B).size
        seen = []
        spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
        BIG_CUTS.each do |at|
          wait_line(RUN_DIR_A, /xfer_probe: held #{at}\b/, a_from, "held #{at}", timeout: 120)
          next @relay.cut!(CUT_DOWN_S) if peer?
          part = File.join(inbox, ".big.bin.file.part")
          seen << [File.exist?(part) ? File.size(part) : nil, file_there?(RUN_DIR_B, PORT_B, "/tmp/big.bin.file.part"),
                   file_there?(RUN_DIR_B, PORT_B, "/tmp/big.bin.file.xfer")]
          @relay.cut!(CUT_DOWN_S)
        end
        i = wait_line(RUN_DIR_A, /xfer_probe: done /, a_from, "the sender's end", timeout: peer? ? 900 : 120)
        done = log_lines(RUN_DIR_A)[i]
        b_lines = log_lines(RUN_DIR_B)[b_from..]
        kill_probes
        raise Failure, "the sender's end: #{done[/xfer_probe: .*/]}" unless done.include?("done ok placed")
        if peer?
          r = numbers(done)
          raise Failure, "went on from #{r[:resumed].inspect}" unless r[:resumed].size == 2 && r[:resumed].all?(&:positive?)
          return "#{BIG} bytes placed there (its sha256 checked there), cut twice, went on from " \
                 "#{r[:resumed].join(' and ')}; #{done[/pieces of \d+, \d+ at a time/]} (#{r[:ms]} ms)"
        end
        seen.each_with_index do |(size, tmp_part, note), k|
          raise Failure, "no hidden piece file in /home/inbox at the #{k + 1}. cut" unless size&.positive?
          raise Failure, "a piece file in /tmp at the #{k + 1}. cut" if tmp_part
          raise Failure, "no note in /tmp at the #{k + 1}. cut" unless note
        end
        r = numbers(done)
        resumed = r[:resumed]
        raise Failure, "went on #{resumed.size} times (#{resumed.inspect}), 2 expected" unless resumed.size == 2
        raise Failure, "went on from 0 (#{resumed.inspect})" unless resumed.all?(&:positive?)
        b_on = b_lines.grep(/goes on with file big\.bin at (\d+)/).map { |l| l[/ at (\d+) of/, 1].to_i }
        raise Failure, "the receiver went on at #{b_on.inspect}, the sender from #{resumed.inspect}" unless b_on == resumed
        began = b_lines.grep(/sends file big\.bin \(#{BIG} bytes\) to (\S+), pieces of (\d+) bytes, (\d+) at a time/).first
        raise Failure, "no 'sends file big.bin' in the receiver's log" unless began
        part, piece, win = began[/ to (\S+), pieces/, 1], began[/pieces of (\d+)/, 1].to_i, began[/(\d+) at a time/, 1].to_i
        raise Failure, "the pieces went to #{part}" unless part == "/home/inbox/.big.bin.file.part"
        unless done.include?("pieces of #{piece}, #{win} at a time")
          raise Failure, "the receiver named #{piece} x #{win}; the sender: #{done[/pieces of \d+, \d+ at a time/]}"
        end
        got = File.join(inbox, "big.bin")
        raise Failure, "no big.bin in /home/inbox" unless File.exist?(got)
        raise Failure, "big.bin has sha256 #{Digest::SHA256.file(got).hexdigest[0, 16]}" unless Digest::SHA256.file(got).hexdigest == sha
        left = Dir.children(inbox) - ["big.bin"]
        raise Failure, "#{left.inspect} left in /home/inbox" unless left.empty?
        tmp = %w[/tmp/big.bin.file.part /tmp/big.bin.file.xfer].select { |p| file_there?(RUN_DIR_B, PORT_B, p) }
        raise Failure, "#{tmp.inspect} left in /tmp" unless tmp.empty?
        "#{BIG} bytes, cut twice, went on from #{resumed.join(' and ')}; pieces in /home/inbox/.big.bin.file.part " \
          "(#{seen.map(&:first).join(', ')} bytes at the cuts), none in /tmp; #{piece} x #{win} named by the receiver " \
          "(#{r[:ms]} ms, #{r[:retries]} retries)"
      end

      # ---- sending one file ----

      # The receiver's probe up, then the sender's with its job; the block
      # (given the sender's log mark) breaks things on the way. -> the
      # sender's numbers and both logs.
      def send_file(name, faults: nil, sha256: nil, pace_ms: nil, expect: :placed)
        prepare(name, { "faults" => faults, "sha256" => sha256, "pace_ms" => pace_ms }.compact)
        a_from = log_lines(RUN_DIR_A).size
        b_from = log_lines(RUN_DIR_B).size
        spawn_probe(RUN_DIR_A, PORT_A, NODE_A)
        yield a_from if block_given?
        b_from = 0 if peer?
        i = wait_line(RUN_DIR_A, /xfer_probe: done /, a_from, "the sender's end", timeout: 90)
        done = log_lines(RUN_DIR_A)[i]
        a_lines = log_lines(RUN_DIR_A)[a_from..]
        b_lines = log_lines(RUN_DIR_B)[b_from..]
        r = numbers(done)
        r[:b_lines] = b_lines
        if expect == :refused
          unless done.include?("done ok refused") && done.include?("does not match")
            raise Failure, "the sender's end: #{done[/xfer_probe: .*/]}"
          end
          r[:why] = done[/refused (.*?); \d+ ms/, 1].to_s
          return r if peer?
          raise Failure, "the receiver placed it" if file_there?(RUN_DIR_B, PORT_B, "/tmp/#{name}.probe")
          raise Failure, "no 'not placed' there" if b_lines.grep(/sha256 .* was sent; not placed/).empty?
        else
          raise Failure, "the sender's end: #{done[/xfer_probe: .*/]}" unless done.include?("done ok placed")
          return r if peer?
          got = File.join(OUT, "#{name}.got")
          native(RUN_DIR_B, PORT_B, "file", "get", "/tmp/#{name}.probe", got, timeout: 120)
          sha = Digest::SHA256.file(got).hexdigest
          FileUtils.rm_f(got)
          raise Failure, "the file there has sha256 #{sha[0, 16]}, not #{@sha[0, 16]}" unless sha == @sha
          whole = b_lines.find { |l| l.include?("probe #{name} from #{NODE_A} is whole") }
          raise Failure, "no 'is whole' in the receiver's log" unless whole
          r[:b_whole] = whole
        end
        r[:a_lines] = a_lines
        kill_probes
        r
      end

      def numbers(done)
        { ms: done[/; (\d+) ms,/, 1].to_i, calls: done[/(\d+) calls/, 1].to_i, again: done[/(\d+) again/, 1].to_i,
          retries: done[/(\d+) retries/, 1].to_i,
          resumed: done[/went on from \[([\d,]*)\]/, 1].to_s.split(",").map(&:to_i) }
      end

      # Both probes stopped, the files of a case written: the receiver's job
      # (keep_ms) and the sender's (to, path, faults), and the receiver's
      # probe started.
      def prepare(name, extra)
        kill_probes
        put_job(RUN_DIR_A, PORT_A, { "to" => @to, "path" => SRC, "name" => name }.merge(T1_SENDER).merge(extra))
        return if peer?
        # the kept case's short keep; the others the receiver's own (60 s),
        # which a connection down for seconds must not run out
        put_job(RUN_DIR_B, PORT_B, name == "kept" ? { "keep_ms" => KEEP_MS } : {})
        spawn_probe(RUN_DIR_B, PORT_B, NODE_B)
      end

      def put_job(dir, port, h)
        f = File.join(OUT, "job.json")
        File.write(f, JSON.generate(h))
        native(dir, port, "file", "put", f, JOB)
      end

      def spawn_probe(dir, port, node)
        mark = log_lines(dir).size
        app(dir, port, "spawn", PROBE)
        wait_line(dir, /xfer_probe: ready as #{node}/, mark, "#{node}'s probe ready", timeout: 30)
      end

      def probe_pid(dir, port)
        app(dir, port, "ps").fetch("apps").find { |x| x["name"] == "Transfer Probe" }&.fetch("pid")
      end

      def kill_probes
        progs = [[RUN_DIR_A, PORT_A]]
        progs << [RUN_DIR_B, PORT_B] unless peer? || @opts[:web]
        progs.each do |d, p|
          pid = probe_pid(d, p)
          next unless pid
          app(d, p, "kill", pid.to_s)
          deadline = now + 5
          sleep 0.1 while probe_pid(d, p) && now < deadline
        end
      end

      def file_there?(dir, port, path)
        out = File.join(OUT, "probe.tmp")
        native(dir, port, "file", "get", path, out)
        FileUtils.rm_f(out)
        true
      rescue Failure
        false
      end

      # ---- the programs and the router ----

      # receive: false leaves /home/asterism/receive.toml out (the system's
      # copy in /etc is read then: receiving off).
      def up(dir, port, node, from, router, receive: true)
        down(dir, port)
        home = File.join(OUT, "#{File.basename(dir)}_home")
        FileUtils.rm_rf(home)
        ast = File.join(home, "user", "home", "asterism")
        FileUtils.mkdir_p([ast, File.join(home, "cache")])
        File.write(File.join(ast, "config.toml"), "router = \"#{router}\"\nnode = \"#{node}\"\n")
        File.write(File.join(ast, "receive.toml"), "enabled = true\nfrom = [\"#{from}\"]\n") if receive
        begin
          Tree.need_free_port!(port, "the native program of #{dir}")
        rescue Tree::Busy => e
          raise Failure, e.message
        end
        native(dir, port, "up", "--engine", @engine, "--machine", "sdl", "--debug-port", port.to_s,
               "--user-dir", File.join(home, "user"), "--cache-dir", File.join(home, "cache"),
               "--timeout", "90", timeout: 120)
      end

      def down(dir, port)
        native(dir, port, "down", timeout: 30) if File.exist?(File.join(dir, "pid"))
      rescue Failure
        nil
      end

      def keep_logs
        { RUN_DIR_A => "a", RUN_DIR_B => "b" }.each do |d, n|
          src = File.join(d, "core.log")
          FileUtils.cp(src, File.join(OUT, "#{@engine}_#{n}.log")) if File.exist?(src)
        end
      end

      def native(dir, port, *args, timeout: 60)
        env = { "FMRB_DEBUG_TCP_PORT" => port.to_s }
        out, st = nil
        Open3.popen2e(env, Gem.ruby, NATIVE_TOOL, "--run-dir", dir, *args, chdir: ROOT) do |_in, io, wait|
          reader = Thread.new { io.read }
          unless wait.join(timeout)
            Process.kill("KILL", wait.pid) rescue nil # rubocop:disable Style/RescueModifier
            raise Failure, "fmrb_native.rb #{args.first}: no answer in #{timeout}s"
          end
          out = reader.value
          st = wait.value
        end
        raise Failure, "fmrb_native.rb #{args.join(' ')}: #{out.strip.lines.last(2).join}" unless st.success?
        out
      end

      def app(dir, port, *args)
        JSON.parse(native(dir, port, "app", *args).lines.last.to_s)
      end

      def log_lines(dir)
        f = File.join(dir, "core.log")
        File.exist?(f) ? File.readlines(f, chomp: true) : []
      end

      def wait_line(dir, re, from, what, timeout: 15, quiet: false)
        deadline = now + timeout
        loop do
          lines = log_lines(dir)
          i = (from...lines.size).find { |k| lines[k] =~ re }
          return i if i
          if now > deadline
            return nil if quiet
            raise Failure, "no #{what} in the log within #{timeout}s"
          end
          sleep 0.05
        end
      end

      def router_up
        router_down
        [ROUTER_PORT, RELAY_PORT, ROUTER_WS_PORT].each do |p|
          Tree.need_free_port!(p, "the Zenoh router #{ROUTER_NAME}")
        rescue Tree::Busy => e
          raise Failure, e.message
        end
        out, st = Open3.capture2e("docker", "run", "-d", "--rm", "--name", ROUTER_NAME,
                                  "-p", "127.0.0.1:#{ROUTER_PORT}:7447", "-p", "127.0.0.1:#{ROUTER_WS_PORT}:7449",
                                  ROUTER_IMAGE, "--no-multicast-scouting", "--listen", "tcp/0.0.0.0:7447",
                                  "--listen", "ws/0.0.0.0:7449")
        raise Failure, "the Zenoh router did not start: #{out.strip.lines.last}" unless st.success?
        deadline = now + 20
        loop do
          Socket.tcp("127.0.0.1", ROUTER_PORT, connect_timeout: 0.5).close
          break
        rescue StandardError
          raise Failure, "the Zenoh router is not listening on #{ROUTER_PORT}" if now > deadline
          sleep 0.2
        end
      end

      def router_down
        Open3.capture2e("docker", "rm", "-f", ROUTER_NAME)
        nil
      end
    end

    def self.reap
      [RUN_DIR_A, RUN_DIR_B].each do |dir|
        pid = Tree.native_pid(dir) or next
        warn "transfer: stopping the native program left in #{dir} (pid #{pid})"
        Tree.stop_pid(pid)
      end
      Open3.capture2e("docker", "rm", "-f", ROUTER_NAME)
    rescue SystemCallError
      nil
    end

    def self.main(argv)
      opts = {}
      OptionParser.new do |o|
        o.banner = "usage: transfer.rb [--only CASE] [--engine standard|mruby]"
        o.on("--only CASE") { |v| opts[:only] = v }
        o.on("--engine NAME", %w[standard mruby]) { |v| opts[:engine] = v }
        o.on("--bench") { opts[:bench] = true }
        o.on("--web", "with --bench: the browser build as the receiver") { opts[:web] = true }
        o.on("--peer NODE", "the receiver is the probe on this node (a board)") { |v| opts[:peer] = v }
        o.on("--router LOCATOR", "with --peer: the board's router (tcp/HOST:PORT)") { |v| opts[:router] = v }
        o.on("--sizes LIST", "with --bench: only calls of these sizes") { |v| opts[:sizes] = v.split(",").map(&:to_i) }
        o.on("--size BYTES", Integer, "with --bench: the file's size") { |v| opts[:size] = v }
        o.on("--pieces LIST", "with --bench: the speed runs' pieces") { |v| opts[:pieces] = v.split(",").map(&:to_i) }
        o.on("--windows LIST", "with --bench: the speed runs' windows") { |v| opts[:windows] = v.split(",").map(&:to_i) }
        o.on("--speed", "with --bench: the speed runs only") { opts[:speed] = true }
        o.on("--file", "with --bench: a run of the kind file to the transfer app there") { opts[:file] = true }
        o.on("--web-send", "with --bench --web: the browser sends too") { opts[:web_send] = true }
      end.parse!(argv)
      abort "transfer: --peer needs --router tcp/HOST:PORT" if opts[:peer] && opts[:router].to_s !~ %r{\Atcp/[^:]+:\d+\z}
      Tree.need_root!
      at_exit { Transfer.reap }
      Runner.new(opts).run
    end
  end
end

exit(Regress::Transfer.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__
