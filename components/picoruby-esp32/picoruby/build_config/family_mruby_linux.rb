MRuby::Build.new do |conf|

  conf.toolchain("gcc")

  conf.cc.defines << "MRB_TICK_UNIT=1"
  conf.cc.defines << "MRB_TIMESLICE_TICK_COUNT=10"

  conf.cc.defines << "MRB_INT64"
  conf.cc.defines << "MRB_32BIT"
  conf.cc.defines << "PICORB_ALLOC_ESTALLOC"
  conf.cc.defines << "PICORB_ALLOC_ALIGN=8"

  #conf.cc.defines << "MRB_USE_CUSTOM_RO_DATA_P"
  #conf.cc.defines << "MRB_LINK_TIME_RO_DATA_P"

  conf.cc.defines << "PICORB_PLATFORM_POSIX"

  

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
  #conf.gem core: 'picoruby-gpio'
  #conf.gem core: 'picoruby-i2c'
  #conf.gem core: 'picoruby-spi'
  #conf.gem core: 'picoruby-adc'
  #conf.gem core: 'picoruby-uart'
  #conf.gem core: 'picoruby-pwm'

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
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-dir"
  conf.gem gemdir: "mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-io"
  # ESP32
  #conf.gem core:'picoruby-dir'

  # Family mruby specific
  conf.gem core: "picoruby-fmrb-app"

  # Manual microruby configuration without task scheduler
  conf.cc.include_paths << "#{conf.gems['mruby-compiler2'].dir}/include"
  conf.cc.include_paths << "#{conf.gems['mruby-compiler2'].dir}/lib/prism/include"
  conf.cc.include_paths << "#{conf.gems['picoruby-mruby'].dir}/lib/mruby/include"
  conf.cc.defines << "PICORB_VM_MRUBY"
  
  # conf.microruby

end

