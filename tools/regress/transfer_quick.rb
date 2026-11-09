#!/usr/bin/env ruby
# frozen_string_literal: true
#
# transfer_quick.rb - the gist of the file transfer in about a minute
# (rake regress:transfer:quick, in regress:full; doc/asterism_file_transfer
# T2). regress:transfer (both engines, eight cases, 1 MiB each, about six
# minutes) stays the whole check; this one is one native program (the
# standard engine) and this process as the other machine, through the
# transfer app itself:
#
#   resend    pieces lost, sent twice and swapped on the way (a sender of
#             this process with faults): whole, and its sha256 there
#   cut       the receiver's connection cut once on the way: the sender goes
#             on from where the receiver stood (not from 0)
#   mismatch  a wrong sha256 declared: not placed, the reason comes back
#   names     the kind "file" lands in /home/inbox/ only; the same name
#             again gets name(1), and with overwrite it replaces the file
#             (the one under the other name stays as it was)
#   refused   before a byte is sent: a kind the machine does not take, a
#             kind it does not take from this node, a name that leads out
#             of /home/inbox/, the SD card's inbox on a machine with no
#             card (and an inbox there is no such), a file larger than its
#             free space (the browser build: the page's estimate of the
#             browser's storage)
#   api       an app of the machine (the probe) sends through the transfer
#             app (FmrbTransfer: progress, the name placed under) to this
#             process; then one it cancels on the way: this side drops the
#             piece file
#   managed   the console's part of receive.toml (T6): a list sent by a
#             node other than the console is refused, and so is one sent
#             as the console over a connection without certificates (the
#             router here is plain tcp); the senders written in the managed
#             part (a pattern) are taken, the others refused, also when
#             the part by hand has no [file]
#   big       a 4 MiB file, the connection cut once on the way (T3): its
#             pieces go straight to a hidden file in /home/inbox/ (none in
#             /tmp), it goes on from where the receiver stood, and the
#             piece size and window are the ones the machine named
#   switch    receiving on and off (T7; native only, last: the machine is
#             started again): with no receive.toml of its own (the system's
#             copy in /etc, off) the transfer app is not started at boot;
#             the shell's areceive on starts it and writes /home's copy,
#             areceive allow file lets this process in, and a file is
#             placed; a /home file with no enabled line refuses at begin
#             and the transfer app ends; on again, then areceive off while
#             a file is coming in: that file is placed, a new one refused,
#             and the transfer app ends
#
#   ruby tools/regress/transfer_quick.rb            # the native build there is
#   ruby tools/regress/transfer_quick.rb --web      # the browser build as the
#                                                   # machine (wasm/build/dist;
#                                                   # resend, mismatch, names,
#                                                   # refused, managed)
#   ruby tools/regress/transfer_quick.rb --only names
#
# The machine's receive.toml (the form of T2, a table per kind):
#   [file] from = ["rg-xfer-pc"], [checkpoint] from = ["rg-xfer-a"],
#   [probe] from = ["rg-xfer-pc"]
# This process is the node rg-xfer-pc (CRuby Asterism, the gem of this tree:
# ASTERISM_DIR, else vendor/asterism), joined as the app "transfer" so that
# the machine's transfer app reaches its inbox.
require "digest"
require "fileutils"
require "json"
require_relative "transfer"
require_relative "../fmrb_send"

module Regress
  module TransferQuick
    T = Regress::Transfer
    NODE_PC = "rg-xfer-pc"
    RECEIVE = <<~TOML
      # transfer_quick.rb: a table per kind
      enabled = true

      [file]
      from = ["#{NODE_PC}"]

      [checkpoint]
      from = ["#{T::NODE_A}"]

      [probe]
      from = ["#{NODE_PC}"]
    TOML
    PACE_S = 0.01

    # The probe of this process: the kind "file" placed in a directory of
    # its own; slow: a pause in each piece (for the cancel to land on the
    # way).
    def self.pc_inbox_class
      Class.new(Asterism::Transfer::Inbox) do
        attr_accessor :slow, :placed, :inbox

        def refused(kind, _from, _meta)
          kind == "file" ? nil : "takes no #{kind}"
        end

        def name_ok?(_kind, name)
          Asterism::Transfer.file_name?(name)
        end

        def chunk(id, pos, data, from: nil)
          sleep(@slow) if @slow
          super
        end

        def received(t)
          name = Asterism::Transfer.free_name("#{dir}in/", t["name"])
          FileUtils.mkdir_p("#{dir}in")
          File.rename(t["part"], "#{dir}in/#{name}")
          @placed = name
          @inbox = t["meta"]["inbox"]
          { "status" => "placed", "name" => name }
        end
      end
    end

    class Runner < T::Runner
      def initialize(opts)
        super(opts.merge(engine: "standard"))
        @web = opts[:web]
        @to = @web ? T::NODE_WEB : T::NODE_B
      end

      def run
        t0 = now
        FileUtils.mkdir_p(T::OUT)
        notes = []
        ok = true
        begin
          router_up
          if @web
            web_machine
          else
            @relay = T::CutRelay.new(T::RELAY_PORT, T::ROUTER_PORT)
            up(T::RUN_DIR_B, T::PORT_B, T::NODE_B, NODE_PC, "tcp/127.0.0.1:#{T::RELAY_PORT}")
            File.write(File.join(host_home, "home", "asterism", "receive.toml"), RECEIVE)
            wait_line(T::RUN_DIR_B, /Transfer app connected as #{T::NODE_B}/, 0, "the transfer app connected", timeout: 30)
          end
          pc_up
          cases.each do |name|
            next if @opts[:only] && !name.include?(@opts[:only])
            notes << "#{name}: #{send("q_#{name}")}"
          rescue T::Failure => e
            ok = false
            notes << "#{name}: FAILED #{e.message}"
          end
        rescue T::Failure => e
          ok = false
          notes << "FAILED #{e.message}"
        ensure
          Asterism.close if defined?(Asterism) && Asterism.connected?
          keep_logs
          down(T::RUN_DIR_B, T::PORT_B)
          @relay&.close
          web_down
          router_down
          FileUtils.rm_rf(@pc_dir) if @pc_dir
        end
        notes.each { |n| puts "  #{n}" }
        puts format("transfer quick: %s (%s, %.1f min)", ok ? "ok" : "FAILED", @web ? "browser" : "native",
                    (now - t0) / 60.0)
        ok
      end

      def cases = @web ? %w[resend mismatch names refused managed] : %w[resend cut mismatch names refused managed api big switch]

      # ---- the machine ----

      def web_machine
        raise T::Failure, "no browser bundle in #{T::WEB_DIST} (rake wasm:dist)" unless File.exist?(File.join(T::WEB_DIST, "index.html"))
        chrome = web_chrome or raise T::Failure, "no Linux Chromium (FMRB_CHROME, or npx playwright install chromium)"
        web_up(chrome)
        dir = File.join(T::OUT, "web_home")
        FileUtils.mkdir_p(dir)
        File.write(File.join(dir, "config.toml"), "router = \"ws/127.0.0.1:#{T::ROUTER_WS_PORT}\"\nnode = \"#{T::NODE_WEB}\"\n")
        File.write(File.join(dir, "receive.toml"), RECEIVE)
        File.write(File.join(dir, "hash.json"), JSON.generate({ "bench" => "hash", "path" => "/tmp/xfer_hash.bin",
                                                                "make" => T::SIZE }))
        web_tool("put", File.join(dir, "config.toml"), "/flash/home/asterism/config.toml")
        web_tool("put", File.join(dir, "receive.toml"), "/flash/home/asterism/receive.toml")
        # the SHA-256 of 1 MiB there (the C one, and the plain Ruby one), first
        web_tool("put", File.join(dir, "hash.json"), "/flash/home/xfer_probe.job")
        mark = web_lines.size
        web_tool("reload", timeout: 90)
        web_wait(/xfer_probe: bench done/, mark, "the browser's hash bench", timeout: 300)
        @web_hash = web_lines[mark..].grep(/xfer_probe: bench hash sha256/).map { |l| l[/sha256 .*/] }
        web_wait(/Transfer app connected as #{T::NODE_WEB}/, mark, "the browser's transfer app connected", timeout: 60)
      end

      # The native program's /home is a directory here (its --user-dir);
      # its debug server's file commands reach /tmp only.
      def host_home = File.join(T::OUT, "#{File.basename(T::RUN_DIR_B)}_home", "user")

      def put_machine(local, path)
        return web_tool("put", local, "/flash#{path}") if @web
        return FileUtils.cp(local, File.join(host_home, path)) if path.start_with?("/home/")
        native(T::RUN_DIR_B, T::PORT_B, "file", "put", local, path, timeout: 60)
      end

      # The bytes of a file there, nil when there is none.
      def get_machine(path)
        if !@web && path.start_with?("/home/")
          f = File.join(host_home, path)
          return File.exist?(f) ? File.binread(f) : nil
        end
        out = File.join(T::OUT, "quick.got")
        FileUtils.rm_f(out)
        begin
          if @web
            web_tool("get", "/flash#{path}", out)
          else
            native(T::RUN_DIR_B, T::PORT_B, "file", "get", path, out, timeout: 60)
          end
        rescue T::Failure
          return nil
        end
        File.exist?(out) ? File.binread(out) : nil
      ensure
        FileUtils.rm_f(out) if out
      end

      def machine_lines(from)
        (@web ? web_lines : log_lines(T::RUN_DIR_B))[from..] || []
      end

      def machine_mark = (@web ? web_lines : log_lines(T::RUN_DIR_B)).size

      # ---- this process ----

      def pc_up
        FmrbSend.load_asterism
        @pc_dir = File.join(T::OUT, "pc")
        FileUtils.rm_rf(@pc_dir)
        FileUtils.mkdir_p(@pc_dir)
        Asterism.connect("tcp/127.0.0.1:#{T::ROUTER_PORT}", node: NODE_PC, app: "transfer")
        @pc_inbox = TransferQuick.pc_inbox_class.new("#{@pc_dir}/")
        @pc_inbox.logger = nil
        Asterism.expose("inbox", @pc_inbox, methods: Asterism::Transfer::Inbox::METHODS, caller: :from)
        deadline = now + 20
        until Asterism.nodes.include?(@to)
          Asterism.poll
          raise T::Failure, "#{@to} is not on the network" if now > deadline
          sleep 0.02
        end
      end

      def pump
        Asterism.poll
        @pc_inbox.tick
      end

      # A sender of this process with its faults (nil: none) to the machine's
      # transfer app; the block runs between the steps.
      def pc_send(name, bytes, kind: "file", meta: {}, faults: nil, pace: nil, sha256: nil, path: nil)
        unless path
          path = File.join(@pc_dir, "src_#{name.tr("/.", "__")}")
          File.binwrite(path, bytes)
        end
        s = Class.new(Asterism::Transfer::Sender) do
          attr_accessor :faults

          def fault(pos, _n)
            @seen ||= {}
            return nil if @seen[pos] || @faults.nil?
            @seen[pos] = true
            @faults.call(pos / @piece)
          end
        end.new("#{@to}/transfer/inbox", kind, path, name: name, meta: meta, sha256: sha256)
        s.logger = nil
        s.faults = faults
        deadline = now + 60
        until s.done?
          pump
          s.step
          yield s if block_given?
          raise T::Failure, "#{name}: not done in 60 s" if now > deadline
          sleep(pace || 0.0003)
        end
        s
      end

      def must_place(s, what)
        unless s.ok? && s.answer["status"] == "placed"
          raise T::Failure, "#{what}: #{s.ok? ? s.answer['reason'] : s.reason}"
        end
        s.answer["name"]
      end

      def must_have(path, bytes)
        got = get_machine(path) or raise T::Failure, "no #{path} there"
        return if got == bytes
        raise T::Failure, "#{path} there has sha256 #{Digest::SHA256.hexdigest(got)[0, 12]}, " \
                          "not #{Digest::SHA256.hexdigest(bytes)[0, 12]}"
      end

      def random(n, seed) = Random.new(seed).bytes(n)

      # ---- the cases ----

      def q_resend
        bytes = random(262_144, 1)
        f = ->(i) { (i % 10 == 3 && :drop) || (i % 5 == 1 && :twice) || (i % 7 == 2 && :swap) || nil }
        s = pc_send("q-resend.bin", bytes, faults: f)
        name = must_place(s, "resend")
        must_have("/home/inbox/#{name}", bytes)
        raise T::Failure, "no piece was sent again" unless s.stats["again"].positive?
        "whole, #{s.stats['again']} times sent again from the receiver's position (#{s.took_ms} ms)"
      end

      def q_cut
        bytes = random(524_288, 2)
        cut = false
        s = pc_send("q-cut.bin", bytes, pace: PACE_S) do |x|
          next if cut || x.pos < 131_072
          cut = true
          @relay.cut!(T::CUT_DOWN_S)
        end
        must_place(s, "cut")
        must_have("/home/inbox/q-cut.bin", bytes)
        on = s.stats["resumed_at"]
        raise T::Failure, "went on from #{on.inspect}" unless on.size == 1 && on[0] >= 131_072
        "whole, cut once, went on from #{on[0]} (#{s.took_ms} ms, #{s.stats['retries']} retries)"
      end

      def q_mismatch
        s = pc_send("q-mismatch.bin", random(10_000, 3), sha256: "0" * 64)
        raise T::Failure, "mismatch: #{s.reason}" unless s.ok?
        raise T::Failure, "placed: #{s.answer.inspect}" unless s.answer["status"] == "refused"
        raise T::Failure, "it is in /home/inbox" if get_machine("/home/inbox/q-mismatch.bin")
        "not placed: #{s.answer["reason"][0, 64]}"
      end

      def q_names
        a = "first\n"
        b = "second\n"
        c = "third, over the first\n"
        n1 = must_place(pc_send("same.txt", a), "names 1")
        n2 = must_place(pc_send("same.txt", b), "names 2")
        n3 = must_place(pc_send("same.txt", c, meta: { "overwrite" => true }), "names 3")
        raise T::Failure, "placed as #{[n1, n2, n3].inspect}" unless [n1, n2, n3] == ["same.txt", "same(1).txt", "same.txt"]
        must_have("/home/inbox/same.txt", c)
        must_have("/home/inbox/same(1).txt", b)
        %w[/tmp/same.txt /home/same.txt /tmp/same.txt.file.part /home/inbox/.same.txt.file.part].each do |p|
          raise T::Failure, "#{p} is there" if get_machine(p)
        end
        "same.txt, then same(1).txt beside it, then same.txt overwritten (same(1).txt kept); only in /home/inbox"
      end

      def q_refused
        out = []
        [["checkpoint", "x", "takes no checkpoint from #{NODE_PC}"],
         ["probe", "x", "takes no probe"],
         ["file", "../x", "not a name"],
         ["file", ".hidden", "not a name"]].each do |kind, name, want|
          s = pc_send(name, "x" * 100, kind: kind)
          raise T::Failure, "#{kind} #{name}: taken" if s.ok?
          raise T::Failure, "#{kind} #{name}: #{s.reason}" unless s.reason.include?(want)
          raise T::Failure, "#{kind} #{name}: #{s.stats['calls']} calls" unless s.stats["calls"] == 1
          out << want.split(" from").first
        end
        # the older receive.toml (a from at the top, no tables): the
        # checkpoint's only, so no file
        old = File.join(@pc_dir, "receive_old.toml")
        File.write(old, "enabled = true\nfrom = [\"#{NODE_PC}\"]\n")
        put_machine(old, "/home/asterism/receive.toml")
        s = pc_send("old.txt", "x")
        raise T::Failure, "old form: #{s.ok? ? 'taken' : s.reason}" unless !s.ok? && s.reason.include?("no [file]")
        File.write(old, RECEIVE)
        put_machine(old, "/home/asterism/receive.toml")
        out << "no [file] with the older form"
        # the SD card's inbox on a machine with none (the native program and
        # the browser build show no card): refused at begin, nothing written
        s = pc_send("sd.txt", "x" * 100, meta: { "inbox" => "sd" })
        raise T::Failure, "sd: #{s.ok? ? 'taken' : s.reason}" unless !s.ok? && s.reason.include?("has no SD card")
        raise T::Failure, "sd: #{s.stats['calls']} calls" unless s.stats["calls"] == 1
        raise T::Failure, "an inbox of the SD card there" if get_machine("/sd/inbox/.sd.txt.file.part")
        out << "no SD card"
        s = pc_send("where.txt", "x" * 100, meta: { "inbox" => "usb" })
        raise T::Failure, "usb: #{s.ok? ? 'taken' : s.reason}" unless !s.ok? && s.reason.include?("has no inbox usb")
        out << "no inbox usb"
        # larger than the storage's room (a "file" goes to /home/inbox/ now;
        # 8 TiB, sparse here). The browser build's room is the page's
        # estimate of the browser's storage (navigator.storage.estimate)
        big = File.join(@pc_dir, "big.bin")
        File.open(big, "w") { |f| f.truncate(1 << 43) }
        s = pc_send("big.bin", nil, path: big, sha256: "1" * 64)
        raise T::Failure, "8 TiB: #{s.ok? ? 'taken' : s.reason}" unless !s.ok? && s.reason.include?("do not fit")
        raise T::Failure, "8 TiB: #{s.stats['calls']} calls" unless s.stats["calls"] == 1
        if get_machine("/tmp/big.bin.file.part") || get_machine("/home/inbox/.big.bin.file.part")
          raise T::Failure, "a piece file is there"
        end
        FileUtils.rm_f(big)
        out << s.reason[/\(.*\)/]
        "refused at begin, one call each: #{out.join('; ')}"
      end

      def q_managed
        out = []
        list = { "file" => [NODE_PC], "checkpoint" => [], "at" => "2026-11-09T00:00:00Z" }
        # from a node that is not the console
        why = policy_refusal(list)
        raise T::Failure, "managed: a list from #{NODE_PC}: #{why}" unless why.include?("from console only")
        out << "a list from #{NODE_PC} refused"
        # as the console (config.toml's console names this process), over a
        # connection without certificates
        conf = get_machine("/home/asterism/config.toml") or raise T::Failure, "managed: no config.toml there"
        tmp = File.join(@pc_dir, "config.toml")
        begin
          File.write(tmp, "#{conf}console = \"#{NODE_PC}\"\n")
          put_machine(tmp, "/home/asterism/config.toml")
          why = policy_refusal(list)
          raise T::Failure, "managed: a list as the console over tcp: #{why}" unless why.include?("without certificates")
        ensure
          File.write(tmp, conf)
          put_machine(tmp, "/home/asterism/config.toml")
        end
        out << "as the console over a connection without certificates refused"
        # the managed part written as the transfer app writes it: taken from
        # its senders (a pattern) with no [file] by hand
        recv = File.join(@pc_dir, "receive_managed.toml")
        begin
          File.write(recv, managed_receive(["rg-xfer-*"]))
          put_machine(recv, "/home/asterism/receive.toml")
          bytes = random(20_000, 6)
          name = must_place(pc_send("q-managed.bin", bytes), "managed")
          must_have("/home/inbox/#{name}", bytes)
          out << "taken from the managed part (rg-xfer-*)"
          File.write(recv, managed_receive(["fmruby-*"]))
          put_machine(recv, "/home/asterism/receive.toml")
          s = pc_send("q-managed2.bin", "x" * 100)
          unless !s.ok? && s.reason.include?("not in receive.toml, by hand or from console")
            raise T::Failure, "managed: not in the list: #{s.ok? ? 'taken' : s.reason}"
          end
          raise T::Failure, "managed: #{s.stats['calls']} calls" unless s.stats["calls"] == 1
          out << "refused when not in it"
        ensure
          File.write(recv, RECEIVE)
          put_machine(recv, "/home/asterism/receive.toml")
        end
        out.join("; ")
      end

      # The reason the machine gives for a list sent to its policy_set
      # (a Failure when it takes it).
      def policy_refusal(list)
        Asterism["#{@to}/transfer/inbox", timeout: 10].policy_set(list)
        raise T::Failure, "managed: the list was taken"
      rescue Asterism::Error => e
        e.message
      end

      # receive.toml with a part by hand that has no [file] and the console's
      # part with these senders of files.
      def managed_receive(file_from)
        "enabled = true\n[checkpoint]\nfrom = [\"#{T::NODE_A}\"]\n\n" \
          "# ---- managed by the console (below this line; edit above it) ----\n" \
          "[managed]\nby = \"console\"\nat = \"2026-11-09T00:00:00Z\"\n\n" \
          "[managed.file]\nfrom = [#{file_from.map { "\"#{_1}\"" }.join(', ')}]\n\n" \
          "[managed.checkpoint]\nfrom = []\n"
      end

      def q_api
        big = random(1_048_576, 4)
        File.binwrite(File.join(@pc_dir, "api_big.bin"), big)
        put_machine(File.join(@pc_dir, "api_big.bin"), "/home/api_big.bin")
        # the file placed by names (same.txt), to this process
        r = api_job({ "to" => NODE_PC, "path" => "/home/inbox/same.txt" })
        raise T::Failure, "api: #{r[:end]}" unless r[:end] =~ /api done ok same\.txt/
        got = File.binread(File.join(@pc_dir, "in", "same.txt"))
        raise T::Failure, "api: the file here is #{got.inspect}" unless got == "third, over the first\n"
        raise T::Failure, "api: asked for the inbox #{@pc_inbox.inbox.inspect}" unless @pc_inbox.inbox.nil?
        # the SD card chosen (sd: true): the choice reaches the receiver
        r = api_job({ "to" => NODE_PC, "path" => "/home/inbox/same.txt", "name" => "sd.txt", "sd" => true })
        raise T::Failure, "api sd: #{r[:end]}" unless r[:end] =~ /api done ok sd\.txt/
        raise T::Failure, "api sd: asked for the inbox #{@pc_inbox.inbox.inspect}" unless @pc_inbox.inbox == "sd"
        # one cancelled on the way
        @pc_inbox.slow = 0.003
        r2 = api_job({ "to" => NODE_PC, "path" => "/home/api_big.bin", "cancel_at" => 65_536 })
        @pc_inbox.slow = nil
        raise T::Failure, "api cancel: #{r2[:end]}" unless r2[:end] =~ /api done failed: cancelled/
        deadline = now + 5
        pump while @pc_inbox.current_file && now < deadline
        raise T::Failure, "api cancel: this side still holds it" if @pc_inbox.current_file
        left = Dir.glob(File.join(@pc_dir, "*.{part,xfer}"))
        raise T::Failure, "api cancel: #{left.map { |x| File.basename(x) }} left" unless left.empty?
        sending = r2[:lines].grep(/xfer_probe: api now sending/).size
        "sent through the transfer app (placed as same.txt there; with sd: true the receiver was asked for its SD card); " \
          "1 MiB cancelled at #{r2[:lines].grep(/cancelling at/).first.to_s[/at (\d+)/, 1]} after #{sending} progress report(s): the piece file dropped here"
      end

      def q_big
        bytes = random(4_194_304, 5)
        cut = nil
        s = pc_send("q-big.bin", bytes, pace: PACE_S) do |x|
          next if cut || x.pos < 1_048_576
          cut = [get_machine("/tmp/q-big.bin.file.part").nil?,
                 File.exist?(File.join(host_home, "home", "inbox", ".q-big.bin.file.part"))]
          @relay.cut!(T::CUT_DOWN_S)
        end
        must_place(s, "big")
        must_have("/home/inbox/q-big.bin", bytes)
        raise T::Failure, "a piece file in /tmp on the way" unless cut && cut[0]
        raise T::Failure, "no hidden piece file in /home/inbox on the way" unless cut[1]
        left = Dir.children(File.join(host_home, "home", "inbox")).select { |n| n.start_with?(".") }
        raise T::Failure, "#{left.inspect} left in /home/inbox" unless left.empty?
        on = s.stats["resumed_at"]
        raise T::Failure, "went on from #{on.inspect}" unless on.size == 1 && on[0] >= 1_048_576
        "4 MiB whole, cut once, went on from #{on[0]}; pieces in /home/inbox (hidden), none in /tmp; " \
          "#{s.piece} x #{s.window} as the machine named (#{s.took_ms} ms)"
      end

      def q_switch
        out = []
        mark = 0
        up(T::RUN_DIR_B, T::PORT_B, T::NODE_B, NODE_PC, "tcp/127.0.0.1:#{T::RELAY_PORT}", receive: false)
        wait_line(T::RUN_DIR_B, %r{Transfer: receiving is off \(/etc/asterism/receive\.toml\)}, mark,
                  "the kernel reading the system's copy", timeout: 10)
        sleep 2
        raise T::Failure, "switch: the transfer app started at boot with receiving off" if transfer_app?
        raise T::Failure, "switch: /home has a receive.toml" if File.exist?(user_receive)
        out << "/etc only: not started at boot"
        @shell = app(T::RUN_DIR_B, T::PORT_B, "spawn", SHELL).fetch("pid")
        sleep 1.5
        mark = machine_mark
        shell_line("areceive on")
        wait_line(T::RUN_DIR_B, /Transfer app connected as #{T::NODE_B}/, mark, "the transfer app after areceive on",
                  timeout: 30)
        text = File.exist?(user_receive) ? File.read(user_receive) : ""
        unless text.include?("enabled = true") && text.include?("# Who may send files and apps")
          raise T::Failure, "switch: /home's receive.toml after areceive on: #{text[0, 80].inspect}"
        end
        s = pc_send("q-switch0.txt", "x" * 100)
        raise T::Failure, "switch: nobody listed yet: #{s.ok? ? 'taken' : s.reason}" unless !s.ok? && s.reason.include?("takes no file from")
        shell_line("areceive allow file #{NODE_PC}")
        deadline = now + 10
        sleep 0.1 until File.read(user_receive).include?("\"#{NODE_PC}\"") || now > deadline
        bytes = random(30_000, 7)
        name = must_place(pc_send("q-switch.bin", bytes), "switch")
        must_have("/home/inbox/#{name}", bytes)
        out << "areceive on: started, /home's copy made from /etc; allow file: placed"
        # a /home file with no enabled line: off
        File.write(user_receive, "[file]\nfrom = [\"#{NODE_PC}\"]\n")
        mark = machine_mark
        s = pc_send("q-switch2.txt", "x" * 100)
        unless !s.ok? && s.reason.include?("receiving is off")
          raise T::Failure, "switch: no enabled line: #{s.ok? ? 'taken' : s.reason}"
        end
        raise T::Failure, "switch: no enabled line: #{s.stats['calls']} calls" unless s.stats["calls"] == 1
        wait_line(T::RUN_DIR_B, /transfer: receiving is off; the transfer app ends/, mark,
                  "the transfer app ending (no enabled line)", timeout: 25)
        wait_gone
        out << "no enabled line: refused at begin, the transfer app ended"
        # on again; off while a file is coming in
        mark = machine_mark
        shell_line("areceive on")
        wait_line(T::RUN_DIR_B, /Transfer app connected as #{T::NODE_B}/, mark, "the transfer app after areceive on",
                  timeout: 30)
        bytes = random(524_288, 8)
        said = nil
        s = pc_send("q-switch3.bin", bytes, pace: PACE_S) do |x|
          next if said || x.pos < 65_536
          said = x.pos
          shell_line("areceive off")
        end
        name = must_place(s, "switch (off on the way)")
        must_have("/home/inbox/#{name}", bytes)
        s = pc_send("q-switch4.txt", "x" * 100)
        raise T::Failure, "switch: after off: #{s.ok? ? 'taken' : s.reason}" unless !s.ok? && s.reason.include?("receiving is off")
        wait_line(T::RUN_DIR_B, /transfer: receiving is off; the transfer app ends/, mark,
                  "the transfer app ending (areceive off)", timeout: 25)
        wait_gone
        raise T::Failure, "switch: areceive off left #{File.read(user_receive)[/enabled.*/]}" unless File.read(user_receive).include?("enabled = false")
        out << "areceive off at #{said}: that file placed, a new one refused, then the transfer app ended"
        out.join("; ")
      end

      SHELL = "default/shell"

      def user_receive = File.join(host_home, "home", "asterism", "receive.toml")

      def shell_line(text)
        native(T::RUN_DIR_B, T::PORT_B, "input", "text", text, "key", "enter")
      end

      def transfer_app?
        app(T::RUN_DIR_B, T::PORT_B, "ps").fetch("apps").any? { |x| x["name"] == "Transfer" }
      end

      def wait_gone
        deadline = now + 10
        sleep 0.2 while transfer_app? && now < deadline
        raise T::Failure, "switch: the transfer app is still there" if transfer_app?
      end

      # The probe of the machine with the job {"api" => api}, until it ends.
      def api_job(api)
        f = File.join(T::OUT, "job.json")
        File.write(f, JSON.generate({ "api" => api }))
        native(T::RUN_DIR_B, T::PORT_B, "file", "put", f, T::JOB)
        mark = machine_mark
        app(T::RUN_DIR_B, T::PORT_B, "spawn", T::PROBE)
        deadline = now + 30
        loop do
          pump
          lines = machine_lines(mark)
          done = lines.find { |l| l =~ /xfer_probe: api done/ }
          if done
            pid = probe_pid(T::RUN_DIR_B, T::PORT_B)
            app(T::RUN_DIR_B, T::PORT_B, "kill", pid.to_s) if pid
            return { end: done[/xfer_probe: .*/], lines: lines }
          end
          raise T::Failure, "the probe's api job did not end in 30 s" if now > deadline
          sleep 0.005
        end
      end

      def keep_logs
        src = File.join(T::RUN_DIR_B, "core.log")
        FileUtils.cp(src, File.join(T::OUT, "quick_b.log")) if File.exist?(src)
        FileUtils.cp(web_console, File.join(T::OUT, "quick_web.log")) if @web && File.exist?(web_console)
        puts "  web: #{@web_hash.join(' / ')}" if @web_hash
      end
    end

    def self.main(argv)
      opts = {}
      OptionParser.new do |o|
        o.banner = "usage: transfer_quick.rb [--web] [--only CASE]"
        o.on("--web") { opts[:web] = true }
        o.on("--only CASE") { |v| opts[:only] = v }
      end.parse!(argv)
      Regress::Tree.need_root!
      at_exit { T.reap }
      Runner.new(opts).run
    end
  end
end

exit(Regress::TransferQuick.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__
