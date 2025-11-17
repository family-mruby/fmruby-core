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
  conf.cc.defines << "MRB_32BIT"
  conf.cc.defines << "NDEBUG"
  conf.cc.defines << "USE_FAT_FLASH_DISK"

  conf.cc.defines << "PICORB_ALLOC_ESTALLOC"
  conf.cc.defines << "PICORB_ALLOC_ALIGN=8"

  conf.cc.defines << "MRBC_TICK_UNIT=10"
  conf.cc.defines << "MRBC_TIMESLICE_TICK_COUNT=1"
  conf.cc.defines << "MRBC_USE_FLOAT=2"
  conf.cc.defines << "MRBC_CONVERT_CRLF=1"

  # Common gems
  conf.gembox "family_mruby"

  # peripherals
#   conf.gem core: 'picoruby-gpio'
#   conf.gem core: 'picoruby-i2c'
#   conf.gem core: 'picoruby-spi'
#   conf.gem core: 'picoruby-adc'
#   conf.gem core: 'picoruby-uart'
#   conf.gem core: 'picoruby-pwm'

  # ESP32
  #conf.gem core:'picoruby-dir'

  # settings for microruby
  conf.microruby

end
