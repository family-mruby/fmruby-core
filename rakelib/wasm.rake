# rakelib/wasm.rake
# WebAssembly target: the FreeRTOS Emscripten port and its proof of concept.
#
# Independent of idf.py and of build:linux / build:esp32 -- everything it
# touches lives under wasm/, and it configures its own CMake through emcmake.
# Plan and phase breakdown: doc/wasm/.

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

  desc "Cross-build libmruby with emcc (host emsdk; run before wasm:core)"
  task :mruby do
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

  desc "Build the core firmware for wasm (doc/wasm/ P4a; needs wasm:mruby once)"
  task :core do
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
    files.each do |f|
      dest = File.join(staging, f.sub(%r{\Aflash/}, ""))
      mkdir_p File.dirname(dest)
      cp File.join(ROOT_DIR, f), dest
    end
    mkdir_p File.join(staging, "etc")
    cp File.join(ROOT_DIR, "config/system_conf_wasm.toml"),
       File.join(staging, "etc/system_conf.toml")
    cp File.join(ROOT_DIR, "config/system_conf_wasm.toml"),
       File.join(staging, "etc/system_conf.factory.toml")
    # Belt and braces: refuse to ship anything that smells like a credential.
    bad = files.grep(%r{wifi\.toml|secrets})
    abort "wasm:webflash: refusing to stage #{bad.inspect}" unless bad.empty?
    puts "webflash: #{files.length} tracked files + wasm system_conf"
  end

  desc "Build the browser bundle (core_web.js/.wasm/.data + wasm/web page)"
  task :web => :webflash do
    emcmake = wasm_emcmake!
    sh "#{emcmake} cmake -S #{WASM_DIR} -B #{WASM_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release"
    # The packed .data is produced at link time; the staging just changed, so
    # force the link step to run again.
    rm_f Dir[File.join(WASM_BUILD_DIR, "core_web.{js,wasm,data}")]
    sh "cmake --build #{WASM_BUILD_DIR} -j#{ENV['JOBS'] || 4} --target core_web"
  end

  desc "Serve the repo with COOP/COEP for the wasm page (PORT=n, default 8006)"
  task :serve do
    port = (ENV["PORT"] || 8006).to_i
    puts "open http://localhost:#{port}/wasm/web/index.html"
    require "webrick"
    server = WEBrick::HTTPServer.new(Port: port, DocumentRoot: ROOT_DIR)
    # SharedArrayBuffer needs cross-origin isolation; GitHub Pages gets the
    # same effect from coi-serviceworker in P5.
    server.mount_proc("") do |req, res|
      WEBrick::HTTPServlet::FileHandler.new(server, ROOT_DIR).service(req, res)
      res["Cross-Origin-Opener-Policy"] = "same-origin"
      res["Cross-Origin-Embedder-Policy"] = "require-corp"
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
