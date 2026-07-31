# Rakefile — Family mruby ESP-IDF build wrapper (Docker)
require "rake"

# EXPECTED_CHIP is derived from the HW target after .env is loaded (see below).
PORT_CACHE_FILE = ".serial_port"
PROBE_PORTS = ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1"]

def get_serial_port
  if File.exist?(PORT_CACHE_FILE)
    cached = File.read(PORT_CACHE_FILE).strip
    if File.exist?(cached)
      return cached
    else
      abort "Cached port #{cached} no longer exists. Run 'rake check-port'"
    end
  end
  abort "Serial port not configured. Run 'rake check-port' first."
end

# Load environment variables from .env file
if File.exist?(".env")
  File.readlines(".env").each do |line|
    line.strip!
    next if line.empty? || line.start_with?("#")
    key, value = line.split("=", 2)
    next unless key && value
    value = value.split("#", 2).first.strip  # Remove inline comments
    # Shell/command-line env takes precedence over .env (standard dotenv
    # semantics): only apply the .env value when the var is not already set.
    # Without this a command-line override (e.g. FMRB_HW_TARGET=TAB5) is
    # silently clobbered by .env and the wrong chip is built.
    ENV[key.strip] = value unless value.empty? || ENV.key?(key.strip)
  end
end

UID  = `id -u`.strip
GID  = `id -g`.strip
PWD_ = Dir.pwd

# Spinel AOT (Phase 1). The fork's compiler binary lives outside the project
# (not mounted into the IDF docker build), so PreBuild Ruby is compiled to C on
# the HOST before the build (see `rake spinel:gen`).
#
# Compiler checkout resolution (managed_components-style pinning):
#   1. SPINEL_DIR env override (developer working checkout)
#   2. vendor/spinel -- version-pinned clone managed by `rake spinel:setup`
#      (pin lives in components/fmrb_spinel_rt/SPINEL_PIN; a plain
#      `git clone` of fmruby-core + `rake spinel:setup` is enough to build
#      the spinel engine, no external checkout required)
#   3. ../tmp/spinel -- legacy dev location (family-mruby workspace)
SPINEL_PIN_FILE = File.expand_path("components/fmrb_spinel_rt/SPINEL_PIN", __dir__)
SPINEL_VENDOR_DIR = File.expand_path("vendor/spinel", __dir__)
SPINEL_DIR =
  if ENV["SPINEL_DIR"] && !ENV["SPINEL_DIR"].empty?
    File.expand_path(ENV["SPINEL_DIR"])
  elsif Dir.exist?(SPINEL_VENDOR_DIR)
    SPINEL_VENDOR_DIR
  else
    File.expand_path("../tmp/spinel", __dir__)
  end
SPINEL_SRC_DIR = "main/prebuild_scripts/spinel"
SPINEL_GEN_DIR = "#{SPINEL_SRC_DIR}/gen"

def spinel_pin
  pin = {}
  File.readlines(SPINEL_PIN_FILE).each do |line|
    next if line.strip.empty? || line.start_with?("#")
    k, v = line.split(":", 2)
    pin[k.strip] = v.strip if v
  end
  abort "broken pin file #{SPINEL_PIN_FILE}" unless pin["repo"] && pin["commit"]
  pin
end
FMRB_KERNEL_ENGINE = ENV["FMRB_KERNEL_ENGINE"] || "mruby"
FMRB_APP_ENGINE_DESKTOP = ENV["FMRB_APP_ENGINE_DESKTOP"] || "mruby"

ESP_IDF_VERSION = ENV.fetch("ESP_IDF_VERSION", "v5.5.4")
# Pin the build container to the IDF version tag above -- never :latest. Two
# machines building this same commit ended up with different images (v5.5.1 with
# riscv toolchain esp-14.2.0_20241119 vs v5.5.4 with esp-14.2.0_20260121), and
# the newer IDF defaults ESP32-P4 to chip revision >= v3.1, so its bootloader was
# rejected by esptool on a v1.0 Tab5. To move: bump ESP_IDF_VERSION.
# DOCKER_IMAGE still overrides for a one-off.
IMAGE           = ENV.fetch("DOCKER_IMAGE",
                            "ghcr.io/family-mruby/fmruby-esp32-build:#{ESP_IDF_VERSION}")
DEVICE_ARGS     = ENV["DEVICE_ARGS"].to_s

# All targets (Retro esp32s3, Modern esp32p4, Linux) build in the single IDF
# v5.5.4 container above. The HW target only selects the ESP32 chip:
#   Modern (TAB5 = M5Stack Tab5, NARYAv4 = future board) -> esp32p4
#   everything else (Retro)                              -> esp32s3
MODERN_HW_TARGETS = %w[TAB5 NARYAv4]
HW_TARGET = ENV.fetch("FMRB_HW_TARGET", "").strip
ESP_CHIP  = MODERN_HW_TARGETS.include?(HW_TARGET) ? "esp32p4" : "esp32s3"
# Chip name as reported by esptool (used by check-port to match the device).
EXPECTED_CHIP = (ESP_CHIP == "esp32p4") ? "ESP32-P4" : "ESP32-S3"

# Always use current user's UID:GID to avoid permission issues
USER_OPT = "--user #{UID}:#{GID}"

# The engine selection has to reach EVERY idf.py invocation, not just the build
# task. main/CMakeLists.txt reads these from the environment and falls back to
# mruby when they are absent, and ninja re-runs CMake whenever it feels like it
# -- `rake flash` alone was enough: it re-configured without them, silently
# rebuilt the whole app as mruby, and flashed that instead of the Spinel image
# that had just been built. Put them on the container itself so no call site can
# forget them.
ENGINE_ENV_OPTS = [
  "-e FMRB_KERNEL_ENGINE=#{FMRB_KERNEL_ENGINE}",
  "-e FMRB_APP_ENGINE_DESKTOP=#{FMRB_APP_ENGINE_DESKTOP}"
].join(" ")

DOCKER_CMD = [
  "docker run --rm",
  USER_OPT,
  "-e HOME=/tmp",
  ENGINE_ENV_OPTS,
  "-v #{PWD_}:/project",
  IMAGE
].join(" ")

DOCKER_CMD_PRIVILEGED = [
  "docker run --rm",
  "--group-add=dialout --group-add=plugdev --privileged",
  DEVICE_ARGS,
  USER_OPT,
  "-e HOME=/tmp",
  ENGINE_ENV_OPTS,
  "-v #{PWD_}:/project",
  "-v /dev/bus/usb:/dev/bus/usb",
  IMAGE
].join(" ")

DOCKER_CMD_INTERACTIVE = [
  "docker run --rm -it",
  "--group-add=dialout --group-add=plugdev --privileged",
  DEVICE_ARGS,
  USER_OPT,
  "-e HOME=/tmp",
  ENGINE_ENV_OPTS,
  "-v #{PWD_}:/project",
  "-v /dev/bus/usb:/dev/bus/usb",
  IMAGE
].join(" ")

desc "Regenerate the launcher icon BMPs from their .icon sources"
task :icons do
  # The .icon text files are the editable source; the BMPs beside them are what
  # the device actually loads (graphics-audio decodes them, instead of the
  # launcher pushing pixels one GFX command at a time). Re-run after editing an
  # .icon and commit both.
  sh "ruby tool/gen_icon_bmp.rb"
end

desc "Build Setup (Patch files)"
task :setup do
  mrbgem_path = "components/picoruby-esp32/picoruby/mrbgems"
  # ---------- Add ----------
  # const (must be copied first, as it's used by kernel and app)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-const"
  sh "cp -rf lib/add/picoruby-fmrb-const #{mrbgem_path}/"
  # log
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-log"
  sh "cp -rf lib/add/picoruby-fmrb-log #{mrbgem_path}/"
  # msgpack (must be copied before app, as app depends on it)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-msgpack"
  sh "cp -rf lib/add/picoruby-fmrb-msgpack #{mrbgem_path}/"
  # kernel
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-kernel"
  sh "cp -rf lib/add/picoruby-fmrb-kernel #{mrbgem_path}/"
  # app
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-app"
  sh "cp -rf lib/add/picoruby-fmrb-app #{mrbgem_path}/"
  # debug (on-device debugger API; depends on msgpack, copied above)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-debug"
  sh "cp -rf lib/add/picoruby-fmrb-debug #{mrbgem_path}/"
  # filesystem
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-filesystem"
  sh "cp -rf lib/add/picoruby-fmrb-filesystem #{mrbgem_path}/"
  # io
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-io"
  sh "cp -rf lib/add/picoruby-fmrb-io #{mrbgem_path}/"
  # syntax-highlight
  sh "rm -rf #{mrbgem_path}/picoruby-syntax-highlight"
  sh "cp -rf lib/add/picoruby-syntax-highlight #{mrbgem_path}/"
  # rx8900
  sh "rm -rf #{mrbgem_path}/picoruby-rx8900"
  sh "cp -rf lib/add/picoruby-rx8900 #{mrbgem_path}/"
  # rx8130 (Tab5 RTC)
  sh "rm -rf #{mrbgem_path}/picoruby-rx8130"
  sh "cp -rf lib/add/picoruby-rx8130 #{mrbgem_path}/"
  # picorabbit
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-picorabbit"
  sh "cp -rf lib/add/picoruby-fmrb-picorabbit #{mrbgem_path}/"
  # bmp332
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-bmp332"
  sh "cp -rf lib/add/picoruby-fmrb-bmp332 #{mrbgem_path}/"
  # hal-task-freertos (name-only gem: makes mruby-task drop its own task_hal
  # port so the FreeRTOS case-D port is used via CMake instead; see the gem's
  # mrbgem.rake and doc/work_picoruby_merge/instruct_d7_b1_tick.md sec 3.5)
  sh "rm -rf #{mrbgem_path}/hal-task-freertos"
  sh "cp -rf lib/add/hal-task-freertos #{mrbgem_path}/"
  # conf
  sh "cp -f lib/add/family_mruby.gembox #{mrbgem_path}/"
  sh "cp -f lib/add/family_mruby_linux.rb components/picoruby-esp32/picoruby/build_config/"
  sh "cp -f lib/add/family_mruby_esp32.rb components/picoruby-esp32/picoruby/build_config/"

  # ---------- Replace ----------
  # Machine
  sh "rm -rf #{mrbgem_path}/picoruby-machine"
  sh "cp -rf lib/replace/picoruby-machine #{mrbgem_path}/"

  # ---------- Patch ----------
  # picoruby-mruby: alloc.c (estalloc multi-VM), hal.h, mrbgem.rake
  sh "cp -rf lib/patch/picoruby-mruby #{mrbgem_path}/"

  # mruby-io file_constants.rb
  sh "cp -f lib/patch/picoruby-mruby/mrbgems/mruby-io/mrblib/file_constants.rb #{mrbgem_path}/picoruby-mruby/lib/mruby/mrbgems/mruby-io/mrblib/"

  # littleFS
  sh "cp -f lib/patch/esp_littlefs/CMakeLists.txt components/esp_littlefs/"

  # picoruby-env
  sh "cp -f lib/patch/picoruby-env/ports/posix/env.c #{mrbgem_path}/picoruby-env/ports/posix/"

  # mruby-compiler (renamed upstream from mruby-compiler2; same repo).
  # prism allocator patches dropped: upstream routes prism through the VM
  # estalloc heap via global_mrb (owner decision "Option A"). Only the
  # compile.c NULL-guard remains.
  sh "cp -f lib/patch/compiler/mruby-compiler2-compile.c #{mrbgem_path}/mruby-compiler/src/compile.c"

  # mrbgem.rake patches
  sh "cp -f lib/patch/picoruby-require/mrbgem.rake #{mrbgem_path}/picoruby-require/"
  sh "cp -f lib/patch/picoruby-yaml/mrbgem.rake #{mrbgem_path}/picoruby-yaml/"
  sh "cp -f lib/patch/picoruby-sandbox/mrbgem.rake #{mrbgem_path}/picoruby-sandbox/"
  # (sandbox.c patch dropped: upstream fixed the uninitialized name in Sandbox.new)

  # picoruby-i2c: add I2C#close method and I2C_release declaration
  sh "cp -f lib/patch/picoruby-i2c/include/i2c.h #{mrbgem_path}/picoruby-i2c/include/"
  sh "cp -f lib/patch/picoruby-i2c/src/mruby/i2c.c #{mrbgem_path}/picoruby-i2c/src/mruby/"

  # picoruby-socket: esp32p4 (ESP-IDF) build support, default socket timeouts,
  # TLS via the ESP-IDF certificate bundle (esp_crt_bundle), and a GC-time
  # release of native TLS sessions (leak fix)
  sh "cp -f lib/patch/picoruby-socket/mrbgem.rake #{mrbgem_path}/picoruby-socket/"
  sh "cp -f lib/patch/picoruby-socket/src/mruby/socket.c #{mrbgem_path}/picoruby-socket/src/mruby/"
  sh "cp -f lib/patch/picoruby-socket/src/mruby/ssl_socket.c #{mrbgem_path}/picoruby-socket/src/mruby/"
  sh "cp -f lib/patch/picoruby-socket/ports/esp32/tcp_socket.c #{mrbgem_path}/picoruby-socket/ports/esp32/"
  sh "cp -f lib/patch/picoruby-socket/ports/esp32/ssl_socket.c #{mrbgem_path}/picoruby-socket/ports/esp32/"
  # (tcp_server.c patch dropped: upstream's vm-threaded ports API fixed the alloc crash)
  sh "cp -f lib/patch/picoruby-socket/ports/posix/tcp_socket.c #{mrbgem_path}/picoruby-socket/ports/posix/"
  sh "cp -f lib/patch/picoruby-socket/ports/posix/ssl_socket.c #{mrbgem_path}/picoruby-socket/ports/posix/"

  # picoruby-mbedtls: skip bundled-library objects on esp32p4 (ESP-IDF provides mbedTLS)
  sh "cp -f lib/patch/picoruby-mbedtls/mrbgem.rake #{mrbgem_path}/picoruby-mbedtls/"

  # picoruby-net-websocket: fix mruby-pack gemdir (upstream points to a
  # non-existent picoruby-pack path inside the mruby tree)
  sh "cp -f lib/patch/picoruby-net-websocket/mrbgem.rake #{mrbgem_path}/picoruby-net-websocket/"

  # picoruby-net-http: accept URI objects in get/get_response/post_form (CRuby style)
  sh "cp -f lib/patch/picoruby-net-http/mrblib/http_client.rb #{mrbgem_path}/picoruby-net-http/mrblib/"

  # (picoruby-json parse_float patch dropped: upstream fixed the decimal handling)

  # mruby-task (case-D tick split): task.c (bottom-half) and the FreeRTOS
  # ports/posix/task_hal.c (top-half) are delivered by the bulk
  # `cp -rf lib/patch/picoruby-mruby` above. The mrbgem.rake patch is dropped:
  # upstream rewrote it to select ports via conf.ports/effective_ports, so
  # the old HAL-auto-load removal no longer applies (port wiring is done in
  # build_config instead).

  # mruby-dir: the "flash/" prefix dir_hal now lives at
  # mruby-dir/ports/posix/dir_hal.c (hal-posix-dir gem was removed upstream)
  # and is delivered by the bulk `cp -rf lib/patch/picoruby-mruby` above.
  # D5 (skip HAL auto-detection on ESP32) is dropped: upstream removed the
  # auto-detection logic from mruby-dir/mrbgem.rake entirely.
end

namespace :set_target do
  desc "Linux target (dev/test)"
  task :linux => :setup do
    sh "#{DOCKER_CMD} idf.py --preview -DSDKCONFIG_DEFAULTS=\"config/sdkconfig.defaults.linux\" set-target linux"
  end

  desc "Set ESP32 target (esp32s3 Retro / esp32p4 Modern)"
  task :esp32 => :setup do
    sh "#{DOCKER_CMD} idf.py set-target #{ESP_CHIP}"
  end
end

namespace :spinel do
  desc "Fetch + build the pinned Spinel compiler into vendor/spinel"
  task :setup do
    pin = spinel_pin
    if Dir.exist?(File.join(SPINEL_VENDOR_DIR, ".git"))
      head = `git -C #{SPINEL_VENDOR_DIR} rev-parse HEAD 2>/dev/null`.strip
      unless head == pin["commit"]
        sh "git -C #{SPINEL_VENDOR_DIR} fetch --depth 100 origin #{pin["commit"]}"
        sh "git -C #{SPINEL_VENDOR_DIR} checkout --detach #{pin["commit"]}"
      end
    else
      mkdir_p File.dirname(SPINEL_VENDOR_DIR)
      # Clone then detach at the pin (shallow; deepen if the pin is older
      # than the branch tip).
      sh "git clone --branch #{pin["branch"] || "fmrb-dev"} --depth 100 #{pin["repo"]} #{SPINEL_VENDOR_DIR}"
      sh "git -C #{SPINEL_VENDOR_DIR} checkout --detach #{pin["commit"]}"
    end
    bin = File.join(SPINEL_VENDOR_DIR, "bin/spinel")
    head = `git -C #{SPINEL_VENDOR_DIR} rev-parse HEAD`.strip
    stamp = File.join(SPINEL_VENDOR_DIR, ".built_commit")
    unless File.executable?(bin) && File.exist?(stamp) && File.read(stamp).strip == head
      sh "cd #{SPINEL_VENDOR_DIR} && make deps && make"
      File.write(stamp, head)
    end
    puts "Spinel compiler ready: #{bin} (#{head[0, 12]})"
  end

  desc "Generate Spinel C from PreBuild Ruby on the host (before docker build)"
  task :gen do
    dir = SPINEL_DIR
    # Self-provision on a fresh clone: no usable compiler anywhere -> fetch and
    # build the pinned vendor copy.
    unless File.executable?(File.join(dir, "bin/spinel"))
      Rake::Task['spinel:setup'].invoke
      dir = SPINEL_VENDOR_DIR
    end
    bin = File.join(dir, "bin/spinel")
    abort "spinel not found at #{bin}. Run `rake spinel:setup` or set SPINEL_DIR." unless File.executable?(bin)
    # Guard against compiler/runtime-snapshot divergence: the generated C and
    # components/fmrb_spinel_rt/spinel_rt must come from the same fork commit.
    import_info = File.expand_path("components/fmrb_spinel_rt/spinel_rt/IMPORT_INFO", __dir__)
    if File.exist?(import_info)
      snap = File.read(import_info)[/^fork_commit: (\h+)/, 1]
      head = `git -C #{dir} rev-parse HEAD 2>/dev/null`.strip
      if snap && !head.empty? && snap != head
        warn "WARNING: spinel compiler at #{head[0, 12]} but runtime snapshot is #{snap[0, 12]};" \
             " regenerate the snapshot (import_from_fork.rb) or align the checkouts."
      end
    end
    mkdir_p SPINEL_GEN_DIR
    # Platform passed to the gen scripts (sets PLATFORM in the combined Ruby, which
    # gates ESP32-only code like RTC HW access). Defaults to linux; build:esp32 sets
    # SPINEL_GEN_PLATFORM=esp32. Getting this wrong silently compiles the wrong
    # PLATFORM branch (esp32 build with linux gen = RTC/HW code dropped).
    platform = ENV['SPINEL_GEN_PLATFORM'] || 'linux'
    # Kernel: concatenate the kernel Ruby into one combined program (the Spinel
    # compiler needs a single translation unit; require_relative is stripped)
    # and compile to C (library mode, entry fmrb_kernel_entry). Host-generated
    # into gen/ (gitignored). Skipped when the kernel stays on mruby.
    if FMRB_KERNEL_ENGINE == "spinel"
      combined_rb = "#{SPINEL_GEN_DIR}/fmrb_kernel_combined.rb"
      out_c       = "#{SPINEL_GEN_DIR}/fmrb_kernel_combined.c"
      sh "#{RbConfig.ruby} tool/spinel/gen_kernel_combined.rb #{combined_rb} #{platform}"
      sh "#{bin} --no-main --entry fmrb_kernel_entry -I #{SPINEL_SRC_DIR} -c #{combined_rb} -o #{out_c}"
      puts "Spinel generated #{out_c}"
    end
    # Desktop: same, for system_desktop (entry system_desktop_entry).
    if FMRB_APP_ENGINE_DESKTOP == "spinel"
      d_rb = "#{SPINEL_GEN_DIR}/system_desktop_combined.rb"
      d_c  = "#{SPINEL_GEN_DIR}/system_desktop_combined.c"
      sh "#{RbConfig.ruby} tool/spinel/gen_app_combined.rb system_desktop #{d_rb} #{platform}"
      sh "#{bin} --no-main --entry system_desktop_entry -I #{SPINEL_SRC_DIR} -c #{d_rb} -o #{d_c}"
      puts "Spinel generated #{d_c}"
    end
  end

  desc "Lint Spinel-targeted Ruby with spinel-doctor (source-level: unsupported/unresolved/inference)"
  task :doctor do
    dir = SPINEL_DIR
    unless File.executable?(File.join(dir, "bin/spinel"))
      Rake::Task['spinel:setup'].invoke
      dir = SPINEL_VENDOR_DIR
    end
    doctor = File.join(dir, "bin/spinel-doctor")
    abort "spinel-doctor not found at #{doctor}. Run `rake spinel:setup`." unless File.executable?(doctor)
    mkdir_p SPINEL_GEN_DIR
    # Lint kernel + desktop (both are Spinel-convertible programs). We generate
    # the require-inlined combined Ruby, then run the SOURCE-LEVEL legs only.
    # --skip build,behavior: those legs link/run the combined standalone, which
    # flags the fmruby C shims (fmrb_spx_*) as undefined and reports a CRuby-diff
    # -- both false positives here (the real firmware build links the shims).
    # The gate keys on unsupported/unresolved (error severity); inference notes
    # ("widened to untyped") are informational poly-widen hints.
    targets = [
      ["fmrb_kernel",    "tool/spinel/gen_kernel_combined.rb", nil],
      ["system_desktop", "tool/spinel/gen_app_combined.rb", "system_desktop"],
    ]
    # Known-accepted findings: ESP32-only RTC hardware code that is dead on Linux
    # (PLATFORM guard) but still statically analyzed. Its driver classes
    # (RX8900 / RX8130) are not in the Linux Spinel program, so `write_time`
    # can't resolve here. INTERIM allowlist: Phase 5 routes RTC writes through the
    # set_wallclock FFI (decision (b), phase5.md T5-4) and deletes the direct
    # driver instantiation from clock_setting.rb -- then REMOVE this allowlist.
    allow = [/unresolved call 'write_time' on .* receiver/]
    failed = []
    targets.each do |name, gen, arg|
      rb = "#{SPINEL_GEN_DIR}/#{name}_combined.rb"
      sh "#{RbConfig.ruby} #{gen} #{arg ? "#{arg} " : ""}#{rb} linux"
      puts "== spinel-doctor: #{name} =="
      out = `SPINEL_DIR=#{dir} #{doctor} --only unsupported,unresolved #{rb} 2>&1`
      puts out
      findings = out.lines.select { |l| l =~ /warning:|error:/ }
      unexpected = findings.reject { |l| allow.any? { |p| l =~ p } }
      allowed = findings.size - unexpected.size
      puts "  (#{allowed} known ESP32-only finding(s) allowlisted)" if allowed > 0
      failed << name unless unexpected.empty?
    end
    abort "spinel-doctor UNEXPECTED findings in: #{failed.join(', ')}" unless failed.empty?
    puts "spinel-doctor: clean (modulo allowlisted ESP32-only findings)"
  end
end

namespace :basic do
  # Family BASIC golden tests. The interpreter core (components/basic/core) is
  # host independent pure C++, so the tests build with the host g++ and need
  # neither docker nor an IDF checkout. Flags mirror the firmware constraints
  # (C++20, no exceptions, no RTTI) so a violation fails here first.
  BASIC_DIR        = "components/basic"
  BASIC_TEST_DIR   = "#{BASIC_DIR}/test"
  BASIC_TEST_BUILD = "#{BASIC_TEST_DIR}/build"
  BASIC_RUNNER_BIN = "#{BASIC_TEST_BUILD}/basic_runner"
  BASIC_TEST_SRCS  = Dir["#{BASIC_DIR}/core/*.cpp"].sort + ["#{BASIC_TEST_DIR}/runner/main.cpp"]
  BASIC_CXXFLAGS   = %w[
    -std=c++20 -O1 -g
    -fno-exceptions -fno-rtti
    -Wall -Wextra -Werror
  ].join(" ")

  desc "Build the host BASIC golden test runner (g++, no docker)"
  task :runner do
    cxx = ENV["CXX"] || "g++"
    abort "#{cxx} not found (install g++ or set CXX)" unless system("which #{cxx} > /dev/null 2>&1")
    mkdir_p BASIC_TEST_BUILD
    sh "#{cxx} #{BASIC_CXXFLAGS} -I #{BASIC_DIR}/core " \
       "#{BASIC_TEST_SRCS.join(' ')} -o #{BASIC_RUNNER_BIN}"
  end

  desc "Run the BASIC golden tests (FILTER=name to run a subset)"
  task :test => :runner do
    filter = ENV["FILTER"].to_s
    sh "#{BASIC_TEST_DIR}/run_golden.sh #{BASIC_RUNNER_BIN} #{filter}".strip
  end

  desc "Run one .bas through the host runner (BAS=path [IN=path])"
  task :run => :runner do
    bas = ENV["BAS"] or abort "usage: rake basic:run BAS=path/to/program.bas [IN=input.txt]"
    sh "#{BASIC_RUNNER_BIN} #{bas} #{ENV['IN']}".strip
  end

  desc "Remove the host BASIC test build"
  task :clean do
    rm_rf BASIC_TEST_BUILD
  end

  # Samples that are meant to be played by hand (keys, sound, sprites). The
  # launcher scans /app/<category>/*.app.toml, so a copy under flash/app/basic
  # turns each one into a double-clickable app on the desktop -- unlike a
  # debugd spawn, a launcher start also gets the keyboard focus.
  # test/samples stays the single source: re-run this task after editing a
  # sample and commit the refreshed copies.
  BASIC_SAMPLE_APPS = {
    "sample_04_screen_kana" => ["Kana",  "カナ"],
    "sample_10_dodge"       => ["Dodge", "ヨケロ"],
    "sample_11_shoot"       => ["Shoot", "シュート"],
    "sample_12_maze"        => ["Maze",  "メイロ"],
    "sample_13_music"       => ["Music", "オンガク"],
    "sample_14_hit"         => ["Hit",   "タタケ"],
  }
  BASIC_SAMPLE_APP_DIR = "flash/app/basic"

  # Character sheets. The firmware loads flash/usr/share/basic/*.bmp at app
  # start and falls back to the compiled tables when a sheet is missing, so the
  # artwork can be edited in a graphics editor without a rebuild.
  BASIC_SHEET_DIR = "flash/usr/share/basic"
  BASIC_SHEETS = {
    "font_b" => "gen_basic_font.rb",   # table B: text glyphs
    "tile_a" => "gen_basic_tiles.rb",  # table A: sprite tiles
  }

  desc "Export the built-in art to the editable BMP sheets (FORCE=1 to overwrite)"
  task :sheets do
    mkdir_p BASIC_SHEET_DIR
    BASIC_SHEETS.each do |name, gen|
      bmp = "#{BASIC_SHEET_DIR}/#{name}.bmp"
      if File.exist?(bmp) && ENV["FORCE"] != "1"
        puts "keeping #{bmp} (hand edits are the source now; FORCE=1 to overwrite)"
        next
      end
      sh "ruby tool/basic/#{gen} --bmp #{bmp}"
    end
  end

  desc "Convert a character sheet between PNG and BMP (IN=a.png OUT=b.bmp)"
  task :sheet_convert do
    src = ENV["IN"] or abort "usage: rake basic:sheet_convert IN=sheet.png OUT=sheet.bmp"
    dst = ENV["OUT"] or abort "usage: rake basic:sheet_convert IN=sheet.png OUT=sheet.bmp"
    sh "python3 tool/basic/basic_sheet_convert.py #{src} #{dst}"
  end

  desc "Copy the BASIC samples into flash/app/basic so the launcher lists them"
  task :samples do
    mkdir_p BASIC_SAMPLE_APP_DIR
    BASIC_SAMPLE_APPS.each do |base, (label, label_ja)|
      src = "#{BASIC_TEST_DIR}/samples/#{base}.bas"
      abort "missing sample: #{src}" unless File.exist?(src)
      cp src, "#{BASIC_SAMPLE_APP_DIR}/#{base}.app.bas", verbose: true
      File.write("#{BASIC_SAMPLE_APP_DIR}/#{base}.app.toml", <<~TOML)
        # Generated by "rake basic:samples" from #{BASIC_TEST_DIR}/samples.
        # Edit the sample there, not this copy.
        app_handle_name = "#{base}"
        app_screen_name = "#{label}"
        app_screen_name_ja = "#{label_ja}"
      TOML
    end
    puts "#{BASIC_SAMPLE_APPS.size} samples installed in #{BASIC_SAMPLE_APP_DIR}"
  end

  # The benchmarks have to be on the device to be run there: the editor opens
  # files from the device filesystem, and test/samples/ only exists on the host.
  # /home rather than /app/basic so they stay out of the launcher -- they are a
  # measuring tool, not something to browse to.
  BASIC_BENCH_DIR = "flash/home/bench"

  desc "Copy the BASIC benchmarks into flash/home/bench (needs a reflash to reach the device)"
  task :bench do
    mkdir_p BASIC_BENCH_DIR
    installed = 0
    Dir["#{BASIC_TEST_DIR}/samples/bench_*.bas"].sort.each do |src|
      base = File.basename(src, ".bas")
      cp src, "#{BASIC_BENCH_DIR}/#{base}.bas", verbose: true
      # A benchmark reports through the log, and the log only carries PRINT when
      # the app has no screen -- otherwise the text belongs on the screen and
      # mirroring it would flood the log. So each one runs in background mode.
      File.write("#{BASIC_BENCH_DIR}/#{base}.toml", <<~TOML)
        # Generated by "rake basic:bench" from #{BASIC_TEST_DIR}/samples.
        app_handle_name = "#{base}"
        app_screen_name = "#{base}"
        default_window_mode = "background"
      TOML
      installed += 1
    end
    abort "no benchmarks in #{BASIC_TEST_DIR}/samples" if installed.zero?
    puts "#{installed} benchmarks installed in #{BASIC_BENCH_DIR}"
    puts "run 'rake build:esp32 && rake flash' to put them on the device"
    puts "remove them again with 'rake basic:bench_clean'"
  end

  desc "Remove the benchmarks from flash/home/bench"
  task :bench_clean do
    rm_rf BASIC_BENCH_DIR
    puts "removed #{BASIC_BENCH_DIR}"
  end
end

namespace :micropython do
  # MicroPython is taken in through its "embed" port: one make run turns the
  # submodule plus port/mpconfigport.h into a self-contained C tree (qstr and
  # module tables already generated) under components/micropython/mp_embed.
  # That tree is committed, so rake build:linux / build:esp32 compile plain C
  # and need neither make nor python3 -- only :gen does. Re-run :gen after
  # editing mpconfigport.h or anything under modules/, and commit the result.
  MP_DIR         = "components/micropython"
  MP_SUBMODULE   = "#{MP_DIR}/micropython"
  MP_PORT_DIR    = "#{MP_DIR}/port"
  MP_EMBED_DIR   = "#{MP_DIR}/mp_embed"
  MP_GEN_BUILD   = "#{MP_PORT_DIR}/build-embed"
  MP_SMOKE_DIR   = "#{MP_PORT_DIR}/test"
  MP_SMOKE_BUILD = "#{MP_SMOKE_DIR}/build"
  MP_SMOKE_BIN   = "#{MP_SMOKE_BUILD}/mp_smoke"
  # The generated tree uses GNU C extensions (the GC helper pins registers with
  # "register ... asm"), so a strict -std=c99 compile of it does not work.
  MP_SMOKE_CFLAGS = "-std=gnu99 -Os -Wall -fno-common"

  desc "Regenerate #{MP_EMBED_DIR} from the submodule (needs make + python3)"
  task :gen do
    unless File.exist?("#{MP_SUBMODULE}/ports/embed/embed.mk")
      abort "#{MP_SUBMODULE} is empty. Run 'git submodule update --init #{MP_SUBMODULE}'"
    end
    sh "make -C #{MP_PORT_DIR}"
    puts "regenerated #{MP_EMBED_DIR} -- commit it along with the change that caused it"
  end

  desc "Compile and run the host smoke test against #{MP_EMBED_DIR} (no docker)"
  task :smoke do
    unless File.exist?("#{MP_EMBED_DIR}/port/micropython_embed.h")
      abort "#{MP_EMBED_DIR} is missing. Run 'rake micropython:gen' first"
    end
    cc = ENV["CC"] || "cc"
    abort "#{cc} not found (install gcc or set CC)" unless system("which #{cc} > /dev/null 2>&1")
    srcs = Dir["#{MP_EMBED_DIR}/**/*.c"].sort + ["#{MP_PORT_DIR}/mpport.c", "#{MP_SMOKE_DIR}/main.c"]
    mkdir_p MP_SMOKE_BUILD
    sh "#{cc} #{MP_SMOKE_CFLAGS} -I #{MP_PORT_DIR} -I #{MP_EMBED_DIR} -I #{MP_EMBED_DIR}/port " \
       "#{srcs.join(' ')} -o #{MP_SMOKE_BIN} -lm"
    sh MP_SMOKE_BIN
  end

  desc "Remove the embed generation and smoke test intermediates"
  task :clean do
    rm_rf MP_GEN_BUILD
    rm_rf MP_SMOKE_BUILD
  end
end

namespace :build do
  desc "Linux target build (dev/test). FMRB_KERNEL_ENGINE=spinel swaps the kernel."
  task :linux => :setup do
    # Copy Linux-specific system config
    cp 'config/system_conf_linux.toml', 'flash/etc/system_conf.toml', verbose: true

    # Spinel engine(s): pre-generate the C on the host (the compiler is not
    # available inside the docker build) and forward the engine(s) into the build.
    any_spinel = FMRB_KERNEL_ENGINE == 'spinel' || FMRB_APP_ENGINE_DESKTOP == 'spinel'
    ENV['SPINEL_GEN_PLATFORM'] = 'linux'
    Rake::Task['spinel:gen'].invoke if any_spinel

    unless Dir.exist?('build')
      Rake::Task['set_target:linux'].invoke
    end
    sh "#{DOCKER_CMD} bash -c 'export IDF_TARGET=linux && idf.py --preview -DSDKCONFIG_DEFAULTS=\"config/sdkconfig.defaults.linux\" -DCMAKE_BUILD_TYPE=Debug build'"
    puts 'Linux build complete. Run with: ./build/fmruby-core.elf'
  end

  desc "ESP32(S3) build"
  task :esp32 => :setup do
    hw_target = ENV['FMRB_HW_TARGET'] || ''

    # Select sdkconfig.defaults based on HW target
    # All configs are under config/ directory
    hw_config = {
      'ATOM_DISPLAY' => { chip: 'n8r8', sdkconfig: 'config/sdkconfig.defaults.n8r8',
                          system_conf: 'config/system_conf_n8r8.toml' },
      # Family mruby Modern (ESP32-P4). TAB5 = M5Stack Tab5 (current dev
      # board); NARYAv4 = future dedicated P4 board, which shares the Tab5
      # config as a placeholder until its hardware is finalized.
      'TAB5'         => { chip: 'esp32p4', sdkconfig: 'config/sdkconfig.defaults.p4',
                          system_conf: 'config/system_conf_p4.toml' },
      'NARYAv4'      => { chip: 'esp32p4', sdkconfig: 'config/sdkconfig.defaults.p4',
                          system_conf: 'config/system_conf_p4.toml' },
    }
    default_sdkconfig = 'config/sdkconfig.defaults.n16r8'
    default_system_conf = 'config/system_conf_n16r8.toml'

    sdkconfig_path = default_sdkconfig
    system_conf_path = default_system_conf
    if hw_config.key?(hw_target)
      cfg = hw_config[hw_target]
      sdkconfig_path = cfg[:sdkconfig]
      system_conf_path = cfg[:system_conf]
      puts "HW target: #{hw_target} (#{cfg[:chip]})"
    end

    # Copy HW-specific system_conf.toml to flash directory
    cp system_conf_path, 'flash/etc/system_conf.toml', verbose: true

    # WiFi credentials (Modern remote desktop): config/wifi_p4.toml is
    # kept out of git (see config/wifi_p4.toml.example). Copied into the
    # flash image only when present.
    if File.exist?('config/wifi_p4.toml')
      cp 'config/wifi_p4.toml', 'flash/etc/wifi.toml', verbose: true
    end

    # Spinel engine(s): pre-generate the C on the host (compiler is not in the
    # docker build) with PLATFORM=esp32 so ESP32-only branches (RTC HW etc.) are
    # compiled. Mirrors build:linux; without this the esp32 build used a manually
    # gen'd (or stale linux) combined.
    any_spinel = FMRB_KERNEL_ENGINE == 'spinel' || FMRB_APP_ENGINE_DESKTOP == 'spinel'
    ENV['SPINEL_GEN_PLATFORM'] = 'esp32'
    Rake::Task['spinel:gen'].invoke if any_spinel

    # main/CMakeLists.txt selects the engine from $ENV{} INSIDE the container (a
    # -D value would be overwritten by set(... "$ENV{...}")), and it falls back to
    # mruby when the variable is absent. The host-side spinel:gen above still
    # writes the combined .c in that case, so the build log reads as a successful
    # Spinel build while cmake quietly leaves the generated .c out of
    # COMPONENT_SRCS. ENGINE_ENV_OPTS puts the selection on the container itself,
    # which covers this build and every other idf.py call (flash re-configures).

    # set-target must also receive SDKCONFIG_DEFAULTS so sdkconfig is generated
    # correctly (it runs the first cmake configure).
    unless Dir.exist?('build')
      sh "#{DOCKER_CMD} idf.py -DSDKCONFIG_DEFAULTS=\"#{sdkconfig_path}\" set-target #{ESP_CHIP}"
    end

    # Link transport: default is UART. To use SPI instead:
    #   CMAKE_OPTS="-DFMRB_LINK_TRANSPORT=SPI" rake build:esp32
    cmake_opts = "-DSDKCONFIG_DEFAULTS=\"#{sdkconfig_path}\""
    cmake_opts += " -DFMRB_HW_TARGET=#{hw_target}" unless hw_target.empty?
    cmake_opts += " #{ENV['CMAKE_OPTS']}" if ENV['CMAKE_OPTS']
    sh "#{DOCKER_CMD} idf.py #{cmake_opts.strip} build"
  end
end

desc "Attach USB serial devices to WSL2 via usbipd, selected by VID:PID " \
     "(default 1a86:7523 = CH340; VIDPID=xxxx:yyyy[,xxxx:yyyy] to override, " \
     "BUSID=x-y to force one bus id). Attaches every matching device."
task :attach do
  if ENV["BUSID"]
    sh "powershell.exe -Command \"usbipd attach --wsl --busid #{ENV['BUSID']}\""
    next
  end

  vidpids = (ENV["VIDPID"]|| "1a86:7523").downcase.split(",").map(&:strip)
  list = `powershell.exe -Command "usbipd list" 2>&1`
  connected = list.split(/^Persisted:/).first || ""

  targets = connected.lines.filter_map do |line|
    m = line.match(/^(\d+-\d+)\s+(\h{4}:\h{4})\s+(.+?)\s+(Not shared|Shared.*|Attached.*)\s*$/i)
    next nil unless m
    busid, vidpid, device, state = m.captures
    [busid, vidpid.downcase, device.strip, state.strip] if vidpids.include?(vidpid.downcase)
  end
  abort "No connected USB device matches VID:PID #{vidpids.join(', ')} (usbipd list)" if targets.empty?

  targets.each do |busid, vidpid, device, state|
    if state.start_with?("Attached")
      puts "#{busid} #{vidpid} (#{device}): already attached, skipping"
    elsif state == "Not shared"
      puts "#{busid} #{vidpid} (#{device}): NOT SHARED - run once as admin: usbipd bind --busid #{busid}"
    else
      puts "#{busid} #{vidpid} (#{device}): attaching..."
      sh "powershell.exe -Command \"usbipd attach --wsl --busid #{busid}\""
    end
  end
end

desc "Detect and cache the correct serial port for #{EXPECTED_CHIP}"
task :"check-port" do
  ports = PROBE_PORTS.select { |p| File.exist?(p) }
  abort "No serial devices found in #{PROBE_PORTS}" if ports.empty?

  puts "Scanning ports for #{EXPECTED_CHIP}..."
  detected = nil

  ports.each do |port|
    print "  Probing #{port}... "
    docker_cmd = [
      "docker run --rm --privileged",
      "--device=#{port}",
      "-v /dev/bus/usb:/dev/bus/usb",
      IMAGE
    ].join(" ")

    output = `#{docker_cmd} esptool.py --port #{port} chip_id 2>&1`
    chip_match = output.match(/Detecting chip type\.\.\.\s*(\S+)/)
    if chip_match
      chip = chip_match[1]
      puts chip
      if chip == EXPECTED_CHIP
        detected = port
        break
      end
    else
      puts "no response"
    end
  end

  if detected
    File.write(PORT_CACHE_FILE, detected)
    puts "#{EXPECTED_CHIP} found on #{detected} (cached to #{PORT_CACHE_FILE})"
  else
    abort "ERROR: #{EXPECTED_CHIP} not found on any port"
  end
end

desc "Flash to ESP32 (override baud with FLASH_BAUD=115200 etc; default 460800)"
task :flash do
  baud = ENV['FLASH_BAUD']
  baud_opt = baud && !baud.empty? ? "-b #{baud}" : ''
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py -p #{get_serial_port} #{baud_opt} flash".gsub(/\s+/, ' ')
end

desc "Check ESP32 HW"
task :check do
  sh "#{DOCKER_CMD_PRIVILEGED} esptool.py -p #{get_serial_port} flash_id"
end

desc "Open menuconfig"
task :menuconfig do
  term = ENV['TERM'] || 'xterm-256color'
  docker_cmd_interactive = [
    "docker run --rm -it",
    USER_OPT,
    "-e HOME=/tmp",
    "-e TERM=#{term}",
    "-v #{PWD_}:/project",
    IMAGE
  ].join(" ")
  sh "#{docker_cmd_interactive} idf.py menuconfig"
end

desc "Full clean build artifacts (including host)"
task :clean_all do
  sh "rm -f sdkconfig"
  sh "rm -rf build"
  sh "rm -rf components/picoruby-esp32/picoruby/build/*"
end

desc "Clean picoruby build artifacts"
task :clean do
  sh "rm -rf components/picoruby-esp32/picoruby/build/*"
  sh "rm -f build/esp-idf/picoruby-esp32/libpicoruby-esp32.a"
end

desc "Serial monitor"
task :monitor do
  sh "#{DOCKER_CMD_INTERACTIVE} idf.py -p #{get_serial_port} monitor"
end

namespace :host do
  desc "Build SDL2 host process"
  task :build do
    sh "cd host/sdl2 && mkdir -p build && cd build && cmake .. && make"
  end

  desc "Run SDL2 host process in background"
  task :run => :build do
    puts "Starting SDL2 host process..."
    sh "cd host/sdl2/build && ./fmrb_host_sdl2 &"
    sleep 1
    puts "SDL2 host running on /tmp/fmrb_socket"
  end

  desc "Clean SDL2 host build"
  task :clean do
    sh "rm -rf host/sdl2/build/*"
  end
end

namespace :test do
  desc "Integration test: Run both core and host processes"
  task :integration => ['build:linux', 'host:build'] do
    puts "Starting integration test..."

    # SDL2ホストをバックグラウンドで起動
    host_pid = Process.spawn("cd host/sdl2/build && ./fmrb_host_sdl2")
    sleep 2  # 起動待ち

    begin
      puts "Starting Family mruby Core..."
      sh "./build/fmruby-core.elf"
    ensure
      # 終了処理
      Process.kill("TERM", host_pid) rescue nil
      puts "Integration test completed"
    end
  end
end

desc "Run Linux build (depends on build:linux)"
task :run_linux => 'build:linux' do
  sh "./build/fmruby-core.elf"
end

namespace :doc do
  desc "Generate C/C++ API documentation with Doxygen"
  task :c do
    unless system("which doxygen > /dev/null 2>&1")
      puts "ERROR: Doxygen is not installed."
      puts "Install with: sudo apt-get install doxygen  # or  brew install doxygen"
      exit 1
    end
    sh "doxygen Doxyfile"
    puts "C/C++ API documentation generated at: doc/api/html/index.html"
  end

  desc "Generate Ruby API documentation with YARD"
  task :ruby do
    unless system("which yard > /dev/null 2>&1")
      puts "ERROR: YARD is not installed."
      puts "Install with: gem install yard"
      exit 1
    end
    mrbgem_path = "components/picoruby-esp32/picoruby/mrbgems"
    sh "yard doc #{mrbgem_path}/picoruby-fmrb-app/mrblib/*.rb #{mrbgem_path}/picoruby-fmrb-kernel/mrblib/*.rb -o doc/ruby_api"
    puts "Ruby API documentation generated at: doc/ruby_api/index.html"
  end

  desc "Generate all API documentation (C/C++ and Ruby)"
  task :all => [:c, :ruby]

  desc "Clean generated documentation"
  task :clean do
    sh "rm -rf doc/api doc/ruby_api"
    puts "Documentation cleaned"
  end
end