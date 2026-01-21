# Rakefile — Family mruby ESP-IDF build wrapper (Docker)
require "rake"

USB_SERIAL_PORT="/dev/ttyUSB0"

# Load environment variables from .env file
if File.exist?(".env")
  File.readlines(".env").each do |line|
    line.strip!
    next if line.empty? || line.start_with?("#")
    key, value = line.split("=", 2)
    ENV[key] = value if key && value
  end
end

UID  = `id -u`.strip
GID  = `id -g`.strip
PWD_ = Dir.pwd

ESP_IDF_VERSION = ENV.fetch("ESP_IDF_VERSION", "v5.5.1")
IMAGE           = "esp32_build_container:#{ESP_IDF_VERSION}"
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
  # conf
  sh "cp -f lib/add/family_mruby.gembox #{mrbgem_path}/"
  sh "cp -f lib/add/family_mruby_linux.rb components/picoruby-esp32/picoruby/build_config/"
  sh "cp -f lib/add/family_mruby_esp32.rb components/picoruby-esp32/picoruby/build_config/"

  # ---------- Replace ----------
  # Machine
  sh "rm -rf #{mrbgem_path}/picoruby-machine"
  sh "cp -rf lib/replace/picoruby-machine #{mrbgem_path}/"

  # ---------- Patch ----------
  sh "cp -rf lib/patch/picoruby-mruby #{mrbgem_path}/"
  # is this needed?
  sh "cp -f lib/replace/picoruby-machine/include/hal.h #{mrbgem_path}/picoruby-mruby/include/"

  # Copy mruby-io file_constants.rb patch (avoid conflicts with C extension)
  sh "cp -f lib/patch/picoruby-mruby/mrbgems/mruby-io/mrblib/file_constants.rb #{mrbgem_path}/picoruby-mruby/lib/mruby/mrbgems/mruby-io/mrblib/" 

  # littleFS
  sh "cp -f lib/patch/esp_littlefs/CMakeLists.txt components/esp_littlefs/"

  # Copy picoruby-env patch (thread-safe ENV for multi-VM environment)
  sh "cp -f lib/patch/picoruby-env/ports/posix/env.c #{mrbgem_path}/picoruby-env/ports/posix/"

  # Copy TLSF-based prism allocator files
  sh "cp -f lib/patch/compiler/prism_xallocator.h #{mrbgem_path}/mruby-compiler2/include/"
  sh "cp -f lib/patch/compiler/prism_alloc.c #{mrbgem_path}/mruby-compiler2/lib/"
  sh "cp -f lib/patch/compiler/mruby-compiler2-mrbgem.rake #{mrbgem_path}/mruby-compiler2/mrbgem.rake"

  # Copy picoruby-require patch (use picoruby-fmrb-filesystem instead of posix-io/vfs)
  sh "cp -f lib/patch/picoruby-require/mrbgem.rake #{mrbgem_path}/picoruby-require/"

  # Copy picoruby-yaml patch (use picoruby-fmrb-io instead of mruby-io)
  sh "cp -f lib/patch/picoruby-yaml/mrbgem.rake #{mrbgem_path}/picoruby-yaml/"

  # Copy picoruby-sandbox patch (remove picoruby-io-console dependency)
  sh "cp -f lib/patch/picoruby-sandbox/mrbgem.rake #{mrbgem_path}/picoruby-sandbox/"

  # Copy TLSF library files
  sh "mkdir -p #{mrbgem_path}/mruby-compiler2/lib/tlsf"
  sh "cp -f components/mem_allocator/tlsf/tlsf.c #{mrbgem_path}/mruby-compiler2/lib/tlsf/"
  sh "cp -f components/mem_allocator/tlsf/tlsf.h #{mrbgem_path}/mruby-compiler2/lib/tlsf/"

  # Copy TLSF wrapper with renamed symbols to avoid ESP-IDF heap conflicts
  sh "cp -f lib/patch/compiler/prism_tlsf_wrapper.c #{mrbgem_path}/mruby-compiler2/lib/"

  # Copy thread-safe compile.c with mutex protection for multi-task environment
  sh "cp -f lib/patch/compiler/mruby-compiler2-compile.c #{mrbgem_path}/mruby-compiler2/src/compile.c"
end

namespace :set_target do
  desc "Linux target (dev/test)"
  task :linux => :setup do
    sh "#{DOCKER_CMD} idf.py --preview set-target linux"
  end

  desc "Set ESP32(S3) target"
  task :esp32 => :setup do
    sh "#{DOCKER_CMD} idf.py set-target esp32s3"
  end
end

namespace :build do
  desc "Linux target build (dev/test)"
  task :linux => :setup do
    unless Dir.exist?('build')
      Rake::Task['set_target:linux'].invoke
    end
    sh "#{DOCKER_CMD} bash -c 'export IDF_TARGET=linux && idf.py --preview -DCMAKE_BUILD_TYPE=Debug build'"
    puts 'Linux build complete. Run with: ./build/fmruby-core.elf'
  end

  desc "ESP32(S3) build"
  task :esp32 => :setup do
    unless Dir.exist?('build')
      Rake::Task['set_target:esp32'].invoke    
    end
    sh "#{DOCKER_CMD} idf.py build"
  end
end

desc "Flash to ESP32"
task :flash do
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py -p #{USB_SERIAL_PORT} flash"
end

desc "Check ESP32 HW"
task :check do
  sh "#{DOCKER_CMD_PRIVILEGED} esptool.py -p #{USB_SERIAL_PORT} flash_id"
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

desc "Full clean build artifacts (including host)"
task :clean do
  sh "rm -rf components/picoruby-esp32/picoruby/build/*"
end

desc "Serial monitor"
task :monitor do
  sh "#{DOCKER_CMD_INTERACTIVE} idf.py -p #{USB_SERIAL_PORT} monitor"
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