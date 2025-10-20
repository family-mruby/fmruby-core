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
  sh "cp -f lib/patch/family_mruby_linux.rb components/picoruby-esp32/picoruby/build_config/"
  sh "cp -f lib/patch/family_mruby_esp32.rb components/picoruby-esp32/picoruby/build_config/"
  sh "cp -rf lib/patch/picoruby-fmrb-app components/picoruby-esp32/picoruby/mrbgems/"
  sh "cp -f lib/patch/picoruby-machine/mrbgem.rake components/picoruby-esp32/picoruby/mrbgems/picoruby-machine/"

  # Copy TLSF-based prism allocator files
  sh "cp -f lib/patch/prism_xallocator.h components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/include/"
  sh "cp -f lib/patch/prism_alloc.c components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/lib/"
  sh "cp -f lib/patch/mruby-compiler2-mrbgem.rake components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/mrbgem.rake"

  # Copy TLSF library files
  sh "mkdir -p components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/lib/tlsf"
  sh "cp -f components/mem_allocator/tlsf/tlsf.c components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/lib/tlsf/"
  sh "cp -f components/mem_allocator/tlsf/tlsf.h components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/lib/tlsf/"

  # Copy thread-safe compile.c with mutex protection for multi-task environment
  sh "cp -f lib/patch/mruby-compiler2-compile.c components/picoruby-esp32/picoruby/mrbgems/mruby-compiler2/src/compile.c"
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
    sh "#{DOCKER_CMD} bash -c 'export IDF_TARGET=linux && idf.py --preview build'"
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
task :clean do
  #sh "#{DOCKER_CMD} idf.py fullclean"
  sh "rm -rf build"
  sh "rm -rf components/picoruby-esp32/picoruby/build"
  sh "rm -rf host/sdl2/build"
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
    sh "rm -rf host/sdl2/build"
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