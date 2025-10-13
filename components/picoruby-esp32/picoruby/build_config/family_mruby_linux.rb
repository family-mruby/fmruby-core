MRuby::Build.new do |conf|

  conf.toolchain

  conf.cc.defines << "PICORB_VM_MRUBYC"

#  conf.cc.defines << "MRBC_NO_STDIO"
  conf.cc.defines << "PICORUBY_INT64"
  conf.cc.defines << "MRBC_USE_HAL_POSIX"

  conf.cc.defines << "MRBC_TICK_UNIT=4"
  conf.cc.defines << "MRBC_TIMESLICE_TICK_COUNT=3"
  conf.cc.defines << "PICORB_PLATFORM_POSIX"

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
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-dir"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-io"
  conf.gem core: 'mruby-dir'
  # ESP32
  #conf.gem core:'picoruby-dir'
  
  conf.microruby

end

