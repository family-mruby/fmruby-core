# libmruby for the native Linux program (doc/linux_native/, L1).
#
# The browser build's configuration (family_mruby_wasm.rb) with the host gcc
# instead of emcc, and 64-bit pointers like the Linux simulation. Runs on the
# HOST (rake native:mruby), not in the build container -- see
# rakelib/native.rake.
#
# ABI: every layout-affecting MRB_* define here must match what
# hosted/hosted.cmake passes to the sources that include mruby headers (the
# same rule as mruby_abi_defines.cmake on the IDF side). The boot-time guard
# in fmrb_app_init compares sizeof(mrb_state) across the boundary.
#
# Architectures (L3a): the packages are built by rake native:cross inside
# native/docker's image with FMRB_NATIVE_ARCH set. arm64 cross-compiles with
# Debian's aarch64-linux-gnu toolchain; amd64 (or unset, the developer's own
# build on the host) is the plain gcc. Only the target's compiler changes:
# the host tools (mrbc) stay x86_64, and every define below is the same for
# every architecture -- the Ruby bytecode and the Spinel output are shared.
FMRB_NATIVE_TOOL_PREFIX =
  case ENV['FMRB_NATIVE_ARCH'].to_s
  when '', 'amd64' then ''
  when 'arm64' then 'aarch64-linux-gnu-'
  else raise "FMRB_NATIVE_ARCH must be arm64 or amd64 (got #{ENV['FMRB_NATIVE_ARCH']})"
  end

MRuby::CrossBuild.new('family-mruby-native') do |conf|
  conf.toolchain('gcc')

  conf.cc.command = ENV['FMRB_NATIVE_CC'] || "#{FMRB_NATIVE_TOOL_PREFIX}gcc"
  conf.linker.command = ENV['FMRB_NATIVE_CC'] || "#{FMRB_NATIVE_TOOL_PREFIX}gcc"
  conf.archiver.command = ENV['FMRB_NATIVE_AR'] || "#{FMRB_NATIVE_TOOL_PREFIX}ar"

  conf.cc.host_command = 'gcc'

  # The FreeRTOS port runs every task on a pthread.
  conf.cc.flags << '-pthread'

  conf.cc.defines << 'MRB_TICK_UNIT=5'
  conf.cc.defines << 'MRB_TIMESLICE_TICK_COUNT=10'
  conf.cc.defines << 'MRB_INT64'
  conf.cc.defines << 'MRB_UTF8_STRING'
  conf.cc.defines << 'MRB_NO_BOXING'
  conf.cc.defines << 'MRB_BASELINE_PROFILE=1'
  conf.cc.defines << 'MRB_USE_DEBUG_HOOK'
  # The self-supplied timeslice (doc/wasm/ P2) is not optional here: the
  # native build runs on the same cooperative FreeRTOS port as the browser,
  # where a separate tick task never runs while a CPU-bound Ruby program holds
  # the CPU, so the VM must feed its own ticks.
  conf.cc.defines << 'MRB_TASK_TICK_SELF_SUPPLY'
  conf.cc.defines << 'PICORB_PLATFORM_POSIX'
  conf.cc.defines << 'PICORB_ALLOC_ESTALLOC'
  conf.cc.defines << 'PICORB_ALLOC_ALIGN=8'
  conf.cc.defines << 'FMRB_NO_IO_CONSOLE'
  # Which build this is, for the gem ports (same names as the CMake side).
  conf.cc.defines << 'FMRB_PLATFORM_HOSTED=1'
  conf.cc.defines << 'FMRB_PLATFORM_NATIVE=1'
  # Concurrent app ceiling: the mrbgem ports (picoruby-fmrb-app/kernel) size
  # per-app arrays by it, so it MUST match hosted/hosted.cmake.
  conf.cc.defines << 'FMRB_MAX_APPS=32'

  conf.picoruby

  # Common gems
  conf.gembox 'family_mruby'

  # Port selection, as on linux and wasm: the rake build compiles each gem's
  # ports/posix, while the FreeRTOS-dependent ports (machine tick, mruby-task
  # case-D tick) are compiled by native/CMakeLists.txt against the hosted
  # FreeRTOS port.
  conf.ports :posix
  conf.gem core: 'hal-task-freertos'
  # mruby-dir's posix port roots every path in one flat flash/ directory; the
  # native namespace is spread over several host roots, so the directory HAL
  # is the file-HAL-backed one (lib/add/hal-esp32-dir/dir_hal.c, compiled by
  # native/CMakeLists.txt) and this name-only gem drops the posix port.
  conf.gem core: 'hal-dir-fmrb'

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

  # Networking (doc/linux_native/report/l2n.md): the Linux simulation's set.
  # The gems are unchanged; their posix ports wait in recv, connect,
  # getaddrinfo and OpenSSL, which on the cooperative scheduler would stop
  # every task with them, so the native link routes those calls through the
  # blocking-call gate (native/net/net_wrap.c, hosted/gate/): a helper thread
  # outside FreeRTOS makes the call while the task sleeps. SSL is OpenSSL
  # (linked by native/CMakeLists.txt), verified against the system CA store
  # as on the simulation.
  conf.gem core: "picoruby-socket"
  conf.gem core: "picoruby-net-http"
  conf.gem core: "picoruby-net-websocket"

  # Zenoh and Asterism, as on the simulation: zenoh-pico built single-threaded
  # for its unix platform (ZENOH_LINUX, the gem's default for this build name)
  # and polled from Ruby, its TCP link the gem's own (src/zp_tcp_posix.c).
  # The router is the machine's zenoh_router setting
  # (config/system_conf_native.toml: tcp/127.0.0.1:7447).
  conf.gem core: "picoruby-asterism-zenoh"
  conf.gem core: "picoruby-asterism"
  conf.gem core: "picoruby-fmrb-asterism"
end
