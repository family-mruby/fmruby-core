# rakelib/regress.rake
# One command for the cheap regression checks (doc/regression/usage.md):
#
#   rake regress           the host suites of `rake test` (each one's output to
#                          tmp/regress/host/<suite>.log) and the draw-record
#                          comparison (tools/regress/run.rb); a short summary,
#                          the whole diff in tmp/regress/diff.txt
#   rake regress:draw      the draw-record comparison alone (CI runs it after
#                          rake test). ONLY=<text> narrows it to matching runs
#   rake regress:update    write the records as the new baseline, listing what
#                          changed. Only after a person has looked at the diff
#                          and the change is the one intended
#
# No docker, no sim, no device for those. The heavy ones (rake regress:full)
# build firmware in docker and start the sim:
#
#   rake regress:size      one board per chip (S3, P4; size_default in
#                          tools/regress/targets.rb): idf.py size numbers
#                          against test/regress/size_baseline.json; fails when
#                          internal RAM grows. ALL=1 (every board: after
#                          touching drivers, sdkconfig, partitions, storage
#                          excludes, or adding a board), ONLY=<board>[,..],
#                          TABLE=1 (no build, the last results), NO_RESTORE=1
#                          (leave the last build)
#   rake regress:size:all  the same as ALL=1
#   rake regress:size:update   the last results become the baseline
#   rake regress:run       a fixed sim scenario on Retro and Modern: VM pool
#                          usage and the periodic dump's free memory against
#                          test/regress/sim_baseline.json (with a tolerance).
#                          ONLY=retro|modern, TABLE=1 (no sim, the last results)
#   rake regress:run:update    the last results become the baseline
#   rake regress:full      regress + regress:size + regress:run (ONLY= goes to
#                          neither: run the parts by hand to narrow them)

REGRESS_RUN = File.join(ROOT_DIR, "tools", "regress", "run.rb")

def regress_draw_args
  ENV["ONLY"] ? ["--only", ENV["ONLY"]] : []
end

desc "Host tests + draw-record comparison, short summary (full diff in tmp/regress/diff.txt)"
task :regress do
  require "open3"
  log_dir = File.join(ROOT_DIR, "tmp", "regress", "host")
  mkdir_p log_dir, verbose: false
  suites = Rake::Task[:test].prerequisites
  failed = []
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  suites.each do |s|
    log = File.join(log_dir, "#{s.tr(':', '_')}.log")
    out, st = Open3.capture2e(ENV["RAKE"] || "rake", "-s", s, chdir: ROOT_DIR)
    File.write(log, out)
    failed << [s, log, out] unless st.success?
  end
  took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
  failed.each do |s, log, out|
    puts "FAIL host #{s} (#{log.sub(ROOT_DIR + '/', '')})"
    out.lines.last(4).each { |l| puts "     #{l.rstrip}" }
  end
  puts format("host: %d suites, %d passed, %d failed (%.1f s)", suites.size, suites.size - failed.size, failed.size, took)
  draw_ok = system(Gem.ruby, REGRESS_RUN, *regress_draw_args)
  abort "regress: failed" unless failed.empty? && draw_ok
end

namespace :regress do
  desc "Draw-record comparison only (tools/regress/run.rb; ONLY=<text> narrows it)"
  task :draw do
    ok = system(Gem.ruby, REGRESS_RUN, *regress_draw_args)
    abort "regress:draw: failed (full diff: tmp/regress/diff.txt)" unless ok
  end

  desc "Write the draw records as the new baseline (only for a change a person has checked)"
  task :update do
    ok = system(Gem.ruby, REGRESS_RUN, "--update", *regress_draw_args)
    abort "regress:update: some runs failed" unless ok
  end
end

REGRESS_SIZE = File.join(ROOT_DIR, "tools", "regress", "size.rb")

# targets.rb pins each firmware's build options so .env cannot move the
# numbers; a board's Rakefile defaults have to say the same thing.
def regress_check_board_defaults
  require_relative "../tools/regress/targets"
  Regress::TARGETS.select(&:firmware).each do |t|
    (BOARD_BUILD_DEFAULTS[t.firmware] || {}).each do |k, v|
      next if t.firmware_env[k] == v
      abort "regress: tools/regress/targets.rb builds #{t.firmware} with #{k}=#{t.firmware_env[k].inspect}, " \
            "but the Rakefile's BOARD_BUILD_DEFAULTS say #{v.inspect}; make them agree"
    end
  end
end

namespace :regress do
  desc "Firmware size table, one board per chip (ALL=1 every board, ONLY=<board>, TABLE=1 no build, NO_RESTORE=1)"
  task :size do
    regress_check_board_defaults
    args = []
    args += ["--only", ENV["ONLY"]] if ENV["ONLY"]
    args << "--all" if ENV["ALL"] == "1"
    args << "--table" if ENV["TABLE"] == "1"
    args << "--no-restore" if ENV["NO_RESTORE"] == "1"
    ok = system({ "REGRESS_IDF_IMAGE" => IMAGE }, Gem.ruby, REGRESS_SIZE, *args)
    abort "regress:size: failed" unless ok
  end

  namespace :size do
    desc "Write the last regress:size results as the size baseline (only for a change a person has checked)"
    task :update do
      args = ENV["ONLY"] ? ["--only", ENV["ONLY"]] : []
      args << "--all" if ENV["ALL"] == "1"
      abort "regress:size:update: failed" unless system(Gem.ruby, REGRESS_SIZE, "--update", *args)
    end

    desc "Firmware size table for every board with firmware (the same as regress:size ALL=1)"
    task :all do
      ENV["ALL"] = "1"
      Rake::Task["regress:size"].invoke
    end
  end
end

REGRESS_SIM = File.join(ROOT_DIR, "tools", "regress", "sim_run.rb")

namespace :regress do
  desc "Fixed sim scenario (Retro, Modern): VM pools and free memory vs baseline (ONLY=retro|modern, TABLE=1)"
  task :run do
    args = []
    args += ["--only", ENV["ONLY"]] if ENV["ONLY"]
    args << "--table" if ENV["TABLE"] == "1"
    abort "regress:run: failed" unless system(Gem.ruby, REGRESS_SIM, *args)
  end

  namespace :run do
    desc "Write the last regress:run results as the sim baseline (only for a change a person has checked)"
    task :update do
      args = ENV["ONLY"] ? ["--only", ENV["ONLY"]] : []
      abort "regress:run:update: failed" unless system(Gem.ruby, REGRESS_SIM, "--update", *args)
    end
  end

  desc "Everything: regress + regress:size (one board per chip; ALL=1 every board) + regress:run (sim). About 9 minutes"
  task :full do
    only = ENV.delete("ONLY")
    warn "regress:full: ONLY=#{only} ignored (narrow the parts one by one)" if only
    failed = []
    %w[regress regress:size regress:run].each do |t|
      begin
        Rake::Task[t].invoke
      rescue SystemExit
        failed << t
      end
    end
    abort "regress:full: failed: #{failed.join(', ')}" unless failed.empty?
    puts "regress:full: ok"
  end
end
