# rakelib/setup.rake
# Source patching (mrbgem copy/replace/patch), icons, and target select.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

# Every copy rake setup makes (into the picoruby submodule, its build
# configs, components/esp_littlefs and flash/) goes through these
# (tools/tree_sync.rb): a file is written only when its content differs, so
# an unchanged one keeps its time and ninja, the host mruby build and the
# checkpoint table do not rebuild what it feeds. With cp -rf every build
# recompiled ~50 C files and re-ran the CMake configure.
require_relative "../tools/tree_sync"

def setup_report(what, n)
  puts "setup: #{what} (#{n} file#{n == 1 ? '' : 's'} updated)" if n > 0
end

# dst becomes exactly src (a directory, or a { "rel" => source } map):
# what rm -rf dst + cp -r src dst did.
def setup_mirror(dst, src, keep: [])
  map = src.is_a?(Hash) ? src : { "" => src }
  setup_report(dst, FmrbTreeSync.mirror(dst, map, keep: keep))
end

# src's files laid over dst (cp -rf src/. dst/), nothing deleted.
def setup_overlay(src, dst)
  setup_report(dst, FmrbTreeSync.overlay(src, dst))
end

# One file; a dst ending in / is the directory it goes into (cp -f).
def setup_copy(src, dst)
  dst = File.join(dst, File.basename(src)) if dst.end_with?("/")
  setup_report(dst, FmrbTreeSync.file(src, dst))
end

desc "Regenerate the icon PNGs from their .icon sources (shared and in the app folders)"
task :icons do
  # The .icon text files are the editable source; the PNGs beside them are what
  # the device actually loads (the display side decodes them, instead of the
  # launcher pushing pixels one GFX command at a time). Re-run after editing an
  # .icon and commit both. Every folder under flash/ that holds an .icon is
  # converted: usr/share/icon and the apps' own (an .app.toml's icon = ...).
  dirs = Dir.glob(File.join(ROOT_DIR, "flash", "**", "*.icon")).map { |f| File.dirname(f) }.uniq.sort
  dirs.each { |d| sh "ruby", File.join(ROOT_DIR, "tools", "gen_icon_png.rb"), d }
end

desc "Regenerate the game BGM .fmsq files from their .mml scores"
task :music do
  # The .mml is the score (the same file format FmrbMidi::MmlPlayer reads);
  # the .fmsq beside it is what a game hands to the audio task, which then
  # loops it without the app's help. Listed rather than globbed so a score
  # meant only for the MML player does not get a sequence it has no use for.
  # Re-run after editing a score and commit both.
  ["korobeiniki"].each do |name|
    sh "ruby tools/mml2fmsq.rb flash/usr/share/music/#{name}.mml"
  end
end

desc "Regenerate the shooter's sprite BMPs from their pixel art source"
task :sprites do
  # Same arrangement as :icons - the art lives in the generator, the BMPs it
  # writes are what the device loads (flash/usr/share/sprites/shooter, sent to
  # the graphics side by the app). Re-run after editing the art and commit the
  # regenerated files.
  sh "ruby tools/gen_shooter_sprites.rb"
end

desc "Build Setup (Patch files)"
task :setup do
  # The boundary glue first: the gem ports, Spinel's combined programs and the
  # Lua / MicroPython components all include what it writes (rakelib/boundary.rake).
  Rake::Task['boundary:gen'].invoke
  # WAMR for components/fmrb_wamr (P4, S3 and Linux; the other targets ignore it).
  Rake::Task['wamr:setup'].invoke
  mrbgem_path = "components/picoruby-esp32/picoruby/mrbgems"
  # ---------- Add ----------
  # const (must be copied first, as it's used by kernel and app)
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-const", "lib/gems/picoruby-fmrb-const"
  # log
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-log", "lib/gems/picoruby-fmrb-log"
  # msgpack (must be copied before app, as app depends on it)
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-msgpack", "lib/gems/picoruby-fmrb-msgpack"
  # kernel
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-kernel", "lib/gems/picoruby-fmrb-kernel"
  # app
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-app", "lib/gems/picoruby-fmrb-app"
  # editor-core (document model in C; depends on syntax-highlight, copied below)
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-editor-core", "lib/gems/picoruby-fmrb-editor-core"
  # ti (type inference engine for the editor). Unlike the gems above this one
  # is not ours: it comes from the pinned fork checkout (rake ti:setup), so the
  # copy source is PICORUBY_TI_DIR instead of lib/gems. Everything the firmware
  # does not need is dropped from the copy: .git, the unused lib/prism
  # submodule, the Go LSP server, the host tests, docs and the generator.
  ti_dir = picoruby_ti_dir!
  # Where the engine's working arena lives on the ESP32 targets. The engine
  # includes this by name (TI_ARENA_INCLUDE, set in the esp32 build configs)
  # and finds it because the gem puts its own src/ on the include path. It
  # rides in the same mirror; src/generated is left to picoruby_ti_gen_db.
  unused = %w[.git lib lsp host_test images example tidbgen]
  ti_map = (Dir.children(ti_dir) - unused).to_h { |e| [e, File.join(ti_dir, e)] }
  ti_map.delete("src")
  (Dir.children(File.join(ti_dir, "src")) - ["generated"]).each do |e|
    ti_map["src/#{e}"] = File.join(ti_dir, "src", e)
  end
  ti_map["src/ti_arena_esp32.h"] = "lib/upstream/ti_arena_esp32.h"
  setup_mirror "#{mrbgem_path}/picoruby-ti", ti_map, keep: ["src/generated"]
  # The type database is generated from OUR sig/*.rbs (the FMRB API), on the
  # host, into the copy only -- the docker build has no ruby for this.
  picoruby_ti_gen_db(ti_dir, "#{PICORUBY_TI_GEM_DIR}/src/generated")
  # The other half of those comments becomes the editor's F1 help, under
  # flash/, so it is in place before the storage image is staged.
  picoruby_ti_gen_help
  # debug (on-device debugger API; depends on msgpack, copied above)
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-debug", "lib/gems/picoruby-fmrb-debug"
  # filesystem
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-filesystem", "lib/gems/picoruby-fmrb-filesystem"
  # io
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-io", "lib/gems/picoruby-fmrb-io"
  # syntax-highlight
  setup_mirror "#{mrbgem_path}/picoruby-syntax-highlight", "lib/gems/picoruby-syntax-highlight"
  # rx8900
  setup_mirror "#{mrbgem_path}/picoruby-rx8900", "lib/gems/picoruby-rx8900"
  # rx8130 (Tab5 RTC)
  setup_mirror "#{mrbgem_path}/picoruby-rx8130", "lib/gems/picoruby-rx8130"
  # bmi270 (Tab5 six-axis IMU)
  setup_mirror "#{mrbgem_path}/picoruby-bmi270", "lib/gems/picoruby-bmi270"
  # picorabbit
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-picorabbit", "lib/gems/picoruby-fmrb-picorabbit"
  # bmp332
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-bmp332", "lib/gems/picoruby-fmrb-bmp332"
  # fft (the four-engine FFT: doc/mic_spectrum). Its mrblib/fft_core.rb is also
  # what the Spinel backend compiles, copied into the Spinel source dir by
  # rake spinel:gen -- one file, both engines.
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-fft", "lib/gems/picoruby-fmrb-fft"
  # spinel-hello (minimal sample of "Spinel as a gem": doc/spinel_aot/
  # adding_a_spinel_gem.md). native/ and spinel/ ride along in the copy but are
  # not compiled from here (main compiles native/, rake spinel:gen the entry).
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-spinel-hello", "lib/gems/picoruby-fmrb-spinel-hello"
  # raycast (the raycaster's inner loop on both engines: doc/raycast_spinel).
  # Same shape as fft: mrblib/raycast_core.rb is what the Spinel backend
  # compiles too, staged by rake spinel:gen -- one file, both engines.
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-raycast", "lib/gems/picoruby-fmrb-raycast"
  # sha256 (Digest::SHA256 in C for the browser build, which has no mbedTLS:
  # doc/asterism_file_transfer T2)
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-sha256", "lib/gems/picoruby-fmrb-sha256"
  # midi (imported from Midori; Ruby protocol layer only, see the gem's
  # FAMILY_MRUBY_PORT.md)
  setup_mirror "#{mrbgem_path}/picoruby-midi", "lib/gems/picoruby-midi"
  # midi-mml (the MML parser only, same import rule; the player is ours)
  setup_mirror "#{mrbgem_path}/picoruby-midi-mml", "lib/gems/picoruby-midi-mml"
  # fmrb-midi (APU transport; depends on midi and app, copied above)
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-midi", "lib/gems/picoruby-fmrb-midi"
  # hal-task-freertos (name-only gem: makes mruby-task drop its own task_hal
  # port so the FreeRTOS case-D port is used via CMake instead; see the gem's
  # mrbgem.rake and doc/work_picoruby_merge/instruct_d7_b1_tick.md sec 3.5)
  setup_mirror "#{mrbgem_path}/hal-task-freertos", "lib/upstream/hal-task-freertos"
  # hal-dir-fmrb (name-only gem: makes mruby-dir drop its flat "flash/" posix
  # port; the native build compiles lib/upstream/hal-esp32-dir/dir_hal.c against
  # the file HAL's resolver instead -- doc/linux_native/)
  setup_mirror "#{mrbgem_path}/hal-dir-fmrb", "lib/upstream/hal-dir-fmrb"
  # picoruby-asterism-zenoh (Asterism::Zenoh, the Zenoh client:
  # doc/ruby_asterism). Not ours to keep here: like picoruby-ti it comes from
  # a pinned checkout (rake asterism:setup, PICORUBY_ASTERISM_ZENOH_DIR
  # overrides), and only what an mruby build reads is copied (no .git, host
  # test, Rakefile or vendor/). The zenoh-pico sources the gem pins (rake
  # zenoh:setup) go under the copy's vendor/, where its mrbgem.rake looks for
  # them. Only the Linux and P4 builds link it (listed in
  # lib/upstream/family_mruby_linux.rb and family_mruby_esp32p4.rb). Like
  # every copy here, a file is written only when its content changed, so the
  # ~150 zenoh-pico objects are not rebuilt on every build. The copy under the
  # gem's former name (picoruby-zenoh) is removed.
  zz_dir = picoruby_asterism_zenoh_dir!
  zp_dir = zenoh_pico_dir!
  rm_rf "#{mrbgem_path}/picoruby-zenoh"
  # LICENSE-APACHE and NOTICE: the gem's zenoh-pico derived files and
  # zenoh-pico itself are used under Apache-2.0 (THIRD_PARTY_LICENSES.md).
  # mrblib/: the gem's Ruby half (0.4.0 on: keywords, deprecations).
  zz_map = {}
  %w[mrbgem.rake include src ports mrblib README.md LICENSE LICENSE-APACHE NOTICE].each do |entry|
    next if entry == "mrblib" && !File.directory?("#{zz_dir}/mrblib")
    zz_map[entry] = "#{zz_dir}/#{entry}"
  end
  %w[src include LICENSE NOTICE.md version.txt].each do |entry|
    zz_map["vendor/zenoh-pico/#{entry}"] = "#{zp_dir}/#{entry}"
  end
  setup_mirror "#{mrbgem_path}/picoruby-asterism-zenoh", zz_map
  # picoruby-asterism (Asterism, the object layer and the ROS 2 node over
  # Asterism::Zenoh; pure Ruby). From the pinned asterism checkout (rake
  # asterism:setup, ASTERISM_DIR overrides): the mrbgem is its mrbgem.rake
  # and mrblib/. Linked by the same builds as picoruby-asterism-zenoh.
  ast_dir = asterism_dir!
  setup_mirror "#{mrbgem_path}/picoruby-asterism",
               %w[mrbgem.rake mrblib README.md LICENSE].to_h { |e| [e, "#{ast_dir}/#{e}"] }
  # Family mruby's default Asterism node profile (V2.5): pure Ruby, listed
  # after picoruby-asterism by the builds that have it.
  setup_mirror "#{mrbgem_path}/picoruby-fmrb-asterism", "lib/gems/picoruby-fmrb-asterism"
  # The bundled ROS 2 message types, from the same checkout into the storage
  # tree (/usr/share/asterism/msgs on the device; the sim reads flash/
  # directly). Generated here and gitignored, like flash/help: the source is
  # the asterism repository. NOTICE and the Apache-2.0 text travel with them.
  setup_mirror ASTERISM_MSGS_DIR, "#{ast_dir}/data/msgs"
  # Asterism::Transfer, loaded at run time by the transfer app
  # (/usr/share/asterism/transfer/transfer.rb), from the same checkout.
  setup_mirror ASTERISM_TRANSFER_DIR, "#{ast_dir}/data/transfer"
  # mruby-checkpoint (doc/checkpoint): from the pinned checkout (rake
  # checkpoint:setup, MRUBY_CHECKPOINT_DIR overrides), only what an mruby
  # build reads -- no tools/ (the repository's own driver, whose absence also
  # keeps the gem from declaring an executable), tests or vendored mruby.
  # Linked by the Linux and native builds only (their build configs list it).
  ck_dir = mruby_checkpoint_dir!
  ck_map = %w[mrbgem.rake include src].to_h { |e| [e, "#{ck_dir}/mrbgems/mruby-checkpoint/#{e}"] }
  %w[README.md LICENSE].each { |e| ck_map[e] = "#{ck_dir}/#{e}" }
  setup_mirror "#{mrbgem_path}/mruby-checkpoint", ck_map
  # conf
  setup_copy "lib/upstream/family_mruby.gembox", "#{mrbgem_path}/"
  setup_copy "lib/upstream/family_mruby_linux.rb", "components/picoruby-esp32/picoruby/build_config/"
  setup_copy "lib/upstream/family_mruby_esp32.rb", "components/picoruby-esp32/picoruby/build_config/"
  setup_copy "lib/upstream/family_mruby_wasm.rb", "components/picoruby-esp32/picoruby/build_config/"
  # picoruby's build helper: src/version.c written only when it changes
  # (upstream writes it on every rake run, which put the presym scan, mrbc and
  # every gem_init.c of every libmruby out of date each time). Whole-file
  # copy: refresh it when the picoruby submodule moves.
  setup_copy "lib/upstream/patch/picoruby-lib/build.rb", "components/picoruby-esp32/picoruby/lib/picoruby/build.rb"

  # ---------- Replace ----------
  # Machine
  setup_mirror "#{mrbgem_path}/picoruby-machine", "lib/upstream/replace/picoruby-machine"

  # ---------- Patch ----------
  # picoruby-mruby: alloc.c (estalloc multi-VM), hal.h, mrbgem.rake, and
  # lib/mruby/src/vm.c (the self-supplied timeslice hook, four additive hunks
  # marked "fmrb: self-supplied timeslice" -- see doc/wasm/report/p2.md; it
  # compiles to nothing unless MRB_TASK_TICK_SELF_SUPPLY is defined). vm.c,
  # include/mruby.h and mruby-task's task.h, task.c and task_queue.c also carry
  # mruby-checkpoint's safe point hook and switch-request count, and gc.c its
  # GC root hook (only with MRB_VM_SAFEPOINT_HOOK, doc/checkpoint Ff), marked
  # "fmrb: checkpoint" (doc/checkpoint/plan.md). src/string.c carries one
  # backported line, marked "fmrb: backport" (mrb_utf8_strlen stops at the
  # end of a shared string; doc/ai_coding/report/a3e.md). These are whole-file copies,
  # so they have to be refreshed when the mruby submodule moves.
  setup_overlay "lib/upstream/patch/picoruby-mruby", "#{mrbgem_path}/picoruby-mruby"

  # mruby-io file_constants.rb
  setup_copy "lib/upstream/patch/picoruby-mruby/mrbgems/mruby-io/mrblib/file_constants.rb", "#{mrbgem_path}/picoruby-mruby/lib/mruby/mrbgems/mruby-io/mrblib/"

  # littleFS
  setup_copy "lib/upstream/patch/esp_littlefs/CMakeLists.txt", "components/esp_littlefs/"

  # picoruby-env
  setup_copy "lib/upstream/patch/picoruby-env/ports/posix/env.c", "#{mrbgem_path}/picoruby-env/ports/posix/"

  # mruby-compiler (renamed upstream from mruby-compiler2; same repo).
  # prism allocator patches dropped: upstream routes prism through the VM
  # estalloc heap via global_mrb (owner decision "Option A"). Only the
  # compile.c NULL-guard remains.
  setup_copy "lib/upstream/patch/compiler/mruby-compiler2-compile.c", "#{mrbgem_path}/mruby-compiler/src/compile.c"
  # prism's allocator: per calling task instead of one process-wide mrb_state
  # (doc/editor_ti/report/p2.md). The two files go together -- the header names
  # the hooks, ccontext.c implements them.
  setup_copy "lib/upstream/patch/compiler/prism_xallocator.h", "#{mrbgem_path}/mruby-compiler/include/prism_xallocator.h"
  setup_copy "lib/upstream/patch/compiler/mruby-compiler2-ccontext.c", "#{mrbgem_path}/mruby-compiler/src/ccontext.c"
  # cdump.c: mrbc -B emits the child-irep tables (`*_reps_N`) as
  # `const mrb_irep *const`, so every gem's mrblib lands in flash instead of
  # internal RAM (doc/iram_reduction/report/r1.md). Whole-file copy: refresh it
  # when the mruby-compiler submodule moves.
  setup_copy "lib/upstream/patch/compiler/mruby-compiler-cdump.c", "#{mrbgem_path}/mruby-compiler/src/cdump.c"
  # codegen.c: case/in fixes backported from mruby/mruby 0e6bac0e5a7e (a hash
  # pattern's value no longer overwrites the value it is compared with) and
  # 680084ac1275 (a capture inside a block binds a local of the enclosing
  # scope). The copy is the vendored 10408c3 plus those two commits only
  # (doc/ruby_asterism/upstream/picoruby_case_in.patch, report/c8.md).
  # Whole-file copy: drop it when the mruby-compiler submodule moves to
  # picoruby/mruby-compiler2 a2c72afb or later, which has both.
  setup_copy "lib/upstream/patch/compiler/mruby-compiler-codegen.c", "#{mrbgem_path}/mruby-compiler/src/codegen.c"

  # mrbgem.rake patches
  setup_copy "lib/upstream/patch/picoruby-require/mrbgem.rake", "#{mrbgem_path}/picoruby-require/"
  # The list of prebuilt gems as `const char *const` (the template and its
  # reader): read-only data, in flash on ESP32, instead of a writable array of
  # pointers in internal RAM that grew by 4 bytes with every gem (doc/checkpoint
  # F6). Whole-file copies: refresh them when the picoruby submodule moves.
  setup_copy "lib/upstream/patch/picoruby-require/templates/mruby/picogem_init.c.erb", "#{mrbgem_path}/picoruby-require/templates/mruby/"
  setup_copy "lib/upstream/patch/picoruby-require/src/mruby/require.c", "#{mrbgem_path}/picoruby-require/src/mruby/"
  setup_copy "lib/upstream/patch/picoruby-yaml/mrbgem.rake", "#{mrbgem_path}/picoruby-yaml/"
  setup_copy "lib/upstream/patch/picoruby-sandbox/mrbgem.rake", "#{mrbgem_path}/picoruby-sandbox/"
  # (sandbox.c patch dropped: upstream fixed the uninitialized name in Sandbox.new)

  # picoruby-i2c: add I2C#close method and I2C_release declaration
  setup_copy "lib/upstream/patch/picoruby-i2c/include/i2c.h", "#{mrbgem_path}/picoruby-i2c/include/"
  setup_copy "lib/upstream/patch/picoruby-i2c/src/mruby/i2c.c", "#{mrbgem_path}/picoruby-i2c/src/mruby/"

  # picoruby-socket: esp32p4 (ESP-IDF) build support, default socket timeouts,
  # TLS via the ESP-IDF certificate bundle (esp_crt_bundle), and a GC-time
  # release of native TLS sessions (leak fix)
  setup_copy "lib/upstream/patch/picoruby-socket/mrbgem.rake", "#{mrbgem_path}/picoruby-socket/"
  setup_copy "lib/upstream/patch/picoruby-socket/src/mruby/socket.c", "#{mrbgem_path}/picoruby-socket/src/mruby/"
  setup_copy "lib/upstream/patch/picoruby-socket/src/mruby/ssl_socket.c", "#{mrbgem_path}/picoruby-socket/src/mruby/"
  setup_copy "lib/upstream/patch/picoruby-socket/ports/esp32/tcp_socket.c", "#{mrbgem_path}/picoruby-socket/ports/esp32/"
  setup_copy "lib/upstream/patch/picoruby-socket/ports/esp32/ssl_socket.c", "#{mrbgem_path}/picoruby-socket/ports/esp32/"
  # (tcp_server.c patch dropped: upstream's vm-threaded ports API fixed the alloc crash)
  setup_copy "lib/upstream/patch/picoruby-socket/ports/posix/tcp_socket.c", "#{mrbgem_path}/picoruby-socket/ports/posix/"
  setup_copy "lib/upstream/patch/picoruby-socket/ports/posix/ssl_socket.c", "#{mrbgem_path}/picoruby-socket/ports/posix/"

  # picoruby-mbedtls: skip bundled-library objects on esp32p4 (ESP-IDF provides mbedTLS)
  setup_copy "lib/upstream/patch/picoruby-mbedtls/mrbgem.rake", "#{mrbgem_path}/picoruby-mbedtls/"

  # picoruby-net-websocket: fix mruby-pack gemdir (upstream points to a
  # non-existent picoruby-pack path inside the mruby tree)
  setup_copy "lib/upstream/patch/picoruby-net-websocket/mrbgem.rake", "#{mrbgem_path}/picoruby-net-websocket/"
  # client.rb: the handshake accepts any "HTTP/1.1 101" status line (not only
  # "Switching Protocols"), Host carries a non-default port, and #receive
  # re-polls every 30 ms instead of 100 ms.
  setup_copy "lib/upstream/patch/picoruby-net-websocket/mrblib/client.rb", "#{mrbgem_path}/picoruby-net-websocket/mrblib/"

  # picoruby-net-http: accept URI objects in get/get_response/post_form (CRuby
  # style); read headers and chunks with readpartial so a server that keeps
  # the connection open does not stall the response
  setup_copy "lib/upstream/patch/picoruby-net-http/mrblib/http_client.rb", "#{mrbgem_path}/picoruby-net-http/mrblib/"

  # (picoruby-json parse_float patch dropped: upstream fixed the decimal handling)

  # picoruby-json: do not take the Regexp fast path on a wasm32 build that has
  # no Regexp. Ours is one, and JSON.parse died on it.
  setup_copy "lib/upstream/patch/picoruby-json/mrblib/json.rb", "#{mrbgem_path}/picoruby-json/mrblib/"

  # mruby-task (case-D tick split): task.c (bottom-half) and the FreeRTOS
  # ports/posix/task_hal.c (top-half) are delivered by the overlay of
  # lib/upstream/patch/picoruby-mruby above. The mrbgem.rake patch is dropped:
  # upstream rewrote it to select ports via conf.ports/effective_ports, so
  # the old HAL-auto-load removal no longer applies (port wiring is done in
  # build_config instead).

  # mruby-dir: the "flash/" prefix dir_hal now lives at
  # mruby-dir/ports/posix/dir_hal.c (hal-posix-dir gem was removed upstream)
  # and is delivered by the overlay of lib/upstream/patch/picoruby-mruby above.
  # D5 (skip HAL auto-detection on ESP32) is dropped: upstream removed the
  # auto-detection logic from mruby-dir/mrbgem.rake entirely.
end

namespace :set_target do
  desc "Linux target (dev/test)"
  task :linux => :setup do
    # In its own build directory with its own sdkconfig (LINUX_IDF_ARGS,
    # rakelib/build.rake), so the ESP32 build in build/ and the top-level
    # sdkconfig are left alone.
    sh "#{DOCKER_CMD} idf.py --preview #{LINUX_IDF_ARGS} set-target linux"
  end

  desc "Set ESP32 target (esp32s3 Retro / esp32p4 Modern)"
  task :esp32 => :setup do
    sh "#{DOCKER_CMD} idf.py set-target #{ESP_CHIP}"
  end
end
