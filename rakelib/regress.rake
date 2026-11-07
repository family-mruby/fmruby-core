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
#                          sim (Retro, Modern), the browser build and the
#                          native program, pixel for pixel against
#                          test/regress/screens/baseline/.
#                          ONLY=retro|modern|web|native, SCENE=<text>, FULL=1 (also
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
#   rake regress:wl        the compositor (doc/linux_native, L8-L11): the
#                          program with wlroots built in the Ubuntu 26.04 image
#                          (rake native:wl) for the standard and the all-mruby
#                          configuration, and rake native:wl_check headless on
#                          pixman for each (test/native/wl/wl_check.rb), then
#                          the launcher's Linux apps, Xwayland and the end
#                          (test/native/wl/wl_apps_check.rb), then dialogs and
#                          menus (test/native/wl/wl_dialog_check.rb), then the
#                          keyboard layout from Linux's settings
#                          (test/native/wl/wl_keys_check.rb), then files
#                          opened in Linux apps from the File Manager (L11,
#                          test/native/wl/wl_open_check.rb), then the
#                          clipboard with foot, the Shell and an X11 program
#                          (L12a, test/native/wl/wl_clip_check.rb), then the
#                          IME (fcitx5 and mozc) with foot, the editor and an
#                          SDL window, and the kana input without it (L12b,
#                          test/native/wl/wl_ime_check.rb), then maximize,
#                          Super+arrows and the screen lock, with a
#                          logind stand-in on a bus of its own (L13,
#                          test/native/wl/wl_winops_check.rb), then the PC's
#                          settings and robustness: Wi-Fi and Bluetooth with
#                          NetworkManager and BlueZ stand-ins, PipeWire's
#                          volume and its keys, notify-send's notices, the
#                          portal's screen share, the session starting it
#                          again after SIGSEGV (L14,
#                          test/native/wl/wl_settings_check.rb), then the
#                          touch screen through the tools' own (L17,
#                          test/native/wl/wl_touch_check.rb). Headless on a
#                          1280x800 output (a 640x400 screen; the dialogs'
#                          turned one keeps 426x240). No baseline: the
#                          composited screen is checked against the picture
#                          taken at the same moment
#   rake regress:spinel    the Spinel C for the all-Spinel configuration (kernel,
#                          editor and desktop), as CI's spinel-gen job makes
#                          it: generation only, into tmp/regress/spinel/ (the
#                          build's main/prebuild_scripts/spinel/gen is left
#                          alone); the compiler's refused lines on failure.
#                          About 20 s
#   rake regress:full      regress + regress:spinel + regress:size + regress:run +
#                          regress:screens + regress:live + regress:wl +
#                          regress:checkpoint SIM=1 (ONLY=
#                          goes to none of them: run the parts by hand to
#                          narrow them)

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

namespace :regress do
  # The Spinel C for the all-Spinel configuration (kernel, editor and desktop
  # all Spinel), as CI's spinel-gen job makes it. Locally the desktop is built
  # with mruby, so a method the desktop calls that only the mruby binding of
  # FmrbApp has goes unnoticed until CI (doc/regression/report/
  # ci_develop_20261107.md). Generation only, no C compile; written into
  # tmp/regress/spinel/, so the build's main/prebuild_scripts/spinel/gen and
  # the staged gem sources are not touched.
  desc "Spinel C for the all-Spinel configuration (as CI's spinel-gen), into tmp/regress/spinel/; the refused lines on failure"
  task :spinel do
    require "open3"
    bin = File.join(spinel_compiler_dir, "bin/spinel")
    # relative to ROOT_DIR, where the commands run (as SPINEL_GEN_DIR is)
    out_dir = "tmp/regress/spinel"
    rm_rf File.join(ROOT_DIR, out_dir), verbose: false
    mkdir_p File.join(ROOT_DIR, out_dir), verbose: false
    programs = spinel_gen_programs(bin, gen_dir: out_dir, stage_dir: out_dir,
                                   kernel: true, editor: true, desktop: true, fft: true,
                                   platform: "linux")
    t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    # The combining steps (quick) one after another, as they share
    # fmrb_const_generated.rb; then the compiler on every program at once.
    run = lambda do |c, log|
      out, st = Open3.capture2e(c, chdir: ROOT_DIR)
      log << "$ #{c}\n" << out
      st.success?
    end
    logs = programs.to_h { |name, _| [name, +""] }
    combined = programs.to_h { |name, cmds| [name, cmds[0..-2].all? { |c| run.call(c, logs[name]) }] }
    results = programs.map do |name, cmds|
      Thread.new do
        ok = combined[name] && run.call(cmds.last, logs[name])
        File.write(File.join(ROOT_DIR, out_dir, "#{name}.log"), logs[name])
        [name, ok, logs[name]]
      end
    end.map(&:value)
    took = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
    failed = results.reject { |_, ok, _| ok }
    failed.each do |name, _, log|
      puts "FAIL spinel #{name} (tmp/regress/spinel/#{name}.log)"
      lines = log.lines.reject { |l| l.start_with?("$ ") }
      refused = lines.select { |l| l =~ /error|warning|refus|unsupported|unresolved|cannot/i }
      (refused.empty? ? lines.last(6) : refused.first(20)).each { |l| puts "     #{l.rstrip}" }
      puts "     ... (#{refused.size - 20} more)" if refused.size > 20
    end
    puts format("spinel: %d programs, %d generated, %d failed (%.1f s)",
                results.size, results.size - failed.size, failed.size, took)
    abort "regress:spinel: failed" unless failed.empty?
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

  desc "The compositor in the Ubuntu 26.04 image: native:wl, wl_check, wl_apps_check, wl_dialog_check, wl_keys_check, wl_open_check, wl_clip_check, wl_ime_check, wl_winops_check, wl_settings_check, wl_size_check and wl_touch_check (headless, pixman), standard and all-mruby (built one after the other, checked side by side; WL_SERIAL=1 one after the other). About 18 minutes"
  task :wl do
    checks = %w[wl_check wl_apps_check wl_dialog_check wl_keys_check wl_open_check wl_clip_check wl_ime_check
                wl_winops_check wl_settings_check wl_size_check wl_touch_check]
    label = ->(c) { c == "wl_check" ? "" : " #{c[/wl_(\w+)_check/, 1].sub('dialog', 'dialogs')}" }
    failed = []
    out_dir = File.join(ROOT_DIR, "tmp/wl_check")
    FileUtils.mkdir_p(out_dir)
    # The two builds share the image's libmruby (native/build-wl/mruby): one
    # after the other
    built = %w[standard mruby].select do |engine|
      log = File.join(out_dir, "build-#{engine}.log")
      ok = system({ "NATIVE_ENGINE" => engine }, "rake", "-f", File.join(ROOT_DIR, "Rakefile"), "native:wl",
                  out: log, err: [:child, :out])
      failed << "#{engine} build (#{log})" unless ok
      ok
    end
    # Each configuration's checks in a row; the two configurations side by
    # side (containers, run directories and debug ports are the engine's own:
    # the mruby checks publish theirs 100 above). Each stream's output goes
    # to tmp/wl_check/checks-<engine>.log and is printed when both are done.
    run_checks = lambda do |engine, io|
      env = { "NATIVE_ENGINE" => engine }
      checks.each do |c|
        ok = system(env, Gem.ruby, File.join(ROOT_DIR, "test/native/wl/#{c}.rb"), "--engine", engine,
                    "--renderer", "pixman", out: io, err: io)
        failed << "#{engine}#{label.call(c)}" unless ok
      end
    end
    if ENV["WL_SERIAL"] == "1"
      built.each { |e| run_checks.call(e, $stdout) }
    else
      logs = built.to_h { |e| [e, File.join(out_dir, "checks-#{e}.log")] }
      built.map { |e| Thread.new { File.open(logs[e], "w") { |f| run_checks.call(e, f) } } }.each(&:join)
      built.each { |e| $stdout.write(File.read(logs[e])) }
    end
    abort "regress:wl: failed: #{failed.join(', ')}" unless failed.empty?
    puts "regress:wl: ok (tmp/wl_check/)"
  end

  desc "Everything: regress + regress:spinel (the all-Spinel generation, as CI) + regress:size (one board per chip; ALL=1 every board) + regress:run + regress:screens (sim, browser) + regress:live + regress:wl (the compositor) + regress:checkpoint SIM=1 (moving an app, with the simulation and the browser build). Each sim build is made once and shared between the parts"
  task :full do
    only = ENV.delete("ONLY")
    warn "regress:full: ONLY=#{only} ignored (narrow the parts one by one)" if only
    # A sim build is wanted by several parts (the standard Retro one by run,
    # screens and checkpoint; the standard Modern one by those and live):
    # made once in this run and handed from part to part (build_tree.rb).
    cache = File.join(ROOT_DIR, "tmp", "regress", "build_cache")
    FileUtils.rm_rf(cache)
    FileUtils.mkdir_p(cache)
    ENV["REGRESS_BUILD_CACHE"] = cache
    # A browser or a development server someone keeps open on the page's port
    # is left alone: screens skips the browser build and says so
    ENV["REGRESS_SCREENS_SKIP_BUSY_WEB"] = "1"
    # the checkpoint rounds with the simulation too (SIM=1)
    ENV["SIM"] = "1"
    failed = []
    took = []
    t_all = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    begin
      %w[regress regress:spinel regress:size regress:run regress:screens regress:live regress:wl regress:checkpoint].each do |t|
        t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
        begin
          Rake::Task[t].invoke
        rescue SystemExit
          failed << t
        end
        took << format("%s %.1f", t.sub("regress:", ""), (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0) / 60.0)
      end
    ensure
      FileUtils.rm_rf(cache)
      ENV.delete("REGRESS_BUILD_CACHE")
    end
    total = (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t_all) / 60.0
    puts format("regress:full: %.1f min (%s)", total, took.join(", "))
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
  args << "--skip-busy-web" if ENV["REGRESS_SCREENS_SKIP_BUSY_WEB"] == "1"
  args
end

namespace :regress do
  desc "Scripted screens on the sim, the browser build and the native program vs baseline PNGs (ONLY=retro|modern|web|native, SCENE=, FULL=1, WEB_PORT=)"
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

REGRESS_CHECKPOINT = File.join(ROOT_DIR, "tools", "regress", "checkpoint.rb")

namespace :regress do
  desc "Move an app (doc/checkpoint): inside one native program and between two, standard and all-mruby; SIM=1 adds the Linux simulation and the native program; the browser build and the native program over Asterism too (own server and headless Chromium; WEB=0 leaves them out), BOARD=<name> a connected board (opt-in: the fmrb MCP serial capture running, BOARD_IP=/HOST_IP= when they cannot be found, BOARD_ONLY=1 its rounds alone) (ONLY=standard|mruby, NO_BUILD=1: the builds there are, SIM=1|retro|modern|noja: noja is a Modern sim built with FMRB_FONT_JA=0, one round)"
  task :checkpoint do
    args = []
    args += ["--only", ENV["ONLY"]] if ENV["ONLY"]
    args << "--no-build" if ENV["NO_BUILD"] == "1"
    sim = ENV["SIM"].to_s
    args += ["--sim", sim == "1" ? "retro,modern,noja" : sim] unless sim.empty? || sim == "0" || sim == "none"
    # the browser build's rounds (doc/checkpoint F5): on by default since the
    # published browser build carries the gem (report/web_public.md); WEB=0
    # leaves them out (no emsdk or Chromium at hand, or in a hurry)
    args << "--web" unless ENV["WEB"] == "0" || ENV["BOARD_ONLY"] == "1"
    # a connected board (doc/checkpoint F6): opt-in like regress:boot, never
    # from regress:full
    if ENV["BOARD"] && !ENV["REGRESS_BUILD_CACHE"]
      args += ["--board", ENV["BOARD"]]
      args << "--board-only" if ENV["BOARD_ONLY"] == "1"
    end
    abort "regress:checkpoint: failed" unless system(Gem.ruby, REGRESS_CHECKPOINT, *args)
  end
end
