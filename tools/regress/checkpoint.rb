#!/usr/bin/env ruby
# frozen_string_literal: true
#
# checkpoint.rb - moving an app inside one native program and between two (rake
# regress:checkpoint, doc/checkpoint F1 and F2, doc/regression/usage.md chapter 12).
#
#   ruby tools/regress/checkpoint.rb                  # build, then standard and mruby
#   ruby tools/regress/checkpoint.rb --only mruby     # one engine configuration
#   ruby tools/regress/checkpoint.rb --no-build       # use the native builds there are
#   ruby tools/regress/checkpoint.rb --sim retro,modern   # also the simulation's rounds (F4a)
#   ruby tools/regress/checkpoint.rb --sim noja       # a Modern simulation without the efontJA fonts
#   ruby tools/regress/checkpoint.rb --web            # also the browser build's rounds (F5)
#   ruby tools/regress/checkpoint.rb --board naryav4  # also a connected board's rounds (F6, opt-in)
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
#             so is a movable app that keeps a file open (an open File is C
#             data no gem carries), refused at the safe point by the gem.
#   window    (F3) /app/test/ckpt_window.app.rb: a window that draws when it
#             is clicked or typed into, in a Japanese font and with a GfxBlock
#             of its own, a Task counting in the log and a topic. Clicked and
#             typed into, written and started again ("--restart"): the window
#             is the same pixel for pixel (the app draws it again in
#             on_restore, report/f3.md), the Task goes on from its next step,
#             the topic is subscribed again, and a click and a key reach it.
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
#             goes on from where it was held. The second program's desktop
#             shows no error dialog (the requester has the reason).
#   move no answer  "app checkpoint PID --hold 1500" and nobody answers: the
#             first program's kernel lets the app go on once the hold runs out.
#   window move  (F3) first held and let go on: while held, the moving mark
#             stands in the window's title bar, and once it goes on the window
#             is as it was. Then moved into the second program: the mark shown
#             and taken down once here, the window there the same as it was
#             here, a click and a key reach it there, and it is gone here.
#   window theme  a third program of the same build under another theme
#             (cyberpunk, in its user's /etc/system_conf.toml): the window
#             moved there takes that machine's title bar and border (the base
#             builds its frame again there, not the writing side's program),
#             pixel for pixel the window a fresh start there shows after the
#             same clicks and keys. The window's two FmrbUI labels, checked
#             apart from the frame: the one made without bg: takes the window
#             background of the theme there, the one made with bg: keeps the
#             app's colour (report/lang_bg_restore.md).
#   language  the same third program also runs in another language ("ja" in
#             its system_conf.toml; the first is "en"): ckpt_calls moved there
#             answers that language from FmrbI18n (which it asked before the
#             move, and which kept the writing side's language) as from
#             FmrbApp.language, and looks its word up in it.
#   requires  (F4) /app/test/ckpt_requires.app.rb declares in its .app.toml a
#             method it cannot go on without (requires = ["linux_settings"],
#             the native program's own): moved into the second program (the
#             same build has it), its answer names nothing missing, and the
#             image's .toml there carries the declaration on. The refusal (a
#             build without it) and the warning (a call not declared) need a
#             build that differs: the simulation's rounds (checkpoint_sim.rb).
#   machine   /app/test/ckpt_calls.app.rb moved into the second program: what
#             the base answers from the machine is the machine's where it
#             arrived (report/platform_restore.md). platform is the one
#             FmrbConst::PLATFORM there says, the app keeps no @platform
#             (the image would carry it), and FmrbAssoc's table, read before
#             the move, is no longer cached. Every build here says :linux;
#             the value itself differs only on a board (checkpoint_board.rb),
#             so this round holds the form that keeps it right. The rounds
#             with the simulation, the browser build over Asterism and the
#             board check the same on their on_restore line.
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
require_relative "tree"
require_relative "screens/image"
require_relative "checkpoint_sim"
require_relative "checkpoint_asterism"
require_relative "checkpoint_web"
require_relative "checkpoint_board"

module Regress
  module Checkpoint
    CORE = File.expand_path("../..", __dir__)
    # family-mruby (tools/fmrb_native.rb and the other tools); FMRB_ROOT= when
    # this tree is not inside one (a worktree of fmruby-core: a directory with
    # those tools whose fmruby-core is this tree; tree.rb)
    ROOT = Tree::ROOT
    NATIVE_TOOL = File.join(ROOT, "tools", "fmrb_native.rb")
    OUT = File.join(CORE, "tmp", "regress", "checkpoint")
    # The run directories and debug ports are this tree's own (tree.rb): the
    # shared checkout's are /tmp/fmrb-native-ckpt-<uid> and 5585 (5586, 5587
    # for the second and third program), another tree's carry its ID and take
    # ports from its block, so two trees run side by side.
    RUN_DIR = Tree.run_dir("fmrb-native-ckpt")
    PORT = Tree.port(5585, :ckpt_a)
    # the second program, for the moves between two programs (F2)
    RUN_DIR_B = Tree.run_dir("fmrb-native-ckpt-b")
    PORT_B = Tree.port(5586, :ckpt_b)
    # the third program, under another theme (round_window_theme)
    RUN_DIR_C = Tree.run_dir("fmrb-native-ckpt-c")
    PORT_C = Tree.port(5587, :ckpt_c)
    # its [theme]: the cyberpunk preset (config_dialog.rb's CFG_THEME_PRESETS)
    THEME_C = { "desktop_bg" => 0x00, "menu_bg" => 0x22, "window_bg" => 0x00, "text" => 0x1C,
                "text_light" => 0xFF, "highlight" => 0xE3, "border" => 0x0E, "button" => 0x46,
                "dir_color" => 0xFC }.freeze
    # its language (the first program's is "en", as every config ships)
    LANGUAGE_C = "ja"
    # the light theme every config/system_conf_* ships (the first program's)
    THEME_A = { "menu_bg" => 0xC5, "border" => 0x60, "window_bg" => 0xFF }.freeze
    # the colour ckpt_window gives its second FmrbUI label with bg:
    CHOSEN_BG = 0x92
    HOLD_NO_ANSWER_MS = 1500
    APP = "/app/test/ckpt_counter.app.rb"
    IMAGE = "/tmp/ckpt_counter.ckpt"
    NOT_MOVABLE = "/app/test/task_sleep.app.rb"
    TURNS = "/app/test/task_turns.app.rb"
    WINDOWED = "/app/test/ckpt_window.app.rb"
    WINDOWED_IMAGE = "/tmp/ckpt_window.ckpt"
    # where ckpt_window.app.toml puts the window (x, y, w, h), and a place for
    # the mouse pointer away from it while the screen is compared
    WINDOW_RECT = [60, 50, 160, 90].freeze
    POINTER_AWAY = [400, 200].freeze
    OPEN_FILE = "/app/test/ckpt_file.app.rb"
    SELF = "/app/test/ckpt_self.app.rb"
    SELF_REFUSED = "/app/test/ckpt_self_refused.app.rb"
    SHELL = "default/shell"
    REQUIRES = "/app/test/ckpt_requires.app.rb"
    MACHINE = "/app/test/ckpt_calls.app.rb"
    # one step of the counter Task "a" is its sleep (200 ms) behind the main
    # loop's wait (250 ms); a move may add a little to it, a lost or doubled
    # clock adds seconds
    MAX_T_STEP_MS = 800
    WAIT_S = 2.0

    class Failure < StandardError; end

    class Runner
      include SimRounds
      include AsterismRounds
      include WebRounds
      include BoardRounds

      def initialize(opts)
        @opts = opts
        @lines = []
      end

      def run
        t0 = now
        FileUtils.mkdir_p(OUT)
        configs = @opts[:only] ? [@opts[:only]] : %w[standard mruby]
        sim_check!
        web_check! if @opts[:web]
        board_check! if @opts[:board]
        build(configs) unless @opts[:no_build]
        web_build if @opts[:web] && !@opts[:no_build]
        results = []
        unless @opts[:no_native]
          begin
            router_up
            # --board-only: the board's rounds alone (the native program
            # is still their other end)
            results = @opts[:board_only] ? [] : configs.map { |c| run_config(c) }
            # doc/checkpoint F5: the browser build, once, with the first
            # configuration's native program (checkpoint_web.rb)
            results << run_web(configs.first) if @opts[:web]
            # doc/checkpoint F6: a connected board (checkpoint_board.rb)
            results << run_board(configs.first) if @opts[:board]
          ensure
            router_down
          end
        end
        # F4a: the simulation and the native program, after the native rounds
        # (the native programs are built by then; build/ is borrowed)
        results += run_sims(configs)
        results.each { |r| puts r }
        ok = results.all? { |r| r.include?(": ok") || r.start_with?("build/:") }
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
        if args.first == "up"
          begin
            Tree.need_free_port!(port, "the native program of #{run_dir}")
          rescue Tree::Busy => e
            raise Failure, e.message
          end
        end
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
        asterism_home(File.join(home, "user"), NODE_A, NODE_B)
        native("up", "--engine", config, "--machine", "sdl", "--user-dir", File.join(home, "user"),
               "--cache-dir", File.join(home, "cache"), "--timeout", "90", timeout: 120)
        notes = []
        notes << round_turns
        notes << round_restart
        notes << round_wait
        notes << round_refuse
        notes << round_refuse_file
        notes << round_window_restart
        notes << round_self
        notes << round_self_refused
        notes << round_shell
        up_b(config)
        notes << round_move
        notes << round_move_refused
        notes << round_move_no_answer
        notes << round_window_move
        notes.concat(third_program_rounds(config))
        notes << round_requires_move
        notes << round_machine_move
        notes.concat(asterism_rounds)
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
        [[RUN_DIR, PORT], [RUN_DIR_B, PORT_B], [RUN_DIR_C, PORT_C]].each do |dir, port|
          native("down", timeout: 30, run_dir: dir, port: port) if File.exist?(File.join(dir, "pid"))
        rescue Failure
          nil
        end
      end

      # The second program: the same build, a run dir, port and home of its own.
      def up_b(config)
        home = File.join(OUT, "#{config}_home_b")
        @home_b = home
        FileUtils.rm_rf(home)
        FileUtils.mkdir_p(%w[user cache].map { |d| File.join(home, d) })
        asterism_home(File.join(home, "user"), NODE_B, NODE_A)
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

      # Declared movable, refused at the safe point (the gem cannot carry the
      # file it keeps open): the app is told nothing and goes on.
      def round_refuse_file
        from = log_lines.size
        pid = app("spawn", OPEN_FILE).fetch("pid")
        wait_line(/ckpt_file: n=2\b/, from, "ckpt_file counting")
        r = checkpoint(pid, restart: true)
        unless r["status"] == "refused" && r["reason"].to_s.include?("File")
          raise Failure, "refuse file: answered #{r.inspect}"
        end
        mark = log_lines.size
        wait_line(/ckpt_file: n=\d+/, mark, "ckpt_file going on after the refusal")
        raise Failure, "refuse file: the app is gone" unless ps.any? { |a| a["pid"] == pid }
        app("kill", pid.to_s)
        "file refused at the safe point: #{r['reason'][0, 80]}"
      end

      # ---- a window (F3) ----

      def shot(run_dir = RUN_DIR, port = PORT)
        out = File.join(OUT, "shot.png")
        native("shot", out, run_dir: run_dir, port: port)
        Regress::Screens::Image.load(out)
      end

      # The pointer away from the window, then the screen once it stays the
      # same (a frame drawn a moment ago may still be on its way).
      def window_shot(run_dir = RUN_DIR, port = PORT)
        native("input", "move", *POINTER_AWAY.map(&:to_s), run_dir: run_dir, port: port)
        prev = nil
        deadline = now + 5
        loop do
          sleep 0.3
          img = shot(run_dir, port)
          return img if prev && img.same?(prev, [], rect: WINDOW_RECT)
          raise Failure, "the window did not settle in 5 s" if now > deadline
          prev = img
        end
      end

      # A click in the window's drawing area and a key, at (x, y) inside the
      # window; the app logs both.
      def poke_window(x, y, key, run_dir: RUN_DIR, port: PORT)
        wx, wy = WINDOW_RECT
        from = log_lines(run_dir).size
        native("input", "click", (wx + x).to_s, (wy + y).to_s, "sleep", "200", "text", key,
               run_dir: run_dir, port: port)
        wait_line(/ckpt_window: click #{x},#{y} dots=\d+/, from, "the click at #{x},#{y}", run_dir: run_dir)
        wait_line(/ckpt_window: key #{key} text=/, from, "the key #{key}", run_dir: run_dir)
      end

      def start_window
        from = log_lines.size
        pid = app("spawn", WINDOWED).fetch("pid")
        wait_line(/ckpt_window: task=2 /, from, "ckpt_window's Task")
        poke_window(40, 50, "a")
        poke_window(90, 66, "b")
        pid
      end

      # The window once it looks like `want`, or as it is after 2 s.
      def settled_window_shot(want, timeout: 2.0)
        deadline = now + timeout
        loop do
          shot = window_shot
          return shot if want.diff(shot, [], rect: WINDOW_RECT)[0].zero? || now > deadline
          sleep 0.2
        end
      end

      def compare_window(before, after, round)
        count, box = before.diff(after, [], rect: WINDOW_RECT)
        return if count.zero?
        out = File.join(OUT, "#{round.tr(' ', '_')}.png")
        after.write(out)
        before.diff_picture(after, []).write(out.sub(".png", ".diff.png"))
        raise Failure, "#{round}: the window differs in #{count} px (#{box.inspect}; #{out.sub("#{CORE}/", '')})"
      end

      # The Task's count across the move, and the topic subscribed again
      # (subs: the restored side's lines from the image on).
      def window_goes_on(before, after, subs, round, pid_after)
        last = before.reverse.find { |l| l =~ /ckpt_window: task=\d+ / } or raise Failure, "#{round}: no task step before"
        first = after.find { |l| l =~ /ckpt_window: task=\d+ / } or raise Failure, "#{round}: no task step after"
        n0 = last[/task=(\d+)/, 1].to_i
        n1 = first[/task=(\d+)/, 1].to_i
        raise Failure, "#{round}: the Task went from #{n0} to #{n1}" unless n1 == n0 + 1
        unless subs.any? { |l| l =~ /PID #{pid_after} subscribed to 'ckpt_window'/ }
          raise Failure, "#{round}: the topic was not subscribed again"
        end
        "#{n0}->#{n1}"
      end

      # Written and started again in the same program (F1's way, F3's app).
      def round_window_restart
        pid = start_window
        before = window_shot
        mark = log_lines.size
        r = checkpoint(pid, restart: true)
        raise Failure, "window: answered #{r.inspect}" unless r["status"] == "written" && r["path"] == WINDOWED_IMAGE
        restored = wait_line(/ckpt_window: on_restore dots=2 text=ab/, mark, "the window's on_restore")
        wait_line(/ckpt_window: task=\d+ /, restored + 1, "a Task step after the restore")
        after = window_shot
        compare_window(before, after, "window")
        lines = log_lines
        w = (mark...lines.size).find { |k| lines[k] =~ /checkpoint written: \S+ \((\d+) bytes/ } or
          raise Failure, "window: no 'checkpoint written'"
        bytes = lines[w][/\((\d+) bytes/, 1]
        new_pid = ps.find { |a| a["name"] == "Ckpt Window" }&.fetch("pid") or raise Failure, "window: not running again"
        task = window_goes_on(lines[0...w], lines[restored..], lines[w..], "window", new_pid)
        poke_window(60, 60, "c")
        app("kill", new_pid.to_s)
        sleep 0.3
        "window: same #{WINDOW_RECT[2]}x#{WINDOW_RECT[3]} px, task #{task}, image #{bytes} B, click and key after"
      end

      # Held and let go on (the moving mark): while held, a badge stands in
      # the window's title bar; once it goes on, the window is as it was.
      def window_mark(pid, before)
        mark = log_lines.size
        r = app("checkpoint", pid.to_s, "--hold", "5000", "--max", "8000")
        raise Failure, "window mark: answered #{r.inspect}" unless r["status"] == "held"
        wait_line(/ckpt: \[Ckpt Window\] moving mark shown/, mark, "the moving mark")
        held = window_shot
        held.write(File.join(OUT, "window_mark.png"))   # for a person to look at
        count, box = before.diff(held, [], rect: WINDOW_RECT)
        title_bar = box && box[1] >= WINDOW_RECT[1] && box[3] < WINDOW_RECT[1] + 11
        unless count.positive? && title_bar
          raise Failure, "window mark: no mark in the title bar while held (#{count} px, #{box.inspect})"
        end
        app("finish", pid.to_s, "continue", "regress")
        wait_line(/ckpt: \[Ckpt Window\] moving mark taken down/, mark, "the mark taken down")
        wait_line(/ckpt: \[Ckpt Window\] goes on from its image/, mark, "the window app going on")
        # The line is logged when the screen is asked to compose again, not
        # when it has: under load the first shot can still show the mark.
        compare_window(before, settled_window_shot(before), "window mark gone")
        "#{count} px in the title bar"
      end

      # Moved into the second program (F2's way, F3's app).
      def round_window_move
        pid = start_window
        before = window_shot
        mark_note = window_mark(pid, before)
        mark = log_lines.size
        mark_b = log_lines(RUN_DIR_B).size
        r = move(pid)
        raise Failure, "window move: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        restored = wait_line(/ckpt_window: on_restore dots=2 text=ab/, mark_b, "on_restore in the second program",
                             run_dir: RUN_DIR_B)
        wait_line(/ckpt_window: task=\d+ /, restored + 1, "a Task step in the second program", run_dir: RUN_DIR_B)
        wait_line(/\[Ckpt Window gen=\d+\] Task exiting normally/, mark, "the first program's copy closing")
        after = window_shot(RUN_DIR_B, PORT_B)
        compare_window(before, after, "window move")
        raise Failure, "window move: the first program still has the window" if ps.any? { |x| x["name"] == "Ckpt Window" }
        a_mark = log_lines[mark..]
        unless a_mark.grep(/\[Ckpt Window\] moving mark shown/).size == 1 && a_mark.grep(/\[Ckpt Window\] moving mark taken down/).size == 1
          raise Failure, "window move: the moving mark was not shown and taken down once while held"
        end
        a = log_lines
        w = (mark...a.size).find { |k| a[k] =~ /checkpoint written: / } or raise Failure, "window move: no 'checkpoint written'"
        stray = a[(w + 1)..].grep(/ckpt_window: task=/)
        raise Failure, "window move: the first program counted on after its image (#{stray.first})" unless stray.empty?
        b = log_lines(RUN_DIR_B)
        task = window_goes_on(a[0...w], b[restored..], b[mark_b..], "window move", r["pid"])
        # the font picked there from what the app asked for (report/font_pct.md)
        font_on(b[restored], "ja,12", "window move")
        poke_window(60, 60, "c", run_dir: RUN_DIR_B, port: PORT_B)
        app_b("kill", r["pid"].to_s)
        sleep 0.3
        "window move: mark while held (#{mark_note}) and gone after, same there, task #{task}, click and key there, " \
          "carried in #{r['carried_ms'] - r['held_ms']} ms"
      end

      # The third program: the same build under THEME_C and in LANGUAGE_C.
      # Its user's /etc/system_conf.toml is the build's first-start one with
      # the [theme] section and the language written over (the program fills
      # in only what a user's file lacks, so the colours and the language here
      # are the ones it runs with).
      def up_c(config)
        home = File.join(OUT, "#{config}_home_c")
        FileUtils.rm_rf(home)
        FileUtils.mkdir_p(%w[user/etc cache].map { |d| File.join(home, d) })
        seed = File.join(CORE, "native", "build", "root", "etc", "system_conf.toml")
        seed = File.join(CORE, "config", "system_conf_native.toml") unless File.exist?(seed)
        theme = "[theme]\n" + THEME_C.map { |k, v| format("%s = 0x%02X\n", k, v) }.join
        conf = File.read(seed).sub(/^\[theme\]\n(?:[a-z_]+ *=.*\n)+/, theme)
        raise Failure, "window theme: no [theme] section in #{seed}" unless conf.include?(theme)
        language = "language = \"#{LANGUAGE_C}\""
        conf = conf.sub(/^language *=.*$/, language)
        raise Failure, "language: no language line in #{seed}" unless conf.include?(language)
        File.write(File.join(home, "user", "etc", "system_conf.toml"), conf)
        native("up", "--engine", config, "--machine", "sdl", "--debug-port", PORT_C.to_s,
               "--user-dir", File.join(home, "user"), "--cache-dir", File.join(home, "cache"),
               "--timeout", "90", timeout: 120, run_dir: RUN_DIR_C, port: PORT_C)
      end

      # An RGB332 theme colour as the native program's shot shows it (through
      # its RGB565 frame), within a step either way.
      def theme_colour?(rgb, c332)
        want = [(c332 >> 5) * 255 / 7, ((c332 >> 2) & 7) * 255 / 7, (c332 & 3) * 255 / 3]
        rgb.zip(want).all? { |a, b| (a - b).abs <= 8 }
      end

      # A title bar pixel clear of the hamburger, the title and the close
      # button, and one of the left border below the title bar.
      def frame_pixels(img)
        wx, wy, ww, = WINDOW_RECT
        [img.pixel(wx + ww - 30, wy + 5), img.pixel(wx, wy + 50)]
      end

      # The rounds with the third program (another theme, another language).
      def third_program_rounds(config)
        up_c(config)
        notes = round_window_theme
        notes << round_language_move
        forced = log_lines(RUN_DIR_C).grep(/No response in \d+ms, forcing termination/)
        raise Failure, "forced kill in the third program: #{forced.first}" unless forced.empty?
        notes
      ensure
        FileUtils.cp(log_path(RUN_DIR_C), File.join(OUT, "#{config}_c.log")) if File.exist?(log_path(RUN_DIR_C))
        begin
          native("down", timeout: 30, run_dir: RUN_DIR_C, port: PORT_C) if File.exist?(File.join(RUN_DIR_C, "pid"))
        rescue Failure
          nil
        end
      end

      # A pixel of each of ckpt_window's two FmrbUI labels clear of its text:
      # the one made without bg:, the one made with bg: CHOSEN_BG.
      def label_pixels(img)
        wx, wy, = WINDOW_RECT
        [img.pixel(wx + 31, wy + 35), img.pixel(wx + 67, wy + 35)]
      end

      # The labels' ground in a shot: the default one in want_default, the
      # chosen one in CHOSEN_BG.
      def labels_in?(img, want_default)
        dflt, chosen = label_pixels(img)
        theme_colour?(dflt, want_default) && theme_colour?(chosen, CHOSEN_BG)
      end

      # Moved into a program under another theme: the window there wears that
      # theme's frame, and is the window a fresh start there draws. Its
      # widgets are checked apart from the frame: the default ground follows
      # the theme there, the chosen one is carried.
      def round_window_theme
        # the window as a fresh start draws it there, after the same clicks
        # and keys start_window gives it here
        from_c = log_lines(RUN_DIR_C).size
        fresh_pid = app("spawn", WINDOWED, run_dir: RUN_DIR_C, port: PORT_C).fetch("pid")
        wait_line(/ckpt_window: task=2 /, from_c, "ckpt_window's Task in the third program", run_dir: RUN_DIR_C)
        poke_window(40, 50, "a", run_dir: RUN_DIR_C, port: PORT_C)
        poke_window(90, 66, "b", run_dir: RUN_DIR_C, port: PORT_C)
        fresh = window_shot(RUN_DIR_C, PORT_C)
        app("kill", fresh_pid.to_s, run_dir: RUN_DIR_C, port: PORT_C)
        sleep 0.5
        bar, border = frame_pixels(fresh)
        unless theme_colour?(bar, THEME_C["menu_bg"]) && theme_colour?(border, THEME_C["border"])
          raise Failure, "window theme: a fresh window there is not in its theme (bar #{bar.inspect}, border #{border.inspect})"
        end
        unless labels_in?(fresh, THEME_C["window_bg"])
          raise Failure, "widget ground: a fresh window there has its labels on #{label_pixels(fresh).inspect}"
        end
        pid = start_window
        before = window_shot
        bar, border = frame_pixels(before)
        unless theme_colour?(bar, THEME_A["menu_bg"]) && theme_colour?(border, THEME_A["border"])
          raise Failure, "window theme: the first program's window is not in the light theme " \
                         "(bar #{bar.inspect}, border #{border.inspect})"
        end
        unless labels_in?(before, THEME_A["window_bg"])
          raise Failure, "widget ground: the first program's labels are on #{label_pixels(before).inspect}"
        end
        mark_c = log_lines(RUN_DIR_C).size
        r = move(pid, to: RUN_DIR_C)
        raise Failure, "window theme: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        restored = wait_line(/ckpt_window: on_restore dots=2 text=ab/, mark_c, "on_restore in the third program",
                             run_dir: RUN_DIR_C)
        wait_line(/ckpt_window: task=\d+ /, restored + 1, "a Task step in the third program", run_dir: RUN_DIR_C)
        after = window_shot(RUN_DIR_C, PORT_C)
        bar, border = frame_pixels(after)
        unless theme_colour?(bar, THEME_C["menu_bg"]) && theme_colour?(border, THEME_C["border"])
          after.write(File.join(OUT, "window_theme.png"))
          raise Failure, format("window theme: the frame kept the writing side's theme (title bar %s for 0x%02X, " \
                                "border %s for 0x%02X; window_theme.png)",
                                bar.inspect, THEME_C["menu_bg"], border.inspect, THEME_C["border"])
        end
        frame_note = "window theme: title bar #{bar.inspect} and border #{border.inspect} there"
        # the labels, apart from the frame: what the app holds (its log line)
        # and what it drew
        line = log_lines(RUN_DIR_C)[restored]
        held = [line[/ ui_bg=(\d+)/, 1], line[/ chosen_bg=(\d+)/, 1], line[/ window_bg=(\d+)/, 1]]
        raise Failure, "widget ground: no colours in #{line.inspect}" if held.include?(nil)
        ui_bg, chosen_bg, window_bg = held.map(&:to_i)
        unless window_bg == THEME_C["window_bg"] && ui_bg == window_bg && chosen_bg == CHOSEN_BG
          raise Failure, format("widget ground: the labels hold 0x%02X (default) and 0x%02X (chosen) where the window " \
                                "background there is 0x%02X (%s)", ui_bg, chosen_bg, window_bg, line)
        end
        unless labels_in?(after, THEME_C["window_bg"])
          after.write(File.join(OUT, "widget_ground.png"))
          raise Failure, "widget ground: the labels are drawn on #{label_pixels(after).inspect} (widget_ground.png)"
        end
        compare_window(fresh, after, "window theme")
        app("kill", r["pid"].to_s, run_dir: RUN_DIR_C, port: PORT_C)
        sleep 0.3
        [frame_note + ", the window a fresh start draws there",
         format("widget ground: default 0x%02X there, chosen 0x%02X carried", ui_bg, chosen_bg)]
      end

      # ckpt_calls moved into the third program, which runs in LANGUAGE_C:
      # FmrbI18n answers that language there, as FmrbApp.language does, and
      # looks its words up in it (report/lang_bg_restore.md).
      def round_language_move
        from = log_lines.size
        pid = app("spawn", MACHINE).fetch("pid")
        i = wait_line(/ckpt_calls: main=3 /, from, "ckpt_calls counting")
        here = log_lines[i]
        unless here.include?(" lang=en word=word-en language=en")
          raise Failure, "language: the first program is not in English (#{here})"
        end
        mark_c = log_lines(RUN_DIR_C).size
        r = move(pid, to: RUN_DIR_C)
        raise Failure, "language: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        j = wait_line(/ckpt_calls: on_restore main=\d+/, mark_c, "on_restore in the third program", run_dir: RUN_DIR_C)
        line = log_lines(RUN_DIR_C)[j]
        lang = line[/ lang=(\w+)/, 1]
        word = line[/ word=(\S+)/, 1]
        language = line[/ language=(\w+)/, 1]
        raise Failure, "language: no language in #{line.inspect}" unless lang && word && language
        raise Failure, "language: FmrbApp.language there is #{language}, not #{LANGUAGE_C}" unless language == LANGUAGE_C
        raise Failure, "language: FmrbI18n says #{lang} where FmrbApp.language is #{language} (#{line})" unless lang == language
        raise Failure, "language: the word there is #{word}, not word-#{language}" unless word == "word-#{language}"
        app("kill", r["pid"].to_s, run_dir: RUN_DIR_C, port: PORT_C)
        sleep 0.3
        "language: #{lang} there (#{word}), as FmrbApp.language"
      end

      # An app that declares a method it requires, moved into the same build
      # (F4): nothing missing there, the declaration carried with the image.
      def round_requires_move
        from = log_lines.size
        pid = app("spawn", REQUIRES).fetch("pid")
        wait_line(/ckpt_requires: main=3 /, from, "ckpt_requires counting")
        mark_b = log_lines(RUN_DIR_B).size
        r = move(pid)
        raise Failure, "requires: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        raise Failure, "requires: the same build named #{r['missing'].inspect} missing" if r["missing"]
        i = wait_line(/ckpt_requires: on_restore main=(\d+)/, mark_b, "on_restore in the second program", run_dir: RUN_DIR_B)
        m0 = log_lines(RUN_DIR_B)[i][/main=(\d+)/, 1].to_i
        j = wait_line(/ckpt_requires: main=\d+ /, i + 1, "a step in the second program", run_dir: RUN_DIR_B)
        m1 = log_lines(RUN_DIR_B)[j][/main=(\d+)/, 1].to_i
        raise Failure, "requires: main went from #{m0} to #{m1}" unless m1 == m0 + 1
        toml = File.join(OUT, "ckpt_requires.toml")
        native("file", "get", "/tmp/ckpt_requires.toml", toml, run_dir: RUN_DIR_B, port: PORT_B)
        line = File.read(toml)[/^requires = .*$/]
        raise Failure, "requires: the image's .toml there says #{line.inspect}" unless line == 'requires = ["linux_settings"]'
        app_b("kill", r["pid"].to_s)
        sleep 0.3
        "requires: moved, nothing missing, #{line} carried"
      end

      # ckpt_calls moved into the second program: the base answers from the
      # machine there (report/platform_restore.md).
      def round_machine_move
        from = log_lines.size
        pid = app("spawn", MACHINE).fetch("pid")
        i = wait_line(/ckpt_calls: main=3 /, from, "ckpt_calls counting")
        here = log_lines[i]
        raise Failure, "machine: FmrbAssoc's table not cached before the move (#{here})" unless here.include?("assoc_cached=true")
        mark_b = log_lines(RUN_DIR_B).size
        r = move(pid)
        raise Failure, "machine: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        j = wait_line(/ckpt_calls: on_restore main=\d+/, mark_b, "on_restore in the second program", run_dir: RUN_DIR_B)
        note = machine_values!(log_lines(RUN_DIR_B)[j], "machine")
        app_b("kill", r["pid"].to_s)
        sleep 0.3
        "machine: #{note}"
      end

      # ckpt_calls's on_restore line: what the base answered from the machine
      # where the app arrived. platform must be that machine's (the symbol of
      # its FmrbConst::PLATFORM, and `want` when the caller knows it), the app
      # must keep no @platform, and FmrbAssoc's table must have been forgotten.
      def machine_values!(line, round, want: nil)
        got = line.to_s[/ platform=(\w+)/, 1]
        const = line.to_s[/ const=(\w+)/, 1]
        raise Failure, "#{round}: no machine values in #{line.inspect}" unless got && const
        raise Failure, "#{round}: platform #{got} where FmrbConst::PLATFORM is #{const}" unless got == const
        raise Failure, "#{round}: platform #{got}, not #{want}" if want && got != want
        raise Failure, "#{round}: the app keeps @platform (#{line})" unless line.include?("platform_ivar=false")
        raise Failure, "#{round}: FmrbAssoc's table still cached (#{line})" unless line.include?("assoc_cached=false")
        "platform #{got} there, no @platform, associations read again"
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

      def move(pid, *opts, to: RUN_DIR_B)
        JSON.parse(native("app", "move", pid.to_s, "--to", to, *opts, timeout: 60).lines.last.to_s)
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
        mark_b = log_lines(RUN_DIR_B).size
        r = move(pid, "--damage")
        unless r["status"] == "stayed" && r["here"] == "continued" && r["reason"].to_s.include?("restore")
          raise Failure, "move refused: answered #{r.inspect}"
        end
        c = goes_on(mark, "move refused")
        raise Failure, "move refused: the second program runs the counter" if app_b("ps").fetch("apps").any? { |x| x["name"] == "Ckpt Counter" }
        # The refusal went back to the requester: the second program logs it
        # but its desktop shows no error dialog (the kernel logs "App '<name>':"
        # exactly when it forwards show_error to the desktop).
        b = log_lines(RUN_DIR_B)[mark_b..]
        unless b.any? { |l| l =~ /\[Ckpt Counter\] cannot restore .*told the requester; no error dialog/ }
          raise Failure, "move refused: the second program did not log the refusal"
        end
        shown = b.grep(/App 'Ckpt Counter': /)
        raise Failure, "move refused: the second program's desktop was told to show an error (#{shown.first})" unless shown.empty?
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

    # Stop this tree's native programs and routers that are still up.
    def self.reap
      [RUN_DIR, RUN_DIR_B, RUN_DIR_C, SIM_RUN_DIR].each do |dir|
        pid = Tree.native_pid(dir) or next
        warn "checkpoint: stopping the native program left in #{dir} (pid #{pid})"
        Tree.stop_pid(pid)
      end
      names = [AsterismRounds::ROUTER_NAME, BoardRounds::ROUTER_NAME_BOARD]
      out, = Open3.capture2e("docker", "ps", "-q", "--filter", "name=^(#{names.join('|')})$")
      return if out.strip.empty?
      warn "checkpoint: removing the Zenoh router(s) left running (#{names.join(', ')})"
      Open3.capture2e("docker", "rm", "-f", *names)
    rescue SystemCallError
      nil
    end

    def self.main(argv)
      opts = {}
      OptionParser.new do |o|
        o.banner = "usage: checkpoint.rb [--only standard|mruby] [--no-build] [--sim retro,modern,noja|none] [--web] " \
                   "[--board NAME [--board-only]]"
        o.on("--only NAME", %w[standard mruby]) { |v| opts[:only] = v }
        o.on("--no-build") { opts[:no_build] = true }
        o.on("--sim LIST") { |v| opts[:sim] = v }
        o.on("--no-native") { opts[:no_native] = true }
        o.on("--web") { opts[:web] = true }
        o.on("--board NAME") { |v| opts[:board] = v }
        o.on("--board-only") { opts[:board_only] = true }
      end.parse!(argv)
      # What this run starts is stopped when it ends however it ends (the
      # ensures do it; this is for an end they did not reach, e.g. a second
      # Ctrl+C while they run). Only this tree's own run directories and
      # routers (tree.rb): another tree's run is never touched.
      at_exit { Checkpoint.reap }
      Runner.new(opts).run
    rescue Failure => e
      puts "checkpoint: FAILED #{e.message}"
      false
    end
  end
end

exit(Regress::Checkpoint.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__
