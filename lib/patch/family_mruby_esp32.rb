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

  conf.cc.defines << "MRBC_TICK_UNIT=10"
  conf.cc.defines << "MRBC_TIMESLICE_TICK_COUNT=1"
  conf.cc.defines << "MRBC_USE_FLOAT=2"
  conf.cc.defines << "MRBC_CONVERT_CRLF=1"
  conf.cc.defines << "USE_FAT_FLASH_DISK"
  conf.cc.defines << "NDEBUG"
  conf.cc.defines << "PICORB_ALLOC_ESTALLOC"
  conf.cc.defines << "PICORB_ALLOC_ALIGN=8"

  # Common
  conf.gem core: 'mruby-compiler2'
  conf.gem core: 'picoruby-mruby'
  conf.gem core: 'picoruby-machine'
  conf.gem core: 'picoruby-require'

  conf.gem core: 'picoruby-sandbox'
  conf.gem core: 'picoruby-env'
  conf.gem core: 'picoruby-crc'
  conf.gem core: 'picoruby-io-console'

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

  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-kernel-ext"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-string-ext"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-array-ext"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-time"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-objectspace"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-metaprog"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-error"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-sprintf"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-math"

  # POSIX
  #conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-dir"
  #conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-io"
  #conf.gem core: 'mruby-dir'
  # ESP32
  #conf.gem core:'picoruby-dir'

  # Family mruby specific
  conf.gem core: "picoruby-fmrb-app"
  conf.gem core: "picoruby-fmrb-kernel"

  # Manual microruby configuration without task scheduler
  # conf.cc.include_paths << "#{conf.gems['mruby-compiler2'].dir}/include"
  # conf.cc.include_paths << "#{conf.gems['mruby-compiler2'].dir}/lib/prism/include"
  # conf.cc.include_paths << "#{conf.gems['picoruby-mruby'].dir}/lib/mruby/include"
  # conf.cc.include_paths << "#{conf.gems['picoruby-machine'].dir}/include"
  # conf.cc.include_paths << "#{conf.gems['picoruby-io-console'].dir}/include"
  # conf.cc.defines << "PICORB_VM_MRUBY"

  # settings for microruby
  conf.microruby

end
