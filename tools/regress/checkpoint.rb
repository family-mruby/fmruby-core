#!/usr/bin/env ruby
# frozen_string_literal: true
#
# checkpoint.rb - moving an app inside one native program and between two (rake
# regress:checkpoint, doc/checkpoint F1 and F2, doc/regression/usage.md chapter 12).
#
#   ruby tools/regress/checkpoint.rb                  # build, then standard and mruby
#   ruby tools/regress/checkpoint.rb --only mruby     # one engine configuration
#   ruby tools/regress/checkpoint.rb --no-build       # use the native builds there are
#
# The native program is built in both configurations (standard: the Spinel
# kernel; mruby: everything on mruby), since the kernel holds the steps of a
# move (D14). Each is started headless (tools/fmrb_native.rb) in a run
# directory, debug port and user directory of its own, so a developer's copy
# can stay up beside it. Then, through each way of asking (F1b: all of them
# reach the app the same way, kernel -> the app's _spin -> a safe point):
#
#   restart   start /app/test/ckpt_counter.app.rb (two Tasks and the main loop
#             count and log), let it count, "app checkpoint PID --restart"
#             (the debug server): the app writes its image and closes, the
#             kernel starts the image again.
#   wait      the same without --restart, wait 2 s, "app restore" the .ckpt.
#   refuse    an app that does not declare movable is refused, and goes on;
#             so is a movable app with a window (its FmrbGfx cannot be carried
#             until F3), refused at the safe point by the gem.
#   self      /app/test/ckpt_self.app.rb asks for its own image from on_update
#             (FmrbApp#checkpoint, restart): written, started again,
#             and asked only once. ckpt_self_refused does the same without
#             movable: told why (on_checkpoint_refused), goes on.
#   shell     the shell's `checkpoint -r PID`, typed into a shell window: the
#             counter moves as in restart, and the shell is answered.
#   move      (F2) a second native program of the same build beside the first
#             (a run directory, debug port and user directory of its own):
#             "app move PID --to <its run dir>" writes the counter's image and
#             holds it, carries the image to the second program in pieces,
#             starts it there and closes it in the first once it is up there
#             (D15). The counters go on in the second program; the first
#             closes the normal way; both find the same permanent names (the
#             digest of the baseline) and the image's task tick goes on.
#   move refused  the same with --damage (the image is cut short on the way):
#             the second program cannot restore it and says why, the first
#             goes on from where it was held.
#   move no answer  "app checkpoint PID --hold 1500" and nobody answers: the
#             first program's kernel lets the app go on once the hold runs out.
#   turns     (first, not a move) /app/test/task_turns.app.rb: a Task that
#             passes after every step gets its steps while the main loop
#             waits longer than a timeslice per turn. The native program
#             supplies its own tick (FMRB_TASK_SELF_TICK), where a slice used
#             up by the main loop was handed to the next task, which then
#             never ran (doc/checkpoint/report/hang_task_sleep.md).
#
# For restart, wait, self and shell the log must show every counter going on
# from the step after the last one written (a, b, main), on_checkpoint and
# on_restore with the same main, and Machine.board_millis ("t=") going on
# from where it was: the step across the move is short, also across the 2 s
# wait (D9).
#
# No app may end in a forced kill ("No response in ...ms"): every app here is
# closed by asking it, and an app's Tasks end with it.
#
# The summary is one line per configuration; the program's log is kept in
# tmp/regress/checkpoint/<configuration>.log (the second program's in
# <configuration>_b.log).
require "fileutils"
require "json"
require "open3"
require "optparse"

module Regress
  module Checkpoint
    CORE = File.expand_path("../..", __dir__)
    ROOT = File.expand_path("..", CORE)
    NATIVE_TOOL = File.join(ROOT, "tools", "fmrb_native.rb")
    OUT = File.join(CORE, "tmp", "regress", "checkpoint")
    RUN_DIR = "/tmp/fmrb-native-ckpt-#{Process.uid}"
    PORT = 5585
    # the second program, for the moves between two programs (F2)
    RUN_DIR_B = "/tmp/fmrb-native-ckpt-b-#{Process.uid}"
    PORT_B = 5586
    HOLD_NO_ANSWER_MS = 1500
    APP = "/app/test/ckpt_counter.app.rb"
    IMAGE = "/tmp/ckpt_counter.ckpt"
    NOT_MOVABLE = "/app/test/task_sleep.app.rb"
    TURNS = "/app/test/task_turns.app.rb"
    WINDOWED = "/app/test/ckpt_window.app.rb"
    SELF = "/app/test/ckpt_self.app.rb"
    SELF_REFUSED = "/app/test/ckpt_self_refused.app.rb"
    SHELL = "default/shell"
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

      def native(*args, timeout: 60, run_dir: RUN_DIR, port: PORT)
        env = { "FMRB_DEBUG_TCP_PORT" => port.to_s }
        cmd = [Gem.ruby, NATIVE_TOOL, "--run-dir", run_dir, *args]
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

      def app(*args, **kw)
        JSON.parse(native("app", *args, **kw).lines.last.to_s)
      end

      def app_b(*args) = app(*args, run_dir: RUN_DIR_B, port: PORT_B)

      # the tool's way of asking (the debug server's checkpoint)
      def checkpoint(pid, restart:)
        args = ["checkpoint", pid.to_s, "--max", "8000"]
        args << "--restart" if restart
        app(*args)
      end

      def log_path(run_dir = RUN_DIR) = File.join(run_dir, "core.log")

      def log_lines(run_dir = RUN_DIR)
        File.exist?(log_path(run_dir)) ? File.readlines(log_path(run_dir), chomp: true) : []
      end

      # the first line at or after `from` that matches, waiting for it
      def wait_line(re, from, what, timeout: 15, run_dir: RUN_DIR)
        deadline = now + timeout
        loop do
          lines = log_lines(run_dir)
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
        notes << round_turns
        notes << round_restart
        notes << round_wait
        notes << round_refuse
        notes << round_refuse_window
        notes << round_self
        notes << round_self_refused
        notes << round_shell
        up_b(config)
        notes << round_move
        notes << round_move_refused
        notes << round_move_no_answer
        no_forced_kill
        "#{config}: ok (#{notes.join('; ')}; #{(now - t0).round}s)"
      rescue Failure => e
        "#{config}: FAILED #{e.message}"
      ensure
        FileUtils.cp(log_path, File.join(OUT, "#{config}.log")) if File.exist?(log_path)
        FileUtils.cp(log_path(RUN_DIR_B), File.join(OUT, "#{config}_b.log")) if File.exist?(log_path(RUN_DIR_B))
        down_if_running
      end

      def down_if_running
        [[RUN_DIR, PORT], [RUN_DIR_B, PORT_B]].each do |dir, port|
          native("down", timeout: 30, run_dir: dir, port: port) if File.exist?(File.join(dir, "pid"))
        rescue Failure
          nil
        end
      end

      # The second program: the same build, a run dir, port and home of its own.
      def up_b(config)
        home = File.join(OUT, "#{config}_home_b")
        FileUtils.rm_rf(home)
        FileUtils.mkdir_p(%w[user cache].map { |d| File.join(home, d) })
        native("up", "--engine", config, "--machine", "sdl", "--debug-port", PORT_B.to_s,
               "--user-dir", File.join(home, "user"), "--cache-dir", File.join(home, "cache"),
               "--timeout", "90", timeout: 120, run_dir: RUN_DIR_B, port: PORT_B)
      end

      # Every app was closed by asking it: none was dropped by force.
      def no_forced_kill
        [RUN_DIR, RUN_DIR_B].each do |dir|
          forced = log_lines(dir).grep(/No response in \d+ms, forcing termination/)
          raise Failure, "forced kill in #{dir == RUN_DIR ? 'the first' : 'the second'} program: #{forced.first}" unless forced.empty?
        end
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
        app("restore", IMAGE)
        restored = wait_line(/ckpt_counter: on_restore /, mark, "on_restore of the image started by hand")
        wait_line(/ckpt_counter: b=\d+ /, restored + 1, "a step of b after the restore")
        wait_line(/ckpt_counter: a=\d+ /, restored + 1, "a step of a after the restore")
        c = continuity(mark, "wait")
        kill_counter
        "wait #{WAIT_S}s: t step #{c[:t_step]} ms"
      end

      # Not a move: the scheduler of a native program hands out its turns.
      def round_turns
        from = log_lines.size
        pid = app("spawn", TURNS).fetch("pid")
        done = wait_line(/task_turns: done /, from, "the passing Task's last step (task_turns)", timeout: 10)
        app("kill", pid.to_s)
        "turns: #{log_lines[done][/done .*/]}"
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

      # The app asks for its own image from on_update (FmrbApp#checkpoint).
      def round_self
        from = log_lines.size
        app("spawn", SELF)
        restored = wait_line(/ckpt_self: on_restore /, from, "on_restore of the app that asked for itself")
        wait_line(/ckpt_self: b=\d+ /, restored + 1, "a step of b after the restore")
        wait_line(/ckpt_self: a=\d+ /, restored + 1, "a step of a after the restore")
        c = continuity(from, "self", "ckpt_self")
        asked = log_lines[from..].grep(/ckpt_self: asked for its own image/)
        raise Failure, "self: asked #{asked.size} times (#{asked.first})" unless asked.size == 1 && asked[0].include?("(ok)")
        kill_named("Ckpt Self")
        "self: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}, t step #{c[:t_step]} ms"
      end

      # The same request from an app without movable: told why, goes on.
      def round_self_refused
        from = log_lines.size
        pid = app("spawn", SELF_REFUSED).fetch("pid")
        i = wait_line(/ckpt_self_refused: refused: /, from, "on_checkpoint_refused")
        reason = log_lines[i][/refused: (.*)\z/, 1].to_s
        raise Failure, "self refused: the reason was #{reason.inspect}" unless reason.include?("movable")
        wait_line(/ckpt_self_refused: main=\d+/, i + 1, "ckpt_self_refused going on after the refusal")
        raise Failure, "self refused: the app is gone" unless ps.any? { |a| a["pid"] == pid }
        app("kill", pid.to_s)
        "self refused: told why"
      end

      # The shell's command, typed into a shell window.
      def round_shell
        pid = start_counter
        app("spawn", SHELL)
        sleep 1.5
        mark = log_lines.size
        native("input", "text", "checkpoint -r #{pid}", "key", "enter")
        wait_line(/checkpoint: PID #{pid}: wrote #{Regexp.escape(IMAGE)}/, mark, "the shell's answer")
        restored = wait_line(/ckpt_counter: on_restore /, mark, "on_restore after the shell's checkpoint")
        wait_line(/ckpt_counter: b=\d+ /, restored + 1, "a step of b after the restore")
        wait_line(/ckpt_counter: a=\d+ /, restored + 1, "a step of a after the restore")
        c = continuity(mark, "shell")
        kill_counter
        kill_named("FM-Shell")
        "shell: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}"
      end

      def move(pid, *opts)
        JSON.parse(native("app", "move", pid.to_s, "--to", RUN_DIR_B, *opts, timeout: 60).lines.last.to_s)
      end

      # A move into the second program (F2, D15): held, carried, started there,
      # then closed here.
      def round_move
        pid = start_counter
        mark = log_lines.size
        mark_b = log_lines(RUN_DIR_B).size
        r = move(pid)
        raise Failure, "move: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        restored = wait_line(/ckpt_counter: on_restore /, mark_b, "on_restore in the second program", run_dir: RUN_DIR_B)
        wait_line(/ckpt_counter: b=\d+ /, restored + 1, "a step of b in the second program", run_dir: RUN_DIR_B)
        wait_line(/ckpt_counter: a=\d+ /, restored + 1, "a step of a in the second program", run_dir: RUN_DIR_B)
        wait_line(/\[Ckpt Counter gen=\d+\] Task exiting normally/, mark, "the first program's copy closing")
        sleep 0.5
        a = log_lines
        b = log_lines(RUN_DIR_B)
        raise Failure, "move: the first program still runs the counter" if ps.any? { |x| x["name"] == "Ckpt Counter" }
        w = (mark...a.size).find { |k| a[k] =~ /checkpoint written: / } or raise Failure, "move: no 'checkpoint written'"
        stray = a[(w + 1)..].grep(/ckpt_counter: (a|b|main)=/)
        raise Failure, "move: the first program counted on after its image (#{stray.first})" unless stray.empty?
        r_i = (mark_b...b.size).find { |k| b[k] =~ /ckpt_counter: on_restore / }
        c = continuity_of(a[0...w], b[(r_i + 1)..], a[w..], b[r_i], "move")
        tick_a = a[w..].find { |l| l =~ /image at task tick (\d+)/ }.to_s[/tick (\d+)/, 1]
        tick_b = b[mark_b..].find { |l| l =~ /restored \S+ .* task tick (\d+)/ }.to_s[/task tick (\d+)/, 1]
        raise Failure, "move: task tick #{tick_a.inspect} written, #{tick_b.inspect} restored" unless tick_a && tick_a == tick_b
        dig_a = a.reverse.find { |l| l =~ /\[Ckpt Counter\] VM set up for checkpoints: .*digest (\h+)/ }.to_s[/digest (\h+)/, 1]
        dig_b = b[mark_b..].find { |l| l =~ /restoring an image\).*digest (\h+)/ }.to_s[/digest (\h+)/, 1]
        names = b[mark_b..].find { |l| l =~ /restoring an image\).*\((\d+) names/ }.to_s[/\((\d+) names/, 1]
        raise Failure, "move: the names' digest #{dig_a.inspect} here, #{dig_b.inspect} there" unless dig_a && dig_a == dig_b
        app_b("kill", r["pid"].to_s)
        sleep 0.3
        "move: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}, t step #{c[:t_step]} ms, tick #{tick_a}, " \
          "#{names} names alike, carried in #{r['carried_ms'] - r['held_ms']} ms, held #{r['done_ms']} ms"
      end

      # The second program cannot restore a damaged image: the first goes on.
      def round_move_refused
        pid = start_counter
        mark = log_lines.size
        r = move(pid, "--damage")
        unless r["status"] == "stayed" && r["here"] == "continued" && r["reason"].to_s.include?("restore")
          raise Failure, "move refused: answered #{r.inspect}"
        end
        c = goes_on(mark, "move refused")
        raise Failure, "move refused: the second program runs the counter" if app_b("ps").fetch("apps").any? { |x| x["name"] == "Ckpt Counter" }
        kill_counter
        "move refused: #{r['reason'][0, 60]}...; went on here: a #{c[:a]}, main #{c[:main]}"
      end

      # Held, and nobody answers: the kernel lets the app go on after the hold.
      def round_move_no_answer
        pid = start_counter
        mark = log_lines.size
        r = app("checkpoint", pid.to_s, "--hold", HOLD_NO_ANSWER_MS.to_s, "--max", "8000")
        raise Failure, "move no answer: answered #{r.inspect}" unless r["status"] == "held"
        i = wait_line(/ckpt: \[Ckpt Counter\] held (\d+) ms after its image; goes on/, mark, "the held app going on", timeout: 10)
        held = log_lines[i][/held (\d+) ms/, 1].to_i
        raise Failure, "move no answer: held #{held} ms for a hold of #{HOLD_NO_ANSWER_MS}" unless held.between?(HOLD_NO_ANSWER_MS, HOLD_NO_ANSWER_MS + 1000)
        c = goes_on(mark, "move no answer")
        kill_counter
        "move no answer: held #{held} ms, went on: a #{c[:a]}, main #{c[:main]}"
      end

      # Held, then let go on: every counter goes on from the step after the
      # image, in the same program, and nothing ran while it was held.
      def goes_on(mark, round)
        on = wait_line(/ckpt: \[Ckpt Counter\] goes on from its image/, mark, "#{round}: the app going on")
        wait_line(/ckpt_counter: a=\d+ /, on + 1, "#{round}: a step of a after going on")
        wait_line(/ckpt_counter: main=\d+/, on + 1, "#{round}: a step of main after going on")
        lines = log_lines
        w = (mark...lines.size).find { |k| lines[k] =~ /checkpoint written: / } or raise Failure, "#{round}: no 'checkpoint written'"
        held = lines[(w + 1)...on].grep(/ckpt_counter: (a|b|main)=/)
        raise Failure, "#{round}: counted while held (#{held.first})" unless held.empty?
        res = {}
        %w[a main].each do |k|
          re = /ckpt_counter: #{k}=(\d+)/
          n0 = lines[0...w].reverse.find { |l| l =~ re }.to_s[re, 1].to_i
          n1 = lines[(on + 1)..].find { |l| l =~ re }.to_s[re, 1].to_i
          raise Failure, "#{round}: #{k} went from #{n0} to #{n1}" unless n1 == n0 + 1
          res[k.to_sym] = "#{n0}->#{n1}"
        end
        res
      end

      def kill_counter = kill_named("Ckpt Counter")

      def kill_named(name)
        ps.each { |a| app("kill", a["pid"].to_s) if a["name"] == name }
        sleep 0.3
      end

      # Read the log around the move: every counter goes on from the next step.
      def continuity(mark, round, tag = "ckpt_counter")
        lines = log_lines
        w = (mark...lines.size).find { |k| lines[k] =~ /checkpoint written: (\S+) \((\d+) bytes/ } or
          raise Failure, "#{round}: no 'checkpoint written' line"
        r = (w...lines.size).find { |k| lines[k] =~ /#{tag}: on_restore / } or
          raise Failure, "#{round}: no on_restore"
        continuity_of(lines[0...w], lines[(r + 1)..], lines[w..r], lines[r], round, tag)
      end

      # before: the lines up to the image; after: those after on_restore;
      # written: from the image on (its size, on_checkpoint); restore: the
      # on_restore line. The two halves may come from two programs.
      def continuity_of(before, after, written, restore, round, tag = "ckpt_counter")
        bytes = written.first[/\((\d+) bytes/, 1].to_i
        res = { bytes: bytes }
        %w[a b].each do |k|
          last = before.reverse.find { |l| l =~ /#{tag}: #{k}=\d+ t=/ } or raise Failure, "#{round}: no #{k} before"
          first = after.find { |l| l =~ /#{tag}: #{k}=\d+ t=/ } or raise Failure, "#{round}: no #{k} after"
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
        cp = written.find { |l| l =~ /#{tag}: on_checkpoint main=(\d+)/ } or raise Failure, "#{round}: no on_checkpoint"
        m0 = cp[/main=(\d+)/, 1].to_i
        m_restore = restore[/main=(\d+)/, 1].to_i
        raise Failure, "#{round}: on_restore has main=#{m_restore}, on_checkpoint had #{m0}" unless m_restore == m0
        nxt = after.find { |l| l =~ /#{tag}: main=\d+/ } or raise Failure, "#{round}: no main after"
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
