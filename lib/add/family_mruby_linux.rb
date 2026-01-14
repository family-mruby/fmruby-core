MRuby::Build.new do |conf|

  conf.toolchain("gcc")

  conf.cc.defines << "MRB_TICK_UNIT=5"
  conf.cc.defines << "MRB_TIMESLICE_TICK_COUNT=10"

  conf.cc.defines << "MRB_64BIT"
  conf.cc.defines << "MRB_INT64"

  #conf.cc.defines << "MRB_USE_CUSTOM_RO_DATA_P"
  #conf.cc.defines << "MRB_LINK_TIME_RO_DATA_P"

  conf.cc.defines << "PICORB_PLATFORM_POSIX"
  conf.cc.defines << "PICORB_ALLOC_ESTALLOC"
  conf.cc.defines << "PICORB_ALLOC_ALIGN=8"
  
  conf.cc.defines << "ESTALLOC_DEBUG=1"

  # Common gems
  conf.gembox "family_mruby"

  # POSIX
  dir = "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems"
  conf.gem gemdir: "#{dir}/mruby-dir"
  conf.gem gemdir: "#{dir}/mruby-io"

  # Linux specific - Filesystem
  conf.gem core: "picoruby-fmrb-filesystem"  # HAL-based filesystem

  # settings for microruby
  conf.microruby

end

