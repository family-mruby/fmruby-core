# frozen_string_literal: true
#
# checkpoint_sim.rb - moving an app between the Linux simulation and the
# native program (rake regress:checkpoint, doc/checkpoint F4a,
# doc/regression/usage.md chapter 12). Part of checkpoint.rb's run: its
# Runner includes this, after the native rounds of each configuration.
#
# The two are different builds: the simulation is the ESP-IDF Linux build
# (its tick advanced by a task of its own, the display of a family: Retro
# 320x240 or Modern 426x240), the native program the hosted one (the VM
# supplies its own tick; machine "sdl", Modern's 426x240). For each family
# (--sim retro,modern) the simulation is built in the configuration's
# engines (rake build:linux with the board's pinned options, as
# regress:screens does), its three containers come up headless, and a native
# program of the same configuration beside it. Only when asked (--sim
# retro,modern; rake regress:checkpoint SIM=1, which regress:full does).
# Then:
#
#   sim restart   the counter written and started again inside the
#                 simulation (F1 on the simulation's tick).
#   sim hold      the counter held and nobody answers: it goes on in the
#                 simulation after the hold (F2's hold on the simulation).
#   sim window    ckpt_window written and started again inside the
#                 simulation: the window the same pixel for pixel (F3).
#   to native     the counter moved from the simulation to the native program
#                 (the debug server's commands on both: the simulation's is on
#                 5555), every counter going on from the next step there.
#   to sim        and back into the simulation.
#   window        ckpt_window, clicked and typed into in the simulation, moved
#                 to the native program: the window there the same in RGB332
#                 (the two screenshot tools expand RGB332 differently; the
#                 four corner pixels show the desktop behind the rounded
#                 corners and are left out), a click and a key reach it.
#   wide          ckpt_wide (a 380 px window) moved from the native program to
#                 the simulation: on Retro the window is made to fit the
#                 screen and the app gets on_resize before on_restore; the red
#                 squares it draws in the corners of its user area are where
#                 the new size puts them. On Modern it keeps its size and gets
#                 no on_resize.
#   full          ckpt_full (fullscreen) moved from the simulation to the
#                 native program: from Retro's 320x240 to 426x240 (on_resize),
#                 from Modern's at the same size (none).
#   calls         ckpt_calls, whose code calls FmrbApp.linux_settings (the
#                 native program's; the simulation has no such method; the
#                 call is guarded and never made), moved from the native
#                 program to the simulation: it moves, the answer names
#                 linux_settings as missing, the simulation logs the warning
#                 and the app counts on there (doc/checkpoint/movable_apps.md).
#   requires      ckpt_requires, the same app declaring the name as required
#                 in its .app.toml: the simulation refuses it, and it goes on
#                 in the native program from where it was held.
#
# And once, in the first configuration, a Modern simulation built without the
# efontJA fonts (FMRB_FONT_JA=0, as the AtomS3R and StickS3 are; "noja" in
# --sim, which SIM=1 includes):
#
#   font          ckpt_window, which asks for [:ja, 12], moved from the native
#                 program into it and back: misaki 8 there, 12 again back
#                 here (the base picks the font from what was asked for, not
#                 from what the machine it passed through picked;
#                 doc/checkpoint/report/font_pct.md), and the window back here
#                 the same as before it left.
#
# A sim stack that is already running (someone's) is never touched: the run
# refuses. build/ is moved aside and put back (build_tree.rb), as
# regress:run and regress:screens do; do not run those at the same time.
require_relative "build_tree"
require_relative "targets"
require_relative "sim_run"

module Regress
  module Checkpoint
    SIM_PORT = 5555
    # fmrb_native.rb wants a run directory for its own files; nothing is kept
    # in this one (the simulation's log is docker's)
    SIM_RUN_DIR = "/tmp/fmrb-native-ckpt-sim-#{Process.uid}"
    WIDE = "/app/test/ckpt_wide.app.rb"
    FULL = "/app/test/ckpt_full.app.rb"
    CALLS = "/app/test/ckpt_calls.app.rb"
    # where ckpt_wide.app.toml puts the window: [x, y, w, h]
    WIDE_RECT = [30, 60, 380, 100].freeze
    NATIVE_SCREEN = [426, 240].freeze
    # the simulation without the efontJA fonts (Modern's screen)
    NOJA = "noja"
    TITLE_BAR_H = 11
    MRUBY_ENV = {
      "FMRB_KERNEL_ENGINE" => "mruby",
      "FMRB_APP_ENGINE_DESKTOP" => "mruby",
      "FMRB_APP_ENGINE_EDITOR" => "mruby"
    }.freeze

    # One end of a move: the native program A or the simulation.
    Endpoint = Struct.new(:name, :kind, :run_dir, :port)

    module SimRounds
      # Native rounds only unless asked (--sim retro,modern; rake SIM=1)
      def sim_families
        v = (@opts[:sim] || "none").split(",").map(&:strip)
        v = ["retro", "modern", NOJA] if v == ["1"]
        v == ["none"] || v == ["0"] ? [] : v
      end

      # Checked before anything is built: the stack must be free.
      def sim_check!
        return if sim_families.empty?
        raise Failure, "a sim stack is already running (someone's?); leave it or take it down first" if SimRun.core_running?
        ga = File.join(ROOT, "fmruby-graphics-audio", "build", "fmruby-graphics-audio.elf")
        raise Failure, "fmruby-graphics-audio has no Linux build (rake build:linux there first)" unless SimRun.elf_x86?(ga)
      end

      # Every configuration's families, build/ borrowed for the time.
      def run_sims(configs)
        return [] if sim_families.empty?
        tree = BuildTree.new(CORE)
        begin
          tree.stash!
        rescue RuntimeError => e
          raise Failure, e.message
        end
        lines = []
        begin
          sim_asterism_setup
          # family by family, the one the sim is at first: graphics-audio
          # needs a second start for each switch (SimRun.current_first)
          SimRun.current_first(sim_families - [NOJA]) { |f| f }.each do |f|
            configs.each { |c| lines << run_sim(c, f, tree) }
          end
          lines << run_sim(configs.first, NOJA, tree) if sim_families.include?(NOJA)
        ensure
          SimRun.down if SimRun.core_running?
          sim_asterism_teardown
          lines << "build/: #{tree.restore!}"
        end
        lines
      end

      def sim_env(config, family)
        if family == NOJA
          return sim_env(config, "modern").merge("FMRB_FONT_JA" => "0")
        end
        t = TARGETS.find { |x| x.family == family && x.firmware } or raise Failure, "no board of family #{family}"
        e = t.firmware_env
        config == "mruby" ? e.merge(MRUBY_ENV) : e
      end

      def run_sim(config, family, tree)
        t0 = now
        name = "#{config} sim #{family}"
        blog = File.join(OUT, "sim_#{config}_#{family}.build.log")
        ok, bsecs, cached = tree.sim_build!(sim_env(config, family), blog)
        raise Failure, "build:linux failed (#{blog.sub("#{CORE}/", '')})" unless ok
        raise Failure, "build/fmruby-core.elf is not an x86-64 build" unless SimRun.elf_x86?(File.join(CORE, "build", "fmruby-core.elf"))
        screen = family == NOJA ? "modern" : family
        sim_up(screen)
        down_if_running
        home = File.join(OUT, "#{config}_home")
        FileUtils.rm_rf(home)
        FileUtils.mkdir_p(%w[user cache].map { |d| File.join(home, d) })
        asterism_home(File.join(home, "user"), AsterismRounds::NODE_A, AsterismRounds::NODE_SIM,
                      router: AsterismRounds::SIM_ROUTER_HERE)
        native("up", "--engine", config, "--machine", "sdl", "--user-dir", File.join(home, "user"),
               "--cache-dir", File.join(home, "cache"), "--timeout", "90", timeout: 120)
        @sim = Endpoint.new("the simulation", :sim, SIM_RUN_DIR, SIM_PORT)
        @nat = Endpoint.new("the native program", :native, RUN_DIR, PORT)
        @family = screen
        notes = []
        if family == NOJA
          notes << sim_round_font
          sim_no_forced_kill
          return "#{name}: ok (#{notes.join('; ')}; build #{cached ? 'cached' : "#{bsecs.round}s"}, #{(now - t0 - bsecs).round}s)"
        end
        notes << sim_round_restart
        notes << sim_round_hold
        notes << sim_round_window_restart
        notes << sim_round_to_native
        notes << sim_round_to_sim
        notes << sim_round_window
        notes << sim_round_wide
        notes << sim_round_full
        notes << sim_round_calls
        notes << sim_round_requires
        # F5: the same two over Asterism
        notes.concat(sim_asterism_rounds)
        sim_no_forced_kill
        "#{name}: ok (#{notes.join('; ')}; build #{cached ? 'cached' : "#{bsecs.round}s"}, #{(now - t0 - bsecs).round}s)"
      rescue Failure, RuntimeError => e
        "#{name}: FAILED #{e.message}"
      ensure
        File.write(File.join(OUT, "sim_#{config}_#{family}.log"), SimRun.core_log) if SimRun.core_running?
        FileUtils.cp(log_path, File.join(OUT, "sim_#{config}_#{family}_native.log")) if File.exist?(log_path)
        down_if_running
        SimRun.down if SimRun.core_running?
        tree.keep_for_later!
      end

      # graphics-audio applies a new frame-buffer size on the next boot: one
      # restart when it came up at the other family's (SimRun#boot_and_run).
      def sim_up(family)
        size = begin
          SimRun.up(family)
        rescue RuntimeError
          nil
        end
        if size != SimRun::SIZES[family]
          SimRun.down
          size = SimRun.up(family)
        end
        raise Failure, "the sim came up at #{size&.join('x') || '?'}" unless size == SimRun::SIZES[family]
        SimRun.wait_for(60, "the desktop's boot to finish") { SimRun.core_log.include?(SimRun::BOOT_DONE) }
        sleep 2
      end

      # ---- the two ends ----

      def ep_app(ep, *args) = app(*args, run_dir: ep.run_dir, port: ep.port)

      def ep_lines(ep)
        ep.kind == :sim ? SimRun.core_log.lines(chomp: true) : log_lines(ep.run_dir)
      end

      def ep_wait(ep, re, from, what, timeout: 15)
        deadline = now + timeout
        loop do
          lines = ep_lines(ep)
          i = (from...lines.size).find { |k| lines[k] =~ re }
          return i if i
          raise Failure, "no #{what} in #{ep.name}'s log within #{timeout}s" if now > deadline
          sleep(ep.kind == :sim ? 0.3 : 0.1)
        end
      end

      def ep_ps(ep) = ep_app(ep, "ps").fetch("apps")

      def ep_kill_named(ep, name)
        ep_ps(ep).each { |a| ep_app(ep, "kill", a["pid"].to_s) if a["name"] == name }
        sleep 0.3
      end

      def ep_input(ep, *cmds)
        if ep.kind == :sim
          out, st = Open3.capture2e(Gem.ruby, File.join(ROOT, "tools", "fmrb_input.rb"), *cmds, chdir: ROOT)
          raise Failure, "fmrb_input.rb: #{out.strip}" unless st.success?
        else
          native("input", *cmds)
        end
      end

      def ep_shot(ep)
        out = File.join(OUT, "shot_#{ep.kind}.png")
        if ep.kind == :sim
          o, st = Open3.capture2e("python3", File.join(ROOT, "tools", "fmrb_screenshot.py"), "--wait", "10", out, chdir: ROOT)
          raise Failure, "fmrb_screenshot.py: #{o.strip}" unless st.success?
        else
          native("shot", out)
        end
        Regress::Screens::Image.load(out)
      end

      # The screen once a region stays the same, the pointer away from it.
      def ep_settled_shot(ep, rect)
        ep_input(ep, "move", *POINTER_AWAY.map(&:to_s))
        prev = nil
        deadline = now + 6
        loop do
          sleep 0.3
          img = ep_shot(ep)
          return img if prev && rgb332_diff(prev, img, rect).empty?
          raise Failure, "the screen of #{ep.name} did not settle in 6 s" if now > deadline
          prev = img
        end
      end

      def rgb332(p) = [p[0] >> 5, p[1] >> 5, p[2] >> 6]

      # The pixels of rect that differ in RGB332, as [x, y] inside rect (the
      # two images may be of different sizes: each is read at its own place).
      def rgb332_diff(a, b, rect, corners: false)
        x0, y0, w, h = rect
        bad = []
        h.times do |y|
          w.times do |x|
            next if corners && (x < 3 || x >= w - 3) && (y < 3 || y >= h - 3)
            bad << [x, y] unless rgb332(a.pixel(x0 + x, y0 + y)) == rgb332(b.pixel(x0 + x, y0 + y))
          end
        end
        bad
      end

      # ---- the rounds ----

      def ep_start_counter(ep)
        from = ep_lines(ep).size
        pid = ep_app(ep, "spawn", APP).fetch("pid")
        ep_wait(ep, /ckpt_counter: b=3 /, from, "the counter's third step of b")
        pid
      end

      def written_line(lines, from, round)
        (from...lines.size).find { |k| lines[k] =~ /checkpoint written: / } or raise Failure, "#{round}: no 'checkpoint written'"
      end

      # F1 on the simulation: written and started again by its kernel.
      def sim_round_restart
        pid = ep_start_counter(@sim)
        mark = ep_lines(@sim).size
        r = ep_app(@sim, "checkpoint", pid.to_s, "--max", "8000", "--restart")
        raise Failure, "sim restart: answered #{r.inspect}" unless r["status"] == "written" && r["path"] == IMAGE
        restored = ep_wait(@sim, /ckpt_counter: on_restore /, mark, "on_restore after the restart")
        ep_wait(@sim, /ckpt_counter: b=\d+ /, restored + 1, "a step of b after the restore")
        ep_wait(@sim, /ckpt_counter: a=\d+ /, restored + 1, "a step of a after the restore")
        l = ep_lines(@sim)
        w = written_line(l, mark, "sim restart")
        c = continuity_of(l[0...w], l[(restored + 1)..], l[w..restored], l[restored], "sim restart")
        ep_kill_named(@sim, "Ckpt Counter")
        "sim restart: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}, t step #{c[:t_step]} ms"
      end

      # F2's hold on the simulation: held, nobody answers, it goes on.
      def sim_round_hold
        pid = ep_start_counter(@sim)
        mark = ep_lines(@sim).size
        r = ep_app(@sim, "checkpoint", pid.to_s, "--hold", HOLD_NO_ANSWER_MS.to_s, "--max", "8000")
        raise Failure, "sim hold: answered #{r.inspect}" unless r["status"] == "held"
        on = ep_wait(@sim, /ckpt: \[Ckpt Counter\] goes on from its image/, mark, "the held app going on", timeout: 10)
        ep_wait(@sim, /ckpt_counter: main=\d+/, on + 1, "a step of main after going on")
        l = ep_lines(@sim)
        w = written_line(l, mark, "sim hold")
        held = l[(w + 1)...on].grep(/ckpt_counter: (a|b|main)=/)
        raise Failure, "sim hold: counted while held (#{held.first})" unless held.empty?
        re = /ckpt_counter: main=(\d+)/
        n0 = l[0...w].reverse.find { |x| x =~ re }.to_s[re, 1].to_i
        n1 = l[(on + 1)..].find { |x| x =~ re }.to_s[re, 1].to_i
        raise Failure, "sim hold: main went from #{n0} to #{n1}" unless n1 == n0 + 1
        ms = l.find { |x| x =~ /held (\d+) ms after its image; goes on/ }.to_s[/held (\d+) ms/, 1]
        ep_kill_named(@sim, "Ckpt Counter")
        "sim hold: held #{ms} ms, main #{n0}->#{n1}"
      end

      def ep_poke_window(ep, x, y, key)
        wx, wy = WINDOW_RECT
        from = ep_lines(ep).size
        ep_input(ep, "click", (wx + x).to_s, (wy + y).to_s, "sleep", "200", "text", key)
        ep_wait(ep, /ckpt_window: click #{x},#{y} dots=\d+/, from, "the click at #{x},#{y}")
        ep_wait(ep, /ckpt_window: key #{key} text=/, from, "the key #{key}")
      end

      def ep_start_window(ep)
        from = ep_lines(ep).size
        pid = ep_app(ep, "spawn", WINDOWED).fetch("pid")
        ep_wait(ep, /ckpt_window: task=2 /, from, "ckpt_window's Task")
        ep_poke_window(ep, 40, 50, "a")
        ep_poke_window(ep, 90, 66, "b")
        pid
      end

      def window_same!(before, after, round, corners: false)
        bad = rgb332_diff(before, after, WINDOW_RECT, corners: corners)
        return if bad.empty?
        after.write(File.join(OUT, "#{round.tr(' ', '_')}.png"))
        raise Failure, "#{round}: the window differs in #{bad.size} px (first #{bad.first(3).inspect})"
      end

      # F3 on the simulation.
      def sim_round_window_restart
        pid = ep_start_window(@sim)
        before = ep_settled_shot(@sim, WINDOW_RECT)
        mark = ep_lines(@sim).size
        r = ep_app(@sim, "checkpoint", pid.to_s, "--max", "8000", "--restart")
        raise Failure, "sim window: answered #{r.inspect}" unless r["status"] == "written"
        restored = ep_wait(@sim, /ckpt_window: on_restore dots=2 text=ab/, mark, "the window's on_restore")
        ep_wait(@sim, /ckpt_window: task=\d+ /, restored + 1, "a Task step after the restore")
        window_same!(before, ep_settled_shot(@sim, WINDOW_RECT), "sim window")
        ep_kill_named(@sim, "Ckpt Window")
        "sim window: same"
      end

      def move_between(from, to, pid)
        dest = to.kind == :sim ? to.port.to_s : to.run_dir
        JSON.parse(native("app", "move", pid.to_s, "--to", dest, run_dir: from.run_dir, port: from.port, timeout: 60)
                     .lines.last.to_s)
      end

      # The counter from one end to the other, every counter going on.
      def counter_move(from, to, round)
        pid = ep_start_counter(from)
        mark = ep_lines(from).size
        mark_to = ep_lines(to).size
        r = move_between(from, to, pid)
        raise Failure, "#{round}: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        restored = ep_wait(to, /ckpt_counter: on_restore /, mark_to, "on_restore")
        ep_wait(to, /ckpt_counter: b=\d+ /, restored + 1, "a step of b")
        ep_wait(to, /ckpt_counter: a=\d+ /, restored + 1, "a step of a")
        ep_wait(from, /\[Ckpt Counter gen=\d+\] Task exiting normally/, mark, "the copy closing where it was")
        a = ep_lines(from)
        b = ep_lines(to)
        w = written_line(a, mark, round)
        stray = a[(w + 1)..].grep(/ckpt_counter: (a|b|main)=/)
        raise Failure, "#{round}: counted on after its image (#{stray.first})" unless stray.empty?
        c = continuity_of(a[0...w], b[(restored + 1)..], a[w..], b[restored], round)
        tick_a = a[w..].find { |l| l =~ /image at task tick (\d+)/ }.to_s[/tick (\d+)/, 1]
        tick_b = b[mark_to..].find { |l| l =~ /restored \S+ .* task tick (\d+)/ }.to_s[/task tick (\d+)/, 1]
        raise Failure, "#{round}: task tick #{tick_a.inspect} written, #{tick_b.inspect} restored" unless tick_a && tick_a == tick_b
        names = ->(ls, re) { ls.reverse.find { |l| l =~ re }.to_s[/\((\d+) names/, 1] }
        n_a = names.call(a, /\[Ckpt Counter\] VM set up for checkpoints: /)
        n_b = names.call(b[mark_to..], /restoring an image\)/)
        ep_kill_named(to, "Ckpt Counter")
        "#{round}: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}, t step #{c[:t_step]} ms, tick #{tick_a}, " \
          "names #{n_a} there #{n_b}, carried in #{r['carried_ms'] - r['held_ms']} ms"
      end

      def sim_round_to_native = counter_move(@sim, @nat, "to native")
      def sim_round_to_sim = counter_move(@nat, @sim, "to sim")

      # The window from the simulation to the native program.
      def sim_round_window
        pid = ep_start_window(@sim)
        before = ep_settled_shot(@sim, WINDOW_RECT)
        mark_to = ep_lines(@nat).size
        r = move_between(@sim, @nat, pid)
        raise Failure, "window: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        restored = ep_wait(@nat, /ckpt_window: on_restore dots=2 text=ab/, mark_to, "on_restore")
        ep_wait(@nat, /ckpt_window: task=\d+ /, restored + 1, "a Task step")
        window_same!(before, ep_settled_shot(@nat, WINDOW_RECT), "window", corners: true)
        ep_poke_window(@nat, 60, 60, "c")
        ep_kill_named(@nat, "Ckpt Window")
        "window: same in RGB332, click and key there"
      end

      # The size a window of rect gets on a screen of sw x sh (the spawner's
      # fit for an image): [x, y, w, h].
      def fitted(rect, sw, sh)
        x, y, w, h = rect
        w = [w, sw].min
        h = [h, sh].min
        x = [[x, sw - w].min, 0].max
        y = [[y, sh - h].min, 0].max
        [x, y, w, h]
      end

      # The four red squares ckpt_wide / ckpt_full draw inside the corners of
      # the user area (x0, y0, w, h in screen coordinates).
      def corners_red!(img, area, round)
        x0, y0, w, h = area
        [[x0 + 2, y0 + 2], [x0 + w - 4, y0 + 2], [x0 + 2, y0 + h - 4], [x0 + w - 4, y0 + h - 4]].each do |x, y|
          next if rgb332(img.pixel(x, y)) == [7, 0, 0]
          img.write(File.join(OUT, "#{round}.png"))
          raise Failure, "#{round}: no red square at #{x},#{y} (#{img.pixel(x, y).inspect})"
        end
      end

      def sized_move(from, to, path, tag, name, rect, round)
        mark = ep_lines(from).size
        pid = ep_app(from, "spawn", path).fetch("pid")
        ep_wait(from, /#{tag}: task=2 /, mark, "#{tag}'s Task")
        mark_to = ep_lines(to).size
        r = move_between(from, to, pid)
        raise Failure, "#{round}: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"]
        i = ep_wait(to, /#{tag}: on_restore /, mark_to, "on_restore")
        ep_wait(to, /#{tag}: task=\d+ /, i + 1, "a Task step")
        lines = ep_lines(to)
        got = lines[i]
        sw, sh = to.kind == :sim ? SimRun::SIZES[@family] : NATIVE_SCREEN
        x, y, w, h = rect ? fitted(rect, sw, sh) : [0, 0, sw, sh]
        want = rect ? "window=#{w}x#{h} user=1,#{TITLE_BAR_H},#{w - 2}x#{h - TITLE_BAR_H - 1} at #{x},#{y}" :
                      "window=#{w}x#{h} user=0,0,#{w}x#{h} at 0,0"
        raise Failure, "#{round}: on_restore said #{got[/on_restore (.*)/, 1]}, #{want} expected" unless got.include?(want)
        resized = lines[mark_to...i].grep(/#{tag}: on_resize (\d+x\d+)/)
        before = ep_lines(from)[mark..].find { |l| l =~ /#{tag}: drawn window=(\d+)x(\d+)/ }
        ow, oh = before.to_s.scan(/window=(\d+)x(\d+)/).first.to_a.map(&:to_i)
        if [ow, oh] == [w, h]
          raise Failure, "#{round}: on_resize at the same size (#{resized.first})" unless resized.empty?
        elsif resized.size != 1 || !resized[0].include?("on_resize #{w}x#{h}")
          raise Failure, "#{round}: on_resize #{w}x#{h} expected once, got #{resized.inspect}"
        end
        area = rect ? [x + 1, y + TITLE_BAR_H, w - 2, h - TITLE_BAR_H - 1] : [0, 0, w, h]
        corners_red!(ep_settled_shot(to, [x, y, w, h]), area, round)
        ep_kill_named(to, name)
        "#{round}: #{ow}x#{oh} -> #{w}x#{h}#{resized.empty? ? '' : ' (on_resize)'}"
      end

      def sim_round_wide = sized_move(@nat, @sim, WIDE, "ckpt_wide", "Ckpt Wide", WIDE_RECT, "wide")
      def sim_round_full = sized_move(@sim, @nat, FULL, "ckpt_full", "Ckpt Full", nil, "full")

      def ep_start_calls(ep, path, tag)
        from = ep_lines(ep).size
        pid = ep_app(ep, "spawn", path).fetch("pid")
        ep_wait(ep, /#{tag}: main=3 /, from, "#{tag} counting")
        pid
      end

      # A call this build lacks, not declared: it moves with a warning.
      def sim_round_calls
        pid = ep_start_calls(@nat, CALLS, "ckpt_calls")
        mark_to = ep_lines(@sim).size
        r = move_between(@nat, @sim, pid)
        unless r["status"] == "moved" && r["pid"] && r["missing"] == ["linux_settings"]
          raise Failure, "calls: answered #{r.inspect}"
        end
        i = ep_wait(@sim, /ckpt_calls: on_restore main=\d+/, mark_to, "on_restore")
        machine = machine_values!(ep_lines(@sim)[i], "calls")
        ep_wait(@sim, /ckpt_calls: main=\d+ pack=A linux_settings=false/, i + 1, "a step there, without linux_settings")
        ep_wait(@sim, /\[Ckpt Calls\] the program calls 1 method\(s\) this build does not have: linux_settings /, mark_to,
                "the warning")
        ep_kill_named(@sim, "Ckpt Calls")
        "calls: moved, missing #{r['missing'].join(',')}, #{machine}"
      end

      # A method its .app.toml requires and this build lacks: refused there,
      # it goes on here.
      def sim_round_requires
        pid = ep_start_calls(@nat, REQUIRES, "ckpt_requires")
        mark = ep_lines(@nat).size
        r = move_between(@nat, @sim, pid)
        unless r["status"] == "stayed" && r["here"] == "continued" && r["reason"].to_s.include?("requires the method 'linux_settings'")
          raise Failure, "requires: answered #{r.inspect}"
        end
        on = ep_wait(@nat, /ckpt: \[Ckpt Requires\] goes on from its image/, mark, "the app going on")
        ep_wait(@nat, /ckpt_requires: main=\d+ /, on + 1, "a step after going on")
        raise Failure, "requires: the simulation runs it" if ep_ps(@sim).any? { |a| a["name"] == "Ckpt Requires" }
        ep_kill_named(@nat, "Ckpt Requires")
        "requires: refused there (#{r['reason'][0, 50]}...), went on here"
      end

      # The font ckpt_window's log line says the machine picked ("ja,12").
      def font_on(line, want, round)
        got = line.to_s[/ font=(\S+)/, 1]
        raise Failure, "#{round}: font #{got.inspect} where #{want} was expected (#{line})" unless got == want
        got
      end

      # Through a machine without the efontJA fonts and back (report/font_pct.md).
      def sim_round_font
        from = ep_lines(@nat).size
        pid = ep_app(@nat, "spawn", WINDOWED).fetch("pid")
        drawn = ep_wait(@nat, /ckpt_window: drawn /, from, "ckpt_window drawn")
        ep_wait(@nat, /ckpt_window: task=2 /, from, "ckpt_window's Task")
        font_on(ep_lines(@nat)[drawn], "ja,12", "font here")
        before = ep_settled_shot(@nat, WINDOW_RECT)
        mark_sim = ep_lines(@sim).size
        r = move_between(@nat, @sim, pid)
        raise Failure, "font: answered #{r.inspect} moving there" unless r["status"] == "moved" && r["pid"]
        there = ep_wait(@sim, /ckpt_window: on_restore /, mark_sim, "on_restore there")
        ep_wait(@sim, /ckpt_window: task=\d+ /, there + 1, "a Task step there")
        font_on(ep_lines(@sim)[there], "ja,8", "font there")
        mark_nat = ep_lines(@nat).size
        r2 = move_between(@sim, @nat, r["pid"])
        raise Failure, "font: answered #{r2.inspect} moving back" unless r2["status"] == "moved" && r2["pid"]
        back = ep_wait(@nat, /ckpt_window: on_restore /, mark_nat, "on_restore back here")
        ep_wait(@nat, /ckpt_window: task=\d+ /, back + 1, "a Task step back here")
        font_on(ep_lines(@nat)[back], "ja,12", "font back")
        window_same!(before, ep_settled_shot(@nat, WINDOW_RECT), "font back")
        ep_kill_named(@nat, "Ckpt Window")
        "font: ja,12 here, ja,8 there, ja,12 back, the window the same"
      end

      def sim_no_forced_kill
        [@sim, @nat].each do |ep|
          forced = ep_lines(ep).grep(/No response in \d+ms, forcing termination/)
          raise Failure, "forced kill in #{ep.name}: #{forced.first}" unless forced.empty?
        end
      end
    end
  end
end
