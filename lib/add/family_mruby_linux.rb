MRuby::CrossBuild.new('family-mruby-linux') do |conf|
  conf.toolchain('gcc')

  conf.cc.command = 'gcc'
  conf.linker.command = 'gcc'
  conf.archiver.command = 'ar'

  conf.cc.host_command = 'gcc'

  conf.cc.defines << 'MRB_TICK_UNIT=5'
  conf.cc.defines << 'MRB_TIMESLICE_TICK_COUNT=10'
  conf.cc.defines << 'MRB_INT64'
  conf.cc.defines << 'PICORB_PLATFORM_POSIX'
  conf.cc.defines << 'PICORB_ALLOC_ESTALLOC'
  conf.cc.defines << 'PICORB_ALLOC_ALIGN=8'
  conf.cc.defines << 'FMRB_NO_IO_CONSOLE'

  if ENV['PICORB_DEBUG']
    conf.cc.defines << 'ESTALLOC_DEBUG'
    conf.enable_debug
  else
    conf.cc.defines << 'ESTALLOC_DEBUG=1'
  end

  conf.microruby

  # Common gems
  conf.gembox 'family_mruby'

  # POSIX HAL gems and their dependents
  # NOTE: hal-posix-io is NOT loaded (it depends on mruby-io which conflicts with fmrb-io)
  dir = "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems"
  conf.gem gemdir: "#{dir}/hal-posix-task"
  conf.gem gemdir: "#{dir}/hal-posix-dir"
  conf.gem gemdir: "#{dir}/mruby-dir"

  # mruby extension gems
  conf.gem gemdir: "#{dir}/mruby-kernel-ext"
  conf.gem gemdir: "#{dir}/mruby-string-ext"
  conf.gem gemdir: "#{dir}/mruby-array-ext"
  conf.gem gemdir: "#{dir}/mruby-time"
  conf.gem gemdir: "#{dir}/mruby-objectspace"
  conf.gem gemdir: "#{dir}/mruby-metaprog"
  conf.gem gemdir: "#{dir}/mruby-error"
  conf.gem gemdir: "#{dir}/mruby-sprintf"
  conf.gem gemdir: "#{dir}/mruby-math"

  # Ruby networking client API (see doc/ruby_network_api_design.md).
  # ports/posix sources are compiled automatically by the picoruby build.
  # SSL uses OpenSSL (libssl-dev in the build container); the system CA
  # store verifies HTTPS by default, matching the esp32p4 behavior.
  conf.gem core: "picoruby-socket"
  conf.gem core: "picoruby-net-http"
  conf.gem core: "picoruby-net-websocket"
end
