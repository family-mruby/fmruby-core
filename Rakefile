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
  sh "cp -f lib/patch/CMakeLists.txt components/picoruby-esp32/CMakeLists.txt"
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
    sh "#{DOCKER_CMD} idf.py --preview build"
    puts 'Linux build complete. Run with: ./build/fmruby-core.elf'
  end

  desc "ESP32(S3) build"
  task :esp32 => :setup do
    sh "#{DOCKER_CMD} idf.py build"
  end
end

desc "Flash to ESP32"
task :flash do
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py flash"
end

desc "Open menuconfig"
task :menuconfig do
  sh "#{DOCKER_CMD} idf.py menuconfig"
end

desc "Full clean build artifacts"
task :clean do
  #sh "#{DOCKER_CMD} idf.py fullclean"
  sh "rm -rf build"
end

desc "Serial monitor"
task :monitor do
  sh "#{DOCKER_CMD_PRIVILEGED} idf.py monitor"
end

desc "Run Linux build (depends on :linux_build)"
task :run_linux => :linux_build do
  sh "./build/fmruby-core.elf"
end