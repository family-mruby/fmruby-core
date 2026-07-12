MRuby::CrossBuild.new('family-mruby-linux') do |conf|
  conf.toolchain('gcc')

  conf.cc.command = 'gcc'
  conf.linker.command = 'gcc'
  conf.archiver.command = 'ar'

  conf.cc.host_command = 'gcc'

  conf.cc.defines << 'MRB_TICK_UNIT=5'
  conf.cc.defines << 'MRB_TIMESLICE_TICK_COUNT=10'
  conf.cc.defines << 'MRB_INT64'
  # Set UTF-8 string support at the build_config level so it applies to the
  # mruby core (which defines mrb_utf8len_table / mrb_utf8len / mrb_utf8_strlen)
  # as well as the gems that reference them (mruby-string-ext). Setting it only
  # inside picoruby-mruby's mrbgem.rake ran after the core, leaving the table
  # undefined at link.
  conf.cc.defines << 'MRB_UTF8_STRING'
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

  conf.picoruby

  # Common gems
  conf.gembox 'family_mruby'

  # HAL port selection. Upstream folded the standalone hal-*-task/-dir gems into
  # per-gem ports/<name>/ dirs, compiled by the rake build when conf.ports lists
  # a matching port (a CrossBuild compiles none unless set). The rake build has
  # no FreeRTOS headers, so FreeRTOS-dependent ports (the machine tick and the
  # mruby-task case-D tick) are compiled on the CMake/ESP-IDF side instead
  # (PICORUBY_SRCS, which PRIV_REQUIRES freertos). Here we only let the rake
  # build pick posix ports (socket, mruby-dir, and picoruby-machine's console).
  conf.ports :posix
  # ...but mruby-task's posix port is a SIGALRM+setitimer timer that collides
  # with the Linux FreeRTOS POSIX simulator's own signal scheduler (instruct
  # sec 3.5) and would race case-D. This name-only gem makes mruby-task drop its
  # port (resolve_external_hal!); its HAL comes from the CMake-built freertos
  # port (mrbgems/mruby-task/ports/freertos/task_hal.c).
  conf.gem core: 'hal-task-freertos'

  # mruby-dir is still an explicit dep (it is not pulled in transitively).
  # NOTE: hal-posix-io is NOT loaded (it depends on mruby-io which conflicts with fmrb-io)
  dir = "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems"
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
