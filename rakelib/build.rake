# rakelib/build.rake
# Firmware builds (Linux sim / ESP32) and the build-artifact clean tasks.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.
#
# The two builds live side by side, so switching between them costs no
# rebuild:
#   build-linux/   the Linux sim (its sdkconfig is build-linux/sdkconfig; it
#                  uses no managed component, CMakeLists.txt)
#   build/         the ESP32 firmware (sdkconfig at the top, as idf.py expects;
#                  the generated /etc files of its storage image in
#                  build/fmrb_etc, not in flash/etc, which is the sim's)
# and so do their Spinel programs (main/ruby/spinel/gen/linux and gen/esp32,
# rakelib/spinel.rake).
# Each Linux build stamps the sources it was made from (tools/build_stamp.rb),
# and the starters refuse a stale one.
require_relative "../tools/build_stamp"

LINUX_BUILD_DIR = 'build-linux'
LINUX_SDKCONFIG = "#{LINUX_BUILD_DIR}/sdkconfig"
# The ESP32 build's generated /etc (system_conf, services, secrets, wifi,
# associations): staged here by rake build:esp32 and laid over the copy of
# flash/ by the storage_staging target (CMakeLists.txt). flash/etc holds the
# sim's, which it reads live.
ESP32_ETC_DIR = 'build/fmrb_etc'
# idf.py arguments that put the Linux build in its own place.
LINUX_IDF_ARGS = "-B #{LINUX_BUILD_DIR} -DSDKCONFIG=#{LINUX_SDKCONFIG} " \
                 "-DSDKCONFIG_DEFAULTS=config/sdkconfig.defaults.linux"

# Developer's tts key (doc/user_extension/voice/report/secrets.md). A reflash
# rewrites the storage image and with it /home, which kept wiping a key the
# user had typed into /home -- so a dev build bakes the key into a GENERATED
# /etc/secrets.toml, read by tts.rb after the user's own /home/secrets.toml.
# It is a file of its own, not a line in /etc/services.toml, because the
# remote file services refuse to hand out secrets.toml (fmrb_secret_path.c)
# while the service list stays readable. The key lives outside the repo in
# ~/.openai_key, like config/wifi.toml; the generated file is gitignored and
# checked to be untracked before anything is written. Never on CI: that is
# where release firmware is built. The key's value is never printed.
DEV_SECRETS_TOML = 'flash/etc/secrets.toml'

def write_dev_secrets(path = DEV_SECRETS_TOML)
  # Refuse outright if git would pick the file up: a tracked or unignored
  # secrets file is exactly how a key ends up on GitHub.
  if system('git', 'ls-files', '--error-unmatch', path, out: File::NULL, err: File::NULL)
    abort "secrets: #{path} is tracked by git; remove it from the index (git rm --cached) before building"
  end
  unless system('git', 'check-ignore', '-q', path)
    abort "secrets: #{path} is not gitignored; add it to .gitignore before building"
  end
  # Always start from nothing, so a key removed from ~/.openai_key does not
  # linger in an old generated file.
  rm_f path
  return if ENV['CI']
  key_file = File.expand_path('~/.openai_key')
  return unless File.exist?(key_file)
  key = File.read(key_file).strip
  return if key.empty?
  if key =~ /["\\\s]/
    abort "secrets: ~/.openai_key holds a quote, backslash or space; it should be the key alone"
  end
  File.open(path, File::WRONLY | File::CREAT | File::TRUNC, 0o600) do |f|
    f.puts "# Written by rake from ~/.openai_key (dev build only, not in git)."
    f.puts "# Your own key goes in /home/secrets.toml on the board, same shape."
    f.puts "[openai]"
    f.puts "api_key = \"#{key}\""
  end
  puts "secrets: api_key from ~/.openai_key written to #{path} (gitignored)"
end

# tts.rb reads its key from secrets.toml only, so an api_key line in the
# service list does nothing but leak: the remote file services hand that file
# to anyone who asks. Stop the build if config/services.toml ever gets one.
def check_services_toml_has_no_key(path = 'flash/etc/services.toml')
  return unless File.exist?(path)
  if File.readlines(path).any? { |l| l =~ /^\s*api_key\s*=/ }
    abort "secrets: #{path} has an api_key line; keys belong in secrets.toml, not the service list"
  end
end

def stage_services(modern, etc = 'flash/etc')
  services = "#{etc}/services.toml"
  secrets = "#{etc}/secrets.toml"
  if modern
    cp 'config/services.toml', services, verbose: true
    check_services_toml_has_no_key(services)
    write_dev_secrets(secrets)
  else
    rm_f services
    rm_f secrets
  end
end

# libmruby is rebuilt only when its .a is missing (the custom command in
# components/picoruby-esp32/CMakeLists.txt), which is why lib/ edits want
# `rake clean`. Say so when the last stamp shows lib/ has moved since.
def warn_libmruby_left_behind(now)
  prev = FmrbBuildStamp.read(LINUX_BUILD_DIR)
  return unless prev
  lib_a = 'components/picoruby-esp32/picoruby/build/family-mruby-linux/lib/libmruby.a'
  return unless File.exist?(lib_a)
  res = FmrbBuildStamp.check(ROOT_DIR, :linux, LINUX_BUILD_DIR)
  libs = res[:files].select { |f| f.start_with?('lib/') }
  return if libs.empty?
  newer = libs.select { |f| File.exist?(f) && File.mtime(f) > File.mtime(lib_a) }
  return if newer.empty?
  warn "build:linux: lib/ changed since libmruby was built (#{newer.first(3).join(', ')}" \
       "#{newer.size > 3 ? ', ...' : ''}); it is not rebuilt by itself -- run `rake clean` first if those go into libmruby"
end

namespace :build do
  desc "Linux target build (dev/test). FMRB_KERNEL_ENGINE=spinel swaps the kernel."
  task :linux => :setup do
    # Copy Linux-specific system config. A Modern (P4) HW target gets the
    # Modern-sized sim (426x240) so its UI can be checked here; Retro keeps
    # 320x240. Note that graphics-audio applies a changed framebuffer size on
    # the next boot, so the first sim run after switching targets still comes
    # up at the previous size.
    linux_conf = MODERN_HW_TARGETS.include?(HW_TARGET) ?
                   'config/system_conf_linux_p4.toml' :
                   'config/system_conf_linux.toml'
    cp linux_conf, 'flash/etc/system_conf.toml', verbose: true
    # Factory copy, never written at runtime: the kernel boots from this when
    # /etc/system_conf.toml is unreadable (e.g. power lost mid-save).
    cp linux_conf, 'flash/etc/system_conf.factory.toml', verbose: true

    # System service list (doc/user_extension/services). Same shape as
    # system_conf: config/ is the source, flash/etc/ is generated. Modern only,
    # and a stale copy is removed so a simulator switched back to Retro stands
    # in for a machine that has no services.
    stage_services(MODERN_HW_TARGETS.include?(HW_TARGET))

    # File associations (doc/user_extension/assoc). Both machines have this
    # one -- unlike services, opening a file is not Modern-only.
    cp 'config/associations.toml', 'flash/etc/associations.toml', verbose: true

    # Spinel engine(s): pre-generate the C on the host (the compiler is not
    # available inside the docker build) and forward the engine(s) into the build.
    # Always true, matching FMRB_ANY_SPINEL in main/CMakeLists.txt: the gems
    # that are Spinel-only (SpinelHello, raycast) name their generated C in
    # COMPONENT_SRCS unconditionally, so skipping generation here would fail
    # the cmake configure rather than build something smaller.
    any_spinel = FMRB_KERNEL_ENGINE == 'spinel' || FMRB_APP_ENGINE_DESKTOP == 'spinel' ||
                 FMRB_APP_ENGINE_EDITOR == 'spinel' || FMRB_FFT_SPINEL || true
    # Into main/ruby/spinel/gen/linux, the sim's own (the ESP32 build has
    # gen/esp32), so switching between the two generates nothing.
    spinel_gen!('linux') if any_spinel

    # The sources this build is made from, taken before it starts: a file
    # changed while it runs makes the build stale, as it should.
    stamp_state = FmrbBuildStamp.state(ROOT_DIR, :linux)
    warn_libmruby_left_behind(stamp_state)

    unless File.exist?("#{LINUX_BUILD_DIR}/CMakeCache.txt")
      Rake::Task['set_target:linux'].invoke
    end
    # Which machine the sim stands in for. Reaches the C as
    # FMRB_HW_FAMILY_MODERN (fmrb_hw_defines.cmake) and Ruby as
    # FmrbConst::HW_FAMILY, so a Modern sim has Modern's fonts and a Retro
    # sim has Retro's.
    hw_family = MODERN_HW_TARGETS.include?(HW_TARGET) ? 'modern' : 'retro'
    sh "#{DOCKER_CMD} bash -c 'export IDF_TARGET=linux && idf.py --preview #{LINUX_IDF_ARGS} -DFMRB_HW_FAMILY=#{hw_family} -DCMAKE_BUILD_TYPE=Debug build'"
    # The guard believes the ELF header, not this task's word: a Linux build
    # dir that somehow holds another ISA must not be stamped as good.
    elf = "#{LINUX_BUILD_DIR}/fmruby-core.elf"
    machine = File.exist?(elf) ? File.binread(elf, 20)[18, 2].unpack1('v') : nil
    abort "build:linux: #{elf} is not an x86-64 ELF (e_machine #{machine.inspect})" unless machine == 0x3E
    FmrbBuildStamp.write(ROOT_DIR, :linux, LINUX_BUILD_DIR, state: stamp_state,
                         options: { 'hw_family' => hw_family, 'hw_target' => HW_TARGET,
                                    'kernel' => FMRB_KERNEL_ENGINE, 'editor' => FMRB_APP_ENGINE_EDITOR,
                                    'desktop' => FMRB_APP_ENGINE_DESKTOP })
    puts "Linux build complete (#{hw_family}): #{elf}. Start it with the sim tools (sim_up, tools/dev_run_check.sh)."
  end

  desc "ESP32(S3) build"
  task :esp32 => :setup do
    hw_target = ENV['FMRB_HW_TARGET'] || ''

    # Select sdkconfig.defaults based on HW target
    # All configs are under config/ directory
    hw_config = {
      'ATOM_DISPLAY' => { chip: 'n8r8', sdkconfig: 'config/sdkconfig.defaults.atom_display',
                          system_conf: 'config/system_conf_atom_display.toml' },
      # Family mruby Modern (ESP32-P4). TAB5 = M5Stack Tab5 (current dev
      # board); NARYAv4 = the dedicated P4 board being designed, brought up on
      # a Waveshare ESP32-P4-Nano + Olimex MIPI-HDMI. The two share the
      # partition table (both 16MB) but not the chip revision or the C6 wiring,
      # so they get their own sdkconfig/system_conf pair each.
      # cmake/check_storage_inputs.cmake mirrors this table -- change both.
      'TAB5'         => { chip: 'esp32p4', sdkconfig: 'config/sdkconfig.defaults.tab5',
                          system_conf: 'config/system_conf_tab5.toml' },
      'NARYAv4'      => { chip: 'esp32p4', sdkconfig: 'config/sdkconfig.defaults.naryav4',
                          system_conf: 'config/system_conf_naryav4.toml' },
      # M5Stack AtomS3R (ESP32-S3, 8MB flash): draws on its own 128x128 LCD
      # and runs one app (doc/headless_s3/).
      'ATOMS3R'      => { chip: 'esp32s3', sdkconfig: 'config/sdkconfig.defaults.atoms3r',
                          system_conf: 'config/system_conf_atoms3r.toml' },
      # M5Stack StickS3 (ESP32-S3, 8MB flash): the AtomS3R's shape with a
      # 135x240 LCD. Same chip and partitions, so the same sdkconfig defaults
      # (which name partitions_atoms3r.csv).
      'STICKS3'      => { chip: 'esp32s3', sdkconfig: 'config/sdkconfig.defaults.atoms3r',
                          system_conf: 'config/system_conf_sticks3.toml' },
    }
    default_sdkconfig = 'config/sdkconfig.defaults.naryav3'
    default_system_conf = 'config/system_conf_naryav3.toml'

    sdkconfig_path = default_sdkconfig
    system_conf_path = default_system_conf
    if hw_config.key?(hw_target)
      cfg = hw_config[hw_target]
      sdkconfig_path = cfg[:sdkconfig]
      system_conf_path = cfg[:system_conf]
      puts "HW target: #{hw_target} (#{cfg[:chip]})"
    end

    # Spinel engine(s): pre-generate the C on the host (compiler is not in the
    # docker build) with PLATFORM=esp32 so ESP32-only branches (RTC HW etc.) are
    # compiled. Mirrors build:linux; without this the esp32 build used a manually
    # gen'd (or stale linux) combined.
    # Always true, matching FMRB_ANY_SPINEL in main/CMakeLists.txt: the gems
    # that are Spinel-only (SpinelHello, raycast) name their generated C in
    # COMPONENT_SRCS unconditionally, so skipping generation here would fail
    # the cmake configure rather than build something smaller.
    any_spinel = FMRB_KERNEL_ENGINE == 'spinel' || FMRB_APP_ENGINE_DESKTOP == 'spinel' ||
                 FMRB_APP_ENGINE_EDITOR == 'spinel' || FMRB_FFT_SPINEL || true
    # Into main/ruby/spinel/gen/esp32, this build's own (the sim has gen/linux).
    spinel_gen!('esp32') if any_spinel

    # main/CMakeLists.txt selects the engine from $ENV{} INSIDE the container (a
    # -D value would be overwritten by set(... "$ENV{...}")), and it falls back to
    # mruby when the variable is absent. The host-side spinel:gen above still
    # writes the combined .c in that case, so the build log reads as a successful
    # Spinel build while cmake quietly leaves the generated .c out of
    # COMPONENT_SRCS. ENGINE_ENV_OPTS puts the selection on the container itself,
    # which covers this build and every other idf.py call (flash re-configures).

    # The devices carry mruby-checkpoint (doc/checkpoint F6; the S3 too,
    # report/f6_s3.md): the baseline's table is made here on the host from
    # the same gems (tmp/ckpt_table/<board>/) and main/CMakeLists.txt links
    # it. Made again only when the host libmruby or the gem ports changed.
    Rake::Task['checkpoint:table'].invoke

    # The P4's Japanese font image (doc/flash_layout F4): made here on the
    # host, before the configure that looks for it (the top-level
    # CMakeLists.txt). Written only when it changes.
    if cfg && cfg[:chip] == 'esp32p4' && FMRB_FONT_JA
      Rake::Task['font_ja:image'].invoke
    end

    # FMRB_EDITOR (the Rakefile) decides what CMake and libmruby are made of,
    # but CMake reads it only when it configures, and changing it does not
    # make it configure again: a build/ made with the other value would link
    # the editor's gems without their ports, or the other way round. Refused
    # here instead (a build/ from before the option was recorded had it on).
    editor_opt = 'build/fmrb_editor_option'
    editor_now = FMRB_EDITOR ? '1' : '0'
    if File.exist?('build/CMakeCache.txt')
      editor_was = File.exist?(editor_opt) ? File.read(editor_opt).strip : '1'
      unless editor_was == editor_now
        abort "build:esp32: build/ was configured with FMRB_EDITOR=#{editor_was} and this build " \
              "asks for FMRB_EDITOR=#{editor_now}; run `rake clean_all` first"
      end
    end

    # set-target must also receive SDKCONFIG_DEFAULTS so sdkconfig is generated
    # correctly (it runs the first cmake configure).
    unless File.exist?('build/CMakeCache.txt')
      sh "#{DOCKER_CMD} idf.py -DSDKCONFIG_DEFAULTS=\"#{sdkconfig_path}\" set-target #{ESP_CHIP}"
    end

    # The storage image's generated /etc, into the build (ESP32_ETC_DIR):
    # flash/etc holds the sim's, which it reads live, so a board build no
    # longer overwrites them and the sim build stays usable beside it.
    # Started from nothing every time, so a file a board does not get (the
    # service list on Retro, wifi.toml when config/ has none) is not left
    # over from another board.
    etc = ESP32_ETC_DIR
    rm_rf etc
    mkdir_p etc
    # The HW-specific system_conf.toml, plus the factory copy the kernel
    # falls back to when the live file is unreadable.
    cp system_conf_path, "#{etc}/system_conf.toml", verbose: true
    cp system_conf_path, "#{etc}/system_conf.factory.toml", verbose: true

    # System service list (doc/user_extension/services), generated the same
    # way as system_conf above. Modern only; the storage image drops the rest
    # of the service files for Retro (top-level CMakeLists).
    stage_services(MODERN_HW_TARGETS.include?(hw_target), etc)

    # File associations (doc/user_extension/assoc), both machines.
    cp 'config/associations.toml', "#{etc}/associations.toml", verbose: true

    # WiFi credentials (P4 via the C6, S3 native): config/wifi.toml is kept
    # out of git (see config/wifi.toml.example). Copied into the flash image
    # only when present. wifi_p4.toml is the pre-rename fallback.
    if File.exist?('config/wifi.toml')
      cp 'config/wifi.toml', "#{etc}/wifi.toml", verbose: true
    elsif File.exist?('config/wifi_p4.toml')
      warn 'config/wifi_p4.toml is deprecated; rename it to config/wifi.toml'
      cp 'config/wifi_p4.toml', "#{etc}/wifi.toml", verbose: true
    end

    # Link transport: default is UART. To use SPI instead:
    #   CMAKE_OPTS="-DFMRB_LINK_TRANSPORT=SPI" rake build:esp32
    cmake_opts = "-DSDKCONFIG_DEFAULTS=\"#{sdkconfig_path}\""
    cmake_opts += " -DFMRB_HW_TARGET=#{hw_target}" unless hw_target.empty?
    # No image carries the test apps (flash/app/test) any more, release or
    # not (config/storage_exclude_common.txt, doc/flash_layout F1). What the
    # release workflow's FMRB_RELEASE=1 changes is the remote control: a
    # release build has it behind the guard (doc/remote_guard). Always passed,
    # because CMake keeps an option in its cache: a release build followed by a
    # plain one would otherwise stay a release build. CMAKE_OPTS comes after
    # and wins.
    release = ENV["FMRB_RELEASE"] == "1"
    cmake_opts += " -DFMRB_DEV_REMOTE_CTL=#{release ? 'OFF' : 'ON'}"
    cmake_opts += " #{ENV['CMAKE_OPTS']}" if ENV['CMAKE_OPTS']
    File.write(editor_opt, "#{editor_now}\n")
    sh "#{DOCKER_CMD} idf.py #{cmake_opts.strip} build"
  end
end

# The libmruby builds under components/picoruby-esp32/picoruby/build that
# belong to the ESP32 firmware (one per ISA, and the checkpoint tables).
ESP32_MRUBY_BUILDS = %w[esp32 esp32p4 ckpt-table-*].freeze
PICORUBY_BUILD_DIR = 'components/picoruby-esp32/picoruby/build'

desc "Clean the ESP32 build (build/, sdkconfig, its libmruby and Spinel programs); the sim, native and browser builds stay"
task :clean_all do
  sh "rm -f sdkconfig"
  sh "rm -rf build"
  sh "rm -rf #{spinel_gen_dir('esp32')}"
  ESP32_MRUBY_BUILDS.each { |b| sh "rm -rf #{PICORUBY_BUILD_DIR}/#{b}" }
end

desc "Clean picoruby build artifacts (every libmruby: after a lib/ edit)"
task :clean do
  sh "rm -rf #{PICORUBY_BUILD_DIR}/*"
  sh "rm -f build/esp-idf/picoruby-esp32/libpicoruby-esp32.a"
  sh "rm -f #{LINUX_BUILD_DIR}/esp-idf/picoruby-esp32/libpicoruby-esp32.a"
end

namespace :clean do
  desc "Clean the Linux sim build (build-linux/, its sdkconfig, libmruby and Spinel programs)"
  task :linux do
    sh "rm -rf #{LINUX_BUILD_DIR}"
    sh "rm -rf #{PICORUBY_BUILD_DIR}/family-mruby-linux"
    sh "rm -rf #{spinel_gen_dir('linux')}"
  end

  desc "Clean the browser build (wasm/build and the wasm libmruby)"
  task :web => "wasm:clean" do
    sh "rm -rf #{PICORUBY_BUILD_DIR}/#{WASM_MRUBY_BUILD}"
  end
end
