# frozen_string_literal: true
#
# checkpoint_web.rb - moving an app between the native program and the
# browser build over Asterism (rake regress:checkpoint unless WEB=0, doc/checkpoint
# F5, doc/regression/usage.md chapter 12). Part of checkpoint.rb's run: its
# Runner includes this, and the rounds run once, after the native rounds,
# with the first configuration's native program and the run's own router
# (its ws/ port, which the page reaches as zenoh-pico's WebSocket link).
#
# The browser build is the published one (it carries the gem; its bundle is
# wasm/build/dist, what rake wasm:dist makes), served by a
# development server of the run's own (rake wasm:serve DIST=1 on WEB_PORT)
# and opened by a headless Chromium of the run's own: a Linux one
# (FMRB_CHROME, /usr/bin's, or Playwright's in ~/.cache/ms-playwright), a
# profile under tmp/regress/checkpoint and its console in a file. Neither
# tools/fmrb_web.rb's `up` (its state file is the one a developer's page
# uses) nor a page someone has open is touched; the page is driven through
# the run's own server with fmrb_web.rb --port (put, reload, text, key).
#
# The page gets /home/asterism (config.toml: the router and the node ID
# rg-ckpt-web; receive.toml: images from rg-ckpt-a only) through the page's
# filesystem (/flash/home is the machine's /home) and is reloaded, so its
# kernel starts the transfer app at boot. ?app=default/shell opens the shell.
#
#   web move       the counter from the native program into the browser
#                  (asked first, held, carried, started there, closed here),
#                  then back with the browser shell's `move PID rg-ckpt-a`:
#                  every counter goes on from the next step both ways.
#   web calls      ckpt_calls into the browser: the browser build has no
#                  linux_settings, it is named missing and moves all the same.
#   web requires   ckpt_requires (requires linux_settings): the browser
#                  refuses it when asked, before it stops.
#   web late       the counter with a hold of 1 ms: it goes on here, the copy
#                  that comes up in the browser is stopped there.
require "fileutils"
require "json"
require "net/http"
require "open3"
require_relative "tree"

module Regress
  module Checkpoint
    module WebRounds
      # the page's server: 8067 from the shared checkout, else the tree's own (tree.rb)
      WEB_PORT = Tree.port(8067, :ckpt_web)
      NODE_WEB = "rg-ckpt-web"
      NODE_A = AsterismRounds::NODE_A
      ROUTER_WS_PORT = AsterismRounds::ROUTER_WS_PORT
      WEB_DIST = File.join(CORE, "wasm", "build", "dist")
      CALLS_APP = "/app/test/ckpt_calls.app.rb"
      EMSDK_ENV = File.expand_path("~/emsdk/emsdk_env.sh")

      def web_tool_path = File.join(ROOT, "tools", "fmrb_web.rb")
      def web_profile = File.join(OUT, "web_profile")
      def web_console = File.join(OUT, "web.chrome.log")

      def web_check!
        @chrome = web_chrome or
          raise Failure, "web: no Linux Chromium (set FMRB_CHROME, or `npx playwright install chromium`)"
        raise Failure, "web: port #{WEB_PORT} is in use (someone's development server?)" if web_port_open?
      end

      def web_build
        log = File.join(OUT, "web.build.log")
        cmd = "source #{EMSDK_ENV} >/dev/null 2>&1; rake spinel:gen && " \
              "rake wasm:mruby && rake wasm:dist"
        out, st = Open3.capture2e("bash", "-c", cmd, chdir: CORE)
        File.write(log, out)
        raise Failure, "web: the browser build failed (#{log.sub("#{CORE}/", '')})" unless st.success?
      end

      def run_web(config)
        t0 = now
        raise Failure, "web: no browser bundle (#{WEB_DIST.sub("#{CORE}/", '')}); run without NO_BUILD" unless
          File.exist?(File.join(WEB_DIST, "index.html"))
        down_if_running
        home = File.join(OUT, "web_native_home")
        FileUtils.rm_rf(home)
        FileUtils.mkdir_p(%w[user cache].map { |d| File.join(home, d) })
        asterism_home(File.join(home, "user"), NODE_A, NODE_WEB)
        native("up", "--engine", config, "--machine", "sdl", "--user-dir", File.join(home, "user"),
               "--cache-dir", File.join(home, "cache"), "--timeout", "90", timeout: 120)
        web_up
        wait_line(/Transfer app connected as #{NODE_A}/, 0, "the native program's transfer app connected", timeout: 30)
        notes = [web_round_move, web_round_calls, web_round_requires, web_round_late]
        "web (#{config}): ok (#{notes.join('; ')}; #{(now - t0).round}s)"
      rescue Failure => e
        "web (#{config}): FAILED #{e.message}"
      ensure
        FileUtils.cp(log_path, File.join(OUT, "web_native.log")) if File.exist?(log_path)
        web_down
        down_if_running
      end

      private

      def web_chrome
        return ENV["FMRB_CHROME"] if ENV["FMRB_CHROME"] && File.executable?(ENV["FMRB_CHROME"])
        found = %w[/usr/bin/google-chrome /usr/bin/chromium /usr/bin/chromium-browser].find { |p| File.executable?(p) }
        found || Dir.glob(File.expand_path("~/.cache/ms-playwright/chromium-*/chrome-linux*/chrome"))
                    .max_by { |p| p[/chromium-(\d+)/, 1].to_i }
      end

      def web_port_open?
        Net::HTTP.start("localhost", WEB_PORT, open_timeout: 1, read_timeout: 2) { |h| h.head("/") }
        true
      rescue StandardError
        false
      end

      def web_up
        serve_log = File.join(OUT, "web.serve.log")
        @web_serve = Process.spawn({ "DIST" => "1", "PORT" => WEB_PORT.to_s },
                                   ENV["RAKE"] || "rake", "wasm:serve", chdir: CORE,
                                   in: File::NULL, out: serve_log, err: serve_log, pgroup: true)
        deadline = now + 30
        until web_port_open?
          raise Failure, "web: the development server did not come up on #{WEB_PORT}" if now > deadline
          sleep 0.3
        end
        FileUtils.rm_rf(web_profile)
        url = "http://localhost:#{WEB_PORT}/?autostart=1&drive=1&app=default/shell" \
              "&zenoh=ws/127.0.0.1:#{ROUTER_WS_PORT}"
        @web_chrome = Process.spawn(@chrome, "--headless=new", "--user-data-dir=#{web_profile}", "--no-first-run",
                                    "--no-default-browser-check", "--disable-gpu", "--mute-audio",
                                    "--enable-logging=stderr", "--v=0", url,
                                    in: File::NULL, out: File::NULL, err: web_console, pgroup: true)
        web_wait(/Boot complete/, 0, "the browser build's boot", timeout: 60)
        dir = File.join(OUT, "web_asterism")
        write_asterism(dir, NODE_WEB, NODE_A, "ws/127.0.0.1:#{ROUTER_WS_PORT}")
        %w[config.toml receive.toml].each do |f|
          web_tool("put", File.join(dir, f), "/flash/home/asterism/#{f}")
        end
        mark = web_lines.size
        web_tool("reload", timeout: 90)
        web_wait(/Transfer app connected as #{NODE_WEB}/, mark, "the browser's transfer app connected", timeout: 60)
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
        out, st = Open3.capture2e(Gem.ruby, web_tool_path, "--port", WEB_PORT.to_s, "--timeout", timeout.to_s, *args,
                                  chdir: ROOT)
        raise Failure, "web: fmrb_web.rb #{args.first}: #{out.strip.lines.last(2).join}" unless st.success?
        out
      end

      # The page's console, one message per line (Chromium's --enable-logging).
      def web_lines
        return [] unless File.exist?(web_console)
        File.readlines(web_console, chomp: true).filter_map do |l|
          l[/INFO:CONSOLE[^\]]*\] "(.*)", source: /, 1]
        end
      end

      def web_wait(re, from, what, timeout: 15)
        deadline = now + timeout
        loop do
          lines = web_lines
          i = (from...lines.size).find { |k| lines[k] =~ re }
          return i if i
          raise Failure, "web: no #{what} in the page's console within #{timeout}s" if now > deadline
          sleep 0.2
        end
      end

      def web_timings(lines)
        carried = lines.find { |l| l =~ /transfer: .* carried in (\d+) ms/ }.to_s[/carried in (\d+) ms/, 1]
        held = lines.find { |l| l =~ /held (\d+) ms after its image; closes/ }.to_s[/held (\d+) ms/, 1]
        raise Failure, "web: no timings (#{carried.inspect}, #{held.inspect})" unless carried && held
        "carried #{carried} ms, held #{held} ms"
      end

      # Into the browser and back with its shell.
      def web_round_move
        pid = start_counter
        mark = log_lines.size
        wmark = web_lines.size
        r = asterism_move(pid, NODE_WEB)
        raise Failure, "web move: answered #{r.inspect}" unless r["status"] == "moved" && r["pid"].to_i.positive?
        raise Failure, "web move: the browser lacks #{r['missing'].inspect}" if r["missing"]
        there = r["pid"].to_i
        wr = web_wait(/ckpt_counter: on_restore /, wmark, "on_restore in the browser")
        web_wait(/ckpt_counter: b=\d+ /, wr + 1, "a step of b in the browser")
        web_wait(/ckpt_counter: a=\d+ /, wr + 1, "a step of a in the browser")
        wait_line(/\[Ckpt Counter gen=\d+\] Task exiting normally/, mark, "the native program's copy closing")
        a = log_lines
        w = (mark...a.size).find { |k| a[k] =~ /checkpoint written: / } or raise Failure, "web move: no 'checkpoint written'"
        b = web_lines
        c1 = continuity_of(a[0...w], b[(wr + 1)..], a[w..], b[wr], "web move (into the browser)")
        t1 = web_timings(a[mark..])
        raise Failure, "web move: the native program still runs the counter" if ps.any? { |x| x["name"] == "Ckpt Counter" }

        sleep 2
        mark = log_lines.size
        wmark = web_lines.size
        web_tool("text", "move #{there} #{NODE_A}", "key", "enter")
        web_wait(/move: PID #{there} moved to #{NODE_A} \(PID \d+ there\)/, wmark, "the browser shell's answer", timeout: 30)
        nr = wait_line(/ckpt_counter: on_restore /, mark, "on_restore in the native program")
        wait_line(/ckpt_counter: a=\d+ /, nr + 1, "a step of a in the native program")
        wait_line(/ckpt_counter: b=\d+ /, nr + 1, "a step of b in the native program")
        web_wait(/\[Ckpt Counter gen=\d+\] Task exiting normally/, wmark, "the browser's copy closing")
        b = web_lines
        w2 = (wmark...b.size).find { |k| b[k] =~ /checkpoint written: / } or raise Failure, "web move back: no 'checkpoint written'"
        a = log_lines
        c2 = continuity_of(b[0...w2], a[(nr + 1)..], b[w2..], a[nr], "web move (back)")
        t2 = web_timings(b[wmark..])
        kill_counter
        "web move: in a #{c1[:a]}, main #{c1[:main]}, t step #{c1[:t_step]} ms, #{t1}, #{c1[:bytes]} B; " \
          "back a #{c2[:a]}, main #{c2[:main]}, t step #{c2[:t_step]} ms, #{t2}, #{c2[:bytes]} B"
      end

      def web_round_calls
        from = log_lines.size
        pid = app("spawn", CALLS_APP).fetch("pid")
        wait_line(/ckpt_calls: main=2\b/, from, "ckpt_calls counting")
        wmark = web_lines.size
        r = asterism_move(pid, NODE_WEB)
        unless r["status"] == "moved" && r["missing"] == ["linux_settings"]
          raise Failure, "web calls: answered #{r.inspect}"
        end
        web_wait(/\[Ckpt Calls\] the program calls 1 method\(s\) this build does not have: linux_settings /, wmark,
                 "the warning in the browser")
        i = web_wait(/ckpt_calls: on_restore main=\d+/, wmark, "on_restore in the browser")
        web_wait(/ckpt_calls: main=\d+ pack=\S+ linux_settings=false/, i + 1, "a step in the browser, without linux_settings")
        "web calls: moved, missing linux_settings (#{r['took_ms']} ms)"
      end

      def web_round_requires
        from = log_lines.size
        pid = app("spawn", REQUIRES).fetch("pid")
        wait_line(/ckpt_requires: main=2\b/, from, "ckpt_requires counting")
        mark = log_lines.size
        r = asterism_move(pid, NODE_WEB)
        unless r["status"] == "stayed" && r["here"] == "not stopped" && r["reason"].to_s.include?("linux_settings")
          raise Failure, "web requires: answered #{r.inspect}"
        end
        not_stopped(pid, mark, "web requires", "ckpt_requires")
        app("kill", pid.to_s)
        sleep 0.3
        "web requires: refused before it stopped"
      end

      def web_round_late
        pid = start_counter
        mark = log_lines.size
        wmark = web_lines.size
        r = asterism_move(pid, NODE_WEB, "--hold", "1")
        raise Failure, "web late: answered #{r.inspect}" unless r["status"] == "stayed" && r["here"] == "went on"
        c = goes_on(mark, "web late")
        web_wait(/transfer: #{NODE_A} cancels \S+; stopping PID \d+/, wmark, "the copy in the browser stopped")
        web_wait(/\[Ckpt Counter gen=\d+\] Task exiting normally/, wmark, "the browser's copy closing")
        raise Failure, "web late: the native program's counter is gone" unless ps.any? { |x| x["pid"] == pid }
        kill_counter
        "web late: went on here (a #{c[:a]}, main #{c[:main]}), the copy in the browser stopped"
      end
    end
  end
end
