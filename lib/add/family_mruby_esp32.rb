MRuby::CrossBuild.new("esp32") do |conf|
  conf.toolchain("gcc")

  conf.cc.command = "xtensa-esp32-elf-gcc"
  conf.linker.command = "xtensa-esp32-elf-gcc"
  conf.archiver.command = "xtensa-esp32-elf-ar"

  conf.cc.host_command = "gcc"
  conf.cc.flags << "-Wall"
  conf.cc.flags << "-Wno-format"
  conf.cc.flags << "-Wno-unused-function"
  conf.cc.flags << "-Wno-maybe-uninitialized"
  conf.cc.flags << "-mlongcalls"

  conf.cc.defines << "MRB_TICK_UNIT=5"
  conf.cc.defines << "MRB_TIMESLICE_TICK_COUNT=10"
  conf.cc.defines << "MRBC_CONVERT_CRLF=1"
  conf.cc.defines << "MRB_INT64"
  conf.cc.defines << "MRB_32BIT"
  conf.cc.defines << "PICORB_ALLOC_ESTALLOC"
  conf.cc.defines << "PICORB_ALLOC_ALIGN=8"
  conf.cc.defines << "USE_FAT_FLASH_DISK"
  conf.cc.defines << "NDEBUG"
  conf.cc.defines << "ESP32_PLATFORM"
  conf.cc.defines << "FMRB_NO_IO_CONSOLE"

  if ENV['PICORB_DEBUG']
    conf.cc.defines << "ESTALLOC_DEBUG"
    conf.enable_debug
  end

  conf.microruby

  # HAL port selection (see family_mruby_linux.rb for the rationale).
  # mruby-task -> ports/freertos (case-D top-half), machine/socket -> ports/esp32.
  # TODO(esp32 build): confirm task_hal.c is compiled by exactly one of the rake
  # port mechanism vs CMake PICORUBY_SRCS (avoid double-compile / neither) and
  # record the result in resolutions.md.
  conf.ports :esp32, :freertos, :posix

  # Common gems
  conf.gembox "family_mruby"

  # mruby extension gems
  dir = "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems"
  conf.gem gemdir: "#{dir}/mruby-kernel-ext"
  conf.gem gemdir: "#{dir}/mruby-string-ext"
  conf.gem gemdir: "#{dir}/mruby-array-ext"
  conf.gem gemdir: "#{dir}/mruby-time"
  conf.gem gemdir: "#{dir}/mruby-objectspace"
  conf.gem gemdir: "#{dir}/mruby-metaprog"
  conf.gem gemdir: "#{dir}/mruby-error"
  conf.gem gemdir: "#{dir}/mruby-sprintf"
  conf.gem gemdir: "#{dir}/mruby-math"
  conf.gem gemdir: "#{dir}/mruby-dir"

  # Hardware peripheral gems
  hw = "#{MRUBY_ROOT}/mrbgems"
  conf.gem gemdir: "#{hw}/picoruby-gpio"
  conf.gem gemdir: "#{hw}/picoruby-rmt"
  conf.gem gemdir: "#{hw}/picoruby-i2c"

  # RTC driver (pure Ruby, depends on picoruby-i2c)
  conf.gem core: "picoruby-rx8900"
  # conf.gem gemdir: "#{hw}/picoruby-adc"
  # conf.gem gemdir: "#{hw}/picoruby-pwm"
  # conf.gem gemdir: "#{hw}/picoruby-spi"
  # conf.gem gemdir: "#{hw}/picoruby-uart"
  # conf.gem gemdir: "#{hw}/picoruby-watchdog"
end
