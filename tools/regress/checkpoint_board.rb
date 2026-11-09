# frozen_string_literal: true
#
# checkpoint_board.rb - moving an app on a connected board and between it and
# the native program over Asterism (rake regress:checkpoint BOARD=naryav4,
# doc/checkpoint F6, doc/regression/usage.md chapter 12). Opt-in like
# regress:boot: only when the board is connected and may be used; neither the
# default run nor regress:full has it. Part of checkpoint.rb's run: its Runner
# includes this, and the rounds run once, after the native rounds, with the
# first configuration's native program.
#
# What the board needs:
#   - the firmware of this tree (rake build:esp32 and flash; app only is
#     enough when storage did not change) with the development remote control
#     (FMRB_DEV_REMOTE_CTL, the default): the rounds launch, list and kill
#     apps, move files and ask for a checkpoint or a move over HTTP
#     (/app/checkpoint, /app/move: the debug server of a board is BLE only)
#   - the fmrb MCP server's serial capture running (serial_start): the board's
#     log is read from its file (~/.fmrb_mcp/current.log, BOARD_LOG= another),
#     never from the port, so nothing here resets the board
#   - WiFi: the board's address from the log ("Connected, ip=") or BOARD_IP=,
#     and this machine's address on that network (HOST_IP=, else the source
#     address of the route to the board) for the router
#
# A router of the run's own listens on this machine's network address
# (ROUTER_PORT_BOARD, all interfaces), so that the board reaches it; the
# native program reaches it on loopback. The board's /home/asterism
# (config.toml, receive.toml) is put back as it was after the run, and the
# transfer app this run started there is stopped (the kernel takes it as its
# own when it connects: "Transfer app adopted"). The apps the rounds start on
# the board (BOARD_FILES) are put there from this tree when the board's copy
# is missing or differs, and put back after the run.
#
#   board self       /app/test/ckpt_self.app.rb asks for its own image
#                    (restart): written and started again on the board (F1's
#                    form), every counter goes on from the next step.
#   board in         the counter from the native program to the board
#                    (asked first, held, carried, started there, closed here).
#   board out        the same counter from the board back to the native
#                    program (/app/move on the board).
#   board window     ckpt_window to the board and back (F3): on_restore on
#                    each side and its Task going on. The pixels are not
#                    compared (the board's picture is on HDMI).
#   board calls      ckpt_calls to the board: linux_settings is named missing
#                    and it moves all the same; there platform is :esp32
#                    (report/platform_restore.md).
#   board requires   ckpt_requires to the board: refused when asked, before it
#                    stops.
#   board no answer  the counter on the board to a node nobody is: refused
#                    before it stops, it counts on there.
require "fileutils"
require "json"
require "net/http"
require "open3"
require "socket"
require "uri"
require_relative "targets"
require_relative "tree"

module Regress
  module Checkpoint
    module BoardRounds
      # this tree's own name (tree.rb); the port is the one below for every tree
      ROUTER_NAME_BOARD = Tree.name("fmrb_regress_ckpt_zenohd_board")
      # The port the board reaches the router on, on all interfaces: 7447,
      # Zenoh's own, which a machine that runs a router for its boards lets in
      # already (WSL2 mirrored: a Hyper-V firewall rule per port; any other
      # port is dropped before it gets here). BOARD_ROUTER_PORT= another.
      # The simulation's zenohd takes the same port: not with a sim running.
      ROUTER_PORT_BOARD = (ENV["BOARD_ROUTER_PORT"].to_s.empty? ? 7447 : Integer(ENV["BOARD_ROUTER_PORT"]))
      NODE_BOARD = "rg-ckpt-board"
      # the native program's node (a module's methods see only its own and
      # the enclosing constants, not those of the modules the Runner includes)
      NODE_A = AsterismRounds::NODE_A
      BOARD_ASTERISM = "/home/asterism"
      BOARD_ASTERISM_FILES = %w[config.toml receive.toml].freeze
      TRANSFER = "/usr/share/transfer/transfer.app.rb"
      SELF_APP = "/app/test/ckpt_self.app.rb"
      COUNTER_APP = "/app/test/ckpt_counter.app.rb"
      WINDOW_APP = "/app/test/ckpt_window.app.rb"
      CALLS_APP_B = "/app/test/ckpt_calls.app.rb"
      REQUIRES_APP_B = "/app/test/ckpt_requires.app.rb"
      # The files the rounds start on the board, from this tree's flash/: put
      # there for the run when the board's copy is missing or differs (an app
      # only flash leaves the storage as it was), and put back after it
      BOARD_FILES = [SELF_APP, COUNTER_APP, WINDOW_APP, CALLS_APP_B, REQUIRES_APP_B, TRANSFER]
                    .flat_map { |p| [p, p.sub(/\.rb\z/, ".toml")] }.freeze
      MCP_LOG = File.expand_path(File.join(ENV["FMRB_MCP_STATE_DIR"] || "~/.fmrb_mcp", "current.log"))

      def board_check!
        name = @opts[:board]
        @board = TARGETS.find { |t| t.firmware && (t.name.casecmp?(name) || t.firmware.casecmp?(name)) } or
          raise Failure, "board: BOARD=#{name}: no such board in tools/regress/targets.rb"
        @board_log = ENV["BOARD_LOG"].to_s.empty? ? MCP_LOG : ENV["BOARD_LOG"]
        raise Failure, "board: no serial capture log (#{@board_log}); start one (serial_start)" unless File.exist?(@board_log)
        @board_ip = ENV["BOARD_IP"].to_s.empty? ? board_ip_from_log : ENV["BOARD_IP"]
        raise Failure, "board: no 'Connected, ip=' in the board's log; give BOARD_IP=" unless @board_ip
        board_http(:get, "/app/list")
        @host_ip = ENV["HOST_IP"].to_s.empty? ? route_source(@board_ip) : ENV["HOST_IP"]
        raise Failure, "board: no address of this machine toward #{@board_ip}; give HOST_IP=" unless @host_ip
      end

      def run_board(config)
        t0 = now
        down_if_running
        board_router_up
        home = File.join(OUT, "board_native_home")
        FileUtils.rm_rf(home)
        FileUtils.mkdir_p(%w[user cache].map { |d| File.join(home, d) })
        asterism_home(File.join(home, "user"), NODE_A, NODE_BOARD, router: "tcp/127.0.0.1:#{ROUTER_PORT_BOARD}")
        native("up", "--engine", config, "--machine", "sdl", "--user-dir", File.join(home, "user"),
               "--cache-dir", File.join(home, "cache"), "--timeout", "90", timeout: 120)
        put = board_files_setup
        board_asterism_setup
        wait_line(/Transfer app connected as #{NODE_A}/, 0, "the native program's transfer app connected", timeout: 30)
        notes = [board_round_self, board_round_in_out, board_round_window, board_round_calls,
                 board_round_requires, board_round_no_answer]
        put_note = put.empty? ? "" : "#{put.size} file(s) put there for the run; "
        "board #{@board.name} (#{config}): ok (#{put_note}#{notes.join('; ')}; #{(now - t0).round}s)"
      rescue Failure => e
        "board #{@board&.name} (#{config}): FAILED #{e.message}"
      ensure
        FileUtils.cp(log_path, File.join(OUT, "board_native.log")) if File.exist?(log_path)
        board_save_log
        board_asterism_teardown
        board_files_teardown
        down_if_running
        board_router_down
      end

      private

      # ---- the board ----

      def board_lines
        File.binread(@board_log).force_encoding("UTF-8").scrub.lines(chomp: true)
            .map { |l| l.delete("\r").gsub(/\e\[[0-9;]*m/, "") }
      end

      def board_wait(re, from, what, timeout: 20)
        deadline = now + timeout
        loop do
          lines = board_lines
          i = (from...lines.size).find { |k| lines[k] =~ re }
          return i if i
          raise Failure, "no #{what} in the board's log within #{timeout}s" if now > deadline
          sleep 0.2
        end
      end

      def board_save_log
        return unless @board_mark0
        File.write(File.join(OUT, "board.log"), board_lines[@board_mark0..].to_a.join("\n") + "\n")
      rescue StandardError
        nil
      end

      def board_ip_from_log
        board_lines.reverse_each { |l| return Regexp.last_match(1) if l =~ /Connected, ip=(\d+\.\d+\.\d+\.\d+)/ }
        nil
      end

      def route_source(ip)
        out, st = Open3.capture2("ip", "-4", "route", "get", ip)
        st.success? ? out[/\bsrc (\d+\.\d+\.\d+\.\d+)/, 1] : nil
      end

      # The development remote control: JSON answers; raises unless ok.
      def board_http(method, path, query = {}, body: nil, timeout: 15)
        uri = URI("http://#{@board_ip}#{path}")
        uri.query = board_query(query) unless query.empty?
        req = { get: Net::HTTP::Get, post: Net::HTTP::Post, put: Net::HTTP::Put, delete: Net::HTTP::Delete }
              .fetch(method).new(uri)
        req.body = body if body
        res = Net::HTTP.start(uri.host, uri.port, open_timeout: 5, read_timeout: timeout) { |h| h.request(req) }
        [res.code.to_i, res.body.to_s]
      rescue StandardError => e
        raise Failure, "board: #{path} did not answer (#{e.class.name.split('::').last}); is the board up on #{@board_ip}?"
      end

      # A query, every value encoded but "/". The firmware decodes every
      # value (%XX, "+" a space) on every route; a board flashed before it did
      # (2026-11-06) decoded only on /fs/, so "/" left as it is keeps a path
      # of plain characters going the same to both (report/font_pct.md).
      def board_query(query)
        query.map { |k, v| "#{k}=#{v.to_s.gsub(%r{[^A-Za-z0-9_.~/-]}) { |c| c.bytes.map { |b| format('%%%02X', b) }.join }}" }
             .join("&")
      end

      def board_json(method, path, query = {})
        code, body = board_http(method, path, query)
        j = JSON.parse(body) rescue nil # rubocop:disable Style/RescueModifier
        raise Failure, "board: #{path} #{query.inspect} answered #{code} #{body[0, 120]}" unless code == 200 && j && j["ok"]
        j
      end

      def board_apps
        code, body = board_http(:get, "/app/list")
        j = JSON.parse(body) rescue nil # rubocop:disable Style/RescueModifier
        raise Failure, "board: /app/list answered #{code} #{body[0, 120]}" unless code == 200 && j
        j.fetch("apps", [])
      end

      def board_spawn(path)
        board_json(:post, "/app/launch", path: path).fetch("pid")
      end

      def board_kill_named(name)
        board_apps.each { |a| board_json(:post, "/app/kill", pid: a["pid"]) if a["name"] == name }
        sleep 0.5
      end

      def board_file_get(path)
        code, body = board_http(:get, "/fs/get", { path: path })
        code == 200 ? body : nil
      end

      def board_file_put(path, data)
        code, body = board_http(:put, "/fs/put", { path: path }, body: data)
        raise Failure, "board: put #{path} answered #{code} #{body[0, 120]}" unless code == 200
      end

      # A checkpoint or a move asked on the board settles: the last status
      # that is not pending (or held, while waiting for the move's end).
      def board_ckpt_wait(pid, timeout: 45)
        deadline = now + timeout
        loop do
          j = board_json(:get, "/app/checkpoint", pid: pid)
          return j unless %w[pending held].include?(j["status"])
          raise Failure, "board: pid #{pid} still #{j['status']} after #{timeout}s" if now > deadline
          sleep 0.3
        end
      end

      # ---- the router and the board's settings ----

      def board_router_up
        board_router_down
        if router_listening?(ROUTER_PORT_BOARD)
          raise Failure, "board: port #{ROUTER_PORT_BOARD} is taken (the simulation's zenohd?); stop it or give BOARD_ROUTER_PORT="
        end
        cmd = ["docker", "run", "-d", "--rm", "--name", ROUTER_NAME_BOARD, "-p", "#{ROUTER_PORT_BOARD}:7447",
               AsterismRounds::ROUTER_IMAGE, "--no-multicast-scouting", "--listen", "tcp/0.0.0.0:7447"]
        out, st = Open3.capture2e(*cmd)
        raise Failure, "board: the Zenoh router did not start: #{out.strip.lines.last}" unless st.success?
        deadline = now + 20
        until router_listening?(ROUTER_PORT_BOARD)
          raise Failure, "board: the Zenoh router is not listening on #{ROUTER_PORT_BOARD}" if now > deadline
          sleep 0.2
        end
      end

      def board_router_down
        Open3.capture2e("docker", "rm", "-f", ROUTER_NAME_BOARD)
        nil
      end

      # The board's /home/asterism for the run (what was there is kept), and
      # its transfer app started again on it.
      # This tree's copies of BOARD_FILES on the board for the run; what was
      # there (nil: nothing) is kept, with the directories made for them.
      def board_files_setup
        @board_files_kept = {}
        @board_dirs_made = []
        BOARD_FILES.each do |path|
          data = File.binread(File.join(CORE, "flash", path))
          there = board_file_get(path)
          next if there&.b == data
          dir = File.dirname(path)
          unless board_dir?(dir)
            board_json(:post, "/fs/mkdir", path: dir)
            @board_dirs_made << dir
          end
          @board_files_kept[path] = there
          board_file_put(path, data)
        end
        @board_files_kept.keys
      end

      def board_files_teardown
        return unless @board_files_kept
        @board_files_kept.each do |path, data|
          if data
            board_file_put(path, data)
          else
            board_http(:delete, "/fs/del", { path: path })
          end
        rescue Failure
          nil
        end
        @board_dirs_made.reverse_each { |d| board_http(:delete, "/fs/del", { path: d }) rescue nil } # rubocop:disable Style/RescueModifier
        @board_files_kept = nil
      end

      def board_dir?(dir)
        code, body = board_http(:get, "/fs/list", { path: dir })
        code == 200 && (JSON.parse(body)["ok"] rescue false) # rubocop:disable Style/RescueModifier
      end

      def board_asterism_setup
        @board_mark0 = board_lines.size
        @board_kept = {}
        BOARD_ASTERISM_FILES.each { |f| @board_kept[f] = board_file_get("#{BOARD_ASTERISM}/#{f}") }
        board_http(:post, "/fs/mkdir", { path: BOARD_ASTERISM })
        board_file_put("#{BOARD_ASTERISM}/config.toml",
                       "router = \"tcp/#{@host_ip}:#{ROUTER_PORT_BOARD}\"\nnode = \"#{NODE_BOARD}\"\n")
        board_file_put("#{BOARD_ASTERISM}/receive.toml", "enabled = true\nfrom = [\"#{NODE_A}\"]\n")
        board_kill_named("Transfer")
        mark = board_lines.size
        board_spawn(TRANSFER)
        board_wait(/Transfer app connected as #{NODE_BOARD}|transfer: connect: cannot open a session/, mark,
                   "the board's transfer app connected", timeout: 60)
        return if board_lines[mark..].any? { |l| l =~ /Transfer app connected as #{NODE_BOARD}/ }
        raise Failure, "board: the board cannot reach the router at tcp/#{@host_ip}:#{ROUTER_PORT_BOARD} " \
                       "(a firewall in between? WSL2 lets in only the ports it is told; BOARD_ROUTER_PORT=, HOST_IP=)"
      end

      def board_asterism_teardown
        return unless @board_kept
        board_kill_named("Transfer") rescue nil # rubocop:disable Style/RescueModifier
        @board_kept.each do |f, data|
          path = "#{BOARD_ASTERISM}/#{f}"
          if data
            board_file_put(path, data)
          else
            board_http(:delete, "/fs/del", { path: path })
          end
        rescue Failure
          nil
        end
        # the transfer app the board starts at boot when it has a receive.toml
        board_spawn(TRANSFER) if @board_kept["receive.toml"]
        @board_kept = nil
      rescue Failure
        nil
      end

      # ---- the rounds ----

      def board_round_self
        mark = board_lines.size
        board_spawn(SELF_APP)
        restored = board_wait(/ckpt_self: on_restore /, mark, "on_restore of ckpt_self on the board", timeout: 30)
        board_wait(/ckpt_self: b=\d+ /, restored + 1, "a step of b after the restore")
        board_wait(/ckpt_self: a=\d+ /, restored + 1, "a step of a after the restore")
        lines = board_lines
        w = (mark...lines.size).find { |k| lines[k] =~ /checkpoint written: / } or raise Failure, "board self: no 'checkpoint written'"
        r = (w...lines.size).find { |k| lines[k] =~ /ckpt_self: on_restore / }
        c = continuity_of(lines[mark...w], lines[(r + 1)..], lines[w..r], lines[r], "board self", "ckpt_self")
        board_kill_named("Ckpt Self")
        "self on the board: a #{c[:a]}, b #{c[:b]}, main #{c[:main]}, t step #{c[:t_step]} ms, image #{c[:bytes]} B"
      end

      def board_round_in_out
        pid = start_counter
        mark = log_lines.size
        bmark = board_lines.size
        r = asterism_move(pid, NODE_BOARD)
        raise Failure, "board in: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"].to_i.positive?
        restored = board_wait(/ckpt_counter: on_restore /, bmark, "on_restore on the board", timeout: 30)
        board_wait(/ckpt_counter: b=\d+ /, restored + 1, "a step of b on the board")
        board_wait(/ckpt_counter: a=\d+ /, restored + 1, "a step of a on the board")
        a = log_lines
        w = (mark...a.size).find { |k| a[k] =~ /checkpoint written: / } or raise Failure, "board in: no image here"
        b = board_lines
        c_in = continuity_of(a[0...w], b[(restored + 1)..], a[w..], b[restored], "board in")
        # and back
        there = r["pid"].to_i
        mark2 = log_lines.size
        board_json(:post, "/app/move", pid: there, node: NODE_A)
        st = board_ckpt_wait(there)
        raise Failure, "board out: the board says #{st.inspect}" unless st["status"] == "closed"
        back = wait_line(/ckpt_counter: on_restore /, mark2, "on_restore back in the native program", timeout: 30)
        wait_line(/ckpt_counter: a=\d+ /, back + 1, "a step of a back here")
        b = board_lines
        w2 = (restored...b.size).find { |k| b[k] =~ /checkpoint written: / } or raise Failure, "board out: no image there"
        a = log_lines
        c_out = continuity_of(b[0...w2], a[(back + 1)..], b[w2..], a[back], "board out")
        kill_counter
        "in: a #{c_in[:a]}, t step #{c_in[:t_step]} ms, image #{c_in[:bytes]} B (#{r['took_ms']} ms); " \
          "out: a #{c_out[:a]}, t step #{c_out[:t_step]} ms, image #{c_out[:bytes]} B (#{st['what']})"
      end

      def board_round_window
        from = log_lines.size
        pid = app("spawn", WINDOW_APP).fetch("pid")
        wait_line(/ckpt_window: task=2 /, from, "ckpt_window's Task")
        bmark = board_lines.size
        r = asterism_move(pid, NODE_BOARD)
        raise Failure, "board window: answered #{r.inspect}" unless r["status"] == "moved"
        restored = board_wait(/ckpt_window: on_restore/, bmark, "ckpt_window's on_restore on the board", timeout: 30)
        board_wait(/ckpt_window: task=\d+ /, restored + 1, "ckpt_window's Task on the board")
        there = r["pid"].to_i
        mark = log_lines.size
        board_json(:post, "/app/move", pid: there, node: NODE_A)
        st = board_ckpt_wait(there)
        raise Failure, "board window back: the board says #{st.inspect}" unless st["status"] == "closed"
        back = wait_line(/ckpt_window: on_restore/, mark, "ckpt_window's on_restore back here", timeout: 30)
        wait_line(/ckpt_window: task=\d+ /, back + 1, "ckpt_window's Task back here")
        kill_named("Ckpt Window")
        "window: to the board and back"
      end

      def board_round_calls
        from = log_lines.size
        pid = app("spawn", CALLS_APP_B).fetch("pid")
        wait_line(/ckpt_calls: main=\d+/, from, "ckpt_calls running")
        bmark = board_lines.size
        r = asterism_move(pid, NODE_BOARD)
        unless r["status"] == "moved" && Array(r["missing"]).include?("linux_settings")
          raise Failure, "board calls: answered #{r.inspect}"
        end
        i = board_wait(/ckpt_calls: on_restore main=\d+/, bmark, "ckpt_calls's on_restore on the board", timeout: 30)
        # the board is the one machine here where platform differs (:esp32)
        machine = machine_values!(board_lines[i], "board calls", want: "esp32")
        board_wait(/ckpt_calls: main=\d+ .*linux_settings=false/, bmark, "ckpt_calls on the board without linux_settings",
                   timeout: 30)
        board_kill_named("Ckpt Calls")
        "calls: moved, missing #{r['missing'].join(',')}, #{machine}"
      end

      def board_round_requires
        from = log_lines.size
        pid = app("spawn", REQUIRES_APP_B).fetch("pid")
        wait_line(/ckpt_requires: main=\d+/, from, "ckpt_requires running")
        mark = log_lines.size
        r = asterism_move(pid, NODE_BOARD)
        unless r["status"] == "stayed" && r["here"] == "not stopped" && r["reason"].to_s.include?("linux_settings")
          raise Failure, "board requires: answered #{r.inspect}"
        end
        sleep 0.6
        lines = log_lines[mark..]
        raise Failure, "board requires: an image was written" unless lines.grep(/checkpoint written: /).empty?
        kill_named("Ckpt Requires")
        "requires: refused before it stopped"
      end

      def board_round_no_answer
        bmark = board_lines.size
        pid = board_spawn(COUNTER_APP)
        board_wait(/ckpt_counter: b=3 /, bmark, "the counter on the board")
        board_json(:post, "/app/move", pid: pid, node: AsterismRounds::NODE_NOBODY)
        st = board_ckpt_wait(pid)
        raise Failure, "board no answer: the board says #{st.inspect}" unless %w[refused continued].include?(st["status"])
        mark = board_lines.size
        board_wait(/ckpt_counter: a=\d+ /, mark, "the counter going on on the board")
        raise Failure, "board no answer: the counter is gone" unless board_apps.any? { |a| a["pid"] == pid }
        board_kill_named("Ckpt Counter")
        "no answer: #{st['status']} (#{st['what'][0, 60]})"
      end
    end
  end
end
