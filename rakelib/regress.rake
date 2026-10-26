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
#   rake regress:run:calibrate the scenario RUNS times on one build: spreads
#                          and tolerance proposals (APPLY=1 or :apply writes
#                          them, after a person has read the summary). The
#                          same for a board: rake regress:boot:calibrate
#   rake regress:screens   scripted screens (test/regress/screens/*.rb) on the
#                          sim (Retro, Modern) and the browser build, pixel
#                          for pixel against test/regress/screens/baseline/.
#                          ONLY=retro|modern|web, SCENE=<text>, FULL=1 (also
#                          everything on mruby), WEB_PORT=n (the page's server,
#                          default 8016)
#   rake regress:screens:update  the last run's failed or new shots become the
#                          baseline (ONLY=, SCENE= narrow it)
#   rake regress:live      live scenes (test/regress/live/*.rb) on the Modern
#                          sim with their counterparts started by the tool: a
#                          local game host (othello, board, mahjong), a step
#                          sender (walk_scroller), the serial MIDI monitor, the
#                          APU's samples; the counterparts' state against
#                          test/regress/live/baseline/. ONLY=<text> (or
#                          SCENE=) narrows the scenes
#   rake regress:live:update   the last run's failed or new checks and shots
#                          become the baseline (ONLY= narrows it)
#   rake regress:full      regress + regress:size + regress:run +
#                          regress:screens + regress:live (ONLY= goes to none
#                          of them: run the parts by hand to narrow them)

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

    desc "Run the sim scenario RUNS times (default 5) on one build per family (ONLY=retro|modern), propose tolerances; APPLY=1 writes the last calibration as the baseline"
    task :calibrate do
      if ENV["APPLY"] == "1"
        Rake::Task["regress:run:calibrate:apply"].invoke
        next
      end
      args = ["--calibrate", ENV["RUNS"] || "5"]
      args += ["--only", ENV["ONLY"]] if ENV["ONLY"]
      abort "regress:run:calibrate: failed" unless system(Gem.ruby, REGRESS_SIM, *args)
    end

    namespace :calibrate do
      desc "Write the last regress:run:calibrate (medians and tolerances) to the sim baseline (after a person has read its summary)"
      task :apply do
        args = ENV["ONLY"] ? ["--only", ENV["ONLY"]] : []
        abort "regress:run:calibrate:apply: failed" unless system(Gem.ruby, REGRESS_SIM, "--apply", *args)
      end
    end
  end

  desc "Everything: regress + regress:size (one board per chip; ALL=1 every board) + regress:run + regress:screens (sim, browser) + regress:live. About 20 minutes"
  task :full do
    only = ENV.delete("ONLY")
    warn "regress:full: ONLY=#{only} ignored (narrow the parts one by one)" if only
    failed = []
    %w[regress regress:size regress:run regress:screens regress:live].each do |t|
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

REGRESS_SCREENS = File.join(ROOT_DIR, "tools", "regress", "screens.rb")

def regress_screens_args
  args = []
  args += ["--only", ENV["ONLY"]] if ENV["ONLY"]
  args += ["--scene", ENV["SCENE"]] if ENV["SCENE"]
  args << "--full" if ENV["FULL"] == "1"
  args += ["--port", ENV["WEB_PORT"]] if ENV["WEB_PORT"]
  args
end

namespace :regress do
  desc "Scripted screens on the sim and the browser build vs baseline PNGs (ONLY=retro|modern|web, SCENE=, FULL=1, WEB_PORT=)"
  task :screens do
    abort "regress:screens: failed" unless system(Gem.ruby, REGRESS_SCREENS, *regress_screens_args)
  end

  namespace :screens do
    desc "Write the last regress:screens run's failed or new shots as the baseline (only for a change a person has checked)"
    task :update do
      args = ["--update"]
      args += ["--only", ENV["ONLY"]] if ENV["ONLY"]
      args += ["--scene", ENV["SCENE"]] if ENV["SCENE"]
      abort "regress:screens:update: failed" unless system(Gem.ruby, REGRESS_SCREENS, *args)
    end
  end
end

REGRESS_LIVE = File.join(ROOT_DIR, "tools", "regress", "live.rb")

def regress_live_args
  scene = ENV["ONLY"] || ENV["SCENE"]
  scene ? ["--scene", scene] : []
end

namespace :regress do
  desc "Live scenes on the Modern sim, counterparts started by the tool (game host, step sender, MIDI monitor, audio probe) vs baseline (ONLY=<scene text>)"
  task :live do
    abort "regress:live: failed" unless system(Gem.ruby, REGRESS_LIVE, *regress_live_args)
  end

  namespace :live do
    desc "Write the last regress:live run's failed or new checks and shots as the baseline (only for a change a person has checked)"
    task :update do
      abort "regress:live:update: failed" unless system(Gem.ruby, REGRESS_LIVE, "--update", *regress_live_args)
    end
  end
end

# The boot log of a connected board against test/regress/boot_baseline.json
# (tools/regress/boot.rb, doc/regression/usage.md chapter 8). Needs the board
# and its serial port, so it is in neither rake regress nor rake regress:full.
REGRESS_BOOT = File.join(ROOT_DIR, "tools", "regress", "boot.rb")

def regress_boot_args
  args = []
  args += ["--board", ENV["BOARD"]] if ENV["BOARD"]
  args << "--no-flash" if ENV["FLASH"] == "0"
  args << "--table" if ENV["TABLE"] == "1"
  args += ["--dumps", ENV["DUMPS"]] if ENV["DUMPS"]
  args
end

namespace :regress do
  desc "Boot log of a connected board vs baseline: build, app-only flash, capture (BOARD=<name>, FLASH=0, TABLE=1, DUMPS=n, PORT=)"
  task :boot do
    regress_check_board_defaults
    ok = system({ "RAKE" => ENV["RAKE"] || "rake" }, Gem.ruby, REGRESS_BOOT, *regress_boot_args)
    abort "regress:boot: failed" unless ok
  end

  namespace :boot do
    desc "Write the last regress:boot result of BOARD= as its baseline (only for a boot a person has checked)"
    task :update do
      args = ENV["BOARD"] ? ["--board", ENV["BOARD"]] : []
      abort "regress:boot:update: failed" unless system(Gem.ruby, REGRESS_BOOT, "--update", *args)
    end

    desc "Boot log reader and calibration tests: made-up logs, no device (host Ruby)"
    task :test do
      sh "#{Gem.ruby} test/regress/boot_log_test.rb"
      sh "#{Gem.ruby} test/regress/calibrate_test.rb"
    end

    desc "Boot BOARD= RUNS times (default 5) on the firmware it has (FLASH=1: build and flash once first), propose tolerances; APPLY=1 writes the last calibration as the baseline"
    task :calibrate do
      if ENV["APPLY"] == "1"
        Rake::Task["regress:boot:calibrate:apply"].invoke
        next
      end
      regress_check_board_defaults if ENV["FLASH"] == "1"
      args = ["--calibrate", ENV["RUNS"] || "5"]
      args += ["--board", ENV["BOARD"]] if ENV["BOARD"]
      args << "--flash" if ENV["FLASH"] == "1"
      args += ["--dumps", ENV["DUMPS"]] if ENV["DUMPS"]
      ok = system({ "RAKE" => ENV["RAKE"] || "rake" }, Gem.ruby, REGRESS_BOOT, *args)
      abort "regress:boot:calibrate: failed" unless ok
    end

    namespace :calibrate do
      desc "Write the last regress:boot:calibrate of BOARD= (medians and tolerances) to the boot baseline (after a person has read its summary)"
      task :apply do
        args = ENV["BOARD"] ? ["--board", ENV["BOARD"]] : []
        abort "regress:boot:calibrate:apply: failed" unless system(Gem.ruby, REGRESS_BOOT, "--apply", *args)
      end
    end
  end
end
