# Rakefile — Family mruby ESP-IDF build wrapper (Docker)
require "rake"

UID  = `id -u`.strip
GID  = `id -g`.strip
PWD_ = Dir.pwd

IMAGE       = ENV.fetch("ESP_IDF_IMAGE", "esp32_build_container:v5.5.1")
DEVICE_ARGS = ENV["DEVICE_ARGS"].to_s

DOCKER_CMD = [
  "docker run --rm",
  "--user #{UID}:#{GID}",
  "-v #{PWD_}:/project",
  IMAGE
].join(" ")

DOCKER_CMD_PRIVILEGED = [
  "docker run --rm",
  "--group-add=dialout --group-add=plugdev --privileged",
  DEVICE_ARGS,
  "--user #{UID}:#{GID}",
  "-v #{PWD_}:/project",
  "-v /dev/bus/usb:/dev/bus/usb",
  IMAGE
].join(" ")

desc "Build Setup (Patch files)"
task :setup do
  mrbgem_path = "components/picoruby-esp32/picoruby/mrbgems"
  # Add
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-app"
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-kernel"
  sh "cp -rf lib/add/picoruby-fmrb-app #{mrbgem_path}/"
  sh "cp -rf lib/add/picoruby-fmrb-kernel #{mrbgem_path}/"

  sh "cp -f lib/add/family_mruby_linux.rb components/picoruby-esp32/picoruby/build_config/"
  sh "cp -f lib/add/family_mruby_esp32.rb components/picoruby-esp32/picoruby/build_config/"

  # Replace
  sh "rm -rf #{mrbgem_path}/picoruby-filesystem-fat"
  sh "cp -rf lib/replace/picoruby-filesystem-fat #{mrbgem_path}/"

  # Patch
  # Machine
  sh "cp -f lib/patch/picoruby-machine/mrbgem.rake #{mrbgem_path}/picoruby-machine/"
  sh "cp -f lib/patch/picoruby-machine/ports/posix/hal.c #{mrbgem_path}/picoruby-machine/ports/posix/"
  sh "cp -f lib/patch/picoruby-machine/ports/posix/machine.c #{mrbgem_path}/picoruby-machine/ports/posix/"
  sh "cp -f lib/patch/picoruby-machine/ports/esp32/machine.c #{mrbgem_path}/picoruby-machine/ports/esp32/"
  sh "cp -f lib/patch/picoruby-mruby/include/hal.h #{mrbgem_path}/picoruby-mruby/include/"
  sh "cp -f lib/patch/picoruby-mruby/include/hal.h #{mrbgem_path}/picoruby-machine/include/"

  # littleFS
  sh "cp -f lib/patch/esp_littlefs/CMakeLists.txt components/esp_littlefs/"

  # Copy picoruby-env patch (thread-safe ENV for multi-VM environment)
  sh "cp -f lib/patch/picoruby-env/ports/posix/env.c #{mrbgem_path}/picoruby-env/ports/posix/"

  # Copy TLSF-based prism allocator files
  sh "cp -f lib/patch/compiler/prism_xallocator.h #{mrbgem_path}/mruby-compiler2/include/"
  sh "cp -f lib/patch/compiler/prism_alloc.c #{mrbgem_path}/mruby-compiler2/lib/"
  sh "cp -f lib/patch/compiler/mruby-compiler2-mrbgem.rake #{mrbgem_path}/mruby-compiler2/mrbgem.rake"

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
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py flash"
end

desc "Open menuconfig"
task :menuconfig do
  term = ENV['TERM'] || 'xterm-256color'
  docker_cmd_interactive = [
    "docker run --rm -it",
    "--user #{UID}:#{GID}",
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
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py monitor"
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
      puts "Starting FMRuby Core..."
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