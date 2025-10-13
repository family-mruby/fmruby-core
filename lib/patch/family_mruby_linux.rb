MRuby::Build.new do |conf|

  conf.toolchain

  conf.cc.defines << "PICORB_VM_MRUBYC"

#  conf.cc.defines << "MRBC_NO_STDIO"
  conf.cc.defines << "PICORUBY_INT64"
  conf.cc.defines << "MRBC_USE_HAL_POSIX"

  conf.cc.defines << "MRBC_TICK_UNIT=4"
  conf.cc.defines << "MRBC_TIMESLICE_TICK_COUNT=3"
  conf.cc.defines << "PICORB_PLATFORM_POSIX"

  #---conf.gembox "minimum"
  conf.gem core: 'mruby-compiler2'
  conf.gem core: 'mruby-bin-mrbc2'
  conf.gem core: 'picoruby-bin-microruby'
  conf.gem core: 'picoruby-mrubyc'
  conf.gem core: 'picoruby-require'

  #---conf.gembox "posix"
#  conf.gem core: "picoruby-bin-r2p2"
#  conf.gem core: "picoruby-picotest"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-dir"
  conf.gem gemdir: "picoruby-mruby/lib/mruby/mrbgems/mruby-io"

  #---conf.gembox "stdlib"
  conf.gem core: 'picoruby-machine'
  conf.gem core: 'picoruby-base16'
  conf.gem core: 'picoruby-base64'
  conf.gem core: 'picoruby-json'
  conf.gem core: 'picoruby-yaml'
  conf.gem core: "picoruby-picorubyvm"
  conf.gem core: "picoruby-metaprog"
  conf.gem core: "picoruby-eval"
  conf.gem core: "picoruby-numeric-ext"
  conf.gem core: "picoruby-pack"
  conf.gem core: "picoruby-time-class"
  conf.gem core: "picoruby-vram"
  conf.gem core: "picoruby-rapicco"

  #---conf.gembox "utils"
  conf.gem core: "picoruby-picoline"
  conf.gem core: "picoruby-rng"
  conf.gem core: "picoruby-vim"
    
  conf.gem core: "picoruby-machine"

  conf.picoruby(alloc_libc: true)

end

