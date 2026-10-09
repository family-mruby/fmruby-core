# rakelib/setup.rake
# Source patching (mrbgem copy/replace/patch), icons, and target select.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

desc "Regenerate the launcher icon BMPs from their .icon sources"
task :icons do
  # The .icon text files are the editable source; the BMPs beside them are what
  # the device actually loads (graphics-audio decodes them, instead of the
  # launcher pushing pixels one GFX command at a time). Re-run after editing an
  # .icon and commit both.
  sh "ruby tool/gen_icon_bmp.rb"
end

desc "Regenerate the game BGM .fmsq files from their .mml scores"
task :music do
  # The .mml is the score (the same file format FmrbMidi::MmlPlayer reads);
  # the .fmsq beside it is what a game hands to the audio task, which then
  # loops it without the app's help. Listed rather than globbed so a score
  # meant only for the MML player does not get a sequence it has no use for.
  # Re-run after editing a score and commit both.
  ["korobeiniki"].each do |name|
    sh "ruby tool/mml2fmsq.rb flash/usr/share/music/#{name}.mml"
  end
end

desc "Regenerate the shooter's sprite BMPs from their pixel art source"
task :sprites do
  # Same arrangement as :icons - the art lives in the generator, the BMPs it
  # writes are what the device loads (flash/usr/share/sprites/shooter, sent to
  # the graphics side by the app). Re-run after editing the art and commit the
  # regenerated files.
  sh "ruby tool/gen_shooter_sprites.rb"
end

desc "Build Setup (Patch files)"
task :setup do
  mrbgem_path = "components/picoruby-esp32/picoruby/mrbgems"
  # ---------- Add ----------
  # const (must be copied first, as it's used by kernel and app)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-const"
  sh "cp -rf lib/add/picoruby-fmrb-const #{mrbgem_path}/"
  # log
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-log"
  sh "cp -rf lib/add/picoruby-fmrb-log #{mrbgem_path}/"
  # msgpack (must be copied before app, as app depends on it)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-msgpack"
  sh "cp -rf lib/add/picoruby-fmrb-msgpack #{mrbgem_path}/"
  # kernel
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-kernel"
  sh "cp -rf lib/add/picoruby-fmrb-kernel #{mrbgem_path}/"
  # app
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-app"
  sh "cp -rf lib/add/picoruby-fmrb-app #{mrbgem_path}/"
  # editor-core (document model in C; depends on syntax-highlight, copied below)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-editor-core"
  sh "cp -rf lib/add/picoruby-fmrb-editor-core #{mrbgem_path}/"
  # ti (type inference engine for the editor). Unlike the gems above this one
  # is not ours: it comes from the pinned fork checkout (rake ti:setup), so the
  # copy source is PICORUBY_TI_DIR instead of lib/add. Everything the firmware
  # does not need is dropped from the copy: .git, the unused lib/prism
  # submodule, the Go LSP server, the host tests, docs and the generator.
  ti_dir = picoruby_ti_dir!
  sh "rm -rf #{mrbgem_path}/picoruby-ti"
  sh "cp -rf #{ti_dir} #{mrbgem_path}/picoruby-ti"
  # (rm_rf, not a brace expansion in sh: /bin/sh is dash here and would take
  # the braces literally, silently leaving the directories in place)
  %w[.git lib lsp host_test images example tidbgen].each do |unused|
    rm_rf "#{mrbgem_path}/picoruby-ti/#{unused}"
  end
  # Where the engine's working arena lives on the ESP32 targets. The engine
  # includes this by name (TI_ARENA_INCLUDE, set in the esp32 build configs)
  # and finds it because the gem puts its own src/ on the include path.
  sh "cp -f lib/add/ti_arena_esp32.h #{mrbgem_path}/picoruby-ti/src/"
  # The type database is generated from OUR sig/*.rbs (the FMRB API), on the
  # host, into the copy only -- the docker build has no ruby for this.
  picoruby_ti_gen_db(ti_dir, "#{PICORUBY_TI_GEM_DIR}/src/generated")
  # The other half of those comments becomes the editor's F1 help, under
  # flash/, so it is in place before the storage image is staged.
  picoruby_ti_gen_help
  # debug (on-device debugger API; depends on msgpack, copied above)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-debug"
  sh "cp -rf lib/add/picoruby-fmrb-debug #{mrbgem_path}/"
  # filesystem
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-filesystem"
  sh "cp -rf lib/add/picoruby-fmrb-filesystem #{mrbgem_path}/"
  # io
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-io"
  sh "cp -rf lib/add/picoruby-fmrb-io #{mrbgem_path}/"
  # syntax-highlight
  sh "rm -rf #{mrbgem_path}/picoruby-syntax-highlight"
  sh "cp -rf lib/add/picoruby-syntax-highlight #{mrbgem_path}/"
  # rx8900
  sh "rm -rf #{mrbgem_path}/picoruby-rx8900"
  sh "cp -rf lib/add/picoruby-rx8900 #{mrbgem_path}/"
  # rx8130 (Tab5 RTC)
  sh "rm -rf #{mrbgem_path}/picoruby-rx8130"
  sh "cp -rf lib/add/picoruby-rx8130 #{mrbgem_path}/"
  # bmi270 (Tab5 six-axis IMU)
  sh "rm -rf #{mrbgem_path}/picoruby-bmi270"
  sh "cp -rf lib/add/picoruby-bmi270 #{mrbgem_path}/"
  # picorabbit
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-picorabbit"
  sh "cp -rf lib/add/picoruby-fmrb-picorabbit #{mrbgem_path}/"
  # bmp332
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-bmp332"
  sh "cp -rf lib/add/picoruby-fmrb-bmp332 #{mrbgem_path}/"
  # fft (the four-engine FFT: doc/mic_spectrum). Its mrblib/fft_core.rb is also
  # what the Spinel backend compiles, copied into the Spinel source dir by
  # rake spinel:gen -- one file, both engines.
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-fft"
  sh "cp -rf lib/add/picoruby-fmrb-fft #{mrbgem_path}/"
  # spinel-hello (minimal sample of "Spinel as a gem": doc/spinel_aot/
  # adding_a_spinel_gem.md). native/ and spinel/ ride along in the copy but are
  # not compiled from here (main compiles native/, rake spinel:gen the entry).
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-spinel-hello"
  sh "cp -rf lib/add/picoruby-fmrb-spinel-hello #{mrbgem_path}/"
  # raycast (the raycaster's inner loop on both engines: doc/raycast_spinel).
  # Same shape as fft: mrblib/raycast_core.rb is what the Spinel backend
  # compiles too, staged by rake spinel:gen -- one file, both engines.
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-raycast"
  sh "cp -rf lib/add/picoruby-fmrb-raycast #{mrbgem_path}/"
  # midi (imported from Midori; Ruby protocol layer only, see the gem's
  # FAMILY_MRUBY_PORT.md)
  sh "rm -rf #{mrbgem_path}/picoruby-midi"
  sh "cp -rf lib/add/picoruby-midi #{mrbgem_path}/"
  # midi-mml (the MML parser only, same import rule; the player is ours)
  sh "rm -rf #{mrbgem_path}/picoruby-midi-mml"
  sh "cp -rf lib/add/picoruby-midi-mml #{mrbgem_path}/"
  # fmrb-midi (APU transport; depends on midi and app, copied above)
  sh "rm -rf #{mrbgem_path}/picoruby-fmrb-midi"
  sh "cp -rf lib/add/picoruby-fmrb-midi #{mrbgem_path}/"
  # hal-task-freertos (name-only gem: makes mruby-task drop its own task_hal
  # port so the FreeRTOS case-D port is used via CMake instead; see the gem's
  # mrbgem.rake and doc/work_picoruby_merge/instruct_d7_b1_tick.md sec 3.5)
  sh "rm -rf #{mrbgem_path}/hal-task-freertos"
  sh "cp -rf lib/add/hal-task-freertos #{mrbgem_path}/"
  # picoruby-asterism-zenoh (Asterism::Zenoh, the Zenoh client:
  # doc/ruby_asterism). Not ours to keep here: like picoruby-ti it comes from
  # a pinned checkout (rake asterism:setup, PICORUBY_ASTERISM_ZENOH_DIR
  # overrides), and only what an mruby build reads is copied (no .git, host
  # test, Rakefile or vendor/). The zenoh-pico sources the gem pins (rake
  # zenoh:setup) go under the copy's vendor/, where its mrbgem.rake looks for
  # them. Only the Linux and P4 builds link it (listed in
  # lib/add/family_mruby_linux.rb and family_mruby_esp32p4.rb). The
  # zenoh-pico files keep their timestamps (cp -p) so its ~150 objects are
  # not rebuilt on every build. The copy under the gem's former name
  # (picoruby-zenoh) is removed.
  zz_dir = picoruby_asterism_zenoh_dir!
  zp_dir = zenoh_pico_dir!
  sh "rm -rf #{mrbgem_path}/picoruby-zenoh"
  sh "rm -rf #{mrbgem_path}/picoruby-asterism-zenoh"
  mkdir_p "#{mrbgem_path}/picoruby-asterism-zenoh"
  # LICENSE-APACHE and NOTICE: the gem's zenoh-pico derived files and
  # zenoh-pico itself are used under Apache-2.0 (THIRD_PARTY_LICENSES.md).
  # mrblib/: the gem's Ruby half (0.4.0 on: keywords, deprecations).
  %w[mrbgem.rake include src ports mrblib README.md LICENSE LICENSE-APACHE NOTICE].each do |entry|
    next if entry == "mrblib" && !File.directory?("#{zz_dir}/mrblib")
    sh "cp -rf #{zz_dir}/#{entry} #{mrbgem_path}/picoruby-asterism-zenoh/"
  end
  zp_copy = "#{mrbgem_path}/picoruby-asterism-zenoh/vendor/zenoh-pico"
  mkdir_p zp_copy
  %w[src include LICENSE NOTICE.md version.txt].each do |entry|
    sh "cp -rp #{zp_dir}/#{entry} #{zp_copy}/"
  end
  # picoruby-asterism (Asterism, the object layer and the ROS 2 node over
  # Asterism::Zenoh; pure Ruby). From the pinned asterism checkout (rake
  # asterism:setup, ASTERISM_DIR overrides): the mrbgem is its mrbgem.rake
  # and mrblib/. Linked by the same builds as picoruby-asterism-zenoh.
  ast_dir = asterism_dir!
  sh "rm -rf #{mrbgem_path}/picoruby-asterism"
  mkdir_p "#{mrbgem_path}/picoruby-asterism"
  %w[mrbgem.rake mrblib README.md LICENSE].each do |entry|
    sh "cp -rf #{ast_dir}/#{entry} #{mrbgem_path}/picoruby-asterism/"
  end
  # The bundled ROS 2 message types, from the same checkout into the storage
  # tree (/usr/share/asterism/msgs on the device; the sim reads flash/
  # directly). Generated here and gitignored, like flash/help: the source is
  # the asterism repository. NOTICE and the Apache-2.0 text travel with them.
  rm_rf ASTERISM_MSGS_DIR
  mkdir_p File.dirname(ASTERISM_MSGS_DIR)
  cp_r "#{ast_dir}/data/msgs", ASTERISM_MSGS_DIR
  # conf
  sh "cp -f lib/add/family_mruby.gembox #{mrbgem_path}/"
  sh "cp -f lib/add/family_mruby_linux.rb components/picoruby-esp32/picoruby/build_config/"
  sh "cp -f lib/add/family_mruby_esp32.rb components/picoruby-esp32/picoruby/build_config/"
  sh "cp -f lib/add/family_mruby_wasm.rb components/picoruby-esp32/picoruby/build_config/"

  # ---------- Replace ----------
  # Machine
  sh "rm -rf #{mrbgem_path}/picoruby-machine"
  sh "cp -rf lib/replace/picoruby-machine #{mrbgem_path}/"

  # ---------- Patch ----------
  # picoruby-mruby: alloc.c (estalloc multi-VM), hal.h, mrbgem.rake, and
  # lib/mruby/src/vm.c (the self-supplied timeslice hook, four additive hunks
  # marked "fmrb: self-supplied timeslice" -- see doc/wasm/report/p2.md; it
  # compiles to nothing unless MRB_TASK_TICK_SELF_SUPPLY is defined). vm.c is a
  # whole-file copy, so it has to be refreshed when the mruby submodule moves.
  sh "cp -rf lib/patch/picoruby-mruby #{mrbgem_path}/"

  # mruby-io file_constants.rb
  sh "cp -f lib/patch/picoruby-mruby/mrbgems/mruby-io/mrblib/file_constants.rb #{mrbgem_path}/picoruby-mruby/lib/mruby/mrbgems/mruby-io/mrblib/"

  # littleFS
  sh "cp -f lib/patch/esp_littlefs/CMakeLists.txt components/esp_littlefs/"

  # picoruby-env
  sh "cp -f lib/patch/picoruby-env/ports/posix/env.c #{mrbgem_path}/picoruby-env/ports/posix/"

  # mruby-compiler (renamed upstream from mruby-compiler2; same repo).
  # prism allocator patches dropped: upstream routes prism through the VM
  # estalloc heap via global_mrb (owner decision "Option A"). Only the
  # compile.c NULL-guard remains.
  sh "cp -f lib/patch/compiler/mruby-compiler2-compile.c #{mrbgem_path}/mruby-compiler/src/compile.c"
  # prism's allocator: per calling task instead of one process-wide mrb_state
  # (doc/editor_ti/report/p2.md). The two files go together -- the header names
  # the hooks, ccontext.c implements them.
  sh "cp -f lib/patch/compiler/prism_xallocator.h #{mrbgem_path}/mruby-compiler/include/prism_xallocator.h"
  sh "cp -f lib/patch/compiler/mruby-compiler2-ccontext.c #{mrbgem_path}/mruby-compiler/src/ccontext.c"
  # cdump.c: mrbc -B emits the child-irep tables (`*_reps_N`) as
  # `const mrb_irep *const`, so every gem's mrblib lands in flash instead of
  # internal RAM (doc/iram_reduction/report/r1.md). Whole-file copy: refresh it
  # when the mruby-compiler submodule moves.
  sh "cp -f lib/patch/compiler/mruby-compiler-cdump.c #{mrbgem_path}/mruby-compiler/src/cdump.c"
  # codegen.c: case/in fixes backported from mruby/mruby 0e6bac0e5a7e (a hash
  # pattern's value no longer overwrites the value it is compared with) and
  # 680084ac1275 (a capture inside a block binds a local of the enclosing
  # scope). The copy is the vendored 10408c3 plus those two commits only
  # (doc/ruby_asterism/upstream/picoruby_case_in.patch, report/c8.md).
  # Whole-file copy: drop it when the mruby-compiler submodule moves to
  # picoruby/mruby-compiler2 a2c72afb or later, which has both.
  sh "cp -f lib/patch/compiler/mruby-compiler-codegen.c #{mrbgem_path}/mruby-compiler/src/codegen.c"

  # mrbgem.rake patches
  sh "cp -f lib/patch/picoruby-require/mrbgem.rake #{mrbgem_path}/picoruby-require/"
  sh "cp -f lib/patch/picoruby-yaml/mrbgem.rake #{mrbgem_path}/picoruby-yaml/"
  sh "cp -f lib/patch/picoruby-sandbox/mrbgem.rake #{mrbgem_path}/picoruby-sandbox/"
  # (sandbox.c patch dropped: upstream fixed the uninitialized name in Sandbox.new)

  # picoruby-i2c: add I2C#close method and I2C_release declaration
  sh "cp -f lib/patch/picoruby-i2c/include/i2c.h #{mrbgem_path}/picoruby-i2c/include/"
  sh "cp -f lib/patch/picoruby-i2c/src/mruby/i2c.c #{mrbgem_path}/picoruby-i2c/src/mruby/"

  # picoruby-socket: esp32p4 (ESP-IDF) build support, default socket timeouts,
  # TLS via the ESP-IDF certificate bundle (esp_crt_bundle), and a GC-time
  # release of native TLS sessions (leak fix)
  sh "cp -f lib/patch/picoruby-socket/mrbgem.rake #{mrbgem_path}/picoruby-socket/"
  sh "cp -f lib/patch/picoruby-socket/src/mruby/socket.c #{mrbgem_path}/picoruby-socket/src/mruby/"
  sh "cp -f lib/patch/picoruby-socket/src/mruby/ssl_socket.c #{mrbgem_path}/picoruby-socket/src/mruby/"
  sh "cp -f lib/patch/picoruby-socket/ports/esp32/tcp_socket.c #{mrbgem_path}/picoruby-socket/ports/esp32/"
  sh "cp -f lib/patch/picoruby-socket/ports/esp32/ssl_socket.c #{mrbgem_path}/picoruby-socket/ports/esp32/"
  # (tcp_server.c patch dropped: upstream's vm-threaded ports API fixed the alloc crash)
  sh "cp -f lib/patch/picoruby-socket/ports/posix/tcp_socket.c #{mrbgem_path}/picoruby-socket/ports/posix/"
  sh "cp -f lib/patch/picoruby-socket/ports/posix/ssl_socket.c #{mrbgem_path}/picoruby-socket/ports/posix/"

  # picoruby-mbedtls: skip bundled-library objects on esp32p4 (ESP-IDF provides mbedTLS)
  sh "cp -f lib/patch/picoruby-mbedtls/mrbgem.rake #{mrbgem_path}/picoruby-mbedtls/"

  # picoruby-net-websocket: fix mruby-pack gemdir (upstream points to a
  # non-existent picoruby-pack path inside the mruby tree)
  sh "cp -f lib/patch/picoruby-net-websocket/mrbgem.rake #{mrbgem_path}/picoruby-net-websocket/"

  # picoruby-net-http: accept URI objects in get/get_response/post_form (CRuby style)
  sh "cp -f lib/patch/picoruby-net-http/mrblib/http_client.rb #{mrbgem_path}/picoruby-net-http/mrblib/"

  # (picoruby-json parse_float patch dropped: upstream fixed the decimal handling)

  # picoruby-json: do not take the Regexp fast path on a wasm32 build that has
  # no Regexp. Ours is one, and JSON.parse died on it.
  sh "cp -f lib/patch/picoruby-json/mrblib/json.rb #{mrbgem_path}/picoruby-json/mrblib/"

  # mruby-task (case-D tick split): task.c (bottom-half) and the FreeRTOS
  # ports/posix/task_hal.c (top-half) are delivered by the bulk
  # `cp -rf lib/patch/picoruby-mruby` above. The mrbgem.rake patch is dropped:
  # upstream rewrote it to select ports via conf.ports/effective_ports, so
  # the old HAL-auto-load removal no longer applies (port wiring is done in
  # build_config instead).

  # mruby-dir: the "flash/" prefix dir_hal now lives at
  # mruby-dir/ports/posix/dir_hal.c (hal-posix-dir gem was removed upstream)
  # and is delivered by the bulk `cp -rf lib/patch/picoruby-mruby` above.
  # D5 (skip HAL auto-detection on ESP32) is dropped: upstream removed the
  # auto-detection logic from mruby-dir/mrbgem.rake entirely.
end

namespace :set_target do
  desc "Linux target (dev/test)"
  task :linux => :setup do
    sh "#{DOCKER_CMD} idf.py --preview -DSDKCONFIG_DEFAULTS=\"config/sdkconfig.defaults.linux\" set-target linux"
  end

  desc "Set ESP32 target (esp32s3 Retro / esp32p4 Modern)"
  task :esp32 => :setup do
    sh "#{DOCKER_CMD} idf.py set-target #{ESP_CHIP}"
  end
end
