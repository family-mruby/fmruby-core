#!/usr/bin/env ruby
# frozen_string_literal: true
#
# install.rb - apps installed over Asterism (doc/wasm_apps W6; rake
# regress:transfer:install, in regress:full): two native programs (the
# standard engine), A (rg-xfer-a) and B (rg-xfer-b), and this process as a
# PC (rg-xfer-pc, CRuby Asterism and tools/fmrb_send.rb's send_app). B
# installs; its receive.toml takes apps from the PC and A ([app] from).
#
#   auto      install = "auto", but over a plain tcp router (no
#             certificate binds the PC's name): B asks all the same
#             (auto holds only for a certified sender), Install is
#             clicked; the PC's Ruby app (a folder with its
#             .app.toml and its icon) is placed in B's /app/usr/w6rb/ with
#             the icon written as its path there, the desktop is told: the
#             launcher, opened from the desktop's menu, reads /app again and
#             shows it (launcher.png); and the app starts there
#   again     the same app again: refused at begin (installed already);
#             with overwrite (asked, Install) it is replaced
#   wasm_bad  a wasm app that imports a function B's firmware does not have
#             (built, it says, with SDK 9.1.0): refused with the reason,
#             naming the import and the SDK version; nothing is left in
#             /app/usr
#   listed    a sender not in [app] from: refused before a byte is sent
#   ask_yes   install = "ask": A's app (the probe, through its transfer app:
#             FmrbTransfer#send_app) sends the TinyGo hello (a wasm app) to
#             B; B asks its owner (the question's app, a window: who sent
#             it, the name, the language, the size, the SDK version);
#             Install is clicked; A hears where it was installed, and the
#             wasm app starts on B
#   ask_no    the PC's Lua app; the question is closed with Esc: declined,
#             and the PC hears so
#   no_screen B started again into a single app (boot_mode = "app", no
#             desktop): the PC's app is refused at once with the reason
#             ("this board has no screen to ask ...")
#
#   ruby tools/regress/install.rb [--only CASE] [--keep]
#
# The screens of the question are written to tmp/regress/install/
# (ask_yes.png, ask_no.png).
require "fileutils"
require "json"
require_relative "transfer"
require_relative "../fmrb_send"

module Regress
  module Install
    T = Regress::Transfer
    NODE_PC = "rg-xfer-pc"
    OUT = File.join(T::CORE, "tmp", "regress", "install")
    CASES = %w[auto again wasm_bad listed ask_yes ask_no no_screen].freeze

    class Runner < T::Runner
      def initialize(opts)
        super(opts.merge(engine: "standard"))
        @only = opts[:only]
        @results = []
      end

      def router = "tcp/127.0.0.1:#{T::ROUTER_PORT}"
      def home(dir) = File.join(T::OUT, "#{File.basename(dir)}_home", "user")
      def b_usr = File.join(home(T::RUN_DIR_B), "app", "usr")

      def run
        t0 = now
        FileUtils.mkdir_p(OUT)
        FileUtils.mkdir_p(T::OUT)
        ok = true
        begin
          router_up
          up(T::RUN_DIR_B, T::PORT_B, T::NODE_B, NODE_PC, router)
          receive(%W[#{NODE_PC} #{T::NODE_A}], "auto")
          up(T::RUN_DIR_A, T::PORT_A, T::NODE_A, NODE_PC, router)
          wait_line(T::RUN_DIR_B, /Transfer app connected as #{T::NODE_B}/, 0, "B's transfer app connected", timeout: 30)
          make_apps
          FmrbSend.load_asterism
          @net = Asterism.connect(router, node: NODE_PC, app: "fmrb_send")
          CASES.each do |c|
            next if @only && @only != c
            t = now
            begin
              send("case_#{c}")
              @results << [c, true, format("%.1fs", now - t)]
              puts "install: #{c}: ok (#{format('%.1f', now - t)} s)"
            rescue T::Failure => e
              ok = false
              @results << [c, false, e.message]
              puts "install: #{c}: FAILED: #{e.message}"
            end
          end
        rescue T::Failure => e
          ok = false
          puts "install: FAILED: #{e.message}"
        ensure
          @net&.close
          keep_logs
          unless @opts[:keep]
            down(T::RUN_DIR_A, T::PORT_A)
            down(T::RUN_DIR_B, T::PORT_B)
            router_down
          end
        end
        puts "install: #{@results.count { _2 }}/#{@results.size} cases in #{format('%.0f', now - t0)} s" \
             "#{ok ? '' : ' -- FAILED'} (logs: #{T::OUT}/standard_{a,b}.log, screens: #{OUT})"
        ok
      end

      # ---- the apps ----

      def make_apps
        apps = File.join(OUT, "apps")
        FileUtils.rm_rf(apps)
        rb = File.join(apps, "w6rb")
        FileUtils.mkdir_p(rb)
        File.write(File.join(rb, "w6rb.app.rb"), <<~RUBY)
          # An app sent over Asterism (tools/regress/install.rb)
          class W6rbApp < FmrbApp
            def on_create
              Log.info("w6rb: up from \#{__FILE__}")
              clear_user_area
              @gfx.draw_text_mixed(4, 4, "Installed over Asterism", theme_fg)
              @gfx.present
            end
          end
        RUBY
        File.write(File.join(rb, "w6rb.app.toml"),
                   "app_screen_name = \"W6 Ruby\"\nicon = \"w6rb.icon\"\ndefault_window_mode = \"window\"\n" \
                   "default_window_width = 160\ndefault_window_height = 60\n")
        FileUtils.cp(File.join(T::CORE, "flash/usr/share/icon/ruby.png"), File.join(rb, "w6rb.png"))
        @rb = rb
        lua = File.join(apps, "w6lua")
        FileUtils.mkdir_p(lua)
        FileUtils.cp(File.join(T::CORE, "flash/app/lua/lua.app.lua"), File.join(lua, "w6lua.app.lua"))
        File.write(File.join(lua, "w6lua.app.toml"), "app_screen_name = \"W6 Lua\"\ndefault_window_mode = \"window\"\n")
        @lua = lua
        bad = File.join(apps, "w6bad")
        FileUtils.mkdir_p(bad)
        File.binwrite(File.join(bad, "w6bad.app.wasm"), bad_wasm)
        File.write(File.join(bad, "w6bad.app.toml"), "app_screen_name = \"W6 bad\"\nlanguage = \"rust\"\n")
        @bad = bad
        # A's app: the TinyGo hello, in A's /home/w6go/
        go = File.join(home(T::RUN_DIR_A), "home", "w6go")
        FileUtils.mkdir_p(go)
        FileUtils.cp(File.join(T::CORE, "flash/app/tinygo/hello.app.wasm"), File.join(go, "w6go.app.wasm"))
        File.write(File.join(go, "w6go.app.toml"),
                   File.read(File.join(T::CORE, "flash/app/tinygo/hello.app.toml"))
                       .sub("launcher_visible = false", "launcher_visible = true"))
      end

      # A module that imports fmrb.gfx_draw_hologram (no firmware has it) and
      # says it was built with SDK 9.1.0 (the custom section fmrb.sdk).
      def bad_wasm
        name = ->(s) { [s.bytesize].pack("C") + s }
        sec = ->(id, body) { [id, body.bytesize].pack("CC") + body }
        types = sec.call(1, "\x01\x60\x00\x00".b)
        imports = sec.call(2, "\x01".b + name.call("fmrb") + name.call("gfx_draw_hologram") + "\x00\x00".b)
        custom = sec.call(0, name.call("fmrb.sdk") + "9.1.0")
        "\0asm\x01\0\0\0".b + types + imports + custom
      end

      def receive(from, mode)
        list = from.map { "\"#{_1}\"" }.join(", ")
        File.write(File.join(home(T::RUN_DIR_B), "home", "asterism", "receive.toml"),
                   "enabled = true\n\n[app]\nfrom = [#{list}]\ninstall = \"#{mode}\"\n")
      end

      def pc_send(path, overwrite: false)
        out = StringIO.new
        ok = FmrbSend.send_app(@net, T::NODE_B, path, overwrite: overwrite, out: out)
        [ok, out.string]
      end

      # pc_send while B asks its owner: the question answered (:install, a
      # click; :esc) once it is up, its screen kept as shot.png.
      def pc_send_asked(path, name, answer, shot, overwrite: false)
        mark = log_lines(T::RUN_DIR_B).size
        result = nil
        th = Thread.new { result = pc_send(path, overwrite: overwrite) }
        wait_line(T::RUN_DIR_B, /asking the owner about #{name} \(PID \d+\)/, mark, "B asking its owner", timeout: 30)
        if answer == :install
          click_install(T::RUN_DIR_B, T::PORT_B, shot)
        else
          sleep 1.5
          native(T::RUN_DIR_B, T::PORT_B, "shot", File.join(OUT, "#{shot}.png"))
          native(T::RUN_DIR_B, T::PORT_B, "input", "key", "esc")
        end
        th.join(60)
        result || [false, "no answer"]
      end

      # B started again into a single app (boot_mode = "app"): no desktop to
      # ask on. Its home is kept.
      def restart_b_single_app
        down(T::RUN_DIR_B, T::PORT_B)
        conf = File.join(home(T::RUN_DIR_B), "etc", "system_conf.toml")
        text = File.read(conf).sub(/^boot_mode = .*$/, "boot_mode = \"app\"")
                   .sub(/^startup_app = .*$/, "startup_app = \"/app/usr/w6rb/w6rb.app.rb\"")
        File.write(conf, text)
        user = home(T::RUN_DIR_B)
        native(T::RUN_DIR_B, T::PORT_B, "up", "--engine", @engine, "--machine", "sdl", "--debug-port", T::PORT_B.to_s,
               "--user-dir", user, "--cache-dir", File.join(File.dirname(user), "cache"), "--timeout", "90", timeout: 120)
      end

      def expect(what, cond, detail = "")
        raise T::Failure, "#{what}#{detail.empty? ? '' : " (#{detail.strip.lines.last(2).join(' ').strip})"}" unless cond
      end

      # ---- the cases ----

      # install = "auto", but the router here is plain tcp (no certificate
      # binds the PC's name): B asks all the same, and Install is clicked.
      def case_auto
        receive(%W[#{NODE_PC} #{T::NODE_A}], "auto")
        mark = log_lines(T::RUN_DIR_B).size
        ok, said = pc_send_asked(@rb, "w6rb", :install, "auto_uncertified")
        i = wait_line(T::RUN_DIR_B, /the app w6rb from #{NODE_PC}: .*install: ask \(auto, but the sender is not checked by a certificate\)/,
                      mark, "auto turned to ask (no certificate)")
        expect("asked, not placed at once", !i.nil?)
        expect("the PC hears it installed", ok && said.include?("installed on #{T::NODE_B} as /app/usr/w6rb/w6rb.app.rb"), said)
        dir = File.join(b_usr, "w6rb")
        expect("its files in /app/usr/w6rb", Dir.exist?(dir) && Dir.children(dir).sort == %w[w6rb.app.rb w6rb.app.toml w6rb.png],
               Dir.exist?(dir) ? Dir.children(dir).join(" ") : "none")
        toml = File.read(File.join(dir, "w6rb.app.toml"))
        expect("the icon as its path there", toml.include?("icon = \"app/usr/w6rb/w6rb.icon\""), toml)
        expect("no hidden folder left", Dir.children(b_usr).none? { _1.start_with?(".") }, Dir.children(b_usr).join(" "))
        wait_line(T::RUN_DIR_B, /Launcher: w6rb was installed; rescanning when the launcher opens/, mark,
                  "the desktop told")
        # the launcher (the desktop's menu, its first entry) reads /app again
        # as it opens, and shows the app; Esc closes it
        m1 = log_lines(T::RUN_DIR_B).size
        native(T::RUN_DIR_B, T::PORT_B, "input", "move", "30", "5", "sleep", "200", "click", "30", "5", "sleep", "800",
               "click", "30", "19")
        wait_line(T::RUN_DIR_B, %r{Found app: W6 Ruby \(/app/usr/w6rb/w6rb.app.rb\)}, m1, "the launcher's rescan",
                  timeout: 20)
        wait_line(T::RUN_DIR_B, /Launcher: rescan done/, m1, "the rescan's end", timeout: 20)
        native(T::RUN_DIR_B, T::PORT_B, "shot", File.join(OUT, "launcher.png"))
        native(T::RUN_DIR_B, T::PORT_B, "input", "key", "esc")
        m2 = log_lines(T::RUN_DIR_B).size
        app(T::RUN_DIR_B, T::PORT_B, "spawn", "/app/usr/w6rb/w6rb.app.rb")
        wait_line(T::RUN_DIR_B, %r{w6rb: up from /app/usr/w6rb/w6rb.app.rb}, m2, "the installed app started", timeout: 20)
      end

      def case_again
        receive(%W[#{NODE_PC} #{T::NODE_A}], "auto")
        ok, said = pc_send(@rb)
        expect("refused without overwrite", !ok && said.include?("w6rb is installed on #{T::NODE_B} already"), said)
        File.write(File.join(@rb, "w6rb.txt"), "a file more")
        ok, said = pc_send_asked(@rb, "w6rb", :install, "again", overwrite: true)
        FileUtils.rm_f(File.join(@rb, "w6rb.txt"))
        expect("replaced with overwrite", ok, said)
        expect("the new file there", File.exist?(File.join(b_usr, "w6rb", "w6rb.txt")))
      end

      def case_wasm_bad
        receive(%W[#{NODE_PC} #{T::NODE_A}], "auto")
        ok, said = pc_send(@bad)
        expect("refused", !ok, said)
        expect("the reason names the import and the SDK version",
               said.include?("imports fmrb.gfx_draw_hologram") && said.include?("SDK 9.1.0, newer than this firmware"), said)
        expect("nothing in /app/usr", Dir.children(b_usr).sort == ["w6rb"], Dir.children(b_usr).join(" "))
      end

      def case_listed
        receive([T::NODE_A], "auto")
        ok, said = pc_send(@lua)
        expect("refused at begin", !ok && said.include?("takes no app from #{NODE_PC}"), said)
        expect("nothing in /app/usr", !Dir.exist?(File.join(b_usr, "w6lua")))
      end

      def case_ask_yes
        receive(%W[#{NODE_PC} #{T::NODE_A}], "ask")
        kill_probes
        mark_b = log_lines(T::RUN_DIR_B).size
        put_job(T::RUN_DIR_A, T::PORT_A, { "api" => { "app" => true, "to" => T::NODE_B, "path" => "/home/w6go" } })
        mark_a = log_lines(T::RUN_DIR_A).size
        app(T::RUN_DIR_A, T::PORT_A, "spawn", T::PROBE)
        wait_line(T::RUN_DIR_B, /asking the owner about w6go \(PID \d+\)/, mark_b, "B asking its owner", timeout: 30)
        click_install(T::RUN_DIR_B, T::PORT_B, "ask_yes")
        i = wait_line(T::RUN_DIR_A, /xfer_probe: api done/, mark_a, "A's answer", timeout: 30)
        line = log_lines(T::RUN_DIR_A)[i]
        expect("A hears where it was installed", line.include?("api done ok /app/usr/w6go/w6go.app.wasm"), line)
        expect("the wasm app in /app/usr/w6go", File.exist?(File.join(b_usr, "w6go", "w6go.app.wasm")))
        m2 = log_lines(T::RUN_DIR_B).size
        app(T::RUN_DIR_B, T::PORT_B, "spawn", "/app/usr/w6go/w6go.app.wasm")
        sleep 2
        lines = log_lines(T::RUN_DIR_B)[m2..] || []
        bad = lines.find { _1 =~ /w6go.*(did not load|imports|refus|trap)/i }
        expect("the wasm app started on B", bad.nil? && app_running?(T::RUN_DIR_B, T::PORT_B, "TinyGo hello"), bad.to_s)
        kill_probes
      end

      def case_ask_no
        receive(%W[#{NODE_PC} #{T::NODE_A}], "ask")
        ok, said = pc_send_asked(@lua, "w6lua", :esc, "ask_no")
        expect("declined", ok == false && said.to_s.include?("declined on #{T::NODE_B}"), said.to_s)
        expect("nothing in /app/usr", !Dir.exist?(File.join(b_usr, "w6lua")))
        expect("no hidden folder left", Dir.children(b_usr).none? { _1.start_with?(".") }, Dir.children(b_usr).join(" "))
      end

      # The question's window (its .app.toml: at 25,40 of the desktop, 270 x
      # 134): Install is the left of the two buttons at its foot. The
      # window's frame is found from the screen (its title bar is the
      # first row of the frame colour at that x); a shot of it is kept.
      def click_install(dir, port, shot)
        sleep 1.5
        png = File.join(OUT, "#{shot}.png")
        native(dir, port, "shot", png)
        x, y = install_button
        native(dir, port, "input", "click", x.to_s, y.to_s)
      end

      def install_button
        win = ask_window
        # user area: the window less its frame (title bar FRAME_TOP, sides
        # FRAME_SIDE); the button at (w - 156, h - 18), 72 x 15
        [win[0] + win[2] - FRAME_SIDE - 156 + 36, win[1] + win[3] - FRAME_SIDE - 18 + 7]
      end

      FRAME_SIDE = 1

      # x, y, w, h of the question's window
      def ask_window
        a = app(T::RUN_DIR_B, T::PORT_B, "ps").fetch("apps").find { _1["name"].to_s.start_with?("Install an app") }
        raise T::Failure, "no question's window on B" unless a
        @ask_info = a
        [a["x"] || 25, a["y"] || 40, a["w"] || 270, a["h"] || 134]
      end

      # B booted into one app (no desktop): an app it would ask about is
      # refused at once, with the reason, to the PC (auto without a
      # certificate too). Last: B is started again for it.
      def case_no_screen
        receive(%W[#{NODE_PC} #{T::NODE_A}], "auto")
        restart_b_single_app
        wait_line(T::RUN_DIR_B, /Transfer app connected as #{T::NODE_B}/, 0, "B's transfer app connected", timeout: 30)
        t = now
        ok, said = pc_send(@lua)
        expect("refused", !ok && said.include?("this board has no screen to ask"), said)
        expect("at once (no wait for an answer)", now - t < 10, format("%.1f s", now - t))
        expect("nothing in /app/usr", !Dir.exist?(File.join(b_usr, "w6lua")))
      end

      def app_running?(dir, port, name)
        app(dir, port, "ps").fetch("apps").any? { _1["name"].to_s == name }
      end
    end

    def self.main(argv)
      opts = {}
      OptionParser.new do |o|
        o.banner = "usage: install.rb [--only CASE] [--keep]"
        o.on("--only CASE", CASES) { |v| opts[:only] = v }
        o.on("--keep", "leave the programs and the router up") { opts[:keep] = true }
      end.parse!(argv)
      Tree.need_root!
      at_exit { Transfer.reap } unless opts[:keep]
      Runner.new(opts).run
    end
  end
end

require "stringio"
exit(Regress::Install.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__
