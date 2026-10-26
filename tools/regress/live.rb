#!/usr/bin/env ruby
# frozen_string_literal: true
#
# live.rb - scenes where an app talks to someone outside the machine, with
# that someone started by the tool (rake regress:live, doc/regression/
# usage.md chapter 10): a game host's table, a step sender, the serial MIDI
# monitor, the APU's samples. What a scene compares is the other side's
# state (who sits at the table, the moves, the bytes received and their
# spacing, the sound's numbers) summarised by the scene and kept as JSON in
# test/regress/live/baseline/; a scene may take screen shots as well (the
# RG2 shots, PNGs in the same place).
#
#   ruby tools/regress/live.rb                  # every scene (the Modern sim)
#   ruby tools/regress/live.rb --scene othello  # scenes whose name has this (comma: several)
#   ruby tools/regress/live.rb --update         # the last run's failed or new checks become the baseline
#   ruby tools/regress/live.rb --list
#   ruby tools/regress/live.rb --attach modern  # against a sim that is up (writing scenes)
#   ruby tools/regress/live.rb --keep           # leave the sim up (and build/ stashed)
#
# The scenes are written as the screen scenes (tools/regress/screens/dsl.rb,
# test/regress/live/*.rb) with a few more words: s.counterpart, s.check,
# s.expect, s.poll. The runner is the screens runner (build, bring-up, a
# scene from the desktop at rest, the desktop back as it was afterwards);
# the counterparts a scene started are closed after it, also when it fails.
require_relative "screens"
require_relative "counterparts"

module Regress
  module Live
    CORE = File.expand_path("../..", __dir__)
    SCRIPTS = File.join(CORE, "test", "regress", "live")
    BASELINE = File.join(SCRIPTS, "baseline")
    OUT = File.join(CORE, "tmp", "regress", "live")
    RESULTS = File.join(OUT, "results.json")

    SCENES = [] # rubocop:disable Style/MutableConstant

    # The Modern sim by default: it has Asterism, the table apps at their
    # sizes (mahjong at 640x360) and walk_scroller.
    def self.scene(name, on: %w[modern], &block)
      raise ArgumentError, "scene #{name}: a block is needed" unless block
      raise ArgumentError, "scene #{name} is defined twice" if SCENES.any? { |s| s.name == name }
      SCENES << Screens::Scene.new(name: name, on: Array(on).map(&:to_s), block: block,
                                   file: caller_locations(1, 1).first.path)
    end

    def self.helpers(&block)
      Session.class_eval(&block)
    end

    def self.load_scenes
      SCENES.clear
      # The screens' helpers (the desktop, park, the masks) come first.
      shared = File.join(Screens::SCRIPTS, "helpers.rb")
      load shared if File.exist?(shared)
      own = File.join(SCRIPTS, "helpers.rb")
      load own if File.exist?(own)
      Dir[File.join(SCRIPTS, "*.rb")].sort.each { |f| load f unless f == own }
      SCENES
    end

    class Session < Screens::Session
      def initialize(*)
        super
        @counterparts = []
        @checks = []
      end

      attr_reader :checks

      def compared? = !(@shots.empty? && @checks.empty?)

      # Start a counterpart for this scene (tools/regress/counterparts/):
      # it is closed after the scene, whatever happens in it.
      def counterpart(kind, **opts)
        dir = File.join(@runner.out_dir, "counterparts", @scene.name, kind.to_s)
        FileUtils.rm_rf(dir)
        cp = Counterparts.build(kind, dir, **opts)
        @counterparts << cp
        guard { cp.start }
        cp
      end

      def close_counterparts
        @counterparts.reverse_each do |cp|
          cp.close
        rescue StandardError => e
          warn "live: closing #{cp.kind}: #{e.message}"
        end
        @counterparts.clear
      end

      # A counterpart's or the probe's errors are the scene's failures.
      def guard
        yield
      rescue Counterparts::Error, StandardError => e
        raise if e.is_a?(Screens::Failure) || e.is_a?(Screens::DriverError)
        raise Screens::Failure, "#{e.class.name.split('::').last}: #{e.message}"
      end

      # Compare a summary (a Hash of plain values: nothing that moves from
      # run to run) with test/regress/live/baseline/<config>/<scene>.<label>.json.
      # variant: a part of the name, for a summary that legitimately comes
      # out one of a few ways (the colour the host drew).
      def check(label, data, variant: nil)
        id = [@scene.name, label, variant].compact.join(".")
        raise Screens::Failure, "check #{id} twice in one scene" if @checks.any? { |c| c["scene"] == id }
        res = @runner.record_check(@config, id, normalize(data))
        @checks << res
        res
      end

      # Fail the scene unless the block says yes.
      def expect(what)
        ok = guard { yield }
        raise Screens::Failure, "expected #{what}" unless ok
        ok
      end

      # Until the block returns something (not nil or false); that is the
      # result. Errors from the counterpart while waiting count as "not yet".
      def poll(what, timeout: WAIT_TIMEOUT, every: 0.3)
        deadline = now + timeout
        last_err = nil
        loop do
          v = begin
            yield
          rescue Screens::Failure, Screens::DriverError
            raise
          rescue StandardError => e
            last_err = e
            nil
          end
          return v if v
          raise Screens::Failure, "timed out after #{timeout}s waiting for #{what}#{last_err ? " (#{last_err.message})" : ''}" if now > deadline
          sleep every
        end
      end

      private

      def normalize(v)
        JSON.parse(JSON.generate(v))
      end
    end

    class Runner < Screens::Runner
      def scene_list = SCENES
      def session_class = Session
      def out_dir = OUT
      def baseline_dir = BASELINE
      def label = "live"

      def check_path(config, id) = File.join(BASELINE, config.name, "#{id}.json")
      def check_out(config, id) = File.join(OUT, "out", config.name, "#{id}.json")

      def run
        super
      ensure
        Counterparts::Probe.close
      end

      def clean_up(session, driver)
        session.close_counterparts if session.respond_to?(:close_counterparts)
        super
      end

      def record_check(config, id, data)
        out = check_out(config, id)
        FileUtils.mkdir_p(File.dirname(out))
        File.write(out, JSON.pretty_generate(data) + "\n")
        res = { "config" => config.name, "scene" => id, "kind" => "check", "out" => rel(out) }
        base_file = check_path(config, id)
        if File.exist?(base_file)
          lines = Live.diff(JSON.parse(File.read(base_file)), data)
          res["status"] = lines.empty? ? "ok" : "fail"
          res["diff"] = lines unless lines.empty?
        else
          res["status"] = "new"
        end
        @results << res
        res
      end

      def fail_line(r)
        return super unless r["kind"] == "check"
        return "NEW  #{r['config']} #{r['scene']}: no baseline (#{r['out']})" if r["status"] == "new"
        d = r["diff"]
        "FAIL #{r['config']} #{r['scene']}: #{d.first(3).join('; ')}#{d.size > 3 ? " (+#{d.size - 3}, #{r['out']})" : ''}"
      end
    end

    # The differing leaves of two JSON values, as "path: was -> now".
    def self.diff(base, cur, path = "")
      if base.is_a?(Hash) && cur.is_a?(Hash)
        (base.keys | cur.keys).flat_map do |k|
          p = path.empty? ? k : "#{path}.#{k}"
          next ["#{p}: missing (was #{base[k].to_json})"] unless cur.key?(k)
          next ["#{p}: new (#{cur[k].to_json})"] unless base.key?(k)
          diff(base[k], cur[k], p)
        end
      elsif base == cur
        []
      else
        ["#{path.empty? ? '(all)' : path}: #{base.to_json} -> #{cur.to_json}"]
      end
    end

    def self.update(opts)
      raise "no results to write (run rake regress:live first)" unless File.exist?(RESULTS)
      doc = JSON.parse(File.read(RESULTS))
      scene = opts[:scene]&.split(",")&.map(&:strip)
      picked = doc["results"].select do |r|
        r["status"] && r["status"] != "ok" && (scene.nil? || scene.any? { |s| r["scene"].include?(s) })
      end
      if picked.empty?
        puts "live:update: nothing to write (every check and shot of the last run matched)"
        return 0
      end
      picked.each do |r|
        ext = r["kind"] == "check" ? "json" : "png"
        dst = File.join(BASELINE, r["config"], "#{r['scene']}.#{ext}")
        FileUtils.mkdir_p(File.dirname(dst))
        FileUtils.cp(File.join(CORE, r["out"]), dst)
        puts "baseline: #{r['config']}/#{r['scene']}.#{ext} (#{r['status']})"
      end
      0
    end

    def self.list
      SCENES.each { |s| puts format("%-20s %s", s.name, s.on.join(" ")) }
      files = Dir[File.join(BASELINE, "**", "*.{json,png}")]
      kb = (files.sum { |f| File.size(f) } / 1024.0).round
      puts "#{SCENES.size} scenes; baseline #{files.size} files, #{kb} KB"
      0
    end

    def self.main(argv)
      opts = { scene: ENV["SCENE"], port: Screens::WEB_PORT, only: "modern" }
      OptionParser.new do |o|
        o.on("--scene LIST") { |v| opts[:scene] = v }
        o.on("--only LIST") { |v| opts[:only] = v }
        o.on("--update") { opts[:update] = true }
        o.on("--list") { opts[:list] = true }
        o.on("--attach CONFIG") { |v| opts[:attach] = v }
        o.on("--keep") { opts[:keep] = true }
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
    exit Regress::Live.main(ARGV)
  rescue RuntimeError, KeyError, Errno::ENOENT => e
    warn "live: #{e.message}"
    exit 1
  end
end
