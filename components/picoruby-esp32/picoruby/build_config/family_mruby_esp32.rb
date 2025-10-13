MRuby::CrossBuild.new("esp32") do |conf|
  conf.toolchain("gcc")

  conf.cc.command = "xtensa-esp32-elf-gcc"
  conf.linker.command = "xtensa-esp32-elf-ld"
  conf.archiver.command = "xtensa-esp32-elf-ar"

  conf.cc.host_command = "gcc"
  conf.cc.flags << "-Wall"
  conf.cc.flags << "-Wno-format"
  conf.cc.flags << "-Wno-unused-function"
  conf.cc.flags << "-Wno-maybe-uninitialized"
  conf.cc.flags << "-mlongcalls"

  conf.cc.defines << "MRBC_TICK_UNIT=10"
  conf.cc.defines << "MRBC_TIMESLICE_TICK_COUNT=1"
  conf.cc.defines << "MRBC_USE_FLOAT=2"
  conf.cc.defines << "MRBC_CONVERT_CRLF=1"
  conf.cc.defines << "USE_FAT_FLASH_DISK"
  conf.cc.defines << "NDEBUG"

  # Common
  conf.gem core: 'mruby-bin-mrbc2'
  conf.gem core: 'mruby-compiler2'
  conf.gem core: 'picoruby-machine'
  conf.gem core: 'picoruby-bin-microruby'
  conf.gem core: 'picoruby-require'

  conf.gem core: 'picoruby-sandbox'
  conf.gem core: 'picoruby-env'
  conf.gem core: 'picoruby-crc'

  # Optional
  conf.gem core: "picoruby-eval"
  conf.gem core: "picoruby-yaml"

  # peripherals
#   conf.gem core: 'picoruby-gpio'
#   conf.gem core: 'picoruby-i2c'
#   conf.gem core: 'picoruby-spi'
#   conf.gem core: 'picoruby-adc'
#   conf.gem core: 'picoruby-uart'
#   conf.gem core: 'picoruby-pwm'

  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-kernel-ext"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-string-ext"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-array-ext"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-time"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-objectspace"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-metaprog"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-error"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-sprintf"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-math"

  # POSIX
  #conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-dir"
  #conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-io"
  #conf.gem core: 'mruby-dir'
  # ESP32
  conf.gem core:'picoruby-dir'

conf.microruby
end
