#!/usr/bin/env ruby
# frozen_string_literal: true
#
# soak.rb - open and close the same apps many times and see whether the
# memory left after closing settles or keeps growing (rake regress:soak,
# doc/regression/usage.md chapter 14).
#
#   ruby tools/regress/soak.rb                      # Retro and Modern sim, 10 rounds each
#   ruby tools/regress/soak.rb --rounds 6 --only modern
#   ruby tools/regress/soak.rb --group running_langs   # one of regress:run's groups alone
#   ruby tools/regress/soak.rb --group /app/lua/lua.app.lua,/app/python/python.app.py
#   ruby tools/regress/soak.rb --split              # the whole set, then each group alone (one build)
#   ruby tools/regress/soak.rb --no-build           # the Linux build in build/ as it is
#   ruby tools/regress/soak.rb --spinel             # also the Spinel heaps' counters (gdb, sim only)
#   ruby tools/regress/soak.rb --table              # the last results judged again, no sim
#   ruby tools/regress/soak.rb --board naryav4      # a connected board (WiFi + the MCP serial capture)
#
# A round is regress:run's scenario (sim_run.rb SCENARIO, the apps the
# board's storage carries): each group's apps started one by one, then all
# of them killed, the next group, ... Once every group has come and gone the
# round's numbers are taken. The first rounds are a warm-up (tables that
# grow to their working size once, caches); the rounds after them say
# whether each number settled or keeps growing (GROW below: the floors of
# the two halves, since a Spinel VM's numbers saw-tooth). Anything that
# grows fails the run.
#
# Sim: the same build and boot as regress:run (clean:linux, build:linux with
# the family's pinned options, the developer's build/ moved aside and put
# back, the fixed launcher index), then the rounds on one boot. The numbers
# of a round: a full collection in every app, then the debug server's ps
# (sim_run.rb snapshot): the VM pools of the kernel, the desktop and (Modern)
# the service host. The wasm runtime's shared pool comes from a probe app
# (soak/wasm_probe.app.rb, put in the sim's /tmp) started after the numbers
# are taken, and while a group with a wasm app is running. Internal RAM and
# PSRAM are not there: a Linux build has neither, and the sim prints no
# periodic dump (report/rg3b.md).
#
# Board (--board): the same rounds over the development remote control
# (/app/launch, /app/kill, /app/list on WiFi), the numbers from the periodic
# dump (every 10 s, debug mode on) read from the fmrb MCP server's serial
# capture file (~/.fmrb_mcp/current.log, or BOARD_LOG=): the parent starts
# serial_start and leaves it running; this tool never opens the port (Tab5
# reboots when it is opened), so it needs no lock and resets nothing. After a
# round, as on the sim, a full collection in every app (devctl's /sys/gc, the
# debug server's gc over WiFi: a Spinel VM sweeps its strings too), then the
# first whole dump that begins after it; DUMPS (default 1) times, keeping the
# smallest pool usage and the largest free memory of them. A firmware without
# /sys/gc (it answers 404) falls back to what this did before it existed:
# DUMPS (default 2) whole dumps after the last kill, the least of them
# standing in for the collection. IRAM free, PSRAM free, every VM pool and the
# fmrb_wasm: runtime line are kept.
require "fileutils"
require "json"
require "net/http"
require "open3"
require "optparse"
require "uri"
require_relative "sim_run"
require_relative "boot_log"

module Regress
  module Soak
    OUT = File.join(CORE, "tmp", "regress", "soak")
    ROUNDS = 10
    WARMUP = 3
    # What "keeps growing" means. A collected Spinel VM does not come back
    # to the same number after each round: its strings are swept on their
    # own schedule (the young ones once past a trigger, the old ones on a
    # major; spinel_rt/sp_alloc.c, "String sweep, gated on the string heap's
    # own trigger"), and the debug server's full collection does not force
    # that. So the kernel's pool climbs and slides over several rounds and
    # falls back when a sweep comes (a cycle of about 11 rounds and 60 KB on
    # Retro's first group, report soak.md). What a leak moves is the floor,
    # so the rounds after the warm-up are cut in two halves and the least
    # value of each compared (for free memory, the most). A number grows when
    # the later floor is higher by at least `net` and by at least the usual
    # move from one round to the next (the median of the steps: a floor can
    # land anywhere in the round after a sweep), with a slope of at least
    # `slope` bytes per round.
    #
    # For a Spinel VM (SAWTOOTH) the floors mean something only across
    # sweeps: with fewer than two falls (a fall of a quarter of the swing) in
    # the rounds after the warm-up, a higher later floor is "undecided" (run
    # more rounds; it does not fail). An mruby VM's full collection takes its
    # strings too, so a climb there grows.
    GROW = { slope: 128, net: 1024 }.freeze
    SAWTOOTH = %w[fmrb_kernel].freeze
    # The numbers judged (the rest are kept in the json): the VM pools of
    # the system's own VMs and, on a board, the free memory.
    POOLS = %w[fmrb_kernel system_desktop Services].freeze
    # The sim's /tmp is flash/tmp of this checkout, and flat (no
    # subdirectories, like a board's): the probe goes there by a name of
    # its own.
    PROBE_SRC = File.join(__dir__, "soak", "wasm_probe.app.rb")
    PROBE_PATH = "/tmp/regress_soak_wasm_probe.app.rb"
    PROBE_FILE = File.join(CORE, "flash", PROBE_PATH)
    PROBE_LINE = /SOAK_WASM running=(\d+) apps=(\d+) modules=(\d+) used=(\d+) high=(\d+) total=(\d+) traps=(\d+)/
    MCP_LOG = File.expand_path(File.join(ENV["FMRB_MCP_STATE_DIR"] || "~/.fmrb_mcp", "current.log"))

    module_function

    def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

    # --- judging ---------------------------------------------------------------

    # Least-squares slope of values against their index (bytes per round).
    def slope(vals)
      n = vals.size
      return 0.0 if n < 2
      mx = (n - 1) / 2.0
      my = vals.sum.to_f / n
      num = vals.each_with_index.sum { |v, i| (i - mx) * (v - my) }
      den = (0...n).sum { |i| (i - mx)**2 }
      num / den
    end

    # One series (a value per round, nil where it was not there) ->
    # { "verdict" => "settled" | "grows" | "undecided" | "short", ... }.
    # good: :down for usage (growth is bad), :up for free memory (a fall is).
    def judge(vals, warmup, good: :down, sawtooth: false)
      post = vals.drop(warmup).compact
      return { "verdict" => "short", "rounds" => post.size } if post.size < 3
      # turn free memory into "usage" so that one rule judges both
      u = good == :down ? post : post.map { |v| -v }
      h = u.size / 2
      rise = u.drop(h).min - u.first(h).min
      steps = u.each_cons(2).map { |x, y| y - x }
      moves = steps.map(&:abs).sort
      noise = moves[(moves.size - 1) / 2]
      # a sweep: a fall of at least a quarter of the swing (and of `net`);
      # the slow slides between sweeps are smaller
      drop = [GROW[:net], (u.max - u.min) / 4].max
      falls = steps.count { |d| -d >= drop }
      s = slope(u)
      over = rise >= GROW[:net] && rise >= noise && s >= GROW[:slope]
      verdict = !over ? "settled" : (falls >= 2 || !sawtooth ? "grows" : "undecided")
      { "verdict" => verdict, "slope" => (good == :down ? s : -s).round, "floor_rise" => rise, "noise" => noise, "falls" => falls,
        "swing" => post.max - post.min, "first" => vals.compact.first, "warm" => post.first, "last" => post.last,
        "floor" => good == :down ? post.min : post.max, "rounds" => post.size }
    end

    # rows (one hash of name => number per round) -> { name => judgement }
    def judge_rows(rows, warmup, keys, good: :down)
      keys.each_with_object({}) do |k, h|
        vals = rows.map { |r| r[k] }
        next if vals.compact.empty?
        h[k] = judge(vals, warmup, good: good, sawtooth: SAWTOOTH.include?(k))
      end
    end

    def kb(n) = n.nil? ? "-" : format("%.1fK", n / 1024.0)
    def signed(n) = n.positive? ? "+#{n}" : n.to_s

    def verdict_text(name, j)
      swing = j["swing"].to_i >= GROW[:net] ? ", swings #{kb(j['swing'])}" : ""
      case j["verdict"]
      when "short" then "#{name} too few rounds after the warm-up"
      when "grows" then "#{name} GROWS: floor #{signed(j['floor_rise'])} B over #{j['rounds']} rounds " \
                        "(#{signed(j['slope'])} B/round#{swing})"
      when "undecided" then "#{name} UNDECIDED: floor #{signed(j['floor_rise'])} B but #{j['falls']} sweep(s) in " \
                            "#{j['rounds']} rounds (#{signed(j['slope'])} B/round#{swing}; run more rounds, ROUNDS=30)"
      else "#{name} settled (floor #{signed(j['floor_rise'].to_i)} B, #{kb(j['floor'])}#{swing}" \
           "#{j['noise'].to_i >= GROW[:net] ? "; moves #{kb(j['noise'])}/round" : ''})"
      end
    end

    # A result -> summary lines. A result: { "label", "rounds" => [ { "pools"
    # => {name => used}, ... } ], "idle", "judged" => {...} }
    def summary(res)
      first = res["rounds"].first || {}
      idle = res["idle"] || {}
      lines = ["#{res['label']}: #{res['rounds'].size} rounds (warm-up #{res['warmup']}, #{res['seconds']}s" \
               "#{res['collect'] ? "; #{res['collect']}" : ''})"]
      res["judged"].each do |name, j|
        start = idle[name] ? "idle #{kb(idle[name])}, round 1 #{kb(first.dig('pools', name) || first.dig('free', name))}; " : ""
        lines << "  #{start}#{verdict_text(name, j)}"
      end
      res["notes"].to_a.each { |n| lines << "  note: #{n}" }
      lines
    end

    def table(res)
      names = res["judged"].keys - ["wasm_high"]
      head = ["round"] + names.map { |n| n.sub("fmrb_", "").sub("system_", "") } + (res["rounds"].any? { |r| r["wasm"] } ? ["wasm high"] : [])
      rows = res["rounds"].each_with_index.map do |r, i|
        vals = names.map { |n| r.dig("pools", n) || r.dig("free", n) }
        [(i + 1).to_s] + vals.map { |v| v.to_s } + (head.last == "wasm high" ? [r.dig("wasm", "high").to_s] : [])
      end
      idle = res["idle"] || {}
      rows.unshift(["idle"] + names.map { |n| idle[n].to_s } + (head.last == "wasm high" ? [""] : []))
      w = head.each_index.map { |c| ([head[c]] + rows.map { |r| r[c] }).map(&:size).max }
      ([head] + rows).map { |r| r.each_with_index.map { |c, i| i.zero? ? c.ljust(w[i]) : c.rjust(w[i]) }.join("  ") }
    end

    def finish(res, warmup)
      keys = POOLS + res["rounds"].flat_map { |r| (r["pools"] || {}).keys }.uniq.reject { |k| POOLS.include?(k) }
      # a board's dump lists every VM (the result has its idle dump)
      keys = keys.select { |k| POOLS.include?(k) || res.key?("idle_dump") }
      judged = judge_rows(res["rounds"].map { |r| r["pools"] || {} }, warmup, keys)
      if res["rounds"].any? { |r| r["free"] }
        judged.merge!(judge_rows(res["rounds"].map { |r| r["free"] || {} }, warmup, %w[iram_free psram_free], good: :up))
      end
      if res["rounds"].any? { |r| r.dig("wasm", "high") }
        judged["wasm_high"] = judge(res["rounds"].map { |r| r.dig("wasm", "high") }, warmup)
      end
      res["warmup"] = warmup
      res["judged"] = judged
      res
    end

    def grows?(res) = res["judged"].values.any? { |j| j["verdict"] == "grows" }

    # --- the sim -----------------------------------------------------------------

    SR = SimRun

    def sets_for(t, group, split)
      full = SR.scenario_for(t)
      return [["all", full]] if group.nil? && !split
      return [["all", full]] + full.map { |pt, apps| [pt, [[pt, apps]]] } if split
      picked = group.split(",").map(&:strip)
      named = full.select { |pt, _| picked.include?(pt) }
      return [[named.map(&:first).join("+"), named]] unless named.empty?
      [["apps", [["apps", picked]]]]
    end

    def run_sim(opts)
      fams = SR.families(opts[:only])
      SR.need_sim!
      raise "a sim stack is already running (someone's?); leave it or take it down first" if SR.core_running?
      ga = File.join(SR::ROOT, "fmruby-graphics-audio", "build-linux", "fmruby-graphics-audio.elf")
      raise "fmruby-graphics-audio has no Linux build (rake build:linux there first)" unless SR.elf_x86?(ga)
      FileUtils.mkdir_p(OUT)
      results = []
      errors = []
      tree = BuildTree.new(CORE, :linux)
      if opts[:no_build]
        raise "--no-build takes one family (ONLY=retro|modern)" unless fams.size == 1
        have = tree.describe
        want = "linux sim (#{fams[0][0]})"
        raise "--no-build: build-linux/ holds #{have}, #{want} wanted" unless have == want
      else
        tree.stash!
      end
      begin
        SR.current_first(fams, &:first).each do |f, t|
          begin
            bsecs = opts[:no_build] ? 0 : SR.build(f, t, tree)
            sets_for(t, opts[:group], opts[:split]).each do |label, groups|
              res = soak_sim(f, t, "#{f} #{label}", groups, opts)
              res["build_seconds"] = bsecs.round
              finish(res, opts[:warmup])
              File.write(File.join(OUT, "#{f}.#{label.tr('/+', '_-')}.json"), JSON.pretty_generate(res))
              results << res
            end
          rescue RuntimeError => e
            errors << "#{f}: #{e.message}"
          ensure
            tree.keep_for_later! unless opts[:no_build]
          end
        end
      ensure
        SR.down if SR.core_running?
        FileUtils.rm_f(PROBE_FILE)
        FileUtils.rm_f(SR::LAUNCHER_CACHE)
        puts "build-linux/: #{tree.restore!}" unless opts[:no_build]
      end
      [results, errors]
    end

    # One boot, the rounds of these groups on it.
    def soak_sim(family, t, label, groups, opts)
      t0 = now
      SR.retries.clear
      SR.seed_launcher_cache
      size = begin
        SR.up(family)
      rescue RuntimeError
        nil
      end
      if size != SR::SIZES[family]
        SR.down
        size = SR.up(family)
      end
      raise "the sim came up at #{size&.join('x') || '?'}, #{SR::SIZES[family].join('x')} expected" unless size == SR::SIZES[family]
      SR.wait_for(60, "the desktop's boot to finish") { SR.core_log.include?(SR::BOOT_DONE) }
      sleep SR::SETTLE
      wasm = family == "modern" && SR::SCENARIO.flatten.any? { |a| a.end_with?(".wasm") }
      put_probe if wasm
      idle_ps = SR.snapshot("idle")
      res = { "label" => label, "family" => family, "board" => t.firmware, "groups" => groups,
              "commit" => SR.sh("git", "rev-parse", "--short=8", "HEAD", chdir: CORE)[1].strip,
              "idle" => idle_ps.to_h { |n, v| [n, v["used"]] }, "rounds" => [], "notes" => [] }
      res["idle_wasm"] = probe_wasm if wasm
      res["idle_spinel"] = spinel_stats if opts[:spinel]
      opts[:rounds].times do |i|
        r0 = now
        running_wasm = nil
        groups.each do |pt, apps|
          pids = apps.map do |path|
            pid = SR.dbg("spawn", "path=#{path}")["pid"]
            sleep SR::SETTLE
            pid
          end
          running_wasm = probe_wasm if wasm && apps.any? { |a| a.end_with?(".wasm") }
          pids.each { |pid| SR.dbg("kill", "pid=#{pid}") }
          sleep SR::SETTLE
          # a group that did not come up whole is worth saying once
          res["notes"] << "round #{i + 1} #{pt}: #{pids.compact.size} of #{apps.size} started" if pids.compact.size != apps.size
        end
        ps = SR.snapshot("round #{i + 1}")
        left = ps.keys - idle_ps.keys
        res["notes"] << "round #{i + 1}: still running after the kills: #{left.join(', ')}" unless left.empty?
        row = { "pools" => ps.to_h { |n, v| [n, v["used"]] }, "stack_free" => ps.to_h { |n, v| [n, v["stack_free"]] } }
        row["wasm"] = probe_wasm if wasm
        row["wasm_running"] = running_wasm if running_wasm
        row["spinel"] = spinel_stats if opts[:spinel]
        row["seconds"] = (now - r0).round
        res["rounds"] << row
        warn "soak #{label} round #{i + 1}/#{opts[:rounds]}: kernel #{row['pools']['fmrb_kernel']}"
      end
      res["seconds"] = (now - t0).round
      res["retries"] = SR.retries.dup
      File.write(File.join(OUT, "#{label.tr(' /+', '._-')}.log"), SR.core_log)
      res
    ensure
      if SR.core_running?
        File.write(File.join(OUT, "#{label.tr(' /+', '._-')}.fail.log"), SR.core_log) unless res && res["seconds"]
        SR.down
      end
    end

    # The Spinel instances' own counters (--spinel): gdb attached to the core
    # from a container of its own in the core's PID namespace (the way the
    # root CLAUDE.md attaches it to a hung sim; attach and detach leave the
    # process running). Per instance: the object heap's bytes (young + old),
    # the string heap's bytes and the triggers of both. The kernel is the
    # instance made first; after a round the others (an editor, Spinel
    # Hello) are gone.
    SPINEL_FIELDS = %w[gc_bytes gc_old_bytes gc_threshold str_heap_bytes str_threshold str_old_bytes str_old_threshold gc_cycle].freeze
    GDB_IMAGE = "ghcr.io/family-mruby/fmruby-esp32-build:v5.5.4"

    def spinel_stats
      _, ps = SR.sh("docker", "exec", "fmruby_core", "sh", "-c", "pgrep -x fmruby-core.elf | head -1")
      pid = ps.strip[/\d+/] or raise "spinel stats: no fmruby-core.elf in the container"
      inst = "'fmrb_spinel_host.c'::s_instances"
      exprs = (0...4).flat_map do |i|
        ["-ex", "printf \"I#{i} %p\\n\", #{inst}[#{i}].ctx"] +
          SPINEL_FIELDS.map { |f| ["-ex", "printf \"F#{i} #{f} %lu\\n\", #{inst}[#{i}].ctx ? (unsigned long)#{inst}[#{i}].ctx->#{f} : 0"] }.flatten
      end
      cmd = ["docker", "run", "--rm", "-u", "root", "--pid=container:fmruby_core", "--cap-add=SYS_PTRACE",
             "-v", "#{CORE}:/project", GDB_IMAGE, "gdb", "-batch", "-ex", "set pagination off",
             "-ex", "set sysroot /proc/#{pid}/root", "-ex", "file /project/build-linux/fmruby-core.elf",
             "-ex", "attach #{pid}", *exprs, "-ex", "detach"]
      _, out = SR.sh(*cmd)
      live = out.scan(/^I(\d) (0x[0-9a-f]+)/).reject { |_i, p| p == "0x0" }.map(&:first)
      live.to_h do |i|
        [i.to_i, out.scan(/^F#{i} (\w+) (\d+)/).to_h { |f, v| [f, v.to_i] }]
      end.tap { |h| raise "spinel stats: gdb read nothing (#{out.lines.last(3).join.strip})" if h.empty? }
    end

    def put_probe
      FileUtils.mkdir_p(File.dirname(PROBE_FILE))
      FileUtils.cp(PROBE_SRC, PROBE_FILE)
    end

    # The probe's one line: started, read from the core's log, then gone
    # (it stops itself; killed if it has not by the time the line is there).
    def probe_wasm
      seen = SR.core_log.scan(PROBE_LINE).size
      pid = SR.dbg("spawn", "path=#{PROBE_PATH}")["pid"]
      m = SR.wait_for(15, "the wasm probe's line") do
        all = SR.core_log.scan(PROBE_LINE)
        all.size > seen && all.last
      end
      SR.wait_for(10, "the wasm probe to end") { !SR.dbg("ps").fetch("apps").any? { |a| a["pid"] == pid } }
      %w[running apps modules used high total traps].zip(m.map(&:to_i)).to_h
    rescue RuntimeError
      SR.dbg("kill", "pid=#{pid}") if pid
      raise
    end

    # --- a board -------------------------------------------------------------------

    class BoardError < RuntimeError; end

    class Board
      def initialize(name, opts)
        @opts = opts
        @t = TARGETS.find { |x| x.firmware && (x.name.casecmp?(name) || x.firmware.casecmp?(name)) } or
          raise BoardError, "BOARD=#{name}: no such board in tools/regress/targets.rb"
        @log = ENV["BOARD_LOG"].to_s.empty? ? MCP_LOG : ENV["BOARD_LOG"]
        raise BoardError, "no serial capture log (#{@log}); start one with the MCP serial_start" unless File.exist?(@log)
        @ip = ENV["BOARD_IP"].to_s.empty? ? ip_from_log : ENV["BOARD_IP"]
        raise BoardError, "no 'Connected, ip=' in the board's log; give BOARD_IP=" unless @ip
        @gc = nil # nil: not asked yet; true: /sys/gc answers; false: the old firmware's way
      end

      def run
        t0 = Soak.now
        apps
        groups = SR.scenario_for(@t)
        if @opts[:group]
          picked = @opts[:group].split(",").map(&:strip)
          named = groups.select { |pt, _| picked.include?(pt) }
          groups = named.empty? ? [["apps", picked]] : named
        end
        label = "#{@t.name} #{@opts[:group] ? groups.map(&:first).join('+') : 'all'}"
        @notes = []
        idle = settle_dumps("idle", @notes)
        res = { "label" => label, "board" => @t.firmware, "groups" => groups, "ip" => "(from the log)",
                "idle" => flat(idle), "idle_dump" => idle, "rounds" => [], "notes" => @notes.dup,
                "collect" => @gc ? "devctl /sys/gc" : "least of the periodic dumps (no /sys/gc)" }
        @opts[:rounds].times do |i|
          r0 = Soak.now
          groups.each do |pt, list|
            pids = list.map do |path|
              pid = spawn(path)
              sleep SR::SETTLE
              pid
            end
            pids.compact.each { |pid| kill(pid) }
            sleep SR::SETTLE
            res["notes"] << "round #{i + 1} #{pt}: #{pids.compact.size} of #{list.size} started" if pids.compact.size != list.size
          end
          d = settle_dumps("round #{i + 1}", res["notes"])
          row = { "pools" => d["pools"], "free" => { "iram_free" => d["iram_free"], "psram_free" => d["psram_free"] },
                  "wasm" => d["wasm"], "seconds" => (Soak.now - r0).round }
          res["rounds"] << row
          warn "soak #{label} round #{i + 1}/#{@opts[:rounds]}: kernel #{row['pools']['fmrb_kernel']} IRAM free #{d['iram_free']}"
        end
        res["seconds"] = (Soak.now - t0).round
        res
      end

      def flat(d) = d["pools"].merge("iram_free" => d["iram_free"], "psram_free" => d["psram_free"])

      def lines = Regress::BootLog.lines(File.binread(@log).force_encoding("UTF-8"))

      def ip_from_log
        lines.reverse_each { |_at, l| return Regexp.last_match(1) if l =~ /Connected, ip=(\d+\.\d+\.\d+\.\d+)/ }
        nil
      end

      # The numbers after a round: with /sys/gc, DUMPS times a collection in
      # every app and the first whole dump that starts after it; without it,
      # DUMPS whole dumps that start after now. The least usage and the most
      # free memory of them (a dump each 10 s).
      def settle_dumps(what, notes)
        collect(what, notes) if @gc.nil?
        unless @gc
          want = @opts[:dumps] || 2
          return Soak.least(next_dumps(want, what))
        end
        want = @opts[:dumps] || 1
        dumps = Array.new(want) do |k|
          collect(what, notes) unless k.zero? && @gc_fresh
          @gc_fresh = false
          next_dumps(1, what).first
        end
        Soak.least(dumps)
      end

      def next_dumps(want, what)
        from = lines.size
        Soak.wait(15 * want + 20, "#{want} periodic dumps (#{what}); is debug mode on?") do
          got = Soak.parse_dumps(lines.drop(from))
          got.size >= want ? got.first(want) : nil
        end
      end

      # A full collection in every app (devctl /sys/gc), once again for an app
      # that timed out (the sim's snapshot does the same). The first call
      # finds out whether the firmware has the route at all.
      def collect(what, notes)
        2.times do |k|
          j = http(:post, "/sys/gc", { max: 5000 }, allow404: @gc.nil?)
          if j.nil?
            @gc = false
            notes << "the firmware has no /sys/gc: the least of the periodic dumps stands in for the collection"
            warn "soak: no /sys/gc on the board; falling back to the least of #{@opts[:dumps] || 2} dumps"
            return
          end
          @gc = true
          @gc_fresh = true
          slow = j.fetch("apps", []).select { |a| a["status"] == "timeout" }.map { |a| "#{a['name']}(#{a['pid']})" }
          return if slow.empty?
          notes << "#{what}: gc timed out in #{slow.join(', ')}#{k.zero? ? ', asked again' : ''}"
        end
      end

      # The answer's JSON; nil for a 404 when allow404 (a route the firmware lacks).
      def http(method, path, query = {}, allow404: false)
        uri = URI("http://#{@ip}#{path}")
        uri.query = query.map { |k, v| "#{k}=#{v.to_s.gsub(%r{[^A-Za-z0-9_.~/-]}) { |c| c.bytes.map { |b| format('%%%02X', b) }.join }}" }.join("&") unless query.empty?
        req = (method == :get ? Net::HTTP::Get : Net::HTTP::Post).new(uri)
        res = Net::HTTP.start(uri.host, uri.port, open_timeout: 5, read_timeout: 15) { |h| h.request(req) }
        return nil if allow404 && res.code == "404"
        j = JSON.parse(res.body.to_s) rescue nil # rubocop:disable Style/RescueModifier
        raise BoardError, "#{path} #{query.inspect} answered #{res.code} #{res.body.to_s[0, 120]}" unless res.code == "200" && j
        j
      rescue BoardError
        raise
      rescue StandardError => e
        raise BoardError, "#{path} did not answer (#{e.class.name.split('::').last}); is the board up?"
      end

      def apps = http(:get, "/app/list").fetch("apps", [])
      def spawn(path) = http(:post, "/app/launch", { path: path })["pid"]
      def kill(pid) = http(:post, "/app/kill", { pid: pid })
    end

    def wait(timeout, what)
      t_end = now + timeout
      loop do
        v = yield
        return v if v
        raise BoardError, "timed out after #{timeout}s waiting for #{what}" if now > t_end
        sleep 1
      end
    end

    # [[at, line], ...] (BootLog.lines) -> the whole periodic dumps in them,
    # each { "iram_free", "psram_free", "pools" => {name => used}, "wasm" }.
    # A dump starts at "--- Task Status", has "IRAM free:", then the VM pool
    # table; the fmrb_wasm: runtime line follows on a board with WAMR. One
    # cut short at the end of the lines is left out.
    def parse_dumps(lines)
      dumps = []
      cur = nil
      cols = nil
      lines.each do |_at, l|
        if l.include?("--- Task Status")
          cur = { "pools" => {} }
        elsif cur && (m = BootLog::IRAM_FREE.match(l))
          cur["iram_free"] = m[1].to_i
          cur["psram_free"] = m[2].to_i
        elsif cur && l.include?("--- VM Pools ---")
          cols = nil
        elsif cur && (m = BootLog::POOL_HEAD.match(l))
          cols = m[1].split.map(&:downcase)
        elsif cur && cols && l =~ /fmrb_app: -{8,}/
          cur["done"] = true
          dumps << cur
          cols = nil
        elsif cur && cols && (row = BootLog.pool_row(l, cols))
          cur["pools"][row["name"]] = row["used"]
        elsif cur && cur["done"] && (m = /fmrb_wasm: runtime (\w+) apps=(\d+) modules=(\d+) natives=\d+ pool used=(\d+) high=(\d+) total=(\d+)/.match(l))
          cur["wasm"] = { "running" => m[1] == "on" ? 1 : 0, "apps" => m[2].to_i, "modules" => m[3].to_i,
                          "used" => m[4].to_i, "high" => m[5].to_i, "total" => m[6].to_i }
        end
      end
      dumps.map { |d| d.except("done") }
    end

    # Several dumps -> one: each pool's least usage, the most free memory,
    # the last wasm line.
    def least(dumps)
      pools = dumps.flat_map { |d| d["pools"].keys }.uniq.to_h { |n| [n, dumps.filter_map { |d| d["pools"][n] }.min] }
      { "pools" => pools, "iram_free" => dumps.filter_map { |d| d["iram_free"] }.max,
        "psram_free" => dumps.filter_map { |d| d["psram_free"] }.max, "wasm" => dumps.filter_map { |d| d["wasm"] }.last,
        "dumps" => dumps.size }
    end

    # --- main ------------------------------------------------------------------------

    def main(argv)
      opts = { rounds: ROUNDS, warmup: WARMUP, only: ENV["ONLY"], group: nil, split: false, board: nil,
               dumps: nil, table: false, no_build: false }
      OptionParser.new do |o|
        o.on("--rounds N", Integer) { |v| opts[:rounds] = v }
        o.on("--warmup N", Integer) { |v| opts[:warmup] = v }
        o.on("--only LIST") { |v| opts[:only] = v }
        o.on("--group LIST") { |v| opts[:group] = v }
        o.on("--split") { opts[:split] = true }
        o.on("--board NAME") { |v| opts[:board] = v }
        o.on("--dumps N", Integer) { |v| opts[:dumps] = v }
        o.on("--table") { opts[:table] = true }
        o.on("--no-build") { opts[:no_build] = true }
        o.on("--spinel") { opts[:spinel] = true }
      end.parse!(argv)
      raise "--rounds must be more than the warm-up (#{opts[:warmup]}) + 2" if opts[:rounds] < opts[:warmup] + 3 && !opts[:table]
      FileUtils.mkdir_p(OUT)
      t0 = now
      errors = []
      results =
        if opts[:table]
          # judged again, so that a change of the rule reads old rounds too
          Dir[File.join(OUT, "*.json")].sort.map { |p| JSON.parse(File.read(p)).then { |r| finish(r, r["warmup"] || opts[:warmup]) } }
        elsif opts[:board]
          begin
            res = finish(Board.new(opts[:board], opts).run, opts[:warmup])
            File.write(File.join(OUT, "board.#{res['board']}.json"), JSON.pretty_generate(res))
            [res]
          rescue BoardError => e
            errors << "board: #{e.message}"
            []
          end
        else
          r, errors = run_sim(opts)
          r
        end
      errors.each { |e| puts "FAIL #{e}" }
      results.each do |res|
        summary(res).each { |l| puts l }
        table(res).each { |l| puts "    #{l}" } if opts[:table] || ENV["SOAK_TABLE"] == "1"
      end
      grew = results.select { |r| grows?(r) }
      undecided = results.count { |r| r["judged"].values.any? { |j| j["verdict"] == "undecided" } }
      failed = !errors.empty? || grew.any?
      retried = results.flat_map { |r| r["retries"].to_a }.uniq
      took = opts[:table] ? "table only" : "#{((now - t0) / 60).round(1)} min"
      puts "soak: #{results.size} runs, #{failed ? 'FAILED' : 'ok'}#{undecided.positive? ? ", #{undecided} undecided" : ''} (#{took}; tmp/regress/soak/" \
           "#{retried.empty? ? '' : "; retried: #{retried.join('; ')}"})"
      failed ? 1 : 0
    end
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    exit Regress::Soak.main(ARGV)
  rescue RuntimeError, KeyError, Errno::ENOENT => e
    warn "soak: #{e.message}"
    exit 1
  end
end
