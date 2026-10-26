#!/usr/bin/env ruby
# frozen_string_literal: true
#
# boot.rb - a board's boot log against its baseline (rake regress:boot,
# doc/regression/usage.md chapter 8). Only when a board is connected and may
# be used; it is not part of rake regress:full.
#
#   ruby tools/regress/boot.rb --board naryav4        # build, app-only flash, capture, compare
#   ruby tools/regress/boot.rb --board naryav4 --no-flash   # the firmware already on it
#   ruby tools/regress/boot.rb --board naryav4 --table      # no device: the last result again
#   ruby tools/regress/boot.rb --board naryav4 --update     # the last result becomes the baseline
#   ruby tools/regress/boot.rb --board naryav4 --calibrate 5   # boot 5 times, measure the spread
#   ruby tools/regress/boot.rb --board naryav4 --apply      # the last calibration becomes the baseline
#
# One run:
#   1. finds the board's port by the VID:PID in targets.rb (PORT= overrides)
#      and stops when anything holds it: the fmrb MCP server's serial
#      capture (its flock on ~/.fmrb_mcp/<port>.lock, which this tool then
#      holds itself for the run, so the MCP tools are refused meanwhile) or
#      any other process with the port open (a monitor). It never takes the
#      port from them
#   2. (unless --no-flash) moves the developer's build tree aside
#      (build_tree.rb), builds the board with the pinned options
#      (Target#firmware_env, the same as rake regress:size), writes the app
#      partition only (rake flash:app: storage and /home are left alone; a
#      full flash would wipe /home) and puts the build tree back
#   3. resets the board (RTS pulse, or just opening the port on a native
#      USB-Serial-JTAG board) and captures the boot with
#      tools/fmrb_serial_capture.py until the periodic dump has come DUMPS
#      times (default 3, about 35 s after the reset), each line stamped with
#      its arrival time
#   4. reads the log (boot_log.rb), writes tmp/regress/boot/<board>.json and
#      the stamped log <board>.log (device names and addresses masked), and
#      compares with test/regress/boot_baseline.json
#
# Calibration (--calibrate N): the firmware on the board (--flash: build and
# flash it once first) is booted N times; each number's spread over the runs
# gives a tolerance proposal per kind (calibrate.rb, BootLog::CALIBRATE) and
# the per-number medians a baseline candidate. Both go to
# tmp/regress/boot/<board>.calibrate.json with a few lines of summary;
# --apply writes them to the baseline (the board's own "tolerance" there
# overrides the file's default one), after a person has read the summary.
#
# Exit status 1 on a crash marker, a reboot, a missing step, a number worse
# than its tolerance, no baseline, or a failed build / flash / capture.
require "fileutils"
require "json"
require "optparse"
require "time"
require_relative "targets"
require_relative "build_tree"
require_relative "boot_log"
require_relative "calibrate"

module Regress
  CORE = File.expand_path("../..", __dir__) unless const_defined?(:CORE)

  module Boot
    OUT = File.join(CORE, "tmp", "regress", "boot")
    BASELINE = File.join(CORE, "test", "regress", "boot_baseline.json")
    CAPTURE = File.expand_path("../tools/fmrb_serial_capture.py", CORE)
    MAX_CAPTURE_SECS = 90

    class Busy < RuntimeError; end

    module_function

    def board(name)
      name = name.to_s.strip
      name = ENV["FMRB_HW_TARGET"].to_s.strip if name.empty?
      raise "say which board: BOARD=<name> (#{TARGETS.select(&:serial).map(&:name).join(', ')})" if name.empty?
      t = TARGETS.find { |x| x.name.casecmp?(name) || x.firmware.to_s.casecmp?(name) }
      raise "BOARD=#{name}: no such board in tools/regress/targets.rb" unless t
      raise "#{t.name}: no serial settings in targets.rb" unless t.serial
      t
    end

    def commit
      out = `git -C #{CORE} rev-parse --short=8 HEAD 2>/dev/null`.strip
      dirty = `git -C #{CORE} status --porcelain --untracked-files=no --ignore-submodules=all -- . ':!doc' ':!test/regress' ':!tools/regress' 2>/dev/null`
      dirty.strip.empty? ? out : "#{out}+dirty"
    end

    # --- the port -----------------------------------------------------------

    # Serial ports whose USB device has this VID:PID.
    def ports_for(vidpid)
      vid, pid = vidpid.downcase.split(":")
      Dir["/sys/class/tty/{ttyACM,ttyUSB}*"].sort.filter_map do |tty|
        dir = File.realpath(File.join(tty, "device")) rescue next
        dir = File.dirname(dir) until dir == "/" || File.exist?(File.join(dir, "idVendor"))
        next if dir == "/"
        v = File.read(File.join(dir, "idVendor")).strip
        p = File.read(File.join(dir, "idProduct")).strip
        "/dev/#{File.basename(tty)}" if v == vid && p == pid
      end
    end

    def resolve_port(t)
      return ENV["PORT"] if ENV["PORT"] && !ENV["PORT"].empty?
      found = ports_for(t.serial[:usb])
      if found.empty?
        raise "#{t.firmware}: no serial port with USB #{t.serial[:usb]} (attached to WSL? `rake attach`)"
      end
      if found.size > 1
        raise "#{t.firmware}: #{found.join(', ')} all have USB #{t.serial[:usb]}; say which with PORT="
      end
      found.first
    end

    def mcp_lock_path(port)
      dir = File.expand_path(ENV["FMRB_MCP_STATE_DIR"] || "~/.fmrb_mcp")
      File.join(dir, port.gsub(/[^A-Za-z0-9]+/, "-").sub(/\A-+/, "") + ".lock")
    end

    # Take the MCP server's lock for the port (so its serial tools are
    # refused while we run) and check no other process has the port open.
    # Returns the open lock file; raises Busy without touching the port.
    def claim_port(port)
      path = mcp_lock_path(port)
      FileUtils.mkdir_p(File.dirname(path))
      f = File.open(path, File::RDWR | File::CREAT, 0o644)
      unless f.flock(File::LOCK_EX | File::LOCK_NB)
        holder = f.read.to_s.strip
        f.close
        raise Busy, "#{port} is held by the fmrb MCP server (its serial capture or flash) or another regress:boot#{holder.empty? ? '' : " (#{holder})"}. " \
                    "Stop it there first (serial_stop), then run again"
      end
      holders = port_holders(port)
      unless holders.empty?
        f.close
        raise Busy, "#{port} is open in another process: #{holders.join('; ')}. Close it (a serial monitor?) and run again"
      end
      f.truncate(0)
      f.write("rake regress:boot pid #{Process.pid} since #{Time.now.iso8601}")
      f.flush
      f
    end

    # Processes (that we may look at) with the port open.
    def port_holders(port)
      real = File.realpath(port) rescue port
      Dir["/proc/[0-9]*"].filter_map do |d|
        pid = File.basename(d).to_i
        next if pid == Process.pid
        fds = Dir["#{d}/fd/*"] rescue next
        hit = fds.any? { |fd| (l = File.readlink(fd) rescue nil) && (l == port || l == real) }
        next unless hit
        cmd = (File.read("#{d}/cmdline").tr("\0", " ").strip rescue "?")
        "pid #{pid} (#{cmd[0, 80]})"
      end
    end

    # --- build and flash ----------------------------------------------------------

    def run_logged(env, cmd, log)
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      pid = nil
      File.open(log, "w") do |f|
        pid = Process.spawn(env, *cmd, chdir: CORE, out: f, err: f, pgroup: true)
        _, st = Process.wait2(pid)
        pid = nil
        return [st.success?, Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0]
      end
    ensure
      if pid
        Process.kill("-TERM", pid) rescue nil
        Process.wait(pid) rescue nil
      end
    end

    def rake = ENV["RAKE"] || "rake"

    # Build with the pinned options and write the app partition. Returns
    # timings; raises on a failure (the build tree is put back either way).
    def build_and_flash(t, port, timings)
      tree = BuildTree.new(CORE)
      tree.stash!
      begin
        tree.clean_all!
        env = t.firmware_env
        ok, secs = run_logged(env, [rake, "build:esp32"], File.join(OUT, "#{t.name}.build.log"))
        timings["build"] = secs.round
        raise "build failed (tmp/regress/boot/#{t.name}.build.log)" unless ok
        flash_log = File.join(OUT, "#{t.name}.flash.log")
        bauds = [t.serial[:flash_baud], 115_200].uniq
        ok = false
        bauds.each_with_index do |baud, i|
          ok, secs = run_logged(env.merge("PORT" => port, "FLASH_BAUD" => baud.to_s), [rake, "flash:app"],
                                i.zero? ? flash_log : flash_log.sub(".log", ".retry.log"))
          timings["flash"] = (timings["flash"] || 0) + secs.round
          timings["flash_baud"] = baud
          break if ok
          warn "boot: app-only flash at #{baud} failed, again at 115200" if i + 1 < bauds.size
        end
        raise "app-only flash failed (tmp/regress/boot/#{t.name}.flash*.log)" unless ok
      ensure
        puts "build/: #{tree.restore!}"
      end
    end

    # --- capture --------------------------------------------------------------

    # Reset the board and capture its boot until `dumps` periodic dumps (and
    # the VM pool table after the last one) have come, a crash has had a few
    # seconds to print, or MAX_CAPTURE_SECS passed. Writes the stamped,
    # masked log; returns its text.
    def capture(t, port, dumps, log)
      raw = log.sub(/\.log\z/, ".raw")
      FileUtils.rm_f(raw)
      cmd = ["python3", CAPTURE, "-p", port, "-b", t.serial[:baud].to_s, "-t", MAX_CAPTURE_SECS.to_s]
      cmd << "--no-reset" if t.serial[:reset] == "open" # opening reboots it already
      cmd << raw
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      err = File.join(OUT, "#{t.name}.capture.err")
      pid = Process.spawn(*cmd, in: File::NULL, out: err, err: err)
      stamped = +""
      pending = +""
      pos = 0
      seen_dumps = 0
      stop_at = nil
      crash_at = nil
      loop do
        now = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
        if File.exist?(raw) && File.size(raw) > pos
          chunk = File.open(raw, "rb") { |f| f.seek(pos); f.read }
          pos += chunk.bytesize
          pending << chunk.force_encoding(Encoding::UTF_8).scrub("?")
          while (i = pending.index("\n"))
            line = pending.slice!(0..i).chomp.delete("\r")
            stamped << format("[%7.3f] %s\n", now, line)
            seen_dumps += 1 if line.include?("IRAM free:")
            crash_at ||= now if BootLog::CRASH.match?(line)
            # the VM pool table closes a dump
            stop_at ||= now + 0.5 if seen_dumps >= dumps && line =~ /fmrb_app: -{8,}/
          end
        end
        stop_at ||= crash_at + 5 if crash_at
        break if stop_at && now >= stop_at
        break unless (Process.waitpid(pid, Process::WNOHANG).nil? rescue false)
        sleep 0.1
      end
      if (Process.kill(0, pid) rescue false)
        Process.kill("TERM", pid) rescue nil
        Process.wait(pid) rescue nil
      end
      if File.exist?(raw) && File.size(raw) > pos
        pending << File.open(raw, "rb") { |f| f.seek(pos); f.read }.force_encoding(Encoding::UTF_8).scrub("?")
        while (i = pending.index("\n"))
          stamped << format("[%7.3f] %s\n", Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0,
                            pending.slice!(0..i).chomp.delete("\r"))
        end
      end
      stamped << format("[%7.3f] %s\n", Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0, pending) unless pending.empty?
      text = BootLog.mask(stamped.scrub("?"))
      File.write(log, text)
      FileUtils.rm_f(raw)
      if text.empty?
        msg = File.exist?(err) ? File.read(err).strip : ""
        raise "nothing came from #{port}#{msg.empty? ? '' : ": #{msg.lines.last(2).join(' ').strip}"}"
      end
      [text, Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0]
    end

    # --- results -------------------------------------------------------------

    def load_baseline
      File.exist?(BASELINE) ? JSON.parse(File.read(BASELINE)) : { "boards" => {} }
    end

    # The default tolerance (the file's, over BootLog's), or a board's: its
    # own "tolerance" in the baseline (a calibration's) over the default.
    def tolerance(doc, board = nil)
      tol = BootLog::TOLERANCE.merge(doc["tolerance"] || {})
      board ? tol.merge(doc.dig("boards", board, "tolerance") || {}) : tol
    end

    # One board per call (the tolerance is the board's).
    def show(results, doc, timing)
      lines, failed = BootLog.table(results, doc["boards"], tolerance(doc, results.keys.first))
      puts lines
      verdict = failed.empty? ? "ok" : "FAILED"
      puts "boot: #{results.keys.join(', ')} vs baseline #{doc['commit'] || 'none'}, #{verdict} (#{timing})"
      failed.empty?
    end

    def main(argv)
      opts = { board: ENV["BOARD"], flash: ENV["FLASH"] != "0", table: ENV["TABLE"] == "1", update: false,
               dumps: (ENV["DUMPS"] || 3).to_i, calibrate: nil, apply: false }
      OptionParser.new do |o|
        o.on("--board NAME") { |v| opts[:board] = v }
        o.on("--no-flash") { opts[:flash] = false }
        o.on("--flash") { opts[:flash_once] = true } # calibration only: it never flashes otherwise
        o.on("--calibrate N", Integer) { |v| opts[:calibrate] = v }
        o.on("--apply") { opts[:apply] = true }
        o.on("--table") { opts[:table] = true }
        o.on("--update") { opts[:update] = true }
        o.on("--dumps N", Integer) { |v| opts[:dumps] = v }
      end.parse!(argv)
      t = board(opts[:board])
      FileUtils.mkdir_p(OUT)
      json = File.join(OUT, "#{t.name}.json")
      return update(t, json) if opts[:update]
      return apply(t) if opts[:apply]
      return calibrate(t, opts.merge(flash: opts[:flash_once] == true)) if opts[:calibrate]

      doc = load_baseline
      if opts[:table]
        raise "no saved result for #{t.name} (run without TABLE=1 first)" unless File.exist?(json)
        return show({ t.name => JSON.parse(File.read(json)) }, doc, "table only") ? 0 : 1
      end

      timings = {}
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      port = resolve_port(t)
      lock = claim_port(port)
      begin
        build_and_flash(t, port, timings) if opts[:flash]
        log = File.join(OUT, "#{t.name}.log")
        text, secs = capture(t, port, opts[:dumps], log)
        timings["capture"] = secs.round
      ensure
        lock.truncate(0) rescue nil
        lock.flock(File::LOCK_UN) rescue nil
        lock.close rescue nil
      end
      res = BootLog.parse(text)
      res = { "board" => t.name, "firmware" => t.firmware, "flashed" => opts[:flash],
              "commit" => opts[:flash] ? commit : nil, "env" => opts[:flash] ? t.firmware_env : nil,
              "captured_at" => Time.now.iso8601, "timings" => timings,
              "log_lines" => text.count("\n") }.merge(res)
      File.write(json, JSON.pretty_generate(res))
      took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      parts = timings.reject { |k, _| k == "flash_baud" }.map { |k, v| "#{k} #{v}s" }.join(", ")
      timing = "#{(took / 60).round(1)} min: #{parts}; #{opts[:flash] ? 'app-only flash' : 'not flashed'}; " \
               "log #{log.sub("#{CORE}/", '')}"
      show({ t.name => res }, doc, timing) ? 0 : 1
    end

    def update(t, json)
      raise "no saved result for #{t.name} (run rake regress:boot first)" unless File.exist?(json)
      r = JSON.parse(File.read(json))
      unless BootLog.clean?(r)
        raise "#{t.name}: the last boot was not clean (crash / reboot / no dump); not a baseline"
      end
      doc = load_baseline
      keep = %w[board firmware flashed commit version running_on reset m1 iram_free psram_free tasks pools
                boot_seconds dumps]
      # a calibrated tolerance stays with the board
      doc["boards"][t.name] = r.slice(*keep).merge((doc["boards"][t.name] || {}).slice("tolerance", "calibration"))
      doc["boards"] = doc["boards"].sort.to_h
      write_baseline(doc)
      puts "baseline: #{t.firmware} from #{r['commit'] || r['version']} (#{r['m1'].size} M1| steps, #{r['tasks'].size} tasks)"
      0
    end

    KEEP = %w[board firmware flashed commit version running_on reset m1 iram_free psram_free tasks pools
              boot_seconds dumps].freeze

    def write_baseline(doc)
      doc["tolerance"] = tolerance(doc)
      doc["commit"] = doc["boards"].values.map { |b| b["commit"] || b["version"] }.uniq.join(",")
      FileUtils.mkdir_p(File.dirname(BASELINE))
      File.write(BASELINE, JSON.pretty_generate(doc) + "\n")
    end

    # --- calibration -----------------------------------------------------------

    def calibrate(t, opts)
      runs = opts[:calibrate]
      raise "--calibrate needs at least 2 runs" if runs < 2
      timings = {}
      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      port = resolve_port(t)
      lock = claim_port(port)
      results = []
      begin
        build_and_flash(t, port, timings) if opts[:flash]
        runs.times do |i|
          log = File.join(OUT, "#{t.name}.calibrate.#{i + 1}.log")
          text, secs = capture(t, port, opts[:dumps], log)
          (timings["capture"] ||= []) << secs.round
          results << BootLog.parse(text)
        end
      ensure
        lock.truncate(0) rescue nil
        lock.flock(File::LOCK_UN) rescue nil
        lock.close rescue nil
      end
      doc = load_baseline
      cal = calibration(t, results, doc, flashed: opts[:flash])
      cal["timings"] = timings
      json = File.join(OUT, "#{t.name}.calibrate.json")
      File.write(json, JSON.pretty_generate(cal))
      took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      caps = timings["capture"]
      timing = "#{(took / 60).round(1)} min: #{timings['build'] ? "build #{timings['build']}s, flash #{timings['flash']}s, " : ''}" \
               "capture #{caps.min}-#{caps.max}s x#{caps.size}"
      puts calibration_summary(cal, doc)
      puts "calibrate: #{t.name} #{runs} boots (#{timing}); #{json.sub("#{CORE}/", '')}"
      if cal["clean"]
        puts "  to make it the baseline, after reading the above: rake regress:boot:calibrate:apply BOARD=#{t.name}"
        0
      else
        1
      end
    end

    # The numbers of N boots -> spreads, proposals and the baseline candidate.
    def calibration(t, results, doc, flashed:)
      items = Calibrate.collect(results.map { |r| BootLog.flat(r) })
      stats = Calibrate.measure(items, BootLog::CALIBRATE, results.size)
      unclean = results.each_with_index.reject { |r, _| BootLog.clean?(r) }.map do |r, i|
        "run #{i + 1}: crash #{r['crash'].size}, boots #{r['boots']}, boot complete #{r['boot_complete']}, dumps #{r['dumps']}"
      end
      cand = Calibrate.median_record(results, BootLog::MEASURED)
      base = { "board" => t.name, "firmware" => t.firmware, "flashed" => flashed,
               "commit" => flashed ? commit : nil }.merge(cand).slice(*KEEP)
      { "board" => t.name, "firmware" => t.firmware, "runs" => results.size, "clean" => unclean.empty?,
        "unclean" => unclean, "captured_at" => Time.now.iso8601,
        "boot_seconds" => results.filter_map { |r| r["boot_seconds"] }, "stats" => stats,
        "now" => tolerance(doc, t.name), "tolerance" => stats.transform_values { |s| s["proposal"] },
        "baseline" => base }
    end

    def calibration_summary(cal, doc)
      now = cal["now"].transform_values { |v| Calibrate.fmt(v) }
      lines = Calibrate.table(cal["stats"], now)
      cal["unclean"].each { |u| lines << "  NOT CLEAN #{u} (no proposal can be applied)" }
      secs = cal["boot_seconds"]
      lines << "  boot #{secs.min}-#{secs.max}s" unless secs.empty?
      old = doc.dig("boards", cal["board"])
      if old
        b = cal["baseline"]
        was = BootLog.flat(old).to_h { |k, v, _| [k, v] }
        diffs = BootLog.flat(b).filter_map { |k, v, _| (w = was[k]) && v && v != w ? [k, v - w] : nil }
        missing = (was.keys - BootLog.flat(b).map(&:first)).size
        top = diffs.max_by { |_, d| d.abs }
        lines << "  candidate vs the baseline now: #{diffs.size} of #{was.size} numbers differ" \
                 "#{top ? " (most: #{top[0]} #{top[1].positive? ? '+' : ''}#{Calibrate.fmt(top[1])})" : ''}" \
                 "#{missing.positive? ? ", #{missing} missing" : ''}"
      else
        lines << "  no baseline for #{cal['board']} yet"
      end
      lines
    end

    def apply(t)
      json = File.join(OUT, "#{t.name}.calibrate.json")
      raise "no calibration for #{t.name} (run rake regress:boot:calibrate first)" unless File.exist?(json)
      cal = JSON.parse(File.read(json))
      raise "#{t.name}: the calibration had boots that were not clean; not a baseline" unless cal["clean"]
      doc = load_baseline
      doc["boards"][t.name] = cal["baseline"].merge(
        "tolerance" => cal["tolerance"],
        "calibration" => { "runs" => cal["runs"], "at" => cal["captured_at"],
                           "spread" => cal["stats"].transform_values { |s| s["spread"] } }
      )
      doc["boards"] = doc["boards"].sort.to_h
      write_baseline(doc)
      tol = cal["tolerance"].map { |k, v| "#{k} #{Calibrate.fmt(v)}" }.join(", ")
      puts "baseline: #{t.firmware} from a #{cal['runs']}-boot calibration (medians); tolerance #{tol}"
      0
    end
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    exit Regress::Boot.main(ARGV)
  rescue Regress::Boot::Busy => e
    warn "boot: port busy: #{e.message}"
    exit 2
  rescue RuntimeError, KeyError, Errno::ENOENT => e
    warn "boot: #{e.message}"
    exit 1
  end
end
