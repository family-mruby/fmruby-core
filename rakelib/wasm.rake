# rakelib/wasm.rake
# WebAssembly target: the FreeRTOS Emscripten port and its proof of concept.
#
# Independent of idf.py and of build:linux / build:esp32 -- everything it
# touches lives under wasm/, and it configures its own CMake through emcmake.
# Plan and phase breakdown: doc/wasm/.

require "set"

WASM_DIR = File.expand_path("wasm", ROOT_DIR)
WASM_BUILD_DIR = File.join(WASM_DIR, "build")

# Pinned so the numbers in doc/wasm/report/p1.md stay reproducible. Newer
# toolchains are likely fine; when one is adopted, re-run rake wasm:poc five
# times (the port's races show up as flakiness, not as build errors) and update
# these.
WASM_EMSDK_TESTED = "emcc 6.0.6 (ce75e06884093bcefb86a6b8fd56a5d62a4cc245)"
WASM_NODE_TESTED = "v22.23.1"

def wasm_emcmake!
  emcmake = ENV["EMCMAKE"] || "emcmake"
  unless system("which #{emcmake} > /dev/null 2>&1")
    abort "#{emcmake} not found. Run `source ~/emsdk/emsdk_env.sh` in this shell first " \
          "(tested with #{WASM_EMSDK_TESTED})."
  end
  emcmake
end

def wasm_node!
  node = ENV["NODE"] || "node"
  unless system("which #{node} > /dev/null 2>&1")
    abort "#{node} not found (tested with #{WASM_NODE_TESTED})."
  end
  node
end

namespace :wasm do
  desc "Build the wasm FreeRTOS port and its proof of concept (emscripten)"
  task :build do
    emcmake = wasm_emcmake!
    sh "#{emcmake} cmake -S #{WASM_DIR} -B #{WASM_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release"
    sh "cmake --build #{WASM_BUILD_DIR} -j#{ENV['JOBS'] || 4} --target poc freertos_wasm"
  end

  # LovyanGFX for the wasm display: the M5GFX copy (same library the Tab5
  # firmware draws with), fetched shallow and given a wasm platform branch.
  # Only the sprite/font core is compiled -- no bus, no panel, no touch.
  M5GFX_REPO = "https://github.com/m5stack/M5GFX"
  M5GFX_PIN = "d91077b9a607b59404e4e4a49f775c792bfae382" # 2026-08-25
  M5GFX_DIR = File.join(WASM_DIR, "vendor", "m5gfx")

  def wasm_patch_m5gfx!
    # Insert a wasm branch at the head of the two platform-selection chains.
    # The framebuffer platform's common.hpp is already free of Linux
    # dependencies; its .cpp is not compiled (wasm/backend/lgfx_common_wasm.cpp
    # provides millis/delay instead).
    {
      "src/lgfx/v1/platforms/common.hpp" =>
        ["#if defined (ESP_PLATFORM)",
         "#if defined (FMRB_LGFX_WASM)\n\n#include \"framebuffer/common.hpp\"\n\n#elif defined (ESP_PLATFORM)"],
      "src/lgfx/v1/platforms/device.hpp" =>
        ["#if defined (ESP_PLATFORM)",
         "#if defined (FMRB_LGFX_WASM)\n\n// sprite-only build: no bus or panel headers\n\n#elif defined (ESP_PLATFORM)"],
    }.each do |rel, (from, to)|
      path = File.join(M5GFX_DIR, rel)
      src = File.read(path)
      next if src.include?("FMRB_LGFX_WASM")
      src.sub!(from) { to } or abort "wasm:setup: anchor not found in #{rel}"
      File.write(path, src)
    end
  end

  desc "Fetch + patch LovyanGFX (M5GFX #{M5GFX_PIN[0, 8]}) for the wasm display"
  task :setup do
    unless File.directory?(M5GFX_DIR)
      sh "git clone --depth 1 #{M5GFX_REPO} #{M5GFX_DIR}"
      sh "cd #{M5GFX_DIR} && (git checkout #{M5GFX_PIN} 2>/dev/null || " \
         "(git fetch --depth 1 origin #{M5GFX_PIN} && git checkout #{M5GFX_PIN}))"
    end
    wasm_patch_m5gfx!
    puts "wasm:setup done (#{M5GFX_DIR})"
  end

  # "^setup" is the ROOT setup task (a bare :setup here would resolve to
  # wasm:setup, which is the M5GFX fetch). Both are wanted, and the root one
  # for the same reason the docker builds depend on it: the gem sources the
  # wasm build compiles are the COPIES under components/picoruby-esp32/picoruby,
  # and root setup is what refreshes them from lib/add. Without it an edit under
  # lib/add builds green and changes nothing.
  desc "Cross-build libmruby with emcc (host emsdk; run before wasm:core)"
  task :mruby => ["^setup", :setup] do
    wasm_emcmake! # same env check: emcc has to be on PATH
    picoruby = File.expand_path("components/picoruby-esp32/picoruby", ROOT_DIR)
    config = File.expand_path("lib/add/family_mruby_wasm.rb", ROOT_DIR)
    sh "cd #{picoruby} && MRUBY_CONFIG=#{config} rake"
    # libmruby carries both the stock allocator object (allocf.o, only
    # mrb_basic_alloc_func) and the estalloc replacement (alloc.o, which also
    # defines it). GNU ld happens to lazy-load only alloc.o; wasm-ld loads
    # both and refuses the duplicate. Drop the stock object -- estalloc is
    # the allocator on every fmrb target.
    sh "emar d #{picoruby}/build/family-mruby-wasm/lib/libmruby.a allocf.o"
  end

  # The browser build compiles the mruby apps from their PRE-GENERATED
  # bytecode (main/prebuild_scripts/**/mrb/*.c), and only the ESP-IDF / Linux
  # CMake build produces those. Editing an app's Ruby and building for wasm
  # alone therefore linked yesterday's bytecode without a word -- an hour went
  # into a shell command that "did not exist" because of it. This runs the two
  # commands that CMake step runs, for whatever is older than its sources.
  desc "Regenerate stale mruby app/kernel bytecode (the wasm build cannot)"
  task :mrb do
    # Same trap one level down: the gems under lib/add are compiled into
    # libmruby, which wasm:core links but never rebuilds. Adding a method to
    # the app base and building for the browser linked the library from
    # before it, and the desktop died on the first event with a NoMethodError
    # it had no way to report. Cheap check, expensive miss.
    lib_a = File.join(ROOT_DIR, "components/picoruby-esp32/picoruby/build",
                      "family-mruby-wasm/lib/libmruby.a")
    newest_gem = Dir[File.join(ROOT_DIR, "lib/add/**/*.{rb,c,h}")]
                 .map { |f| File.mtime(f) }.max
    if newest_gem && (!File.exist?(lib_a) || File.mtime(lib_a) < newest_gem)
      puts "wasm:mrb: lib/add is newer than libmruby -- rebuilding it first"
      Rake::Task["wasm:mruby"].invoke
    end
    mrbc = File.join(ROOT_DIR, "components/picoruby-esp32/picoruby/bin/mrbc")
    unless File.exist?(mrbc)
      puts "wasm:mrb: no #{mrbc} yet -- skipped (a full build makes it)"
      next
    end
    gen = File.join(ROOT_DIR, "tool/debug/gen_combined_rb.py")
    [File.join(ROOT_DIR, "main/prebuild_scripts/default_app"),
     File.join(ROOT_DIR, "main/prebuild_scripts/kernel")].each do |dir|
      Dir[File.join(dir, "*.rb")].sort.each do |rb|
        # CMake's NAME_WE: the longest extension goes, so shell.app.rb is
        # "shell" and its mixins live in shell/.
        name = File.basename(rb).sub(/\..*\z/, "")
        out_c = File.join(dir, "mrb", "#{name}.c")
        subdir = File.join(dir, name)
        sources = [rb]
        sources += Dir[File.join(subdir, "*.rb")].sort if File.directory?(subdir)
        newest = sources.map { |f| File.mtime(f) }.max
        next if File.exist?(out_c) && File.mtime(out_c) >= newest
        if File.directory?(subdir)
          combined = File.join(dir, "mrb", "#{name}_combined.rb")
          map = File.join(dir, "mrb", "#{name}_combined.map.json")
          sh "python3 #{gen} #{combined} #{map} " \
             "#{(sources - [rb]).join(' ')} #{rb}"
          sh "#{mrbc} -g -B#{name}_irep -o#{out_c} #{combined}"
        else
          sh "#{mrbc} -g -B#{name}_irep -o#{out_c} #{rb}"
        end
        puts "wasm:mrb: #{name} rebuilt"
      end
    end
  end

  desc "Build the core firmware for wasm (doc/wasm/ P4a; needs wasm:mruby once)"
  task :core => ["^setup", :setup, :mrb] do
    emcmake = wasm_emcmake!
    sh "#{emcmake} cmake -S #{WASM_DIR} -B #{WASM_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release"
    sh "cmake --build #{WASM_BUILD_DIR} -j#{ENV['JOBS'] || 4} --target core"
  end

  desc "Stage the distributable flash tree for the browser bundle (tracked files only)"
  task :webflash do
    staging = File.join(WASM_BUILD_DIR, "webflash")
    rm_rf staging
    mkdir_p staging
    # Only what git tracks: local-only material (flash_local/ overlay, the
    # generated wifi.toml, personal files) is structurally excluded from the
    # distributed bundle. The runtime settings come from the wasm config,
    # never from whatever the last target build staged into flash/etc.
    files = `git -C #{ROOT_DIR} ls-files -z flash`.split("\0")
    abort "wasm:webflash: git ls-files returned nothing" if files.empty?
    # Rights: the tunes under usr/share/sounds/nsf are ours to publish and
    # ship, so the player has something to play on the open web. Anything
    # else with that extension does not -- a tune tracked elsewhere would be
    # somebody's music riding along, which is a different act from having a
    # file on a board in one's hand. wasm:scan asserts the same rule.
    files = files.reject { |f| f.end_with?(".nsf") && !f.start_with?("flash/usr/share/sounds/nsf/") }
    # /app/usr is mounted from the browser's own storage, the way /home is, so
    # the bundle must put nothing there -- the placeholder that keeps the
    # directory in git would otherwise be unpacked over the mount.
    files = files.reject { |f| f.start_with?("flash/app/usr/") }
    files.each do |f|
      dest = File.join(staging, f.sub(%r{\Aflash/}, ""))
      mkdir_p File.dirname(dest)
      cp File.join(ROOT_DIR, f), dest
    end
    # The desktop on non-esp32 platforms (wasm included) loads the wallpaper
    # from /data -- on the sim a kernel file-sync puts it there. The web
    # default theme is cyberpunk, so the DEFAULT /data wallpaper is the neon
    # one: the page's JS only has work to do when the user picks classic
    # (a stale cached main.js then cannot leave the default half-applied).
    mkdir_p File.join(staging, "data")
    cp File.join(ROOT_DIR, "flash/usr/share/backgrounds/bg_cyber_426x240.png"),
       File.join(staging, "data/bg_426x240.png")
    mkdir_p File.join(staging, "etc")
    cp File.join(ROOT_DIR, "config/system_conf_wasm.toml"),
       File.join(staging, "etc/system_conf.toml")
    cp File.join(ROOT_DIR, "config/system_conf_wasm.toml"),
       File.join(staging, "etc/system_conf.factory.toml")
    # System service list. flash/etc/services.toml is generated (and on a dev
    # machine carries the tts key), so it is not tracked and never staged --
    # the browser gets its own list from config/, holding only the services
    # that can work without a network.
    cp File.join(ROOT_DIR, "config/services_wasm.toml"),
       File.join(staging, "etc/services.toml")
    # Belt and braces: refuse to ship anything that smells like a credential.
    bad = files.grep(%r{wifi\.toml|secrets})
    abort "wasm:webflash: refusing to stage #{bad.inspect}" unless bad.empty?
    puts "webflash: #{files.length} tracked files + wasm system_conf"
  end

  desc "Build the browser bundle (core_web.js/.wasm/.data + wasm/web page)"
  task :web => [:webflash, :mrb] do
    emcmake = wasm_emcmake!
    sh "#{emcmake} cmake -S #{WASM_DIR} -B #{WASM_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release"
    # The packed .data is produced at link time; the staging just changed, so
    # force the link step to run again.
    rm_f Dir[File.join(WASM_BUILD_DIR, "core_web.{js,wasm,data}")]
    sh "cmake --build #{WASM_BUILD_DIR} -j#{ENV['JOBS'] || 4} --target core_web"
  end

  # ---- distribution -------------------------------------------------------
  #
  # What the browser bundle may contain is decided in wasm:webflash (git-tracked
  # flash/ only). wasm:scan is the check on the RESULT: it reads back what the
  # linker actually packed, so a mistake anywhere between the staging and the
  # .data still gets caught before the directory is handed to anyone.

  # Literal strings that must never appear in the packed filesystem, gathered
  # from this machine rather than guessed: the developer's own key and WiFi
  # credentials are exactly what an accidental staging would leak.
  def wasm_local_secrets
    out = []
    key_file = File.expand_path("~/.openai_key")
    out << File.read(key_file).strip if File.exist?(key_file)
    # Only the keys that actually name a credential. Taking every `k = "v"`
    # pair instead matched the service names ("tts.rb", "pool.ntp.org") that
    # legitimately appear in the shipped files, which is a check that cries
    # wolf and then gets ignored.
    cred = /^\s*(?:password|passwd|psk|ssid|api_key|apikey|key|token|secret)\s*=\s*"([^"]+)"/i
    ["flash/etc/wifi.toml", "config/wifi.toml", "config/wifi_p4.toml",
     "flash/etc/services.toml"].each do |rel|
      path = File.join(ROOT_DIR, rel)
      next unless File.exist?(path)
      File.read(path).scan(cred) { |(v)| out << v }
    end
    out.reject { |v| v.nil? || v.strip.length < 6 }.uniq
  end

  desc "Check the packed browser bundle: no credentials, no untracked files"
  task :scan do
    js   = File.join(WASM_BUILD_DIR, "core_web.js")
    data = File.join(WASM_BUILD_DIR, "core_web.data")
    abort "#{data} not found; run rake wasm:web first" unless File.exist?(data)

    problems = []

    # 1. What the linker packed, read back out of the loader metadata, against
    #    what the staging holds. Anything in one and not the other is a bug in
    #    the packing, not a matter of opinion.
    packed = File.read(js).scan(/filename:"(\/flash\/[^"]*)"/).flatten.sort
    staging = File.join(WASM_BUILD_DIR, "webflash")
    staged = Dir.glob(File.join(staging, "**/*"), File::FNM_DOTMATCH)
                .select { |f| File.file?(f) }
                .map { |f| "/flash" + f.sub(staging, "") }.sort
    (packed - staged).each { |f| problems << "packed but not staged: #{f}" }
    (staged - packed).each { |f| problems << "staged but not packed: #{f}" }

    # 2. Untracked material. The staging copies git ls-files output, so this
    #    can only fire if something else wrote into build/webflash.
    tracked = `git -C #{ROOT_DIR} ls-files -z flash`.split("\0")
                .map { |f| "/" + f }.to_set
    generated = ["/flash/etc/system_conf.toml", "/flash/etc/system_conf.factory.toml",
                 "/flash/etc/services.toml",
                 "/flash/data/bg_426x240.png"].to_set
    packed.each do |f|
      next if tracked.include?(f) || generated.include?(f)
      problems << "packed file is neither tracked nor a known generated one: #{f}"
    end

    # 3. The blob itself, against this machine's real secrets. Byte search, so
    #    it does not matter which file a value would have arrived in.
    blob = File.binread(data)
    wasm_local_secrets.each do |secret|
      problems << "a local credential appears in core_web.data" if blob.include?(secret)
    end
    # And the shapes a key takes even when this machine has none to compare.
    packed.select { |f| f.end_with?(".nsf") && !f.start_with?("/flash/usr/share/sounds/nsf/") }.each do |f|
      problems << "NSF tune from outside usr/share/sounds/nsf in the public bundle (rights): #{f}"
    end
    { "OpenAI-style key" => /sk-[A-Za-z0-9_-]{20}/,
      "PEM private key"  => /BEGIN [A-Z ]*PRIVATE KEY/ }.each do |what, re|
      problems << "#{what} found in core_web.data" if blob =~ re
    end

    unless problems.empty?
      abort "wasm:scan found #{problems.length} problem(s):\n  " + problems.join("\n  ")
    end
    puts "wasm:scan: #{packed.length} packed files, no credentials, no unexpected content"
  end

  # A short content hash over everything the page loads. Every URL carries it,
  # so a browser can never pair a fresh .data with a cached main.js -- the
  # half-old page that bit us once during development.
  def wasm_dist_version(files)
    require "digest"
    d = Digest::SHA256.new
    files.each { |f| d.update(File.binread(f)) }
    d.hexdigest[0, 12]
  end

  desc "Assemble a self-contained, publishable directory in wasm/build/dist"
  task :dist => [:web, :scan] do
    dist = File.join(WASM_BUILD_DIR, "dist")
    web  = File.join(WASM_DIR, "web")
    rm_rf dist
    mkdir_p dist

    payload = %w[core_web.js core_web.wasm core_web.data].map { |f| File.join(WASM_BUILD_DIR, f) }
    page    = [File.join(web, "main.js"), File.join(web, "audio-worklet.js"),
               File.join(ROOT_DIR, "tool/web/remote/keymap.js"),
               File.join(web, "vendor/coi-serviceworker.js")]
    version = wasm_dist_version(payload + page)

    (payload + page).each { |f| cp f, File.join(dist, File.basename(f)) }

    # The page is written for development (module and keymap out of the build
    # and tool trees); here everything sits beside index.html, and every URL
    # gets the version. Anchors are asserted rather than best-effort: a silent
    # miss would publish a page that loads nothing.
    html = File.read(File.join(web, "index.html"))
    {
      %q{src="vendor/coi-serviceworker.js"}     => %Q{src="coi-serviceworker.js?v=#{version}"},
      %q{src="../../tool/web/remote/keymap.js"} => %Q{src="keymap.js?v=#{version}"},
      %q{src="../build/core_web.js"}            => %Q{src="core_web.js?v=#{version}"},
      %q{src="main.js"}                         => %Q{src="main.js?v=#{version}"},
      %q{window.FMRB_WASM_BASE = '../build/';}  => %q{window.FMRB_WASM_BASE = '';},
      %q{window.FMRB_WASM_VER = '';}            => %Q{window.FMRB_WASM_VER = '?v=#{version}';},
    }.each do |from, to|
      abort "wasm:dist: anchor not found in index.html: #{from}" unless html.include?(from)
      html = html.sub(from, to)
    end
    File.write(File.join(dist, "index.html"), html)

    total = Dir.glob(File.join(dist, "*")).sum { |f| File.size(f) }
    puts "wasm:dist: #{dist} (version #{version}, #{(total / 1024.0 / 1024).round(2)} MB)"
    puts "  serve it with any static server, e.g.:"
    puts "    (cd #{dist} && python3 -m http.server 8007)"
    puts "  coi-serviceworker supplies the cross-origin isolation, so no headers are needed."
  end

  desc "Copy the built bundle to a host that serves it (FMRB_WEB_DEST=user@host:/path)"
  task :deploy => :dist do
    dest = ENV["FMRB_WEB_DEST"] or
      abort "set FMRB_WEB_DEST, e.g. FMRB_WEB_DEST=host:/var/www/fmrb-web/ rake wasm:deploy"
    dist = File.join(WASM_BUILD_DIR, "dist")
    # Pre-compressed copies for nginx's gzip_static (5.2MB of wasm goes out as
    # 2.2MB, and nothing is compressed per request). A server without
    # gzip_static ignores them.
    %w[core_web.wasm core_web.data core_web.js main.js].each do |f|
      sh "gzip -9 -k -f #{File.join(dist, f)}"
    end
    sh "rsync -a --delete #{dist}/ #{dest}"
    puts "wasm:deploy: #{dist} -> #{dest}"
    puts "  the host must send COOP/COEP itself, or the page falls back to"
    puts "  coi-serviceworker and a reload on the first visit"
  end

  desc "Serve the repo with COOP/COEP for the wasm page (PORT=n, default 8006; DIST=1 serves build/dist with probe-hold, for headless checks of the real artifacts)"
  task :serve do
    port = (ENV["PORT"] || 8006).to_i
    root = ENV["DIST"] ? File.join(WASM_BUILD_DIR, "dist") : ROOT_DIR
    puts "open http://localhost:#{port}/" + (ENV["DIST"] ? "" : "wasm/web/index.html")
    require "webrick"
    require "json"
    server = WEBrick::HTTPServer.new(Port: port, DocumentRoot: root)
    # The page-driving relay (tools/fmrb_web.rb -> here -> the page's ?drive=1
    # loop -> back). The browser cannot be reached from a shell, but it can
    # come and ask, so the queue lives here: the tool posts a command and
    # waits, the page collects it and posts the answer, the tool is released.
    drive = { m: Mutex.new, cv: ConditionVariable.new, seq: 0,
              pending: [], results: {} }
    # SharedArrayBuffer needs cross-origin isolation; GitHub Pages gets the
    # same effect from coi-serviceworker in P5.
    server.mount_proc("") do |req, res|
      # Probe aid: an "image" that answers after ?ms=N. A page that references
      # it before the load event keeps headless Chrome's --screenshot waiting
      # until the firmware has actually booted (main.js ?autostart&holdload=).
      # The tool's end: enqueue, then wait here for the page to answer.
      if req.path.end_with?("/drive/cmd") && req.request_method == "POST"
        cmd = JSON.parse(req.body)
        id = nil
        drive[:m].synchronize do
          drive[:seq] += 1
          id = drive[:seq]
          drive[:pending] << cmd.merge("id" => id)
          drive[:cv].broadcast
          deadline = Time.now + ((cmd["timeout"] || 20).to_f)
          until drive[:results].key?(id) || Time.now > deadline
            drive[:cv].wait(drive[:m], 0.2)
          end
        end
        answer = drive[:m].synchronize { drive[:results].delete(id) }
        res.status = answer ? 200 : 504
        res["Content-Type"] = "application/json"
        res.body = answer || JSON.dump("error" => "the page did not answer")
        next
      end
      # The page's end: take the next command (204 when there is none), and
      # post results back.
      if req.path.end_with?("/drive/cmd")
        cmd = nil
        drive[:m].synchronize do
          drive[:cv].wait(drive[:m], 1.0) if drive[:pending].empty?
          cmd = drive[:pending].shift
        end
        if cmd
          res.status = 200
          res["Content-Type"] = "application/json"
          res.body = JSON.dump(cmd)
        else
          res.status = 204
          res.body = ""
        end
        next
      end
      if req.path.end_with?("/drive/res")
        body = JSON.parse(req.body)
        drive[:m].synchronize do
          drive[:results][body["id"]] = JSON.dump(body)
          drive[:cv].broadcast
        end
        res.status = 200
        res.body = "ok"
        next
      end
      if req.path.end_with?("/probe-hold")
        sleep((req.query["ms"] || "0").to_i / 1000.0)
        res.status = 200
        res["Content-Type"] = "image/gif"
        res.body = ["47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b"].pack("H*")
        next
      end
      WEBrick::HTTPServlet::FileHandler.new(server, root).service(req, res)
      res["Cross-Origin-Opener-Policy"] = "same-origin"
      res["Cross-Origin-Embedder-Policy"] = "require-corp"
      # Development server: a cached main.js next to a fresh .data made the
      # page half-old once already. Published pages (P5) version their URLs
      # instead.
      res["Cache-Control"] = "no-cache"
    end
    trap("INT") { server.shutdown }
    server.start
  end

  desc "Run the wasm core under node (SECONDS=n to bound the run, default 30)"
  task :run do
    node = wasm_node!
    secs = (ENV["SECONDS"] || 30).to_i
    js = File.join(WASM_BUILD_DIR, "core.js")
    abort "#{js} not found; run rake wasm:core first" unless File.exist?(js)
    # NODERAWFS resolves the flash/ directory relative to the process cwd, so
    # run from the repo root, exactly where the Linux sim runs.
    sh "cd #{ROOT_DIR} && timeout #{secs} #{node} #{js} ; true"
  end

  desc "Build and run the FreeRTOS wasm port proof of concept under node (REPS=n to repeat)"
  task :poc => :build do
    node = wasm_node!
    poc = File.join(WASM_BUILD_DIR, "poc.js")
    reps = (ENV["REPS"] || 1).to_i
    failures = []

    reps.times do |i|
      puts "--- wasm:poc run #{i + 1}/#{reps} ---" if reps > 1
      output = IO.popen([node, poc], err: [:child, :out], &:read)
      ok = $?.success?
      puts output

      # The exit status is the verdict, but read the summary line back too: a
      # run that dies partway through still exits non-zero, and this says how
      # far it got.
      summary = output[/^POC RESULT: (\d+)\/(\d+)$/, 0]
      if summary.nil?
        failures << "run #{i + 1}: no POC RESULT line (the run did not finish)"
      elsif !ok || $1 != $2
        failures << "run #{i + 1}: #{summary}"
      end
    end

    unless failures.empty?
      abort "wasm:poc failed:\n  " + failures.join("\n  ")
    end
    puts "wasm:poc: all checks passed in #{reps} run(s)"
  end

  desc "Remove the wasm build directory"
  task :clean do
    rm_rf WASM_BUILD_DIR
  end
end
