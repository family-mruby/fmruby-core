# Rakefile — Family mruby ESP-IDF build wrapper (Docker)
require "rake"

EXPECTED_CHIP = "ESP32-S3"
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

ESP_IDF_VERSION = ENV.fetch("ESP_IDF_VERSION", "v5.5.1")
IMAGE           = ENV.fetch("DOCKER_IMAGE", "ghcr.io/family-mruby/fmruby-esp32-build:latest")
DEVICE_ARGS     = ENV["DEVICE_ARGS"].to_s

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
  # filesystem
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-filesystem"
  sh "cp -rf lib/add/picoruby-fmrb-filesystem #{mrbgem_path}/"
  # io
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-io"
  sh "cp -rf lib/add/picoruby-fmrb-io #{mrbgem_path}/"
  # syntax-highlight
  sh "rm -rf #{mrbgem_path}/picoruby-syntax-highlight"
  sh "cp -rf lib/add/picoruby-syntax-highlight #{mrbgem_path}/"
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

  # mruby-compiler2
  sh "cp -f lib/patch/compiler/prism_xallocator.h #{mrbgem_path}/mruby-compiler2/include/"
  sh "cp -f lib/patch/compiler/prism_alloc.c #{mrbgem_path}/mruby-compiler2/lib/"
  sh "cp -f lib/patch/compiler/mruby-compiler2-mrbgem.rake #{mrbgem_path}/mruby-compiler2/mrbgem.rake"
  sh "cp -f lib/patch/compiler/mruby-compiler2-compile.c #{mrbgem_path}/mruby-compiler2/src/compile.c"

  # mrbgem.rake patches
  sh "cp -f lib/patch/picoruby-require/mrbgem.rake #{mrbgem_path}/picoruby-require/"
  sh "cp -f lib/patch/picoruby-yaml/mrbgem.rake #{mrbgem_path}/picoruby-yaml/"
  sh "cp -f lib/patch/picoruby-sandbox/mrbgem.rake #{mrbgem_path}/picoruby-sandbox/"
  # picoruby-sandbox: fix uninitialized mrb_value name in Sandbox.new
  sh "cp -f lib/patch/picoruby-sandbox/src/mruby/sandbox.c #{mrbgem_path}/picoruby-sandbox/src/mruby/"

  # mruby-task: add stack clearing in mrb_task_reset_context
  mruby_task_path = "#{mrbgem_path}/picoruby-mruby/lib/mruby/mrbgems/mruby-task"
  sh "cp -f lib/patch/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/task.c #{mruby_task_path}/src/"

  # mruby-dir: patch mrbgem.rake to skip HAL auto-detection on ESP32
  mruby_dir_path = "#{mrbgem_path}/picoruby-mruby/lib/mruby/mrbgems/mruby-dir"
  sh "cp -f lib/patch/mruby-dir/mrbgem.rake #{mruby_dir_path}/"
end

namespace :set_target do
  desc "Linux target (dev/test)"
  task :linux => :setup do
    sh "#{DOCKER_CMD} idf.py --preview -DSDKCONFIG_DEFAULTS=\"config/sdkconfig.defaults.linux\" set-target linux"
  end

  desc "Set ESP32(S3) target"
  task :esp32 => :setup do
    sh "#{DOCKER_CMD} idf.py set-target esp32s3"
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

    # set-target must also receive SDKCONFIG_DEFAULTS so sdkconfig is generated correctly
    unless Dir.exist?('build')
      sh "#{DOCKER_CMD} idf.py -DSDKCONFIG_DEFAULTS=\"#{sdkconfig_path}\" set-target esp32s3"
    end

    cmake_opts = "-DSDKCONFIG_DEFAULTS=\"#{sdkconfig_path}\""
    cmake_opts += " -DFMRB_HW_TARGET=#{hw_target}" unless hw_target.empty?
    cmake_opts += " #{ENV['CMAKE_OPTS']}" if ENV['CMAKE_OPTS']
    sh "#{DOCKER_CMD} idf.py #{cmake_opts.strip} build"
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

desc "Flash to ESP32"
task :flash do
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py -p #{get_serial_port} flash"
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