# libmruby for the WebAssembly target (doc/wasm/, P4a).
#
# family_mruby_linux.rb with the toolchain swapped to Emscripten and the
# pointer width put back to 32 bits. Runs on the HOST (emsdk is not in the
# build container), invoked by rake wasm:mruby -- see rakelib/wasm.rake.
#
# ABI: every layout-affecting MRB_* define here must match what
# wasm/CMakeLists.txt passes to the sources that include mruby headers
# (the same rule as mruby_abi_defines.cmake on the IDF side).
MRuby::CrossBuild.new('family-mruby-wasm') do |conf|
  conf.toolchain('clang')

  conf.cc.command = 'emcc'
  conf.linker.command = 'emcc'
  conf.archiver.command = 'emar'

  conf.cc.host_command = 'gcc'

  # Emscripten pthreads: everything that ends up in the final module must be
  # compiled with -pthread or the atomics/shared-memory ABI does not match.
  conf.cc.flags << '-pthread'

  conf.cc.defines << 'MRB_TICK_UNIT=5'
  conf.cc.defines << 'MRB_TIMESLICE_TICK_COUNT=10'
  # wasm32: ILP32 like the ESP32 targets, unlike the 64-bit linux sim.
  conf.cc.defines << 'MRB_32BIT'
  conf.cc.defines << 'MRB_INT64'
  conf.cc.defines << 'MRB_UTF8_STRING'
  conf.cc.defines << 'MRB_NO_BOXING'
  conf.cc.defines << 'MRB_BASELINE_PROFILE=1'
  conf.cc.defines << 'MRB_USE_DEBUG_HOOK'
  # The self-supplied timeslice (doc/wasm/ P2) is not optional here: on the
  # cooperative wasm FreeRTOS port a separate tick task never runs while a
  # CPU-bound Ruby program holds the CPU, so the VM must feed its own ticks.
  conf.cc.defines << 'MRB_TASK_TICK_SELF_SUPPLY'
  conf.cc.defines << 'PICORB_PLATFORM_POSIX'
  conf.cc.defines << 'PICORB_ALLOC_ESTALLOC'
  conf.cc.defines << 'PICORB_ALLOC_ALIGN=8'
  conf.cc.defines << 'FMRB_NO_IO_CONSOLE'
  # Which build this is, for the gem ports. The gfx port asks it before
  # sending a video command: those are answered by the display task, which
  # this build has (wasm/backend/display_video_wasm.c), and refused on Retro.
  # It cannot ask FMRB_HW_MODERN -- that means P4 silicon -- nor
  # FMRB_HW_FAMILY_MODERN, which the Linux simulator also sets.
  conf.cc.defines << 'FMRB_PLATFORM_WASM=1'
  # Concurrent app ceiling: the mrbgem ports (picoruby-fmrb-app/kernel) size
  # per-app arrays by it, so it MUST match wasm/CMakeLists.txt.
  conf.cc.defines << 'FMRB_MAX_APPS=32'

  conf.picoruby

  # Common gems
  conf.gembox 'family_mruby'

  # Port selection, same reasoning as linux: the rake build compiles each
  # gem's ports/posix (Emscripten is POSIX enough for these), while the
  # FreeRTOS-dependent ports (machine tick, mruby-task case-D tick) are
  # compiled by wasm/CMakeLists.txt against the vendored wasm FreeRTOS port.
  conf.ports :posix
  conf.gem core: 'hal-task-freertos'

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

  # No networking gems: the browser has no BSD sockets to give them
  # (picoruby-socket / net-http / net-websocket are linux/esp32 only).

  # Zenoh and Asterism (doc/ruby_asterism, B1): the same two gems as the
  # Linux and P4 builds. A page cannot open TCP, so zenoh-pico is built for
  # its Emscripten platform with the WebSocket link only (ws/<host>:<port>,
  # zenohd's ws/ listener). The gem compiles the zenoh-pico core here; the
  # platform files (system layer and the WebSocket link, wasm/zenoh/) are
  # compiled by wasm/CMakeLists.txt, the way the P4 build compiles its
  # ESP-IDF ones. The gem reads the platform from ZENOH_PICO_PLATFORM; this
  # rake process (rake wasm:mruby) builds only this configuration, so setting
  # it here reaches nothing else.
  ENV['ZENOH_PICO_PLATFORM'] = 'ZENOH_EMSCRIPTEN'
  # zenoh-pico feature switches the gem's zenoh_generic_config.h leaves
  # overridable. The define list is the whole build's (the gems copy it), and
  # wasm/CMakeLists.txt passes the same values to the platform files.
  conf.cc.defines << 'Z_FEATURE_LINK_TCP=0'
  conf.cc.defines << 'Z_FEATURE_LINK_WS=1'
  # Peer mode needs a listening socket or a wait on several sockets, neither
  # of which a page has: client mode only.
  conf.cc.defines << 'Z_FEATURE_UNICAST_PEER=0'
  # zenoh-pico's platform types come from wasm/zenoh/zp_emscripten_platform.h
  # instead of its own Emscripten header (which lacks <stddef.h> in 1.10.1,
  # and whose WebSocket endpoint is an addrinfo for the socket emulation this
  # build does not use).
  conf.cc.defines << 'ZP_SYSTEM_PLATFORM_HEADER=\\"zp_emscripten_platform.h\\"'
  conf.cc.include_paths << File.expand_path('../../wasm/zenoh', __dir__)
  conf.gem core: "picoruby-asterism-zenoh"
  conf.gem core: "picoruby-asterism"
end
