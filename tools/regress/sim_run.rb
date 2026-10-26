#!/usr/bin/env ruby
# frozen_string_literal: true
#
# sim_run.rb - memory numbers from a fixed sim scenario (rake regress:run,
# doc/regression/usage.md).
#
#   ruby tools/regress/sim_run.rb               # Retro and Modern: build, boot, scenario, compare
#   ruby tools/regress/sim_run.rb --only modern # one family
#   ruby tools/regress/sim_run.rb --table       # no sim: compare the last saved results
#   ruby tools/regress/sim_run.rb --update      # the saved results become the baseline
#   ruby tools/regress/sim_run.rb --calibrate 5 # one build, the scenario 5 times: spreads
#   ruby tools/regress/sim_run.rb --apply       # the last calibration becomes the baseline
#
# For each family (the first board of that family with firmware in
# targets.rb): clean_all, `rake build:linux` with that board's
# FMRB_HW_TARGET and the pinned options (FIRMWARE_ENV), the three
# containers headless, then the scenario:
#
#   boot -> the desktop's "Boot complete" -> snapshot (idle)
#   -> spawn SCENARIO_APPS one by one -> snapshot (running)
#   -> kill them -> snapshot (closed)
#
# A snapshot is the debug server's `ps` (what sim_app ps shows): each app's
# VM pool in use and its task stack's free high-water mark, the kernel, the
# desktop and (Modern) the service host included. From the core's log comes
# each mruby app's VM pool right after compiling (used and peak).
#
# What the sim cannot give, and stays with the boards (RG4): the M1| boot
# lines (fmrb_alloc.c leaves them out of a Linux build: no internal RAM to
# tell apart) and the periodic dump's "IRAM free" (the 10 s dump is not
# printed by the sim; see doc/regression/report/rg3b.md).
#
# The numbers move a little from run to run (how far the GC got when the
# snapshot is taken), so each one is compared with a tolerance (TOLERANCE
# below, why in doc/regression/report/rg3b.md): growth beyond it fails,
# anything else is shown only when it moved past it. A running stack
# (someone's sim) is never touched: the run refuses to start. The
# developer's build tree is moved aside and put back (build_tree.rb).
#
# Calibration (--calibrate N): each family is built once and the scenario
# run N times on it (the stack taken down and brought up between runs).
# Each number's spread gives a proposal for the absolute part of its kind's
# tolerance (calibrate.rb, CALIBRATE below; the relative part is kept), the
# per-number medians a baseline candidate: tmp/regress/sim/<family>.calibrate.json
# and a few lines of summary. --apply writes them to the baseline, the
# tolerance as the family's own (it overrides the file's default).
require "fileutils"
require "json"
require "open3"
require "optparse"
require_relative "targets"
require_relative "build_tree"
require_relative "calibrate"

module Regress
  CORE = File.expand_path("../..", __dir__) unless const_defined?(:CORE)

  module SimRun
    OUT = File.join(CORE, "tmp", "regress", "sim")
    BASELINE = File.join(CORE, "test", "regress", "sim_baseline.json")
    ROOT = File.expand_path("..", CORE) # family-mruby: docker-compose.yml
    COMPOSE = %w[docker compose -f docker-compose.yml -f docker-compose.headless.yml].freeze
    DBG = File.join(CORE, "tool", "debug", "fmrb_dbg_client.py")
    BOOT_MARKER = "main_loop started"
    BOOT_DONE = "Boot complete: switching to heartbeat"
    HOST_TIMEOUT = "waiting host timeout"
    SETTLE = 3 # seconds after each step before the next one
    # Apps both families carry and that need no network, sensor or input to
    # come up: an FmrbUI app, the MML player, StackChan Remote (buttons that
    # publish) and a Spinel program.
    SCENARIO_APPS = %w[
      /app/demo/kamon.app.rb
      /app/demo/mml.app.rb
      /app/demo/stackchan_remote.app.rb
      /app/debug/spinel_hello.app.rb
    ].freeze
    SIZES = { "retro" => [320, 240], "modern" => [426, 240] }.freeze
    # [relative, absolute bytes]: a value may move by the larger of the two.
    # Measured spread over 3 runs x 2 families (report rg3b.md): the apps the
    # scenario starts, their compile numbers and the desktop moved by 0-88 B,
    # the kernel and the service host (collected on their own schedule) by up
    # to 1.9 KB, stack high-water marks by up to 276 B.
    TOLERANCE = {
      "app_used" => [0.0, 512],
      "gc_used" => [0.03, 4096],
      "stack_free" => [0.0, 512],
      "compile" => [0.0, 256]
    }.freeze
    # How a calibration turns a spread into the absolute part of a tolerance:
    # about twice the spread, rounded up, never below the floor. The floors
    # are the RG3b choices for numbers that did not move (512 B for apps,
    # 256 B after compiling) and, for the GC-timed pools, the 4 KB a few runs
    # may not show (a GC that ran in none of them).
    CALIBRATE = {
      "app_used" => { factor: 2, unit: 256, floor: 512 },
      "gc_used" => { factor: 2, unit: 512, floor: 4096 },
      "stack_free" => { factor: 2, unit: 256, floor: 512 },
      "compile" => { factor: 2, unit: 256, floor: 256 }
    }.freeze
    # VMs whose pool follows their own GC rather than the scenario.
    GC_TIMED = %w[fmrb_kernel Services].freeze
    # Not compared, only kept: right after the apps close, the kernel still
    # holds their garbage or has already collected it (161 KB vs 200 KB on
    # the same build).
    UNCOMPARED = ["closed fmrb_kernel used"].freeze

    module_function

    def families(only)
      fams = %w[retro modern].filter_map do |f|
        t = TARGETS.find { |x| x.family == f && x.firmware }
        t && [f, t]
      end
      return fams if only.nil? || only.empty?
      want = only.split(",").map { |w| w.strip.downcase }
      sel = fams.select { |f, t| want.include?(f) || want.include?(t.firmware.downcase) || want.include?(t.name) }
      raise "ONLY=#{only}: no such family (retro, modern)" if sel.empty?
      sel
    end

    def sh(*cmd, chdir: ROOT)
      out, st = Open3.capture2e(*cmd, chdir: chdir)
      [st.success?, out]
    end

    def run_logged(env, cmd, log, chdir: CORE)
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      ok = File.open(log, "w") { |f| system(env, *cmd, chdir: chdir, out: f, err: f) }
      [ok, Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0]
    end

    def core_running?
      ok, out = sh("docker", "inspect", "-f", "{{.State.Running}}", "fmruby_core")
      ok && out.strip == "true"
    end

    def core_log
      _, out = sh("docker", "logs", "fmruby_core")
      out
    end

    def elf_x86?(path)
      File.exist?(path) && File.binread(path, 20)[18, 2].unpack1("v") == 0x3E
    end

    def wait_for(timeout, what)
      t_end = Time.now + timeout
      loop do
        v = yield
        return v if v
        raise "timed out after #{timeout}s waiting for #{what}" if Time.now > t_end
        sleep 0.5
      end
    end

    def dbg(*args)
      ok, out = sh("python3", DBG, "--json", "localhost", *args)
      doc = (JSON.parse(out.lines.last.to_s) rescue nil)
      raise "debug client #{args.join(' ')}: #{out.strip[-300..] || out}" unless ok && doc && !doc["error"]
      doc
    end

    def screen_size
      png = File.join(OUT, "screen.png")
      ok, = sh("python3", File.join(ROOT, "tools", "fmrb_screenshot.py"), "--wait", "10", png)
      return nil unless ok && File.exist?(png)
      head = File.binread(png, 24)
      [head[16, 4].unpack1("N"), head[20, 4].unpack1("N")]
    end

    def up(family)
      ok, out = sh(*COMPOSE, "up", "-d")
      raise "docker compose up failed: #{out.strip[-500..]}" unless ok
      wait_for(90, "the boot marker") do
        raise "fmruby_core exited during boot (#{core_log.lines.last(5).join.strip})" unless core_running?
        log = core_log
        raise "the core gave up waiting for graphics-audio" if log.include?(HOST_TIMEOUT)
        log.include?(BOOT_MARKER)
      end
      sleep 3
      screen_size
    end

    def down
      sh(*COMPOSE, "down")
    end

    # --- one family ---------------------------------------------------------

    def ps
      dbg("ps").fetch("apps").to_h do |a|
        [a["name"], { "used" => a["mem_used"], "total" => a["mem_total"], "stack_free" => a["stack_hw"] }]
      end
    end

    def scenario(family, t, tree)
      bsecs = build(family, t, tree)
      res = boot_and_run(family, t)
      res["build_seconds"] = bsecs.round
      res
    end

    def build(family, t, tree)
      FileUtils.mkdir_p(OUT)
      tree.clean_all!
      blog = File.join(OUT, "#{family}.build.log")
      ok, bsecs = run_logged(t.firmware_env, [ENV["RAKE"] || "rake", "build:linux"], blog)
      raise "build:linux for #{t.firmware} failed (#{blog.sub("#{CORE}/", '')})" unless ok
      raise "build/fmruby-core.elf is not an x86-64 build after build:linux" unless elf_x86?(File.join(CORE, "build", "fmruby-core.elf"))
      bsecs
    end

    # One boot of the built family and the scenario on it; the core's log
    # goes to <log>.
    def boot_and_run(family, t, log_name: "#{family}.log")
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      # graphics-audio applies a new frame-buffer size on the next boot, so
      # the first boot after a family switch comes up at the old size, or
      # does not come up at all (the core waits for an INIT_DISPLAY answer
      # that never comes). One restart fixes either.
      size = begin
        up(family)
      rescue RuntimeError
        nil
      end
      if size != SIZES[family]
        down
        size = up(family)
      end
      raise "the sim came up at #{size&.join('x') || '?'}, #{SIZES[family].join('x')} expected" unless size == SIZES[family]
      wait_for(60, "the desktop's boot to finish") { core_log.include?(BOOT_DONE) }
      sleep SETTLE
      points = { "idle" => ps }
      pids = SCENARIO_APPS.map do |path|
        pid = dbg("spawn", "path=#{path}")["pid"]
        sleep SETTLE
        pid
      end
      points["running"] = ps
      pids.each { |pid| dbg("kill", "pid=#{pid}") }
      sleep SETTLE
      points["closed"] = ps
      log = core_log
      File.write(File.join(OUT, log_name), log)
      res = { "points" => points, "compile" => compile_lines(log) }
      started = points["running"].keys - points["idle"].keys
      if started.size != SCENARIO_APPS.size
        raise "#{SCENARIO_APPS.size - started.size} of the scenario's apps were not running at the snapshot (#{family}.log)"
      end
      res["family"] = family
      res["board"] = t.firmware
      res["commit"] = sh("git", "rev-parse", "--short=8", "HEAD", chdir: CORE)[1].strip
      res["sim_seconds"] = (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0).round
      res
    rescue RuntimeError
      if core_running?
        File.write(File.join(OUT, "#{family}.fail.log"), core_log)
        File.write(File.join(OUT, "#{family}.fail.ga.log"), sh("docker", "logs", "fmruby_graphics_audio")[1])
      end
      raise
    ensure
      down if core_running? && @ours
    end

    # Each mruby app's VM pool right after compiling its script.
    def compile_lines(log)
      out = {}
      log.scan(/\[([^\]]+)\] VM pool after compile: total=\d+ used=(\d+) free=\d+ largest_free=\d+ peak=(\d+)/) do |name, used, peak|
        out[name] = { "used" => used.to_i, "peak" => peak.to_i }
      end
      out
    end

    # --- comparing ----------------------------------------------------------

    # [key, value, kind, good direction] for every number in a result.
    def flatten(res)
      out = []
      res["points"].each do |pt, apps|
        apps.each do |app, v|
          key = "#{pt} #{app} used"
          out << [key, v["used"], GC_TIMED.include?(app) ? "gc_used" : "app_used", :down] unless UNCOMPARED.include?(key)
          out << ["#{pt} #{app} stack_free", v["stack_free"], "stack_free", :up]
        end
      end
      res["compile"].each do |app, v|
        out << ["compile #{app} used", v["used"], "compile", :down]
        out << ["compile #{app} peak", v["peak"], "compile", :down]
      end
      out
    end

    def allowed(kind, base, tol = TOLERANCE)
      rel, abs = tol.fetch(kind)
      [abs, (base.abs * rel).ceil].max
    end

    def compare(cur, base, tol = TOLERANCE)
      fails = []
      moved = []
      return [["no baseline"], []] unless base
      bvals = flatten(base).to_h { |k, v, *| [k, v] }
      cvals = flatten(cur)
      cvals.each do |key, val, kind, good|
        was = bvals.delete(key)
        if was.nil?
          moved << "#{key} new (#{val})"
          next
        end
        d = val - was
        next if d.abs <= allowed(kind, was, tol)
        worse = good == :down ? d.positive? : d.negative?
        (worse ? fails : moved) << "#{key} #{was} -> #{val} (#{d.positive? ? '+' : ''}#{d})"
      end
      bvals.each_key { |k| fails << "#{k} missing" }
      [fails, moved]
    end

    def kb(n) = format("%.0fK", n / 1024.0)

    def summary_line(r)
      pts = r["points"]
      sys = pts["idle"].keys.filter_map do |n|
        pts["closed"][n] && "#{n.sub('fmrb_', '').sub('system_', '')} #{kb(pts['idle'][n]['used'])}->#{kb(pts['closed'][n]['used'])}"
      end
      apps = pts["running"].reject { |k, _| pts["idle"].key?(k) }.map { |k, v| "#{k} #{kb(v['used'])}" }
      format("%-7s idle->closed: %s | running: %s", r["family"], sys.join(", "), apps.join(", "))
    end

    def load_results(fams)
      fams.each_with_object({}) do |(f, _t), h|
        p = File.join(OUT, "#{f}.json")
        h[f] = JSON.parse(File.read(p)) if File.exist?(p)
      end
    end

    def load_doc
      File.exist?(BASELINE) ? JSON.parse(File.read(BASELINE)) : { "families" => {} }
    end

    def load_baseline
      load_doc.fetch("families")
    end

    # The default tolerance (the file's over TOLERANCE), or a family's: its
    # own "tolerance" in the baseline (a calibration's) over the default.
    def tolerance(doc, family = nil)
      tol = TOLERANCE.merge(doc["tolerance"] || {})
      family ? tol.merge(doc.dig("families", family, "tolerance") || {}) : tol
    end

    # --- main -----------------------------------------------------------------

    def main(argv)
      opts = { only: ENV["ONLY"], table: false, update: false, calibrate: nil, apply: false }
      OptionParser.new do |o|
        o.on("--only LIST") { |v| opts[:only] = v }
        o.on("--table") { opts[:table] = true }
        o.on("--update") { opts[:update] = true }
        o.on("--calibrate N", Integer) { |v| opts[:calibrate] = v }
        o.on("--apply") { opts[:apply] = true }
      end.parse!(argv)
      fams = families(opts[:only])
      return update(fams) if opts[:update]
      return apply(fams) if opts[:apply]
      return calibrate(fams, opts[:calibrate]) if opts[:calibrate]

      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      errors = []
      timings = []
      unless opts[:table]
        raise "a sim stack is already running (someone's?); leave it or take it down first" if core_running?
        ga = File.join(ROOT, "fmruby-graphics-audio", "build", "fmruby-graphics-audio.elf")
        raise "fmruby-graphics-audio has no Linux build (rake build:linux there first)" unless elf_x86?(ga)
        tree = BuildTree.new(CORE)
        tree.stash!
        @ours = true
        begin
          fams.each do |f, t|
            begin
              res = scenario(f, t, tree)
              File.write(File.join(OUT, "#{f}.json"), JSON.pretty_generate(res))
              timings << "#{f} build #{res['build_seconds']}s + sim #{res['sim_seconds']}s"
            rescue RuntimeError => e
              FileUtils.rm_f(File.join(OUT, "#{f}.json"))
              errors << "#{f}: #{e.message}"
            end
          end
        ensure
          down if core_running?
          puts "build/: #{tree.restore!}"
        end
      end
      errors.each { |e| puts "FAIL #{e}" }
      results = load_results(fams)
      doc = load_doc
      baseline = doc.fetch("families")
      failed = !errors.empty?
      results.each do |f, r|
        puts summary_line(r)
        fails, moved = compare(r, baseline[f], tolerance(doc, f))
        failed ||= !fails.empty?
        fails.first(8).each { |l| puts "  FAIL #{l}" }
        puts "  ... #{fails.size - 8} more (tmp/regress/sim/#{f}.json)" if fails.size > 8
        moved.first(4).each { |l| puts "  moved #{l}" }
        puts "  ... #{moved.size - 4} more moved" if moved.size > 4
      end
      took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      timing = opts[:table] ? "table only" : "#{(took / 60).round(1)} min: #{timings.join(', ')}"
      puts "sim: #{results.size} families, #{failed ? 'FAILED' : 'ok'} (#{timing}; M1| is boards only)"
      failed ? 1 : 0
    end

    def update(fams)
      results = load_results(fams)
      raise "no saved results to write (run rake regress:run first)" if results.empty?
      doc = File.exist?(BASELINE) ? JSON.parse(File.read(BASELINE)) : { "families" => {} }
      results.each do |f, r|
        # a calibrated tolerance stays with the family
        keep = (doc["families"][f] || {}).slice("tolerance", "calibration")
        doc["families"][f] = r.reject { |k, _| %w[build_seconds sim_seconds].include?(k) }.merge(keep)
        puts "baseline: #{f} (#{r['board']})"
      end
      doc["tolerance"] = tolerance(doc)
      FileUtils.mkdir_p(File.dirname(BASELINE))
      File.write(BASELINE, JSON.pretty_generate(doc) + "\n")
      0
    end

    # --- calibration -----------------------------------------------------------

    def calibrate(fams, runs)
      raise "--calibrate needs at least 2 runs" if runs < 2
      raise "a sim stack is already running (someone's?); leave it or take it down first" if core_running?
      ga = File.join(ROOT, "fmruby-graphics-audio", "build", "fmruby-graphics-audio.elf")
      raise "fmruby-graphics-audio has no Linux build (rake build:linux there first)" unless elf_x86?(ga)
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      doc = load_doc
      cals = {}
      errors = []
      timings = []
      tree = BuildTree.new(CORE)
      tree.stash!
      @ours = true
      begin
        fams.each do |f, t|
          results = []
          bsecs = build(f, t, tree)
          runs.times do |i|
            results << boot_and_run(f, t, log_name: "#{f}.calibrate.#{i + 1}.log")
          end
          secs = results.map { |r| r["sim_seconds"] }
          timings << "#{f} build #{bsecs.round}s + sim #{secs.min}-#{secs.max}s x#{runs}"
          cals[f] = calibration(f, results, doc)
          File.write(File.join(OUT, "#{f}.calibrate.json"), JSON.pretty_generate(cals[f]))
        rescue RuntimeError => e
          errors << "#{f}: #{e.message}"
        end
      ensure
        down if core_running?
        puts "build/: #{tree.restore!}"
      end
      errors.each { |e| puts "FAIL #{e}" }
      cals.each { |f, cal| puts calibration_summary(f, cal, doc) }
      took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      puts "calibrate: #{cals.size} families, #{runs} runs each (#{(took / 60).round(1)} min: #{timings.join(', ')}); " \
           "tmp/regress/sim/<family>.calibrate.json"
      unless cals.empty?
        only = cals.size == families(nil).size ? "" : " ONLY=#{cals.keys.join(',')}"
        puts "  to make it the baseline, after reading the above: rake regress:run:calibrate:apply#{only}"
      end
      errors.empty? ? 0 : 1
    end

    # The numbers of N runs -> spreads, proposals and the baseline candidate.
    def calibration(family, results, doc)
      flats = results.map { |r| flatten(r).map { |k, v, kind, _| [k, v, kind] } }
      items = Calibrate.collect(flats)
      stats = Calibrate.measure(items, CALIBRATE, results.size)
      # what is kept but not compared: its spread for the record
      info = UNCOMPARED.to_h do |key|
        pt, *rest = key.split
        app = rest[0..-2].join(" ")
        vals = results.filter_map { |r| r.dig("points", pt, app, "used") }
        [key, vals.empty? ? nil : vals.max - vals.min]
      end
      now = tolerance(doc, family)
      cand = Calibrate.median_record(results, %w[points compile])
      cand = cand.reject { |k, _| %w[build_seconds sim_seconds].include?(k) }
      { "family" => family, "board" => results.last["board"], "runs" => results.size,
        "captured_at" => Time.now.strftime("%Y-%m-%dT%H:%M:%S%z"), "stats" => stats, "uncompared_spread" => info,
        "now" => now, "tolerance" => stats.to_h { |kind, s| [kind, [now[kind][0], s["proposal"]]] },
        "baseline" => cand }
    end

    def tol_text(rel, abs)
      rel.to_f.positive? ? "#{Calibrate.fmt(abs)} or #{(rel * 100).round(1).to_s.sub(/\.0\z/, '')}%" : Calibrate.fmt(abs)
    end

    def calibration_summary(family, cal, doc)
      now = cal["now"].transform_values { |rel, abs| tol_text(rel, abs) }
      shown = cal["tolerance"].transform_values { |rel, abs| tol_text(rel, abs) }
      lines = ["#{family} (#{cal['board']}, #{cal['runs']} runs):"] + Calibrate.table(cal["stats"], now, shown)
      cal["uncompared_spread"].each { |k, d| lines << "  not compared: #{k} moved #{Calibrate.fmt(d)}" if d }
      old = doc.dig("families", family)
      if old
        was = flatten(old).to_h { |k, v, *| [k, v] }
        now_vals = flatten(cal["baseline"]).to_h { |k, v, *| [k, v] }
        diffs = now_vals.filter_map { |k, v| (w = was[k]) && v != w ? [k, v - w] : nil }
        top = diffs.max_by { |_, d| d.abs }
        missing = (was.keys - now_vals.keys).size
        lines << "  candidate vs the baseline now: #{diffs.size} of #{was.size} numbers differ" \
                 "#{top ? " (most: #{top[0]} #{top[1].positive? ? '+' : ''}#{Calibrate.fmt(top[1])})" : ''}" \
                 "#{missing.positive? ? ", #{missing} missing" : ''}"
      else
        lines << "  no baseline for #{family} yet"
      end
      lines
    end

    def apply(fams)
      doc = load_doc
      done = fams.filter_map do |f, _t|
        p = File.join(OUT, "#{f}.calibrate.json")
        next unless File.exist?(p)
        cal = JSON.parse(File.read(p))
        doc["families"][f] = cal["baseline"].merge(
          "tolerance" => cal["tolerance"],
          "calibration" => { "runs" => cal["runs"], "at" => cal["captured_at"],
                             "spread" => cal["stats"].transform_values { |s| s["spread"] } }
        )
        tol = cal["tolerance"].map { |k, (rel, abs)| "#{k} #{tol_text(rel, abs)}" }.join(", ")
        puts "baseline: #{f} (#{cal['board']}) from a #{cal['runs']}-run calibration (medians); tolerance #{tol}"
        f
      end
      raise "no calibration to write (run rake regress:run:calibrate first)" if done.empty?
      doc["tolerance"] = tolerance(doc)
      File.write(BASELINE, JSON.pretty_generate(doc) + "\n")
      0
    end
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    exit Regress::SimRun.main(ARGV)
  rescue RuntimeError, KeyError, Errno::ENOENT => e
    warn "sim: #{e.message}"
    exit 1
  end
end
