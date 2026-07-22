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
    ENV[key.strip] = value unless value.empty?
  end
end

UID  = `id -u`.strip
GID  = `id -g`.strip
PWD_ = Dir.pwd

ESP_IDF_VERSION = ENV.fetch("ESP_IDF_VERSION", "v5.5.4")
IMAGE           = ENV.fetch("DOCKER_IMAGE", "ghcr.io/family-mruby/fmruby-esp32-build:latest")
DEVICE_ARGS     = ENV["DEVICE_ARGS"].to_s

# All targets (Retro esp32s3, Modern esp32p4, Linux) build in the single IDF
# v5.5.4 container above. The HW target only selects the ESP32 chip:
#   Modern (Family mruby Modern / Tab5) -> esp32p4
#   everything else (Retro)             -> esp32s3
MODERN_HW_TARGETS = %w[NARYAv4]
HW_TARGET = ENV.fetch("FMRB_HW_TARGET", "").strip
ESP_CHIP  = MODERN_HW_TARGETS.include?(HW_TARGET) ? "esp32p4" : "esp32s3"
# Chip name as reported by esptool (used by check-port to match the device).
EXPECTED_CHIP = (ESP_CHIP == "esp32p4") ? "ESP32-P4" : "ESP32-S3"

# Always use current user's UID:GID to avoid permission issues
USER_OPT = "--user #{UID}:#{GID}"

DOCKER_CMD = [
  "docker run --rm",
  USER_OPT,
  "-e HOME=/tmp",
  "-v #{PWD_}:/project",
  IMAGE
].join(" ")

DOCKER_CMD_PRIVILEGED = [
  "docker run --rm",
  "--group-add=dialout --group-add=plugdev --privileged",
  DEVICE_ARGS,
  USER_OPT,
  "-e HOME=/tmp",
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
  "-v #{PWD_}:/project",
  "-v /dev/bus/usb:/dev/bus/usb",
  IMAGE
].join(" ")

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

namespace :build do
  desc "Linux target build (dev/test)"
  task :linux => :setup do
    # Copy Linux-specific system config
    cp 'config/system_conf_linux.toml', 'flash/etc/system_conf.toml', verbose: true

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
      # Family mruby Modern: ESP32-P4 (M5Stack Tab5 equivalent)
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

    # set-target must also receive SDKCONFIG_DEFAULTS so sdkconfig is generated correctly
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