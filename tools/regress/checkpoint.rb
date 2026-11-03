#!/usr/bin/env ruby
# frozen_string_literal: true
#
# checkpoint.rb - moving an app inside one native program (rake
# regress:checkpoint, doc/checkpoint F1, doc/regression/usage.md chapter 12).
#
#   ruby tools/regress/checkpoint.rb                  # build, then standard and mruby
#   ruby tools/regress/checkpoint.rb --only mruby     # one engine configuration
#   ruby tools/regress/checkpoint.rb --no-build       # use the native builds there are
#
# The native program is built in both configurations (standard: the Spinel
# kernel; mruby: everything on mruby), since the kernel holds the steps of a
# move (D14). Each is started headless (tools/fmrb_native.rb) in a run
# directory, debug port and user directory of its own, so a developer's copy
# can stay up beside it. Then, from the debug server:
#
#   restart   start /app/test/ckpt_counter.app.rb (two Tasks and the main loop
#             count and log), let it count, "checkpoint pid restart=1": the app
#             writes its image and closes, the kernel starts the image again.
#   wait      "checkpoint" without restart, wait 2 s, spawn the .ckpt by hand.
#   refuse    an app that does not declare movable is refused, and goes on;
#             so is a movable app with a window (its FmrbGfx cannot be carried
#             until F3), refused at the safe point by the gem.
#
# For the first two, the log must show every counter going on from the step
# after the last one written (a, b, main), on_checkpoint and on_restore with
# the same main, and Machine.board_millis ("t=") going on from where it was:
# the step across the move is short, also across the 2 s wait (D9).
#
# The summary is one line per configuration; the program's log is kept in
# tmp/regress/checkpoint/<configuration>.log.
require "fileutils"
require "json"
require "open3"
require "optparse"

module Regress
  module Checkpoint
    CORE = File.expand_path("../..", __dir__)
    ROOT = File.expand_path("..", CORE)
    NATIVE_TOOL = File.join(ROOT, "tools", "fmrb_native.rb")
    DBG_CLIENT = File.join(CORE, "tool", "debug", "fmrb_dbg_client.py")
    OUT = File.join(CORE, "tmp", "regress", "checkpoint")
    RUN_DIR = "/tmp/fmrb-native-ckpt-#{Process.uid}"
    PORT = 5585
    APP = "/app/test/ckpt_counter.app.rb"
    IMAGE = "/tmp/ckpt_counter.ckpt"
    NOT_MOVABLE = "/app/test/task_sleep.app.rb"
    WINDOWED = "/app/test/ckpt_window.app.rb"
    # one step of the counter Task "a" is its sleep (200 ms) behind the main
    # loop's wait (250 ms); a move may add a little to it, a lost or doubled
    # clock adds seconds
    MAX_T_STEP_MS = 800
    WAIT_S = 2.0

    class Failure < StandardError; end

    class Runner
      def initialize(opts)
        @opts = opts
        @lines = []
      end

      def run
        t0 = now
        FileUtils.mkdir_p(OUT)
        configs = @opts[:only] ? [@opts[:only]] : %w[standard mruby]
        build(configs) unless @opts[:no_build]
        results = configs.map { |c| run_config(c) }
        results.each { |r| puts r }
        ok = results.all? { |r| r.include?(": ok") }
        puts format("checkpoint: %s (%.1f min)", ok ? "ok" : "FAILED", (now - t0) / 60.0)
        ok
      end

      private

      def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

      def build(configs)
        log = File.join(OUT, "build.log")
        steps = []
        steps << "rake spinel:gen" if configs.include?("standard")
        steps << "rake native:mruby"
        steps << "rake native:core" if configs.include?("standard")
        steps << "NATIVE_ENGINE=mruby rake native:core" if configs.include?("mruby")
        out, st = Open3.capture2e("bash", "-c", steps.join(" && "), chdir: CORE)
        File.write(log, out)
        raise Failure, "the native build failed (#{log.sub("#{CORE}/", '')})" unless st.success?
      end

      # ---- the program ----

      def native(*args, timeout: 60)
        env = { "FMRB_DEBUG_TCP_PORT" => PORT.to_s }
        cmd = [Gem.ruby, NATIVE_TOOL, "--run-dir", RUN_DIR, *args]
        out = +""
        st = nil
        Open3.popen2e(env, *cmd, chdir: ROOT) do |_in, io, wait|
          reader = Thread.new { io.read }
          unless wait.join(timeout)
            Process.kill("KILL", wait.pid) rescue nil # rubocop:disable Style/RescueModifier
            raise Failure, "fmrb_native.rb #{args.first}: no answer in #{timeout}s"
          end
          out = reader.value
          st = wait.value
        end
        raise Failure, "fmrb_native.rb #{args.join(' ')}: #{out.strip.lines.last(3).join}" unless st.success?
        out
      end

      def app(*args)
        JSON.parse(native("app", *args).lines.last.to_s)
      end

      def checkpoint(pid, restart:)
        kv = ["pid=#{pid}", "restart=#{restart ? 1 : 0}", "max=8000"]
        out, st = Open3.capture2e("python3", DBG_CLIENT, "--json", "localhost:#{PORT}", "checkpoint", *kv)
        raise Failure, "checkpoint pid=#{pid}: #{out.strip}" unless st.success?
        JSON.parse(out.lines.last.to_s)
      end

      def log_path = File.join(RUN_DIR, "core.log")

      def log_lines
        File.exist?(log_path) ? File.readlines(log_path, chomp: true) : []
      end

      # the first line at or after `from` that matches, waiting for it
      def wait_line(re, from, what, timeout: 15)
        deadline = now + timeout
        loop do
          lines = log_lines
          i = (from...lines.size).find { |k| lines[k] =~ re }
          return i if i
          raise Failure, "no #{what} in the log within #{timeout}s" if now > deadline
          sleep 0.1
        end
      end

      def ps = app("ps").fetch("apps")

      def run_config(config)
        t0 = now
        down_if_running
        home = File.join(OUT, "#{config}_home")
        FileUtils.rm_rf(home)
        FileUtils.mkdir_p(%w[user cache].map { |d| File.join(home, d) })
        native("up", "--engine", config, "--machine", "sdl", "--user-dir", File.join(home, "user"),
               "--cache-dir", File.join(home, "cache"), "--timeout", "90", timeout: 120)
        notes = []
        notes << round_restart
        notes << round_wait
        notes << round_refuse
        notes << round_refuse_window
        "#{config}: ok (#{notes.join('; ')}; #{(now - t0).round}s)"
      rescue Failure => e
        "#{config}: FAILED #{e.message}"
      ensure
        FileUtils.cp(log_path, File.join(OUT, "#{config}.log")) if File.exist?(log_path)
        down_if_running
      end

      def down_if_running
        native("down", timeout: 30) if File.exist?(File.join(RUN_DIR, "pid"))
      rescue Failure
        nil
      end

      def start_counter
        from = log_lines.size
        pid = app("spawn", APP).fetch("pid")
        # let both Tasks count a few steps
        wait_line(/ckpt_counter: b=3 /, from, "the counter's third step of b")
        pid
      end

      # The move through the kernel: written, closed, started again.
      def round_restart
        pid = start_counter
        mark = log_lines.size
        r = checkpoint(pid, restart: true)
        raise Failure, "restart: the checkpoint answered #{r.inspect}" unless r["status"] == "written" && r["path"] == IMAGE
        restored = wait_line(/ckpt_counter: on_restore /, mark, "on_restore after the restart")
        wait_line(/ckpt_counter: b=\d+ /, restored + 1, "a step of b after the restore")
        wait_line(/ckpt_counter: a=\d+ /, restored + 1, "a step of a after the restore")
        c = continuity(mark, "restart")
        kill_counter
        "restart: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}, t step #{c[:t_step]} ms, image #{c[:bytes]} B"
      end

      # Written without a restart; the image waits, then is started by hand.
      def round_wait
        pid = start_counter
        mark = log_lines.size
        r = checkpoint(pid, restart: false)
        raise Failure, "wait: the checkpoint answered #{r.inspect}" unless r["status"] == "written"
        deadline = now + 10
        sleep 0.2 while ps.any? { |a| a["pid"] == pid && a["name"] == "Ckpt Counter" } && now < deadline
        raise Failure, "wait: the app did not close after writing its image" if ps.any? { |a| a["name"] == "Ckpt Counter" }
        sleep WAIT_S
        app("spawn", IMAGE)
        restored = wait_line(/ckpt_counter: on_restore /, mark, "on_restore of the image started by hand")
        wait_line(/ckpt_counter: b=\d+ /, restored + 1, "a step of b after the restore")
        wait_line(/ckpt_counter: a=\d+ /, restored + 1, "a step of a after the restore")
        c = continuity(mark, "wait")
        kill_counter
        "wait #{WAIT_S}s: t step #{c[:t_step]} ms"
      end

      def round_refuse
        from = log_lines.size
        pid = app("spawn", NOT_MOVABLE).fetch("pid")
        wait_line(/task_sleep: round 1 /, from, "task_sleep running")
        r = checkpoint(pid, restart: true)
        unless r["status"] == "refused" && r["reason"].to_s.include?("movable")
          raise Failure, "refuse: a non-movable app answered #{r.inspect}"
        end
        raise Failure, "refuse: the refused app is gone" unless ps.any? { |a| a["pid"] == pid }
        app("kill", pid.to_s)
        "refused: #{r['reason']}"
      end

      # Declared movable, refused at the safe point (the gem cannot carry its
      # window): the app is told nothing and goes on.
      def round_refuse_window
        from = log_lines.size
        pid = app("spawn", WINDOWED).fetch("pid")
        wait_line(/ckpt_window: n=2\b/, from, "ckpt_window counting")
        r = checkpoint(pid, restart: true)
        raise Failure, "refuse window: answered #{r.inspect}" unless r["status"] == "refused" && !r["reason"].to_s.empty?
        mark = log_lines.size
        wait_line(/ckpt_window: n=\d+/, mark, "ckpt_window going on after the refusal")
        raise Failure, "refuse window: the app is gone" unless ps.any? { |a| a["pid"] == pid }
        app("kill", pid.to_s)
        "window refused at the safe point: #{r['reason'][0, 80]}"
      end

      def kill_counter
        ps.each { |a| app("kill", a["pid"].to_s) if a["name"] == "Ckpt Counter" }
        sleep 0.3
      end

      # Read the log around the move: every counter goes on from the next step.
      def continuity(mark, round)
        lines = log_lines
        w = (mark...lines.size).find { |k| lines[k] =~ /checkpoint written: (\S+) \((\d+) bytes/ } or
          raise Failure, "#{round}: no 'checkpoint written' line"
        bytes = lines[w][/\((\d+) bytes/, 1].to_i
        r = (w...lines.size).find { |k| lines[k] =~ /ckpt_counter: on_restore / } or
          raise Failure, "#{round}: no on_restore"
        before = lines[0...w]
        after = lines[(r + 1)..]
        res = { bytes: bytes }
        %w[a b].each do |k|
          last = before.reverse.find { |l| l =~ /ckpt_counter: #{k}=\d+ t=/ } or raise Failure, "#{round}: no #{k} before"
          first = after.find { |l| l =~ /ckpt_counter: #{k}=\d+ t=/ } or raise Failure, "#{round}: no #{k} after"
          n0 = last[/#{k}=(\d+)/, 1].to_i
          n1 = first[/#{k}=(\d+)/, 1].to_i
          raise Failure, "#{round}: #{k} went from #{n0} to #{n1} across the move" unless n1 == n0 + 1
          res[k.to_sym] = "#{n0}->#{n1}"
          if k == "a"
            t0 = last[/t=(-?\d+)/, 1].to_i
            t1 = first[/t=(-?\d+)/, 1].to_i
            step = t1 - t0
            raise Failure, "#{round}: board_millis stepped #{step} ms across the move (#{t0} -> #{t1})" unless step.between?(0, MAX_T_STEP_MS)
            res[:t_step] = step
          end
        end
        cp = lines[w..r].find { |l| l =~ /ckpt_counter: on_checkpoint main=(\d+)/ } or raise Failure, "#{round}: no on_checkpoint"
        m0 = cp[/main=(\d+)/, 1].to_i
        m_restore = lines[r][/main=(\d+)/, 1].to_i
        raise Failure, "#{round}: on_restore has main=#{m_restore}, on_checkpoint had #{m0}" unless m_restore == m0
        nxt = after.find { |l| l =~ /ckpt_counter: main=\d+/ } or raise Failure, "#{round}: no main after"
        m1 = nxt[/main=(\d+)/, 1].to_i
        raise Failure, "#{round}: main went from #{m0} to #{m1}" unless m1 == m0 + 1
        res[:main] = "#{m0}->#{m1}"
        res
      end
    end

    def self.main(argv)
      opts = {}
      OptionParser.new do |o|
        o.banner = "usage: checkpoint.rb [--only standard|mruby] [--no-build]"
        o.on("--only NAME", %w[standard mruby]) { |v| opts[:only] = v }
        o.on("--no-build") { opts[:no_build] = true }
      end.parse!(argv)
      Runner.new(opts).run
    rescue Failure => e
      puts "checkpoint: FAILED #{e.message}"
      false
    end
  end
end

exit(Regress::Checkpoint.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__
