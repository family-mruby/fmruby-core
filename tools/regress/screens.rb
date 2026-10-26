#!/usr/bin/env ruby
# frozen_string_literal: true
#
# screens.rb - scripted screens on the sim, the browser build and the native
# Linux program, compared pixel for pixel with baseline PNGs (rake
# regress:screens, doc/regression/usage.md chapter 9).
#
#   ruby tools/regress/screens.rb                  # retro, modern, web, native
#   ruby tools/regress/screens.rb --only modern    # one family (retro|modern|web|native, or a config name)
#   ruby tools/regress/screens.rb --scene editor   # scenes whose name has this (comma: several)
#   ruby tools/regress/screens.rb --full           # also everything on mruby (retro-mruby, modern-mruby)
#   ruby tools/regress/screens.rb --update         # the last run's shots that failed become the baseline
#   ruby tools/regress/screens.rb --list           # scenes and the configurations they run on
#   ruby tools/regress/screens.rb --attach modern  # scenes against a sim (or with web, a page on
#                                                  # --port) that is already up: no build, nothing
#                                                  # started or stopped (writing scenes)
#
# For each sim configuration: clean_all, `rake build:linux` with the board's
# pinned options (targets.rb FIRMWARE_ENV; the -mruby ones with the kernel,
# desktop and editor on mruby), the three containers headless, the desktop's
# boot, then every scene of that configuration, each from the desktop with
# no app open. For the browser build: `rake spinel:gen wasm:mruby wasm:web`
# (the standard configuration), a development server of its own on --port
# (default 8016, not the 8006 a developer keeps) and a headless browser,
# erased first so the page starts from the shipped settings. For the native
# program: `rake native:mruby native:core` (the standard configuration, after
# the browser build's spinel:gen), started headless as machine "sdl" by
# tools/fmrb_native.rb in a run directory, debug port and user directory of
# its own (a fresh /home and /etc each run), so a developer's copy can stay
# up beside it.
#
# A running sim or a browser started by tools/fmrb_web.rb (someone's) is
# never touched: the run refuses to start. The build tree is moved aside and
# put back (build_tree.rb); wasm/build is left built from the checkout.
require "fileutils"
require "json"
require "optparse"
require "net/http"
require_relative "targets"
require_relative "build_tree"
require_relative "sim_run"
require_relative "screens/image"
require_relative "screens/dsl"
require_relative "screens/drivers"

module Regress
  module Screens
    CORE = File.expand_path("../..", __dir__)
    SCRIPTS = File.join(CORE, "test", "regress", "screens")
    BASELINE = File.join(SCRIPTS, "baseline")
    OUT = File.join(CORE, "tmp", "regress", "screens")
    RESULTS = File.join(OUT, "results.json")
    WEB_PORT = 8016
    WEB_STATE = File.join(CORE, "wasm", "build", "drive_browser.pid")
    EMSDK_ENV = File.expand_path("~/emsdk/emsdk_env.sh")

    # Everything on mruby: the engine_policy's second point.
    MRUBY_ENV = {
      "FMRB_KERNEL_ENGINE" => "mruby",
      "FMRB_APP_ENGINE_DESKTOP" => "mruby",
      "FMRB_APP_ENGINE_EDITOR" => "mruby"
    }.freeze

    # machine: the native program's machine definition (nil = sdl).
    Config = Struct.new(:name, :family, :engine, :kind, :target, :full, :machine, keyword_init: true) do
      def env
        e = target.firmware_env
        engine == "mruby" ? e.merge(MRUBY_ENV) : e
      end

      def size = SimRun::SIZES[family]
    end

    def self.configs
      sim = %w[retro modern].map { |f| [f, TARGETS.find { |t| t.family == f && t.firmware }] }
      std = sim.map { |f, t| Config.new(name: f, family: f, engine: "standard", kind: :sim, target: t, full: false) }
      mr = sim.map { |f, t| Config.new(name: "#{f}-mruby", family: f, engine: "mruby", kind: :sim, target: t, full: true) }
      web = Config.new(name: "web", family: "modern", engine: "standard", kind: :web,
                       target: TARGETS.find { |t| t.name == "wasm" }, full: false)
      native = Config.new(name: "native", family: "modern", engine: "standard", kind: :native,
                          target: TARGETS.find { |t| t.name == "wasm" }, full: false)
      # The same program on the 320x170 screen with the keyboard-only desktop
      # (doc/linux_native, L4): its own scenes (40_keys.rb), same build.
      keys = Config.new(name: "native-keys", family: "modern", engine: "standard", kind: :native,
                        target: TARGETS.find { |t| t.name == "wasm" }, full: false, machine: "sdl_keys")
      std + mr + [web, native, keys]
    end

    def self.load_scenes
      SCENES.clear
      helpers = File.join(SCRIPTS, "helpers.rb")
      load helpers if File.exist?(helpers)
      Dir[File.join(SCRIPTS, "*.rb")].sort.each { |f| load f unless f == helpers }
      SCENES
    end

    class Runner
      # What a runner of another kind of scene (live.rb) changes: where its
      # scenes, baselines and results are, and its session class.
      def scene_list = SCENES
      def session_class = Session
      def out_dir = OUT
      def baseline_dir = BASELINE
      def results_path = File.join(out_dir, "results.json")
      def label = "screens"

      def initialize(opts)
        @opts = opts
        @results = []
        @timings = []
        @errors = []
        @scene_secs = []
      end

      def baseline_path(config, id) = File.join(baseline_dir, config.name, "#{id}.png")
      def out_path(config, id) = File.join(out_dir, "out", config.name, "#{id}.png")
      def diff_path(config, id) = File.join(out_dir, "diff", config.name, "#{id}.png")

      def rel(path) = path.sub("#{CORE}/", "")

      # --- selection ------------------------------------------------------------

      def selected_configs
        all = Screens.configs
        all = all.reject(&:full) unless @opts[:full]
        if @opts[:only]
          want = @opts[:only].split(",").map(&:strip)
          all = all.select { |c| want.include?(c.name) || (c.kind == :sim && want.include?(c.family)) || want.include?(c.kind.to_s) }
          raise "ONLY=#{@opts[:only]}: no such configuration (retro, modern, web, native, native-keys, retro-mruby, modern-mruby)" if all.empty?
        end
        all
      end

      def scenes_for(config)
        list = scene_list.select { |s| s.on.include?(config.name) }
        if @opts[:scene]
          want = @opts[:scene].split(",").map(&:strip)
          list = list.select { |s| want.any? { |w| s.name.include?(w) } }
        end
        list
      end

      # --- running ----------------------------------------------------------------

      def run
        t0 = now
        configs = selected_configs.reject { |c| scenes_for(c).empty? }
        raise "no scene matches" if configs.empty?
        FileUtils.rm_rf(File.join(out_dir, "diff"))
        return run_attached(configs) if @opts[:attach]

        sims = configs.select { |c| c.kind == :sim }
        web = configs.find { |c| c.kind == :web }
        natives = configs.select { |c| c.kind == :native }
        raise "a sim stack is already running (someone's?); leave it or take it down first" if SimRun.core_running?
        check_web_free! if web
        ga = File.join(ROOT, "fmruby-graphics-audio", "build", "fmruby-graphics-audio.elf")
        raise "fmruby-graphics-audio has no Linux build (rake build:linux there first)" if !sims.empty? && !SimRun.elf_x86?(ga)
        tree = BuildTree.new(CORE)
        tree.stash!
        begin
          sims.each { |c| run_sim_config(c, tree) }
          build_web(tree) if web
          build_native(tree, gen: !@web_built) unless natives.empty?
        ensure
          SimRun.down if SimRun.core_running? && !@opts[:keep]
          if @opts[:keep]
            puts "build/: left as is (--keep); put it back with: ruby tools/regress/size.rb --restore"
          else
            puts "build/: #{tree.restore!}"
          end
        end
        run_web_config(web) if web && @web_built
        natives.each { |c| run_native_config(c) } if @native_built
        finish(t0)
      end

      def run_attached(configs)
        t0 = now
        c = configs.find { |x| x.name == @opts[:attach] } || configs.find { |x| x.family == @opts[:attach] }
        raise "--attach #{@opts[:attach]}: not a selected configuration" unless c
        if c.kind == :sim
          raise "no sim is running to attach to" unless SimRun.core_running?
          driver = SimDriver.new(SimRun, tmp_dir)
          driver.settle_baseline!
          run_scenes(c, driver)
        elsif c.kind == :native
          # The developer's copy (tools/fmrb_native.rb up), where it runs.
          driver = NativeDriver.new(tmp_dir, run_dir: "/tmp/fmrb-native-#{Process.uid}",
                                             port: (ENV["FMRB_DEBUG_TCP_PORT"] || 5565).to_i)
          raise "no native program is running to attach to" unless driver.running?
          driver.settle_baseline!
          run_scenes(c, driver)
        else
          driver = WebDriver.new(@opts[:port], tmp_dir)
          driver.tool("reload", timeout: 90) # from the desktop at rest
          settle_page(driver)
          run_scenes(c, driver)
        end
        finish(t0)
      end

      def tmp_dir
        d = File.join(out_dir, "tmp")
        FileUtils.mkdir_p(d)
        d
      end

      def run_sim_config(config, tree)
        FileUtils.mkdir_p(out_dir)
        tree.clean_all!
        blog = File.join(out_dir, "#{config.name}.build.log")
        ok, bsecs = SimRun.run_logged(config.env, [ENV["RAKE"] || "rake", "build:linux"], blog)
        raise "build:linux for #{config.name} failed (#{rel(blog)})" unless ok
        raise "build/fmruby-core.elf is not an x86-64 build" unless SimRun.elf_x86?(File.join(CORE, "build", "fmruby-core.elf"))
        t0 = now
        bring_up(config)
        driver = SimDriver.new(SimRun, tmp_dir)
        driver.settle_baseline!
        run_scenes(config, driver)
        File.write(File.join(out_dir, "#{config.name}.log"), SimRun.core_log)
        @timings << "#{config.name} build #{bsecs.round}s + run #{(now - t0).round}s"
      rescue RuntimeError, DriverError => e
        @errors << "#{config.name}: #{e.message}"
        if SimRun.core_running?
          File.write(File.join(out_dir, "#{config.name}.fail.log"), SimRun.core_log)
          File.write(File.join(out_dir, "#{config.name}.fail.ga.log"), SimRun.sh("docker", "logs", "fmruby_graphics_audio")[1])
        end
      ensure
        SimRun.down if SimRun.core_running? && !(@opts[:keep] && config == selected_configs.select { |c| c.kind == :sim }.last)
      end

      # The first boot after a family switch comes up at the old frame size
      # or not at all (report rg3b.md); one restart fixes either.
      def bring_up(config)
        size = begin
          SimRun.up(config.family)
        rescue RuntimeError
          nil
        end
        if size != config.size
          SimRun.down
          size = SimRun.up(config.family)
        end
        raise "the sim came up at #{size&.join('x') || '?'}, #{config.size.join('x')} expected" unless size == config.size
        SimRun.wait_for(90, "the desktop's boot to finish") { SimRun.core_log.include?(SimRun::BOOT_DONE) }
      end

      def run_scenes(config, driver)
        driver.capture # the screen size, before anything asks for it
        boot = session_class.new(self, config, Scene.new(name: "_boot", on: [], block: nil), driver)
        boot.before_scene if boot.respond_to?(:before_scene)
        @desktop = boot.respond_to?(:after_boot) ? boot.after_boot : nil
        scenes_for(config).each do |scene|
          driver.mark_log!
          t_scene = now
          s = session_class.new(self, config, scene, driver)
          error = nil
          begin
            s.before_scene if s.respond_to?(:before_scene)
            scene.block.call(s)
            error = "nothing compared" unless s.compared?
          rescue Failure, DriverError => e
            error = e.message
          end
          @results << { "config" => config.name, "scene" => scene.name, "error" => error } if error
          clean_up(s, driver)
          @scene_secs << [config.name, scene.name, (now - t_scene).round(1)]
        end
      end

      # Close what the scene opened (the sim: by the process list; the
      # browser build: a reload), then the desktop has to be back as it was
      # after the boot, pixel for pixel: a dialog that leaves a mark behind
      # fails the scene that opened it.
      def clean_up(session, driver)
        driver.close_extra_apps
        return unless @desktop
        session.park if session.respond_to?(:park)
        img = desktop_back(session, session.web? ? 3 : 10)
        if driver.is_a?(WebDriver) && !img.same?(@desktop, session.masks)
          driver.tool("reload", timeout: 90)
          settle_page(driver)
          session.park if session.respond_to?(:park)
          img = desktop_back(session, 30)
        end
        return if img.same?(@desktop, session.masks)
        count, box = @desktop.diff(img, session.masks)
        left = File.join(out_dir, "out", session.config.name, "#{session.scene_name}.left.png")
        FileUtils.mkdir_p(File.dirname(left))
        img.write(left)
        @results << { "config" => session.config.name, "scene" => session.scene_name,
                      "error" => "the desktop did not come back (#{count} px in #{box.inspect}; #{rel(left)})" }
      rescue Failure, DriverError => e
        @errors << "#{session.config.name}: cleaning up after #{session.scene_name}: #{e.message}"
      end

      def desktop_back(session, timeout)
        deadline = now + timeout
        loop do
          img = session.capture
          return img if img.same?(@desktop, session.masks) || now > deadline
          sleep Session::STABLE_GAP
        end
      end

      def record_shot(config, id, img, base, masks, rect)
        out = out_path(config, id)
        FileUtils.mkdir_p(File.dirname(out))
        img.write(out)
        res = { "config" => config.name, "scene" => id, "out" => rel(out), "masks" => masks }
        if base.nil?
          res["status"] = "new"
        else
          count, box = base.diff(img, masks, rect: rect)
          if count.zero?
            res["status"] = "ok"
          else
            res["status"] = "fail"
            res["pixels"] = count
            res["box"] = box
            d = diff_path(config, id)
            FileUtils.mkdir_p(File.dirname(d))
            base.diff_picture(img, masks).write(d)
            res["diff"] = rel(d)
          end
        end
        @results << res
        res
      end

      # --- the browser build ----------------------------------------------------

      def check_web_free!
        raise "a browser started by tools/fmrb_web.rb is open (#{rel(WEB_STATE)}); leave it or close it first" if File.exist?(WEB_STATE)
        raise "port #{@opts[:port]} is in use (someone's development server?); pick another with --port" if port_open?(@opts[:port])
      end

      def port_open?(port)
        Net::HTTP.start("localhost", port, open_timeout: 1, read_timeout: 2) { |h| h.head("/") }
        true
      rescue StandardError
        false
      end

      # spinel:gen with the standard options (the sim builds may have left
      # another configuration's programs), the wasm libmruby (clean_all took
      # it) and the bundle. The fresh libmruby replaces the stashed one, so
      # the developer gets a current one back.
      def build_web(tree)
        log = File.join(out_dir, "web.build.log")
        cfg = Screens.configs.find { |c| c.kind == :web }
        env = cfg.env.reject { |k, _| k == "FMRB_HW_TARGET" }
        cmd = ["bash", "-c", "source #{EMSDK_ENV} >/dev/null 2>&1; " \
                             "#{ENV['RAKE'] || 'rake'} spinel:gen && #{ENV['RAKE'] || 'rake'} wasm:mruby && " \
                             "#{ENV['RAKE'] || 'rake'} wasm:web"]
        ok, secs = SimRun.run_logged(env, cmd, log)
        raise "the browser build failed (#{rel(log)})" unless ok
        tree.keep_built!("family-mruby-wasm")
        @web_build_secs = secs.round
        @web_built = true
      rescue RuntimeError => e
        @errors << "web: #{e.message}"
      end

      def run_web_config(config)
        t0 = now
        port = @opts[:port]
        serve_log = File.join(out_dir, "web.serve.log")
        @serve_pid = Process.spawn({}, ENV["RAKE"] || "rake", "wasm:serve", "PORT=#{port}", chdir: CORE,
                                   in: File::NULL, out: serve_log, err: serve_log, pgroup: true)
        SimRun.wait_for(30, "the development server on port #{port}") { port_open?(port) }
        driver = WebDriver.new(port, tmp_dir)
        driver.tool("up", "--headless", timeout: 60)
        # Erase what an earlier run left in this origin's store (files and
        # settings), so the page starts as shipped.
        driver.tool("reset", timeout: 60)
        settle_page(driver)
        run_scenes(config, driver)
        @timings << "web build #{@web_build_secs}s + run #{(now - t0).round}s"
      rescue RuntimeError, DriverError => e
        @errors << "web: #{e.message}"
      ensure
        if driver && !@opts[:keep]
          begin
            driver.tool("down")
          rescue DriverError
            nil
          end
        end
        if @serve_pid && !@opts[:keep]
          begin
            Process.kill("TERM", -@serve_pid)
          rescue SystemCallError
            nil
          end
          Process.wait(@serve_pid) rescue nil # rubocop:disable Style/RescueModifier
        end
      end

      # --- the native program ---------------------------------------------------

      # spinel:gen when the browser build did not just run it (the sim builds
      # may have left another configuration's programs), the native libmruby
      # (clean_all took it) and the program. The fresh libmruby replaces the
      # stashed one, as the browser's does.
      def build_native(tree, gen:)
        log = File.join(out_dir, "native.build.log")
        cfg = Screens.configs.find { |c| c.kind == :native }
        env = cfg.env.reject { |k, _| k == "FMRB_HW_TARGET" }.merge("NATIVE_ENGINE" => "standard")
        rake = ENV["RAKE"] || "rake"
        steps = gen ? "#{rake} spinel:gen && " : ""
        cmd = ["bash", "-c", "#{steps}#{rake} native:mruby && #{rake} native:core"]
        ok, secs = SimRun.run_logged(env, cmd, log)
        raise "the native build failed (#{rel(log)})" unless ok
        tree.keep_built!("family-mruby-native")
        @native_build_secs = secs.round
        @native_built = true
      rescue RuntimeError => e
        @errors << "native: #{e.message}"
      end

      def run_native_config(config)
        t0 = now
        driver = NativeDriver.new(tmp_dir)
        raise "a native program is already running in #{NativeDriver::RUN_DIR} (an earlier run's?)" if driver.running?
        home = File.join(out_dir, "#{config.name}_home")
        FileUtils.rm_rf(home)
        user = File.join(home, "user")
        cache = File.join(home, "cache")
        FileUtils.mkdir_p([user, cache])
        driver.up(user, cache, machine: config.machine || "sdl")
        driver.settle_baseline!
        run_scenes(config, driver)
        FileUtils.cp(driver.log_path, File.join(out_dir, "#{config.name}.log")) if File.exist?(driver.log_path)
        @timings << "#{config.name} build #{@native_build_secs}s + run #{(now - t0).round}s"
      rescue RuntimeError, DriverError => e
        @errors << "#{config.name}: #{e.message}"
        FileUtils.cp(driver.log_path, File.join(out_dir, "#{config.name}.fail.log")) if driver && File.exist?(driver.log_path)
      ensure
        if driver && !@opts[:keep]
          begin
            driver.down
          rescue DriverError
            nil
          end
        end
      end

      # The page answers before the machine has drawn its desktop: wait for
      # the status to answer twice in a row.
      def settle_page(driver)
        good = 0
        SimRun.wait_for(120, "the page to settle") do
          begin
            driver.tool("status", timeout: 30)
            good += 1
          rescue DriverError
            good = 0
          end
          good >= 2
        end
      end

      # --- the summary ------------------------------------------------------------

      def finish(t0)
        FileUtils.mkdir_p(out_dir)
        File.write(results_path, JSON.pretty_generate("results" => @results, "errors" => @errors,
                                                 "scene_seconds" => @scene_secs, "timings" => @timings))
        shots = @results.select { |r| r["status"] }
        broken = @results.select { |r| r["error"] }
        failed = shots.reject { |r| r["status"] == "ok" }
        per = @results.group_by { |r| r["config"] }.map do |c, rs|
          ok = rs.count { |r| r["status"] == "ok" }
          "#{c} #{ok}/#{rs.size}"
        end
        @errors.each { |e| puts "FAIL #{e}" }
        broken.each { |r| puts "FAIL #{r['config']} #{r['scene']}: #{r['error']}" }
        failed.first(6).each { |r| puts fail_line(r) }
        puts "     ... #{failed.size - 6} more (#{rel(results_path)})" if failed.size > 6
        n_ok = shots.size - failed.size
        n_bad = failed.size + broken.size
        took = now - t0
        puts "#{label}: #{n_ok} passed / #{n_bad} failed (#{per.join(', ')}; #{(took / 60).round(1)} min" \
             "#{@timings.empty? ? '' : ": #{@timings.join(', ')}"})"
        n_bad.zero? && @errors.empty? ? 0 : 1
      end

      def fail_line(r)
        return "NEW  #{r['config']} #{r['scene']}: no baseline (#{r['out']})" if r["status"] == "new"
        b = r["box"]
        "FAIL #{r['config']} #{r['scene']}: #{r['pixels']} px in (#{b[0]},#{b[1]})-(#{b[2]},#{b[3]}) -> #{r['diff']}"
      end

      def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    end

    # --- update and list ----------------------------------------------------------

    def self.update(opts)
      raise "no results to write (run rake regress:screens first)" unless File.exist?(RESULTS)
      doc = JSON.parse(File.read(RESULTS))
      only = opts[:only]&.split(",")&.map(&:strip)
      scene = opts[:scene]&.split(",")&.map(&:strip)
      picked = doc["results"].select do |r|
        next false unless r["status"] && r["status"] != "ok"
        next false if only && !only.any? { |o| r["config"] == o || r["config"].start_with?("#{o}-") }
        next false if scene && !scene.any? { |s| r["scene"].include?(s) }
        true
      end
      if picked.empty?
        puts "screens:update: nothing to write (every shot of the last run matched)"
        return 0
      end
      picked.each do |r|
        dst = File.join(BASELINE, r["config"], "#{r['scene']}.png")
        FileUtils.mkdir_p(File.dirname(dst))
        FileUtils.cp(File.join(CORE, r["out"]), dst)
        puts "baseline: #{r['config']}/#{r['scene']} (#{r['status']})"
      end
      0
    end

    def self.list
      cfgs = configs.map(&:name)
      SCENES.each do |s|
        puts format("%-24s %s", s.name, s.on.select { |o| cfgs.include?(o) }.join(" "))
      end
      bytes = Dir[File.join(BASELINE, "**", "*.png")].sum { |f| File.size(f) }
      puts "#{SCENES.size} scenes; baseline #{Dir[File.join(BASELINE, '**', '*.png')].size} PNGs, #{(bytes / 1024.0).round} KB"
      0
    end

    def self.main(argv)
      opts = { only: ENV["ONLY"], scene: ENV["SCENE"], full: ENV["FULL"] == "1", port: WEB_PORT }
      OptionParser.new do |o|
        o.on("--only LIST") { |v| opts[:only] = v }
        o.on("--scene LIST") { |v| opts[:scene] = v }
        o.on("--full") { opts[:full] = true }
        o.on("--update") { opts[:update] = true }
        o.on("--list") { opts[:list] = true }
        o.on("--attach CONFIG") { |v| opts[:attach] = v }
        o.on("--keep") { opts[:keep] = true }
        o.on("--port N", Integer) { |v| opts[:port] = v }
      end.parse!(argv)
      load_scenes
      return list if opts[:list]
      return update(opts) if opts[:update]
      Runner.new(opts).run
    end
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    exit Regress::Screens.main(ARGV)
  rescue RuntimeError, KeyError, Errno::ENOENT => e
    warn "screens: #{e.message}"
    exit 1
  end
end
