# Rakefile — Family mruby ESP-IDF build wrapper (Docker)
require "rake"

# EXPECTED_CHIP is derived from the HW target after .env is loaded (see below).
PORT_CACHE_FILE = ".serial_port"
PROBE_PORTS = ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1"]
# USB-UART bridges rake attach/detach picks up by default:
# CH340 (NARYA boards) and CH343 (Waveshare ESP32-P4-Nano).
DEFAULT_VIDPIDS = "1a86:7523,1a86:55d3"

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
# Project root. rakelib/*.rake cannot use __dir__ (it would resolve to the
# rakelib/ directory), so tasks moved there reference this instead.
ROOT_DIR = __dir__

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

# picoruby-ti: the on-device type inference engine behind the editor's
# completion/hover/diagnostics (doc/editor_ti/plan.md). Pinned exactly like
# Spinel above -- a fork commit in a PIN file, cloned into vendor/ by
# `rake ti:setup` -- instead of a submodule: the gem is delivered by copying
# it into the picoruby submodule anyway, and picoruby-ti carries a lib/prism
# submodule we deliberately do not use.
#
# Checkout resolution (same order as Spinel):
#   1. PICORUBY_TI_DIR env override (developer working checkout)
#   2. vendor/picoruby-ti -- version-pinned clone from `rake ti:setup`
#   3. ../tmp/picoruby-ti -- dev location in the family-mruby workspace
PICORUBY_TI_PIN_FILE = File.expand_path("lib/add/PICORUBY_TI_PIN", __dir__)
PICORUBY_TI_VENDOR_DIR = File.expand_path("vendor/picoruby-ti", __dir__)
PICORUBY_TI_DIR =
  if ENV["PICORUBY_TI_DIR"] && !ENV["PICORUBY_TI_DIR"].empty?
    File.expand_path(ENV["PICORUBY_TI_DIR"])
  elsif Dir.exist?(PICORUBY_TI_VENDOR_DIR)
    PICORUBY_TI_VENDOR_DIR
  else
    File.expand_path("../tmp/picoruby-ti", __dir__)
  end
# Where the gem is delivered inside the picoruby submodule (rake setup copies
# it there, and the generated database is written into that copy only -- the
# vendor checkout and sig/ stay untouched).
PICORUBY_TI_GEM_DIR = "components/picoruby-esp32/picoruby/mrbgems/picoruby-ti"
# The RBS signatures the type database is generated from. They live here (not
# in the engine checkout) because they describe the FMRB API: this directory
# is the source of truth for what the editor knows about our own classes.
PICORUBY_TI_SIG_DIR = File.expand_path("sig", __dir__)

# Pin files are plain `key: value` lines with # comments (see SPINEL_PIN).
def read_pin_file(path)
  pin = {}
  File.readlines(path).each do |line|
    next if line.strip.empty? || line.start_with?("#")
    k, v = line.split(":", 2)
    pin[k.strip] = v.strip if v
  end
  abort "broken pin file #{path}" unless pin["repo"] && pin["commit"]
  pin
end

def spinel_pin
  read_pin_file(SPINEL_PIN_FILE)
end

def picoruby_ti_pin
  read_pin_file(PICORUBY_TI_PIN_FILE)
end

FMRB_KERNEL_ENGINE = ENV["FMRB_KERNEL_ENGINE"] || "mruby"
FMRB_APP_ENGINE_DESKTOP = ENV["FMRB_APP_ENGINE_DESKTOP"] || "mruby"
FMRB_APP_ENGINE_EDITOR = ENV["FMRB_APP_ENGINE_EDITOR"] || "mruby"
# The Spinel FFT backend (doc/mic_spectrum): a Spinel-compiled library rather
# than a VM, so it is on by default and independent of the engine choices
# above. FMRB_FFT_SPINEL=0 builds without it (main/CMakeLists.txt reads the
# same variable inside the container).
FMRB_FFT_SPINEL = (ENV["FMRB_FFT_SPINEL"] || "1") != "0"

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
  "-e FMRB_APP_ENGINE_DESKTOP=#{FMRB_APP_ENGINE_DESKTOP}",
  "-e FMRB_APP_ENGINE_EDITOR=#{FMRB_APP_ENGINE_EDITOR}",
  "-e FMRB_FFT_SPINEL=#{FMRB_FFT_SPINEL ? 1 : 0}",
  # Measurement build: MRB_GC_PROFILE adds the GC pause histograms to GC.stat
  # (doc/midi/report/p6.md). It grows mrb_gc, hence mrb_state, so the rake side
  # (lib/add/family_mruby_*.rb) and the CMake side
  # (components/picoruby-esp32/mruby_abi_defines.cmake) must agree or the
  # boot-time ABI guard aborts. Both read this one variable inside the
  # container, so they cannot disagree. Off by default; build with
  # FMRB_GC_PROFILE=1 rake build:linux to measure.
  "-e FMRB_GC_PROFILE=#{ENV['FMRB_GC_PROFILE']}",
  # Self-supplied mruby timeslice (doc/wasm/, P2). The VM ticks itself from its
  # own dispatch loop instead of relying on the mruby_tick task. Needed by the
  # cooperative wasm port; off by default everywhere else. Read on both sides of
  # the rake/CMake boundary (lib/add/family_mruby_*.rb and
  # components/picoruby-esp32/CMakeLists.txt), so they cannot disagree.
  # Touching vm.c means `rake clean` before rebuilding.
  "-e FMRB_TASK_SELF_TICK=#{ENV['FMRB_TASK_SELF_TICK']}",
  # Display output backend for Modern (doc/wasm/, P3): unset/ppa is the PPA
  # hardware path, `cpu` the software compositor (main/CMakeLists.txt reads it
  # and defines FMRB_DISPLAY_BACKEND_CPU). Changing only this env var does not
  # make ninja reconfigure -- touch main/CMakeLists.txt when switching, and
  # check the boot log ("CPU display backend" vs "PPA Blend initialized").
  "-e FMRB_DISPLAY_BACKEND=#{ENV['FMRB_DISPLAY_BACKEND']}"
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

# ---- picoruby-ti host helpers -----------------------------------------------
# These stay in the top-level Rakefile (not rakelib/ti.rake) because they use
# __dir__ to resolve project paths, and are called from more than one place
# (the top-level `setup` task copies the gem and generates its database, while
# rakelib/ti.rake exposes the standalone ti:* tasks).
# Resolve the engine checkout, cloning the pinned copy into vendor/ when this
# is a fresh tree. Returns the directory to use.
def picoruby_ti_dir!
  return PICORUBY_TI_DIR if Dir.exist?(File.join(PICORUBY_TI_DIR, "tidbgen"))
  Rake::Task['ti:setup'].invoke
  PICORUBY_TI_VENDOR_DIR
end

# Generate the type database C sources from sig/*.rbs. This runs with the HOST
# ruby, like spinel:gen and for the same reason: the IDF build container has no
# ruby set up for it. tidbgen parses the signatures with the `rbs` gem, so that
# gem is a host build dependency (`gem install rbs`).
# sig_dir defaults to the firmware API signatures (PICORUBY_TI_SIG_DIR). The
# host tests pass a composed directory instead so their GPIO fixture never
# reaches the firmware database (see the :test task).
def picoruby_ti_gen_db(engine_dir, out_dir, sig_dir: PICORUBY_TI_SIG_DIR)
  main = File.join(engine_dir, "tidbgen/main.rb")
  abort "tidbgen not found at #{main}. Run `rake ti:setup`." unless File.exist?(main)
  unless Dir.exist?(sig_dir)
    abort "signature directory #{sig_dir} is missing"
  end
  unless system(RbConfig.ruby, "-e", "require 'rbs'", out: File::NULL, err: File::NULL)
    abort "the rbs gem is required to generate the picoruby-ti type database" \
          " (gem install rbs)"
  end
  mkdir_p out_dir
  sh "#{RbConfig.ruby} #{main} --sig-dir #{sig_dir} --out #{out_dir}"
end

# The signature directory the host tests generate from: the firmware API sig
# plus the test-only fixtures in tool/ti/test_sig (GPIO, which is not an FMRB
# API). Composed into a scratch dir so neither source is mutated.
PICORUBY_TI_TEST_SIG_OVERLAY = File.expand_path("tool/ti/test_sig", __dir__)
def picoruby_ti_test_sig_dir!
  work = File.expand_path("vendor/ti_test_sig", __dir__)
  rm_rf work
  mkdir_p work
  FileUtils.cp(Dir["#{PICORUBY_TI_SIG_DIR}/*.rbs"], work)
  FileUtils.cp(Dir["#{PICORUBY_TI_TEST_SIG_OVERLAY}/*.rbs"], work)
  work
end

# Build libprism from OUR prism (the one the firmware parses with) into a
# scratch copy, so host-side users of the engine link the same parser. The copy
# exists because the picoruby submodule must stay clean -- we never make in it.
def picoruby_ti_prism_work!
  prism_src = File.expand_path(
    "components/picoruby-esp32/picoruby/mrbgems/mruby-compiler/lib/prism", __dir__)
  abort "prism not found at #{prism_src}" unless Dir.exist?(prism_src)
  prism_work = File.expand_path("vendor/ti_prism", __dir__)
  unless File.exist?(File.join(prism_work, "build/libprism.a"))
    rm_rf prism_work
    mkdir_p File.dirname(prism_work)
    sh "cp -r #{prism_src} #{prism_work}"
    # prism's ast.h / diagnostic.h / src/*.c are generated and gitignored in the
    # source tree, so a fresh checkout (CI especially) copies a prism without
    # them and the compile fails on a missing prism/diagnostic.h. Generate them
    # the same way the firmware build does (mruby-compiler's mrbgem.rake):
    # template.rb is plain Ruby (erb/fileutils/yaml), no bundler needed.
    unless File.exist?(File.join(prism_work, "include/prism/diagnostic.h"))
      Dir.chdir(prism_work) { sh "#{RbConfig.ruby} templates/template.rb" }
    end
    sh "make -C #{prism_work} static"
  end
  prism_work
end

# The editor's F1 help: the long half of the doc comments in sig/, written out
# as files the editor opens. Host side and before the docker build, like the
# database -- the storage image is staged from flash/ inside the container.
def picoruby_ti_gen_help
  unless Dir.exist?(PICORUBY_TI_SIG_DIR)
    abort "signature directory #{PICORUBY_TI_SIG_DIR} is missing"
  end
  sh "#{RbConfig.ruby} tool/ti/gen_help.rb --sig-dir #{PICORUBY_TI_SIG_DIR} --out flash/help"
end

# Where the WebConsole's browser build lands (doc/editor_ti/instruction_p7.md).
# The wrapper and the export list live here in git; ti.js / ti.wasm / help.json
# are generated, so they are gitignored -- the source is sig/ plus the engine,
# exactly as it is for the firmware.
PICORUBY_TI_WASM_DIR = File.expand_path("tool/web/wasm", __dir__)

# The prism the firmware parses with. The browser build compiles it straight,
# with no PRISM_XALLOCATOR defined, so prism/defines.h falls back to malloc and
# no part of mruby is dragged in (see instruction_p7.md).
PICORUBY_TI_PRISM_SRC_DIR = File.expand_path(
  "components/picoruby-esp32/picoruby/mrbgems/mruby-compiler/lib/prism", __dir__)

# -I flags for the engine: it includes its headers by bare name.
def picoruby_ti_include_flags(engine_dir)
  %w[include src src/base src/builtin src/context src/diagnostic
     src/eval src/eval/method_evaluator src/generated src/hover
     src/suggest].map { |d| "-I#{engine_dir}/#{d}" }
end

# ---- Task files --------------------------------------------------------------
# The actual tasks live in rakelib/*.rake (Rake auto-loads every file there
# after this one). Grouped by concern: setup, build, device, spinel, ti, basic,
# micropython, test. This file is only shared foundation: env/.env loading, the
# path/pin constants, the docker command strings, and the helpers above.
