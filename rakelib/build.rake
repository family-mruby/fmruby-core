# rakelib/build.rake
# Firmware builds (Linux sim / ESP32) and the build-artifact clean tasks.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

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

# The service list is copied from config/ on every build, so a key appended
# to an older generated copy is gone with the copy. This only guards against
# a key ever being written into config/services.toml itself.
def check_services_toml_has_no_key(path = 'flash/etc/services.toml')
  return unless File.exist?(path)
  if File.readlines(path).any? { |l| l =~ /^\s*api_key\s*=/ }
    abort "secrets: #{path} has an api_key line; keys belong in secrets.toml, not the service list"
  end
end

def stage_services(modern)
  if modern
    cp 'config/services.toml', 'flash/etc/services.toml', verbose: true
    check_services_toml_has_no_key
    write_dev_secrets
  else
    rm_f 'flash/etc/services.toml'
    rm_f DEV_SECRETS_TOML
  end
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
    ENV['SPINEL_GEN_PLATFORM'] = 'linux'
    Rake::Task['spinel:gen'].invoke if any_spinel

    unless Dir.exist?('build')
      Rake::Task['set_target:linux'].invoke
    end
    # Which machine the sim stands in for. Reaches the C as
    # FMRB_HW_FAMILY_MODERN (fmrb_hw_defines.cmake) and Ruby as
    # FmrbConst::HW_FAMILY, so a Modern sim has Modern's fonts and a Retro
    # sim has Retro's.
    hw_family = MODERN_HW_TARGETS.include?(HW_TARGET) ? 'modern' : 'retro'
    sh "#{DOCKER_CMD} bash -c 'export IDF_TARGET=linux && idf.py --preview -DSDKCONFIG_DEFAULTS=\"config/sdkconfig.defaults.linux\" -DFMRB_HW_FAMILY=#{hw_family} -DCMAKE_BUILD_TYPE=Debug build'"
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
      # board); NARYAv4 = the dedicated P4 board being designed, brought up on
      # a Waveshare ESP32-P4-Nano + Olimex MIPI-HDMI. The two share the
      # partition table (both 16MB) but not the chip revision or the C6 wiring,
      # so they get their own sdkconfig/system_conf pair each.
      # cmake/check_storage_inputs.cmake mirrors this table -- change both.
      'TAB5'         => { chip: 'esp32p4', sdkconfig: 'config/sdkconfig.defaults.p4',
                          system_conf: 'config/system_conf_p4.toml' },
      'NARYAv4'      => { chip: 'esp32p4', sdkconfig: 'config/sdkconfig.defaults.naryav4',
                          system_conf: 'config/system_conf_naryav4.toml' },
      # M5Stack AtomS3R (ESP32-S3, 8MB flash): draws on its own 128x128 LCD
      # and runs one app (doc/headless_s3/).
      'ATOMS3R'      => { chip: 'esp32s3', sdkconfig: 'config/sdkconfig.defaults.atoms3r',
                          system_conf: 'config/system_conf_atoms3r.toml' },
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

    # Copy HW-specific system_conf.toml to flash directory, plus the factory
    # copy the kernel falls back to when the live file is unreadable.
    cp system_conf_path, 'flash/etc/system_conf.toml', verbose: true
    cp system_conf_path, 'flash/etc/system_conf.factory.toml', verbose: true

    # System service list (doc/user_extension/services), generated the same
    # way as system_conf above. Modern only; the storage image drops the rest
    # of the service files for Retro (top-level CMakeLists).
    stage_services(MODERN_HW_TARGETS.include?(hw_target))

    # File associations (doc/user_extension/assoc), both machines.
    cp 'config/associations.toml', 'flash/etc/associations.toml', verbose: true

    # WiFi credentials (P4 via the C6, S3 native): config/wifi.toml is kept
    # out of git (see config/wifi.toml.example). Copied into the flash image
    # only when present. wifi_p4.toml is the pre-rename fallback.
    if File.exist?('config/wifi.toml')
      cp 'config/wifi.toml', 'flash/etc/wifi.toml', verbose: true
    elsif File.exist?('config/wifi_p4.toml')
      warn 'config/wifi_p4.toml is deprecated; rename it to config/wifi.toml'
      cp 'config/wifi_p4.toml', 'flash/etc/wifi.toml', verbose: true
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
