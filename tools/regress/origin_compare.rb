#!/usr/bin/env ruby
# frozen_string_literal: true
#
# origin_compare.rb - a one-off for doc/codebase_tidy T4 (the drawing origin
# becomes the area the app owns): every app the launcher can open, opened on
# the sim (Retro and Modern, the standard build) and shot once, before and
# after the change, and the two sets compared pixel for pixel. The screen
# baselines (rake regress:screens) hold some 16 apps; this covers the rest.
#
#   ruby tools/regress/origin_compare.rb before      # shots of the tree as it is now
#   ruby tools/regress/origin_compare.rb after       # shots again, compared with "before"
#   ruby tools/regress/origin_compare.rb after --only retro --scene robo
#
# Shots go to tmp/regress/origin/<phase>/out/<family>/<app>.png; "after"
# writes diffs to tmp/regress/origin/after/diff/ and its results.json lists
# every app with its status. An app that does not settle (an animation) is
# shot anyway and marked "moving" in the results: its difference says
# nothing by itself, look at the two pictures. The menu bar (the clock) is
# left out of the waits and of the comparison.
#
# Built on screens.rb's runner (the build, the bring-up, the desktop check
# after each app), with a scene per app made here instead of written.
require_relative "screens"
require_relative "mini_toml"

module Regress
  module OriginCompare
    CORE = Screens::CORE
    OUT = File.join(CORE, "tmp", "regress", "origin")
    APP_DIR = File.join(CORE, "flash", "app")
    # Nothing to see (a compile error closes at once, a busy loop pins the
    # sim) or nothing a shot can hold.
    # test/json_probe parses for long enough that the debug server stops
    # answering (it wedged both families on the first run).
    SKIP = %w[debug/compile_err test/busy test/crash_soon test/spin_hog test/json_probe].freeze

    # The app's window mode: the .app.toml beside it wins, else the
    # #---fmrb header at its top (doc/app-distribution/spec.md 3.1).
    def self.mode_of(file)
      toml = file.sub(/\.app\.\w+\z/, ".app.toml")
      if File.exist?(toml)
        begin
          m = MiniToml.parse(File.read(toml))["default_window_mode"]
          return m.to_s if m
        rescue StandardError
          nil
        end
      end
      head = File.read(file, 2048).to_s
      if head.start_with?("#---fmrb") && (m = head[/^#\s*default_window_mode\s*=\s*"(\w+)"/, 1])
        return m
      end
      "window"
    end

    def self.apps
      Dir[File.join(APP_DIR, "**", "*.app.{rb,py,lua}")].sort.filter_map do |f|
        rel = f.sub("#{APP_DIR}/", "")
        id = rel.sub(/\.app\.\w+\z/, "")
        id = File.dirname(id) if File.basename(id) == File.basename(File.dirname(id))
        next if SKIP.include?(id)
        next if mode_of(f) == "background"
        { id: id.tr("/", "_"), path: "/app/#{rel}", file: "flash/app/#{rel}" }
      end
    end

    class Runner < Screens::Runner
      def initialize(opts, phase)
        super(opts)
        @phase = phase
      end

      def label = "origin #{@phase}"
      def out_dir = File.join(OUT, @phase)
      # "after" compares with what "before" shot; "before" has nothing to
      # compare with (every shot is "new").
      def baseline_dir = @phase == "after" ? File.join(OUT, "before", "out") : File.join(OUT, "none")

      def selected_configs
        all = Screens.configs.select { |c| c.kind == :sim && !c.full }
        if @opts[:only]
          want = @opts[:only].split(",")
          all = all.select { |c| want.include?(c.family) }
        end
        all
      end

      # A family switch can leave the first boot waiting on graphics-audio
      # for good: one more try from scratch.
      def bring_up(config)
        super
      rescue RuntimeError
        SimRun.down
        super
      end

      # One app that wedges the sim (the debug server stops answering) is
      # one app's result: the sim is brought up again and the list goes on.
      def run_scenes(config, driver)
        @pending = nil
        list = scenes_for(config)
        done = []
        loop do
          rest = list.reject { |sc| done.include?(sc.name) }
          break if rest.empty?
          @pending = rest
          begin
            super(config, driver)
            break
          rescue Screens::DriverError, RuntimeError => e
            last = @results.reverse.find { |r| r["config"] == config.name }
            done.concat(@results.select { |r| r["config"] == config.name }.map { |r| r["scene"] })
            stuck = rest.find { |sc| !done.include?(sc.name) }
            if stuck
              @results << { "config" => config.name, "scene" => stuck.name, "error" => "the sim stopped answering: #{e.message.strip}" }
              done << stuck.name
            end
            warn "origin: #{config.name}: #{e.message.strip} after #{last && last['scene']}; bringing the sim up again"
            SimRun.down
            bring_up(config)
            driver.settle_baseline!
          end
        end
      end

      def scenes_for(config)
        @pending ? @pending.select { |s| s.on.include?(config.name) } : super
      end

      def scene_list
        @scene_list ||= OriginCompare.apps.map do |a|
          on = Screens.configs.select { |c| c.kind == :sim && !c.full && c.target.carries?(a[:file], CORE) }.map(&:name)
          Screens::Scene.new(name: a[:id], on: on, block: lambda { |s|
            # the desktop's menu bar: the clock ticks every second
            s.mask(0, 0, 640, 11)
            s.launch(a[:path])
            moving = false
            begin
              s.wait_stable(timeout: 6, quiet: 1.0)
            rescue Screens::Failure
              moving = true
            end
            res = s.shot(timeout: 2, quiet: 0.5)
            res["moving"] = true if moving
          })
        end
      end
    end

    def self.main(argv)
      phase = argv.shift
      raise "usage: origin_compare.rb before|after [--only retro|modern] [--scene NAME]" unless %w[before after].include?(phase)
      opts = { port: Screens::WEB_PORT }
      OptionParser.new do |o|
        o.on("--only LIST") { |v| opts[:only] = v }
        o.on("--scene LIST") { |v| opts[:scene] = v }
      end.parse!(argv)
      # only what this run shoots again: a family left out keeps its shots
      fams = opts[:only] ? opts[:only].split(",") : %w[retro modern]
      fams.each { |f| FileUtils.rm_rf(File.join(OUT, phase, "out", f)) } unless opts[:scene]
      Runner.new(opts, phase).run
    end
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    exit Regress::OriginCompare.main(ARGV)
  rescue RuntimeError, KeyError, Errno::ENOENT => e
    warn "origin_compare: #{e.message}"
    exit 1
  end
end
