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
    sh "cmake --build #{WASM_BUILD_DIR} -j#{ENV['JOBS'] || 4}"
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
