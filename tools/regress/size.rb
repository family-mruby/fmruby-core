#!/usr/bin/env ruby
# frozen_string_literal: true
#
# size.rb - the firmware size table (rake regress:size, doc/regression/usage.md).
#
#   ruby tools/regress/size.rb                # build every board with firmware, compare
#   ruby tools/regress/size.rb --only sticks3 # one board (name or FMRB_HW_TARGET, comma list)
#   ruby tools/regress/size.rb --table        # no build: compare the last saved results
#   ruby tools/regress/size.rb --update       # the saved results become the baseline
#   ruby tools/regress/size.rb --restore      # put back a build tree a killed run left aside
#   ruby tools/regress/size.rb --no-restore   # leave the last board's build in build/
#
# For each board in targets.rb that has `firmware`: clean_all, `rake
# build:esp32` with FMRB_HW_TARGET and the pinned build options
# (Target#firmware_env, so .env does not move the numbers), then esp_idf_size
# (the tool behind `idf.py size` / `size-components` / `size-files`, json2)
# on the map file. The developer's build tree is moved aside first and put
# back at the end (build_tree.rb).
#
# Compared with test/regress/size_baseline.json:
#   - every internal memory type (DIRAM, IRAM, RTC / LP / HP core RAM): a
#     growth fails the run. A shrink is shown (update the baseline to keep it)
#   - flash (the app image) and the app partition's free space: shown, never
#     fail
#   - the archives (size-components) whose numbers moved: the top few lines
#   - each Spinel program's static internal RAM (its generated TU in
#     size-files): shown per board
#
# Results: tmp/regress/size/<board>.json (and the raw json2 next to it), the
# build log in tmp/regress/size/<board>.build.log. Exit status 1 when
# internal RAM grew, a build failed or a board has no baseline.
require "fileutils"
require "json"
require "open3"
require "optparse"
require_relative "targets"
require_relative "build_tree"

module Regress
  CORE = File.expand_path("../..", __dir__) unless const_defined?(:CORE)

  module Size
    OUT = File.join(CORE, "tmp", "regress", "size")
    BASELINE = File.join(CORE, "test", "regress", "size_baseline.json")
    SPINEL_PROGRAMS = {
      "fmrb_kernel_combined" => "kernel", "system_desktop_combined" => "desktop",
      "editor_combined" => "editor", "fft_kernel" => "fft", "raycast_kernel" => "raycast",
      "spinel_hello_kernel" => "hello"
    }.freeze
    COMPONENT_LINES = 6

    module_function

    def kind(mem)
      return :flash if mem.start_with?("Flash")
      return :psram if mem.start_with?("External") || mem.include?("PSRAM")
      :internal
    end

    def boards(only)
      all = TARGETS.select(&:firmware)
      return all if only.nil? || only.empty?
      want = only.split(",").map { |w| w.strip.downcase }
      sel = all.select { |t| want.include?(t.name.downcase) || want.include?(t.firmware.downcase) }
      raise "ONLY=#{only}: no such board (#{all.map(&:firmware).join(', ')})" if sel.empty?
      sel
    end

    def image
      return ENV["REGRESS_IDF_IMAGE"] if ENV["REGRESS_IDF_IMAGE"] && !ENV["REGRESS_IDF_IMAGE"].empty?
      ver = File.readlines(File.join(CORE, ".env")).map { |l| l[/^\s*ESP_IDF_VERSION\s*=\s*([^#\s]+)/, 1] }.compact.first
      "ghcr.io/family-mruby/fmruby-esp32-build:#{ver || 'v5.5.4'}"
    end

    def commit
      out, st = Open3.capture2("git", "-C", CORE, "rev-parse", "--short=8", "HEAD")
      sha = st.success? ? out.strip : "?"
      # Documents and the regression tool itself do not go into a firmware.
      dirty, = Open3.capture2("git", "-C", CORE, "status", "--porcelain", "--untracked-files=no",
                              "--ignore-submodules=all", "--", ".", ":!doc", ":!test/regress", ":!tools/regress")
      dirty.strip.empty? ? sha : "#{sha}+dirty"
    end

    # Run a command, its output to a log; returns [ok, seconds].
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

    # --- one board --------------------------------------------------------

    def build(t, tree)
      FileUtils.mkdir_p(OUT)
      log = File.join(OUT, "#{t.name}.build.log")
      tree.clean_all!
      env = t.firmware_env
      ok, secs = run_logged(env, [ENV["RAKE"] || "rake", "build:esp32"], log)
      [ok, secs, log]
    end

    def measure(t)
      raw = File.join(OUT, t.name)
      FileUtils.rm_rf(raw)
      FileUtils.mkdir_p(raw)
      rel = raw.sub("#{CORE}/", "")
      size = "python -m esp_idf_size --format json2 /project/build/fmruby-core.map"
      script = "#{size} -o /project/#{rel}/summary.json && " \
               "#{size} --archives -o /project/#{rel}/archives.json && " \
               "#{size} --files -o /project/#{rel}/files.json"
      cmd = ["docker", "run", "--rm", "--user", "#{Process.uid}:#{Process.gid}", "-e", "HOME=/tmp",
             "-e", "ESP_IDF_SIZE_NG=1", "-v", "#{CORE}:/project", image, "bash", "-c", script]
      ok, = run_logged({}, cmd, File.join(raw, "size.log"))
      raise "esp_idf_size failed for #{t.name} (#{rel}/size.log)" unless ok
      summarize(t, raw)
    end

    def summarize(t, raw)
      summary = JSON.parse(File.read(File.join(raw, "summary.json")))
      memory = {}
      summary.fetch("layout").each { |m| memory[m["name"]] = m["used"] }
      archives = {}
      JSON.parse(File.read(File.join(raw, "archives.json"))).each do |name, a|
        sizes = a.fetch("memory_types").transform_values { |v| v["size"] }.reject { |_k, v| v.zero? }
        archives[name] = sizes unless sizes.empty?
      end
      spinel = {}
      JSON.parse(File.read(File.join(raw, "files.json"))).each do |name, f|
        prog = SPINEL_PROGRAMS.find { |src, _| name.end_with?(":#{src}.c.obj") }
        next unless prog
        by_kind = Hash.new(0)
        f.fetch("memory_types").each { |mem, v| by_kind[kind(mem).to_s] += v["size"] }
        spinel[prog[1]] = by_kind
      end
      bin = File.join(CORE, "build", "fmruby-core.bin")
      part = smallest_app_partition(File.join(CORE, "build", "partition_table", "partition-table.bin"))
      {
        "board" => t.name, "firmware" => t.firmware, "commit" => commit, "env" => t.firmware_env,
        "memory" => memory, "app_bin" => File.size(bin), "app_partition" => part,
        "archives" => archives.sort.to_h, "spinel" => spinel.sort.to_h
      }
    end

    # The smallest app partition, as check_sizes.py measures it.
    def smallest_app_partition(path)
      data = File.binread(path)
      sizes = []
      (0...data.bytesize).step(32) do |off|
        e = data.byteslice(off, 32)
        break unless e && e.bytesize == 32 && e.byteslice(0, 2) == "\xAA\x50".b
        type, _sub, _offset, size = e.unpack("x2CCVV")
        sizes << size if type.zero?
      end
      sizes.min
    end

    # --- comparing ----------------------------------------------------------

    def fmt(n) = n.to_s.reverse.scan(/\d{1,3}/).join(",").reverse.then { |s| n.negative? ? "-#{s}" : s }
    def sfmt(d) = d.zero? ? "+0" : (d.positive? ? "+#{fmt(d)}" : fmt(d))

    # Rows for the table and the verdict for one board.
    def compare(cur, base)
      fails = []
      cols = {}
      internal = cur["memory"].keys.select { |m| kind(m) == :internal }
      internal |= base["memory"].keys.select { |m| kind(m) == :internal } if base
      internal.each do |m|
        now = cur["memory"][m] || 0
        was = base ? (base["memory"][m] || 0) : now
        fails << "#{m} +#{fmt(now - was)}" if now > was
        cols[m] = [now, now - was]
      end
      bin_was = base ? base["app_bin"] : cur["app_bin"]
      free = cur["app_partition"] - cur["app_bin"]
      free_was = base ? base["app_partition"] - base["app_bin"] : free
      { cols: cols, fails: fails, bin: [cur["app_bin"], cur["app_bin"] - bin_was],
        free: [free, free - free_was, 100.0 * free / cur["app_partition"]] }
    end

    def component_diffs(cur, base)
      return [] unless base
      names = cur["archives"].keys | base["archives"].keys
      names.filter_map do |n|
        a = cur["archives"][n] || {}
        b = base["archives"][n] || {}
        d = Hash.new(0)
        (a.keys | b.keys).each { |m| d[kind(m)] += (a[m] || 0) - (b[m] || 0) }
        next if d.values.all?(&:zero?)
        [n, d]
      end.sort_by { |_n, d| [-d[:internal].abs, -d[:flash].abs, -d[:psram].abs] }
    end

    def table(results, baseline)
      lines = []
      failed = []
      mem_names = results.values.flat_map { |r| r["memory"].keys.select { |m| kind(m) == :internal } }.uniq
      primary = %w[DIRAM IRAM].select { |m| mem_names.include?(m) }
      other = mem_names - primary
      head = format("%-8s", "board") + primary.map { |m| format(" %-18s", m) }.join
      head += format(" %-12s", "other int.") unless other.empty?
      head += format(" %-21s %s", "app image (flash)", "app partition free")
      lines << head
      comps = []
      results.each do |name, cur|
        base = baseline[name]
        c = compare(cur, base)
        row = format("%-8s", cur["firmware"])
        primary.each do |m|
          v = c[:cols][m]
          row += v ? format(" %-18s", "#{fmt(v[0])} #{sfmt(v[1])}") : format(" %-18s", "-")
        end
        unless other.empty?
          vs = other.filter_map { |m| c[:cols][m] }
          row += format(" %-12s", "#{fmt(vs.sum { |v| v[0] })} #{sfmt(vs.sum { |v| v[1] })}")
        end
        row += format(" %-21s %s %s (%.0f%%)", "#{fmt(c[:bin][0])} #{sfmt(c[:bin][1])}",
                      fmt(c[:free][0]), sfmt(c[:free][1]), c[:free][2])
        if base.nil?
          row += "  NO BASELINE"
          failed << "#{cur['firmware']}: no baseline"
        elsif !c[:fails].empty?
          row += "  FAIL internal RAM grew: #{c[:fails].join(', ')}"
          failed << "#{cur['firmware']}: #{c[:fails].join(', ')}"
        end
        lines << row
        component_diffs(cur, base).first(3).each { |n, d| comps << [cur["firmware"], n, d] }
      end
      lines.concat(spinel_lines(results, baseline))
      unless comps.empty?
        lines << "archives that moved (size-components, top #{COMPONENT_LINES}; full: tmp/regress/size/<board>.json):"
        comps.sort_by { |_b, _n, d| [-d[:internal].abs, -d[:flash].abs] }.first(COMPONENT_LINES).each do |b, n, d|
          lines << format("  %-8s %-28s internal %-8s flash %-10s psram %s", b, n, sfmt(d[:internal]),
                          sfmt(d[:flash]), sfmt(d[:psram]))
        end
      end
      [lines, failed]
    end

    # Each Spinel program's static internal RAM (bytes of .data/.bss in
    # internal memory from its generated TU); the PSRAM part in brackets.
    def spinel_lines(results, baseline)
      progs = results.values.flat_map { |r| r["spinel"].keys }.uniq
      return [] if progs.empty?
      out = ["spinel TU static internal RAM (B) [psram]:"]
      results.each do |name, cur|
        base = baseline[name]
        cells = progs.map do |p|
          v = cur["spinel"][p]
          next "#{p} -" unless v
          was = base && base["spinel"][p] ? base["spinel"][p]["internal"] : v["internal"]
          d = v["internal"] - was
          "#{p} #{fmt(v['internal'])}#{d.zero? ? '' : " (#{sfmt(d)})"}#{v['psram'].to_i.zero? ? '' : " [#{fmt(v['psram'])}]"}"
        end
        out << format("  %-8s %s", cur["firmware"], cells.join("  "))
      end
      out
    end

    def load_results(boards)
      boards.each_with_object({}) do |t, h|
        f = File.join(OUT, "#{t.name}.json")
        h[t.name] = JSON.parse(File.read(f)) if File.exist?(f)
      end
    end

    def load_baseline
      File.exist?(BASELINE) ? JSON.parse(File.read(BASELINE)).fetch("boards") : {}
    end

    # --- main -----------------------------------------------------------------

    def main(argv)
      opts = { only: ENV["ONLY"], table: false, update: false, restore: true }
      OptionParser.new do |o|
        o.on("--only LIST") { |v| opts[:only] = v }
        o.on("--table") { opts[:table] = true }
        o.on("--update") { opts[:update] = true }
        o.on("--no-restore") { opts[:restore] = false }
        o.on("--restore") { opts[:restore_only] = true }
      end.parse!(argv)
      tree = BuildTree.new(CORE)
      if opts[:restore_only]
        puts "build/: #{tree.restore!}"
        return 0
      end
      sel = boards(opts[:only])
      return update(sel) if opts[:update]

      t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      build_failed = []
      timings = []
      unless opts[:table]
        before = tree.stash!
        begin
          sel.each do |t|
            ok, secs, log = build(t, tree)
            timings << "#{t.firmware} #{secs.round}s"
            if ok
              res = measure(t)
              res["build_seconds"] = secs.round
              File.write(File.join(OUT, "#{t.name}.json"), JSON.pretty_generate(res))
            else
              FileUtils.rm_f(File.join(OUT, "#{t.name}.json"))
              build_failed << t
              puts "FAIL build #{t.firmware} (#{log.sub("#{CORE}/", '')})"
              File.readlines(log).last(4).each { |l| puts "     #{l.rstrip}" }
            end
          end
        ensure
          left = opts[:restore] ? tree.restore! : "#{tree.describe} (left by --no-restore; " \
                                                  "#{before} is in #{BuildTree::STASH}/, put back with --restore)"
          puts "build/: #{left}"
        end
      end
      results = load_results(sel - build_failed)
      missing = sel - build_failed - sel.select { |t| results.key?(t.name) }
      missing.each { |t| puts "#{t.firmware}: no saved result (run without --table first)" }
      baseline = load_baseline
      lines, failed = table(results, baseline)
      puts lines
      took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
      base_commit = File.exist?(BASELINE) ? JSON.parse(File.read(BASELINE))["commit"] : "none"
      verdict = failed.empty? && build_failed.empty? && missing.empty? ? "ok" : "FAILED"
      timing = opts[:table] ? "table only" : "#{(took / 60).round(1)} min: #{timings.join(', ')}"
      puts "size: #{results.size} boards vs baseline #{base_commit}, #{verdict} (#{timing})"
      verdict == "ok" ? 0 : 1
    end

    def update(sel)
      results = load_results(sel)
      raise "no saved results to write (run rake regress:size first)" if results.empty?
      doc = File.exist?(BASELINE) ? JSON.parse(File.read(BASELINE)) : { "boards" => {} }
      results.each do |name, r|
        doc["boards"][name] = r.reject { |k, _| k == "build_seconds" }
        puts "baseline: #{r['firmware']} from #{r['commit']}"
      end
      doc["commit"] = results.values.map { |r| r["commit"] }.uniq.join(",")
      doc["boards"] = doc["boards"].sort.to_h
      FileUtils.mkdir_p(File.dirname(BASELINE))
      File.write(BASELINE, JSON.pretty_generate(doc) + "\n")
      0
    end
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    exit Regress::Size.main(ARGV)
  rescue RuntimeError, KeyError, Errno::ENOENT => e
    warn "size: #{e.message}"
    exit 1
  end
end
