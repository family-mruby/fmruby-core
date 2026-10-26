# frozen_string_literal: true
#
# boot_log.rb - read a board's boot log into numbers and compare them with a
# baseline (rake regress:boot, doc/regression/usage.md chapter 8). No device,
# no docker: test/regress/boot_log_test.rb feeds it made-up logs.
#
# What it takes from the log (the firmware's standing instrumentation, root
# CLAUDE.md "ログの読み方"):
#   - crash markers (the MCP flash tool's boot summary set, plus panic /
#     assert / stack overflow / heap corruption)
#   - the ROM's reset cause (rst:0x..) and how many times the chip booted
#     during the capture (more than once = it rebooted by itself)
#   - every M1| line: internal / largest / psram after each boot step
#     (doc/reference/internal_ram_budget.md). A label seen twice becomes
#     "label#2"
#   - the periodic dump (every 10 s): the last "IRAM free:", the last task
#     table (stack high-water: it only gets worse, so the last value is the
#     session's worst) and the last VM pool table
#   - "Boot complete" (status_led) and, when the capture stamped the lines
#     with the host's arrival time ("[  12.345] "), how long the boot took
#
# Strings that name a device (the mDNS name, IP addresses) are masked by
# BootLog.mask before anything is written.
module Regress
  module BootLog
    STAMP = /\A\[\s*(\d+\.\d+)\] /
    ANSI = /\e\[[0-9;]*m/
    CRASH = Regexp.union(
      /Guru Meditation/, /\babort\(\)/, /Backtrace:/, /StoreProhibited/, /LoadProhibited/,
      /InstrFetchProhibited/, /IllegalInstruction/, /panic'ed/, /assert(ion)? failed/i,
      /Stack protection fault/, /stack overflow/i, /CORRUPT HEAP/, /heap_caps_alloc_failed/
    ).freeze
    M1 = /M1\|([^|\s]+)\|internal=(\d+)\|largest=(\d+)\|psram=(\d+)/
    RESET = /\brst:0x(\h+) \(([A-Z0-9_]+)\)/
    TASK_ROW = /fmrb_task: (.+?)\s+([YN])\s+(-|\d+)\s+(\d+)\s+(\d+)\s+(IRAM|PSRAM)\s+(\S+)\s*\z/
    POOL_HEAD = /fmrb_app: Name\s+VM\s+(.*\S)\s*\z/
    IRAM_FREE = /IRAM free: (\d+) bytes, PSRAM free: (\d+) bytes/
    BOOT_COMPLETE = /Boot complete/

    # The worse direction is "smaller" for every compared number (free
    # memory, largest block, stack left). Bytes a number may move that way
    # before the run fails; measured in doc/regression/report/rg4.md.
    # Five boots of one P4-Nano firmware moved at most: M1| internal 436,
    # largest 8,192 (it moves in 4 KB steps), psram 0, IRAM free 440, a task's
    # stack 144. Each tolerance is about twice that (largest: the two steps).
    TOLERANCE = {
      "m1_internal" => 1024,
      "m1_largest" => 8192,
      "m1_psram" => 16_384,
      "iram_free" => 1024,
      "stack_free" => 384
    }.freeze

    module_function

    # Device names, addresses and the access point's name.
    def mask(text)
      text.gsub(/fmruby-\h{6}/, "fmruby-a1b2c3")
          .gsub(/\b(?:\d{1,3}\.){3}\d{1,3}\b/) { |ip| ip.start_with?("0.") ? ip : "192.0.2.1" }
          .gsub(/\b\h\h(?::\h\h){5}\b/, "02:00:00:00:00:01")
          .gsub(/(ssid[=:]\s*)\S+/i, '\1example-ap')
          .gsub(/(svc\[net\] up: \S+ )\(.*\)/, '\1(example-ap)')
    end

    # text -> [[seconds or nil, line], ...] without colours and CRs.
    def lines(text)
      text.scrub("?").split("\n").map do |l|
        l = l.delete("\r").gsub(ANSI, "")
        m = STAMP.match(l)
        m ? [m[1].to_f, l.sub(STAMP, "")] : [nil, l]
      end
    end

    def parse(text)
      res = { "crash" => [], "boots" => 0, "reset" => nil, "m1" => {}, "dumps" => 0, "iram_free" => nil,
              "psram_free" => nil, "tasks" => {}, "pools" => {}, "boot_complete" => false,
              "boot_seconds" => nil, "version" => nil, "running_on" => nil }
      tasks = nil
      pools = nil
      pool_cols = nil
      rom_at = nil
      lines(text).each do |at, l|
        res["crash"] << l.strip if CRASH.match?(l)
        if (m = RESET.match(l))
          res["boots"] += 1
          res["reset"] ||= m[2]
          rom_at ||= at
        end
        if (m = M1.match(l))
          label = m[1]
          if res["m1"].key?(label)
            n = 2
            n += 1 while res["m1"].key?("#{label}##{n}")
            label = "#{label}##{n}"
          end
          res["m1"][label] = { "internal" => m[2].to_i, "largest" => m[3].to_i, "psram" => m[4].to_i }
        end
        res["version"] ||= l[/App version:\s+(\S+)/, 1]
        res["running_on"] ||= l[/Running on (.+?)(?: - |\z)/, 1]
        if !res["boot_complete"] && BOOT_COMPLETE.match?(l)
          res["boot_complete"] = true
          res["boot_seconds"] = (at - (rom_at || 0.0)).round(1) if at
        end

        if l.include?("--- Task Status")
          tasks = {}
        elsif tasks && (m = TASK_ROW.match(l))
          tasks[m[1].strip] = { "stack" => m[4].to_i, "free" => m[5].to_i, "active" => m[2] == "Y" }
        elsif (m = IRAM_FREE.match(l))
          res["dumps"] += 1
          res["iram_free"] = m[1].to_i
          res["psram_free"] = m[2].to_i
          res["tasks"] = tasks if tasks
          tasks = nil
        end

        # The pool table's columns changed over time (Peak, Largest and
        # ExcHW came later), so the header names them.
        if l.include?("--- VM Pools ---")
          pools = {}
          pool_cols = nil
        elsif pools && (m = POOL_HEAD.match(l))
          pool_cols = m[1].split.map(&:downcase)
        elsif pools && pool_cols && l =~ /fmrb_app: -{8,}/
          res["pools"] = pools
          pools = nil
        elsif pools && pool_cols && (row = pool_row(l, pool_cols))
          pools[row.delete("name")] = row
        end
      end
      res["crash"] = res["crash"].first(20).map { |c| mask(c) }
      res
    end

    # "fmrb_app: <name, may have spaces> <vm> <columns...>" -> a hash, or nil.
    def pool_row(line, cols)
      body = line[/fmrb_app: (.*)\z/, 1] or return nil
      toks = body.split
      return nil if toks.size < cols.size + 2
      vals = toks.pop(cols.size)
      vm = toks.pop
      return nil unless vals[0] =~ /\A\d+\z/
      row = { "name" => toks.join(" "), "vm" => vm }
      cols.each_with_index do |c, i|
        v = vals[i]
        row[c] = v =~ /\A-?\d+%?\z/ && c != "exchw" ? v.to_i : v
      end
      row
    end

    def fmt(n) = n.to_s.reverse.scan(/\d{1,3}/).join(",").reverse.then { |s| n.negative? ? "-#{s}" : s }
    def sfmt(d) = d.zero? ? "+0" : (d.positive? ? "+#{fmt(d)}" : fmt(d))

    # Compare one board's result with its baseline. Returns
    # { fails: [...], moved: [...], deltas: {...} }: fails are worse than the
    # tolerance (or a crash / a reboot / a step that did not show up), moved
    # are better than the tolerance (shown, never fail).
    def compare(cur, base, tol = TOLERANCE)
      fails = []
      moved = []
      deltas = {}
      fails << "crash markers: #{cur['crash'].size} (#{cur['crash'].first})" unless cur["crash"].empty?
      fails << "the chip booted #{cur['boots']} times in the capture (rebooted)" if cur["boots"] > 1
      fails << "no \"Boot complete\"" unless cur["boot_complete"]
      fails << "no periodic dump (IRAM free)" if cur["dumps"].zero?
      fails << "no M1| lines" if cur["m1"].empty?
      return { fails: fails << "no baseline", moved: moved, deltas: deltas } unless base

      check = lambda do |what, now, was, t|
        return if now.nil? || was.nil?
        d = now - was
        deltas[what] = d
        if d < -t
          fails << "#{what} #{fmt(now)} (#{sfmt(d)}, tolerance #{fmt(t)})"
        elsif d > t
          moved << "#{what} #{sfmt(d)}"
        end
      end

      base["m1"].each do |label, b|
        c = cur["m1"][label]
        unless c
          fails << "M1|#{label} missing"
          next
        end
        check.call("M1|#{label} internal", c["internal"], b["internal"], tol["m1_internal"])
        check.call("M1|#{label} largest", c["largest"], b["largest"], tol["m1_largest"])
        check.call("M1|#{label} psram", c["psram"], b["psram"], tol["m1_psram"])
      end
      new_steps = cur["m1"].keys - base["m1"].keys
      moved << "new M1| steps: #{new_steps.join(', ')}" unless new_steps.empty?
      check.call("IRAM free", cur["iram_free"], base["iram_free"], tol["iram_free"])
      base["tasks"].each do |name, b|
        c = cur["tasks"][name]
        unless c
          fails << "task #{name} missing from the dump"
          next
        end
        check.call("stack #{name}", c["free"], b["free"], tol["stack_free"])
      end
      { fails: fails, moved: moved, deltas: deltas }
    end

    # The short per-board table: a header and one row per board, then the
    # failures (and a line of moved numbers) under it.
    def table(results, baseline, tol = TOLERANCE)
      head = format("%-8s %-18s %-7s %-5s %-9s %-19s %-19s %-28s %s", "board", "reset", "boot", "crash",
                    "M1 steps", "M1 last internal", "IRAM free", "least stack free", "dumps")
      lines = [head]
      failed = []
      results.each do |name, cur|
        base = baseline[name]
        c = compare(cur, base, tol)
        last = cur["m1"].keys.last
        last_d = last && c[:deltas]["M1|#{last} internal"]
        least = cur["tasks"].min_by { |_n, t| t["free"] }
        least_d = least && c[:deltas]["stack #{least[0]}"]
        cell = ->(v, d) { v.nil? ? "-" : "#{fmt(v)}#{d.nil? ? '' : " #{sfmt(d)}"}" }
        lines << format("%-8s %-18s %-7s %-5s %-9s %-19s %-19s %-28s %s",
                        cur["firmware"] || name, cur["reset"] || "?",
                        cur["boot_seconds"] ? "#{cur['boot_seconds']}s" : "-", cur["crash"].size,
                        cur["m1"].size, cell.call(last && cur["m1"][last]["internal"], last_d),
                        cell.call(cur["iram_free"], c[:deltas]["IRAM free"]),
                        least ? "#{least[0]} #{cell.call(least[1]['free'], least_d)}" : "-", cur["dumps"])
        unless cur["pools"].empty?
          lines << "  pools (used/peak): " + cur["pools"].map { |n, p| "#{n} #{fmt(p['used'])}#{p['peak'] ? "/#{fmt(p['peak'])}" : ''}" }.join("  ")
        end
        c[:fails].first(10).each { |f| lines << "  FAIL #{f}" }
        lines << "  ... #{c[:fails].size - 10} more (#{name}.json)" if c[:fails].size > 10
        lines << "  moved (better than the tolerance): #{c[:moved].first(4).join('; ')}#{c[:moved].size > 4 ? '; ...' : ''}" unless c[:moved].empty?
        failed << name unless c[:fails].empty?
      end
      [lines, failed]
    end
  end
end
