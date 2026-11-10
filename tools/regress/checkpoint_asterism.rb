# frozen_string_literal: true
#
# checkpoint_asterism.rb - moving an app between the two native programs over
# Asterism (rake regress:checkpoint, doc/checkpoint F5, doc/regression/usage.md
# chapter 12). Part of checkpoint.rb's run: its Runner includes this, and the
# rounds run after the moves through the debug server (F2), on the same two
# programs.
#
# A Zenoh router of the run's own (the eclipse/zenoh image the sim's zenohd
# uses, in a container of its own on loopback ports that the sim's does not
# use) is started for the run and taken down after it. Each program gets the
# settings a machine would have: /home/asterism/config.toml (that router and
# a node ID, rg-ckpt-a / rg-ckpt-b) and /home/asterism/receive.toml (each
# takes images from the other only), so the kernel starts its transfer app at
# boot. Then, asked as a tool asks (fmrb_native.rb app move PID --node NODE,
# the debug server's "move"):
#
#   asterism move      the counter to rg-ckpt-b: asked first (the method names
#                      it calls), written and held, carried in pieces over
#                      Asterism, started there and closed here once it is up
#                      there. Every counter goes on from the next step there,
#                      the first program counts nothing after the image.
#   asterism requires  ckpt_requires_absent requires a method no build has:
#                      rg-ckpt-b refuses it when asked, before it is stopped
#                      (no image is written), and it counts on here.
#   asterism not allowed  the counter to this program's own node: its
#                      receive.toml takes no checkpoint from itself, refused before
#                      it is stopped.
#   asterism no answer the counter to a node nobody is: refused before it is
#                      stopped.
#   asterism late      the counter with a hold of 1 ms: the hold runs out
#                      while the image is on its way, the app goes on here
#                      from where it was held, and there is no copy there
#                      (D15: one copy): either the image is given up on its
#                      way (the second program drops what it holds), or,
#                      committed there already, the copy that comes up is
#                      stopped there.
#   asterism given up  the same, made to happen on the way: this process
#                      (CRuby Asterism, node rg-ckpt-pc) begins a file at
#                      rg-ckpt-b and sends no piece, so rg-ckpt-b answers the
#                      image's begin busy for 10 s (STALE_MS); the hold
#                      (1.5 s) runs out meanwhile, the app goes on here, and
#                      once rg-ckpt-b takes the image's begin the transfer
#                      app cancels it (move_stop): rg-ckpt-b drops it at 0
#                      bytes and never starts a copy.
#
# The warning (a method the program calls that the build there lacks) needs
# a build that differs: with the simulation's rounds (--sim, rake SIM=1) the
# native program also moves to the simulation over Asterism (the sim's own
# zenohd, which its stack brings up; the simulation's /home/asterism is the
# working tree's flash/home/asterism, written for the run and put back as it
# was after it):
#
#   asterism calls     ckpt_calls (it calls FmrbApp.linux_settings, guarded):
#                      asked first, the simulation names linux_settings as
#                      missing, it moves all the same and the simulation logs
#                      the warning.
#   asterism requires  ckpt_requires (the same name in requires): the
#                      simulation refuses it when asked, before it stops.
require "digest"
require "fileutils"
require "json"
require "open3"
require "socket"
require_relative "tree"

module Regress
  module Checkpoint
    # The router and the settings of the two programs.
    module AsterismRounds
      ROUTER_IMAGE = "eclipse/zenoh:1.10.1"
      # this tree's own (tree.rb): the shared checkout's are
      # fmrb_regress_ckpt_zenohd on 17457 (ws 17459), another tree's carry its
      # ID and take ports from its block
      ROUTER_NAME = Tree.name("fmrb_regress_ckpt_zenohd")
      ROUTER_PORT = Tree.port(17_457, :ckpt_router)
      ROUTER_WS_PORT = Tree.port(17_459, :ckpt_router_ws)
      NODE_A = "rg-ckpt-a"
      NODE_B = "rg-ckpt-b"
      NODE_NOBODY = "rg-ckpt-nobody"
      # this process, when it keeps rg-ckpt-b's inbox busy (asterism given up)
      NODE_PC = "rg-ckpt-pc"
      REQUIRES_ABSENT = "/app/test/ckpt_requires_absent.app.rb"

      def router_up
        router_down
        [ROUTER_PORT, ROUTER_WS_PORT].each do |p|
          Tree.need_free_port!(p, "the Zenoh router #{ROUTER_NAME}")
        rescue Tree::Busy => e
          raise Failure, e.message
        end
        cmd = ["docker", "run", "-d", "--rm", "--name", ROUTER_NAME,
               "-p", "127.0.0.1:#{ROUTER_PORT}:7447", "-p", "127.0.0.1:#{ROUTER_WS_PORT}:7449",
               ROUTER_IMAGE, "--no-multicast-scouting", "--listen", "tcp/0.0.0.0:7447", "--listen", "ws/0.0.0.0:7449"]
        out, st = Open3.capture2e(*cmd)
        raise Failure, "the Zenoh router did not start: #{out.strip.lines.last}" unless st.success?
        deadline = now + 20
        until router_listening?(ROUTER_PORT)
          raise Failure, "the Zenoh router is not listening on #{ROUTER_PORT}" if now > deadline
          sleep 0.2
        end
      end

      def router_down
        if @pc_on
          Asterism.close if Asterism.connected?
          @pc_on = false
        end
        Open3.capture2e("docker", "rm", "-f", ROUTER_NAME)
        nil
      end

      def router_listening?(port)
        Socket.tcp("127.0.0.1", port, connect_timeout: 0.5).close
        true
      rescue StandardError
        false
      end

      # A program's /home/asterism: the router, its node ID, and the one node
      # it takes images from.
      def asterism_home(user_dir, node, from, router: "tcp/127.0.0.1:#{ROUTER_PORT}")
        write_asterism(File.join(user_dir, "home", "asterism"), node, from, router)
      end

      def write_asterism(dir, node, from, router)
        FileUtils.mkdir_p(dir)
        File.write(File.join(dir, "config.toml"), "router = \"#{router}\"\nnode = \"#{node}\"\n")
        File.write(File.join(dir, "receive.toml"), "enabled = true\nfrom = [\"#{from}\"]\n")
      end

      # This process on the run's router (CRuby Asterism, the gem of this
      # tree: ASTERISM_DIR, else vendor/asterism), once for the run.
      def pc_up
        return if @pc_on
        require_relative "../fmrb_send"
        FmrbSend.load_asterism
        Asterism.connect("tcp/127.0.0.1:#{ROUTER_PORT}", node: NODE_PC, app: "probe")
        @pc_on = true
      end

      # ---- with the simulation (--sim) ----

      NODE_SIM = "rg-ckpt-sim"
      # the sim stack's zenohd: tcp/zenohd:7447 inside, 127.0.0.1:7447 here
      SIM_ROUTER_INSIDE = "tcp/zenohd:7447"
      SIM_ROUTER_HERE = "tcp/127.0.0.1:7447"
      SIM_ASTERISM_DIR = File.join(CORE, "flash", "home", "asterism")
      SIM_ASTERISM_FILES = %w[config.toml receive.toml].freeze

      # The simulation's /home/asterism for the run (what was there is kept).
      def sim_asterism_setup
        @sim_asterism_dir_made = !Dir.exist?(SIM_ASTERISM_DIR)
        @sim_asterism_kept = {}
        SIM_ASTERISM_FILES.each do |f|
          path = File.join(SIM_ASTERISM_DIR, f)
          @sim_asterism_kept[f] = File.binread(path) if File.exist?(path)
        end
        write_asterism(SIM_ASTERISM_DIR, NODE_SIM, NODE_A, SIM_ROUTER_INSIDE)
      end

      def sim_asterism_teardown
        return unless @sim_asterism_kept
        SIM_ASTERISM_FILES.each do |f|
          path = File.join(SIM_ASTERISM_DIR, f)
          if @sim_asterism_kept.key?(f)
            File.binwrite(path, @sim_asterism_kept[f])
          else
            FileUtils.rm_f(path)
          end
        end
        Dir.rmdir(SIM_ASTERISM_DIR) if @sim_asterism_dir_made && Dir.exist?(SIM_ASTERISM_DIR) && Dir.empty?(SIM_ASTERISM_DIR)
        @sim_asterism_kept = nil
      end

      def sim_asterism_rounds
        ep_wait(@sim, /Transfer app connected as #{NODE_SIM}/, 0, "the simulation's transfer app connected", timeout: 30)
        ep_wait(@nat, /Transfer app connected as #{NODE_A}/, 0, "the native program's transfer app connected", timeout: 30)
        [sim_round_asterism_calls, sim_round_asterism_requires]
      end

      def sim_round_asterism_calls
        pid = ep_start_calls(@nat, CALLS, "ckpt_calls")
        mark_to = ep_lines(@sim).size
        r = asterism_move(pid, NODE_SIM)
        unless r["status"] == "moved" && r["pid"].to_i.positive? && r["missing"] == ["linux_settings"]
          raise Failure, "asterism calls: answered #{r.inspect}"
        end
        i = ep_wait(@sim, /ckpt_calls: on_restore main=\d+/, mark_to, "on_restore")
        machine_values!(ep_lines(@sim)[i], "asterism calls")
        ep_wait(@sim, /ckpt_calls: main=\d+ pack=A linux_settings=false/, i + 1, "a step there, without linux_settings")
        ep_wait(@sim, /\[Ckpt Calls\] the program calls 1 method\(s\) this build does not have: linux_settings /, mark_to,
                "the warning")
        ep_kill_named(@sim, "Ckpt Calls")
        "asterism calls: moved, missing #{r['missing'].join(',')} (#{r['took_ms']} ms)"
      end

      def sim_round_asterism_requires
        pid = ep_start_calls(@nat, REQUIRES, "ckpt_requires")
        mark = ep_lines(@nat).size
        r = asterism_move(pid, NODE_SIM)
        unless r["status"] == "stayed" && r["here"] == "not stopped" && r["reason"].to_s.include?("linux_settings")
          raise Failure, "asterism requires: answered #{r.inspect}"
        end
        sleep 0.6
        lines = ep_lines(@nat)[mark..]
        raise Failure, "asterism requires: an image was written" unless lines.grep(/checkpoint written: /).empty?
        raise Failure, "asterism requires: it did not count on" if lines.grep(/ckpt_requires: main=\d+ /).empty?
        ep_kill_named(@nat, "Ckpt Requires")
        "asterism requires: refused before it stopped"
      end

      def asterism_move(pid, node, *opts)
        JSON.parse(native("app", "move", pid.to_s, "--node", node, *opts, timeout: 60).lines.last.to_s)
      end

      def asterism_rounds
        wait_line(/Transfer app connected as #{NODE_A}/, 0, "the first program's transfer app connected", timeout: 30)
        wait_line(/Transfer app connected as #{NODE_B}/, 0, "the second program's transfer app connected",
                  timeout: 30, run_dir: RUN_DIR_B)
        [round_asterism_move, round_asterism_shell, round_asterism_requires, round_asterism_not_allowed,
         round_asterism_no_answer, round_asterism_late, round_asterism_given_up]
      end

      def round_asterism_move
        pid = start_counter
        mark = log_lines.size
        mark_b = log_lines(RUN_DIR_B).size
        r = asterism_move(pid, NODE_B)
        raise Failure, "asterism move: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"].to_i.positive?
        raise Failure, "asterism move: the same build lacks #{r['missing'].inspect}" if r["missing"]
        restored = wait_line(/ckpt_counter: on_restore /, mark_b, "on_restore in the second program", run_dir: RUN_DIR_B)
        wait_line(/ckpt_counter: b=\d+ /, restored + 1, "a step of b in the second program", run_dir: RUN_DIR_B)
        wait_line(/ckpt_counter: a=\d+ /, restored + 1, "a step of a in the second program", run_dir: RUN_DIR_B)
        wait_line(/\[Ckpt Counter gen=\d+\] Task exiting normally/, mark, "the first program's copy closing")
        sleep 0.5
        a = log_lines
        b = log_lines(RUN_DIR_B)
        raise Failure, "asterism move: the first program still runs the counter" if ps.any? { |x| x["name"] == "Ckpt Counter" }
        w = (mark...a.size).find { |k| a[k] =~ /checkpoint written: / } or raise Failure, "asterism move: no 'checkpoint written'"
        stray = a[(w + 1)..].grep(/ckpt_counter: (a|b|main)=/)
        raise Failure, "asterism move: the first program counted on after its image (#{stray.first})" unless stray.empty?
        r_i = (mark_b...b.size).find { |k| b[k] =~ /ckpt_counter: on_restore / }
        c = continuity_of(a[0...w], b[(r_i + 1)..], a[w..], b[r_i], "asterism move")
        after = a[mark..]
        carried = after.find { |l| l =~ /transfer: .* carried in (\d+) ms/ }.to_s[/carried in (\d+) ms/, 1]
        held = after.find { |l| l =~ /held (\d+) ms after its image; closes/ }.to_s[/held (\d+) ms/, 1]
        took = after.find { |l| l =~ /Move: PID \d+ moved to .* \((\d+) ms\)/ }.to_s[/\((\d+) ms\)\z/, 1]
        raise Failure, "asterism move: no timings in the log (#{carried.inspect}, #{held.inspect}, #{took.inspect})" unless carried && held && took
        app_b("kill", r["pid"].to_s)
        sleep 0.3
        "asterism move: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}, t step #{c[:t_step]} ms, " \
          "carried in #{carried} ms, held #{held} ms, the move #{took} ms"
      end

      # The shell's `amove PID NODE` (FmrbApp.move): the same move, and the
      # shell is answered with the PID there.
      def round_asterism_shell
        pid = start_counter
        shell = app("spawn", SHELL).fetch("pid")
        sleep 1.5
        mark_b = log_lines(RUN_DIR_B).size
        mark = log_lines.size
        native("input", "text", "amove #{pid} #{NODE_B}", "key", "enter")
        i = wait_line(/amove: PID #{pid} moved to #{NODE_B} \(PID (\d+) there\)/, mark, "the shell's answer")
        there = log_lines[i][/\(PID (\d+) there\)/, 1].to_i
        restored = wait_line(/ckpt_counter: on_restore /, mark_b, "on_restore in the second program", run_dir: RUN_DIR_B)
        wait_line(/ckpt_counter: a=\d+ /, restored + 1, "a step of a in the second program", run_dir: RUN_DIR_B)
        raise Failure, "asterism shell: the first program still runs the counter" if ps.any? { |x| x["name"] == "Ckpt Counter" }
        app_b("kill", there.to_s)
        app("kill", shell.to_s)
        sleep 0.3
        "shell: moved, PID #{there} there"
      end

      # Refused when asked, before anything is stopped: no image, it counts on.
      def not_stopped(pid, mark, round, tag)
        sleep 0.6
        lines = log_lines[mark..]
        written = lines.grep(/checkpoint written: /)
        raise Failure, "#{round}: an image was written (#{written.first})" unless written.empty?
        raise Failure, "#{round}: the app is gone" unless ps.any? { |x| x["pid"] == pid }
        n = lines.grep(/#{tag}: main=\d+/).size
        raise Failure, "#{round}: the app did not count on" if n.zero?
        n
      end

      def round_asterism_requires
        from = log_lines.size
        pid = app("spawn", REQUIRES_ABSENT).fetch("pid")
        wait_line(/ckpt_requires_absent: main=2\b/, from, "ckpt_requires_absent counting")
        mark = log_lines.size
        mark_b = log_lines(RUN_DIR_B).size
        r = asterism_move(pid, NODE_B)
        unless r["status"] == "stayed" && r["here"] == "not stopped" && r["reason"].to_s.include?("fmrb_no_such_method")
          raise Failure, "asterism requires: answered #{r.inspect}"
        end
        not_stopped(pid, mark, "asterism requires", "ckpt_requires_absent")
        sent = log_lines(RUN_DIR_B)[mark_b..].grep(/transfer: \S+ sends /)
        raise Failure, "asterism requires: an image went to the second program (#{sent.first})" unless sent.empty?
        app("kill", pid.to_s)
        sleep 0.3
        "asterism requires: refused before it stopped (#{r['reason'][0, 50]}...)"
      end

      def round_asterism_not_allowed
        pid = start_counter
        mark = log_lines.size
        r = asterism_move(pid, NODE_A)
        unless r["status"] == "stayed" && r["here"] == "not stopped" && r["reason"].to_s.include?("takes no checkpoint from #{NODE_A}")
          raise Failure, "asterism not allowed: answered #{r.inspect}"
        end
        not_stopped(pid, mark, "asterism not allowed", "ckpt_counter")
        kill_counter
        "not allowed: #{r['reason'][0, 50]}"
      end

      def round_asterism_no_answer
        pid = start_counter
        mark = log_lines.size
        r = asterism_move(pid, NODE_NOBODY)
        unless r["status"] == "stayed" && r["here"] == "not stopped" && r["reason"].to_s.include?(NODE_NOBODY)
          raise Failure, "asterism no answer: answered #{r.inspect}"
        end
        not_stopped(pid, mark, "asterism no answer", "ckpt_counter")
        kill_counter
        "no answer: in #{r['took_ms']} ms, not stopped"
      end

      # The hold runs out while the image is on its way: it goes on here, and
      # the copy that comes up there is stopped.
      def round_asterism_late
        pid = start_counter
        mark = log_lines.size
        mark_b = log_lines(RUN_DIR_B).size
        r = asterism_move(pid, NODE_B, "--hold", "1")
        unless r["status"] == "stayed" && r["here"] == "went on"
          raise Failure, "asterism late: answered #{r.inspect}"
        end
        c = goes_on(mark, "asterism late")
        i = wait_line(LATE_THERE, mark_b, "the image dropped there or the copy there stopped",
                      run_dir: RUN_DIR_B, timeout: 15)
        how = log_lines(RUN_DIR_B)[i].include?("dropped at") ? "the image dropped on its way" : "the copy there stopped"
        deadline = now + 5
        sleep 0.2 while app_b("ps").fetch("apps").any? { |x| x["name"] == "Ckpt Counter" } && now < deadline
        raise Failure, "asterism late: the second program still runs the copy" if app_b("ps").fetch("apps").any? { |x| x["name"] == "Ckpt Counter" }
        raise Failure, "asterism late: the first program's counter is gone" unless ps.any? { |x| x["pid"] == pid }
        kill_counter
        "late: went on here (a #{c[:a]}, main #{c[:main]}), #{how}"
      end

      # The hold runs out while the image waits for the receiver (busy with
      # another node's file): the transfer app gives it up once the receiver
      # takes its begin, the receiver drops it, no copy comes up there.
      def round_asterism_given_up
        pc_up
        # the second program's receive.toml (up_b), read on every call there
        recv = File.join(@home_b, "user", "home", "asterism", "receive.toml")
        kept = File.read(recv)
        File.write(recv, "#{kept}\n[file]\nfrom = [\"#{NODE_PC}\"]\n")
        pid = start_counter
        mark = log_lines.size
        mark_b = log_lines(RUN_DIR_B).size
        # a file begun and never sent: rg-ckpt-b is busy with it for 10 s
        sha = Digest::SHA256.hexdigest("x" * 1024)
        Asterism["#{NODE_B}/transfer/inbox", timeout_ms: 5000]
          .asterism_call("begin", "file", { "name" => "occupy.bin", "size" => 1024, "sha256" => sha })
        r = asterism_move(pid, NODE_B, "--hold", "1500")
        unless r["status"] == "stayed" && r["here"] == "went on"
          raise Failure, "asterism given up: answered #{r.inspect}"
        end
        c = goes_on(mark, "asterism given up")
        i = wait_line(/transfer: \S+ to #{NODE_B}: given up at (\d+) of (\d+) bytes \(the hold ran out\)/, mark,
                      "the transfer app giving the image up", timeout: 25)
        at = log_lines[i][/given up at (\d+)/, 1]
        wait_line(/transfer: checkpoint \S+ from #{NODE_A} dropped at \d+ of \d+ bytes: cancelled by #{NODE_A}/, mark_b,
                  "the image dropped there", run_dir: RUN_DIR_B, timeout: 10)
        sleep 0.5
        started = log_lines(RUN_DIR_B)[mark_b..].grep(/ckpt_counter: on_restore /)
        raise Failure, "asterism given up: the second program started a copy (#{started.first})" unless started.empty?
        raise Failure, "asterism given up: the first program's counter is gone" unless ps.any? { |x| x["pid"] == pid }
        kill_counter
        "given up: went on here (a #{c[:a]}, main #{c[:main]}), the image given up at #{at} bytes and dropped there"
      ensure
        File.write(recv, kept) if recv && kept
      end

      # What the receiving side logs when a move's hold ran out on the way:
      # the image given up before it was whole (the inbox drops it), or a
      # copy committed already and stopped once it came up.
      LATE_THERE = /transfer: (checkpoint \S+ from #{NODE_A} dropped at \d+ of \d+ bytes: cancelled by|#{NODE_A} cancels \S+; stopping PID \d+)/
    end
  end
end
