# rakelib/ti.rake
# picoruby-ti editor type engine: fetch, database gen, host tests, wasm.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

namespace :ti do
  desc "Fetch the pinned picoruby-ti engine into vendor/picoruby-ti"
  task :setup do
    pin = picoruby_ti_pin
    if Dir.exist?(File.join(PICORUBY_TI_VENDOR_DIR, ".git"))
      head = `git -C #{PICORUBY_TI_VENDOR_DIR} rev-parse HEAD 2>/dev/null`.strip
      unless head == pin["commit"]
        sh "git -C #{PICORUBY_TI_VENDOR_DIR} fetch --depth 100 origin #{pin["commit"]}"
        sh "git -C #{PICORUBY_TI_VENDOR_DIR} checkout --detach #{pin["commit"]}"
      end
    else
      mkdir_p File.dirname(PICORUBY_TI_VENDOR_DIR)
      # No --recursive: picoruby-ti's own lib/prism submodule stays empty, the
      # build uses the prism inside mruby-compiler (see PICORUBY_TI_PIN).
      sh "git clone --branch #{pin["branch"] || "fmrb-dev"} --depth 100 #{pin["repo"]} #{PICORUBY_TI_VENDOR_DIR}"
      sh "git -C #{PICORUBY_TI_VENDOR_DIR} checkout --detach #{pin["commit"]}"
    end
    head = `git -C #{PICORUBY_TI_VENDOR_DIR} rev-parse HEAD`.strip
    puts "picoruby-ti ready: #{PICORUBY_TI_VENDOR_DIR} (#{head[0, 12]})"
  end

  desc "Generate the type database from sig/*.rbs into the copied gem (host, pre-docker)"
  task :gen do
    picoruby_ti_gen_db(picoruby_ti_dir!, "#{PICORUBY_TI_GEM_DIR}/src/generated")
    puts "picoruby-ti database generated in #{PICORUBY_TI_GEM_DIR}/src/generated"
  end

  desc "Generate flash/help from the long half of the sig/ doc comments"
  task :help do
    picoruby_ti_gen_help
  end

  desc "Run the picoruby-ti host regression tests against our sig/ + test fixtures"
  task :test do
    dir = picoruby_ti_dir!
    prism_work = picoruby_ti_prism_work!
    # src/generated is gitignored inside the engine checkout, so generating the
    # database in place leaves the checkout clean. Generate from sig/ plus the
    # test-only overlay so the GPIO fixture the tests need never lands in the
    # firmware database (which ti:gen builds from sig/ alone).
    picoruby_ti_gen_db(dir, File.join(dir, "src/generated"),
                       sig_dir: picoruby_ti_test_sig_dir!)
    sh "make -C #{dir}/host_test PRISM_ROOT=#{prism_work} test"
  end

  desc "Measure one completion request on the host (memory peak and time)"
  task :probe do
    dir = picoruby_ti_dir!
    prism_work = picoruby_ti_prism_work!
    picoruby_ti_gen_db(dir, File.join(dir, "src/generated"))
    out = File.expand_path("vendor/ti_probe", ROOT_DIR)
    srcs = Dir["#{dir}/src/**/*.c"].sort
    incs = picoruby_ti_include_flags(dir)
    sh "#{ENV['CC'] || 'gcc'} -O2 -std=gnu11 #{incs.join(' ')} " \
       "-I#{prism_work}/include tool/ti/ti_probe.c #{srcs.join(' ')} " \
       "#{prism_work}/build/libprism.a -o #{out}"
    sh "#{out} #{ENV['REPS'] || 5} #{ENV['SIZES']}".strip
  end

  desc "Build the WebConsole's browser type engine (emscripten -> tool/web/wasm)"
  task :wasm do
    emcc = ENV["EMCC"] || "emcc"
    unless system("which #{emcc} > /dev/null 2>&1")
      abort "#{emcc} not found. Run `source ~/emsdk/emsdk_env.sh` in this shell first."
    end
    dir = picoruby_ti_dir!
    abort "prism not found at #{PICORUBY_TI_PRISM_SRC_DIR}" unless Dir.exist?(PICORUBY_TI_PRISM_SRC_DIR)

    # The same database the firmware gets: generated from sig/ alone, into the
    # engine checkout (gitignored there, like rake ti:probe does).
    picoruby_ti_gen_db(dir, File.join(dir, "src/generated"))

    srcs = Dir["#{dir}/src/**/*.c"].sort +
           Dir["#{PICORUBY_TI_PRISM_SRC_DIR}/src/*.c"].sort +
           Dir["#{PICORUBY_TI_PRISM_SRC_DIR}/src/util/*.c"].sort
    incs = picoruby_ti_include_flags(dir) + ["-I#{PICORUBY_TI_PRISM_SRC_DIR}/include"]
    out = "#{PICORUBY_TI_WASM_DIR}/ti.js"

    # STACK_SIZE: prism descends the syntax tree recursively, and the default
    # 64KB is what overflowed the editor task on the device (report/p5.md).
    # A browser page can spare a megabyte.
    flags = %w[
      -O2 -std=gnu11 --no-entry
      -sMODULARIZE -sEXPORT_ES6 -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=1048576
      -sENVIRONMENT=web,node
      -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8
    ]
    mkdir_p PICORUBY_TI_WASM_DIR
    sh "#{emcc} #{flags.join(' ')} " \
       "-sEXPORTED_FUNCTIONS=@#{PICORUBY_TI_WASM_DIR}/exports.txt " \
       "#{incs.join(' ')} #{PICORUBY_TI_WASM_DIR}/ti_wasm.c #{srcs.join(' ')} -o #{out}"

    # The long half of the doc comments, as one file the page fetches for F1.
    sh "#{RbConfig.ruby} tool/ti/gen_help.rb --sig-dir #{PICORUBY_TI_SIG_DIR} " \
       "--json #{PICORUBY_TI_WASM_DIR}/help.json"
    puts "picoruby-ti (browser): #{out}"
  end

  desc "Remove the scratch prism build used by ti:test / ti:probe"
  task :clean do
    rm_rf File.expand_path("vendor/ti_prism", ROOT_DIR)
    rm_f File.expand_path("vendor/ti_probe", ROOT_DIR)
    rm_f Dir["#{PICORUBY_TI_WASM_DIR}/{ti.js,ti.wasm,help.json}"]
  end
end
