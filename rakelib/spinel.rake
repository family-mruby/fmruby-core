# rakelib/spinel.rake
# Spinel AOT compiler: fetch/build, host codegen, and the doctor lint.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

# The Spinel compiler binary, fetching and building the pinned one on a fresh
# clone (no usable compiler anywhere).
def spinel_compiler_dir
  dir = SPINEL_DIR
  unless File.executable?(File.join(dir, "bin/spinel"))
    Rake::Task['spinel:setup'].invoke
    dir = SPINEL_VENDOR_DIR
  end
  bin = File.join(dir, "bin/spinel")
  abort "spinel not found at #{bin}. Run `rake spinel:setup` or set SPINEL_DIR." unless File.executable?(bin)
  dir
end

# The commands that generate the Spinel C, one group per program:
# [[name, [command, ...], [input, ...]], ...]. The last command is the
# compiler's; the ones before it make its input. The inputs are the files the
# compiler reads, for spinel_gen!'s up-to-date check. The gems' sources are
# staged into stage_dir here. spinel_gen! runs them for a build (gen_dir =
# spinel_gen_dir(target), stage_dir = SPINEL_SRC_DIR); rake regress:spinel
# runs them for the all-Spinel configuration into a directory of its own, so
# the builds' generated files and their timestamps stay as they are.
def spinel_gen_programs(bin, gen_dir:, stage_dir:, kernel:, editor:, desktop:, fft:, platform:)
  # Flags every generated program gets. --no-inline-hot: the compiler forces
  # small leaf methods inline by default, and a large dispatcher that calls
  # many of them absorbs them all into one C frame. The kernel's
  # handle_app_control grew from 4,192 to 12,448 bytes that way and
  # overflowed the kernel task's 16 KB stack at boot (Stack protection fault
  # on the P4, doc/spinel_upstream_ext/report/p2b2.md). The sim does not
  # notice: its pthread stacks are far larger than the device's task stacks.
  # Must match main/ruby/compile_ruby_to_spinel.cmake.
  gen_flags = "--no-inline-hot"
  # Every program is compiled as a Spinel ext program (`--ext-init`): the C
  # gets `void <init>(void)`, which runs the program's top level, plus a
  # contract header next to it. Built SP_MULTI_CTX, several of them share the
  # image, each instance reaching its own program (doc/spinel_upstream_ext/).
  #
  # The VMs (kernel / editor / desktop) are ext programs with no entries: the
  # task makes an instance and calls the init once, and the Ruby top level
  # runs the VM's main loop inside it. Their Ruby is concatenated into one
  # combined program first (the compiler needs a single translation unit;
  # require_relative is stripped). Host-generated into gen/ (gitignored).
  # Must match main/CMakeLists.txt's generate_ruby_spinel_command calls.
  programs = []
  vm = lambda do |name, combine, init|
    rb = "#{gen_dir}/#{name}.rb"
    programs << [name, ["#{RbConfig.ruby} #{combine} #{rb} #{platform}",
                        "#{bin} #{gen_flags} -I #{SPINEL_SRC_DIR} -c #{rb} --ext-init #{init} -o #{gen_dir}/#{name}.c"],
                 [rb]]
  end
  vm.call("fmrb_kernel_combined", "tools/spinel/gen_kernel_combined.rb", "Init_fmrb_kernel") if kernel
  vm.call("editor_combined", "tools/spinel/gen_app_combined.rb editor", "Init_editor") if editor
  vm.call("system_desktop_combined", "tools/spinel/gen_app_combined.rb system_desktop", "Init_system_desktop") if desktop
  # The gems (FFT, SpinelHello, Raycast): libraries an mruby task calls. Each
  # gem's spinel/<name>_kernel.rb names its init and its entries in two
  # comment lines (`# spinel-ext-init:` / `# spinel-ext-entry:`), read here
  # and by main/CMakeLists.txt, so the names live in one place. The entries
  # become typed C functions in the generated header, which the gem's
  # native/ receiver includes. The kernel and the core it requires are staged
  # into stage_dir so require_relative resolves in one dir (the staged
  # copies are gitignored; the gem holds the originals, so the :ruby and
  # :spinel backends can never run different code).
  gem = lambda do |kernel_rb, *cores|
    name = File.basename(kernel_rb, ".rb")
    [kernel_rb, *cores].each do |src|
      abort "#{src} is missing" unless File.exist?(src)
      cp src, "#{stage_dir}/#{File.basename(src)}", verbose: false
    end
    spec = File.read(kernel_rb)
    init = spec[/^# spinel-ext-init: *(\S+)/, 1] or abort "#{kernel_rb}: no `# spinel-ext-init:` line"
    entries = spec[/^# spinel-ext-entry: *(\S+)/, 1] or abort "#{kernel_rb}: no `# spinel-ext-entry:` line"
    programs << [name, ["#{bin} #{gen_flags} -I #{stage_dir} -c #{stage_dir}/#{name}.rb " \
                        "--ext-init #{init} --ext-entry #{entries} -o #{gen_dir}/#{name}.c"],
                 [kernel_rb, *cores].map { |src| "#{stage_dir}/#{File.basename(src)}" }]
  end
  # FFT: not built with FMRB_FFT_SPINEL=0 (doc/mic_spectrum).
  if fft
    gem.call("lib/gems/picoruby-fmrb-fft/spinel/fft_kernel.rb",
             "lib/gems/picoruby-fmrb-fft/mrblib/fft_core.rb",
             "lib/gems/picoruby-fmrb-fft/mrblib/fft_core_q15.rb")
  end
  # SpinelHello: the minimal sample gem. Always built (no flag).
  gem.call("lib/gems/picoruby-fmrb-spinel-hello/spinel/spinel_hello_kernel.rb",
           "lib/gems/picoruby-fmrb-spinel-hello/mrblib/spinel_hello_core.rb")
  # Raycast: the raycaster's ray loop as a gem (doc/raycast_spinel). Always
  # built. Its core is shared with the :ruby backend.
  gem.call("lib/gems/picoruby-fmrb-raycast/spinel/raycast_kernel.rb",
           "lib/gems/picoruby-fmrb-raycast/mrblib/raycast_core.rb")
  programs
end

# What spinel_gen! compares to decide a program is up to date: the compiler
# (its commit, else the binary), the compile command and the inputs. The
# combined programs carry FmrbConst::BUILD_DATE, the time the generator ran,
# which is left out; otherwise every build would compile the programs again.
def spinel_gen_key(compiler_dir, bin, cmd, inputs)
  require "digest"
  head = `git -C #{compiler_dir} rev-parse HEAD 2>/dev/null`.strip
  head = Digest::SHA256.file(bin).hexdigest if head.empty?
  parts = [head, cmd]
  inputs.each do |f|
    parts << f << File.read(f).gsub(/^ *BUILD_DATE = .*$/, "")
  end
  Digest::SHA256.hexdigest(parts.join("\0"))
end

# Generate the Spinel C for one build (target: a key of SPINEL_GEN_TARGETS)
# into spinel_gen_dir(target). Each build has its own directory, so the ESP32
# firmware, the sim and the hosted builds never compile one another's
# programs, and switching between them generates nothing. A program whose
# compiler, command and inputs are unchanged (<name>.key beside it) is not
# compiled again, so its .c keeps its timestamp and the build does not
# recompile it. Once per target in a rake run.
def spinel_gen!(target)
  @spinel_gen_done ||= {}
  return if @spinel_gen_done[target]
  platform = SPINEL_GEN_TARGETS.fetch(target) do
    abort "spinel:gen: unknown target #{target.inspect} (#{SPINEL_GEN_TARGETS.keys.join(', ')})"
  end
  # the app programs splice the generated FmrbBoundary declarations
  Rake::Task['boundary:gen'].invoke
  dir = spinel_compiler_dir
  bin = File.join(dir, "bin/spinel")
  # Guard against compiler/runtime-snapshot divergence: the generated C and
  # components/fmrb_spinel_rt/spinel_rt must come from the same fork commit.
  import_info = File.expand_path("components/fmrb_spinel_rt/spinel_rt/IMPORT_INFO", ROOT_DIR)
  if File.exist?(import_info)
    snap = File.read(import_info)[/^fork_commit: (\h+)/, 1]
    head = `git -C #{dir} rev-parse HEAD 2>/dev/null`.strip
    if snap && !head.empty? && snap != head
      warn "WARNING: spinel compiler at #{head[0, 12]} but runtime snapshot is #{snap[0, 12]};" \
           " re-snapshot it with" \
           " `ruby components/fmrb_spinel_rt/import_from_fork.rb #{dir}`" \
           " (and bump SPINEL_PIN to match), or align the checkouts."
    end
  end
  # The programs of the layout before the per-build directories (gen/*.c and
  # friends directly in gen/): nothing reads them any more.
  rm_f Dir.glob("#{SPINEL_GEN_ROOT}/*").select { |f| File.file?(f) }, verbose: false
  gen_dir = spinel_gen_dir(target)
  mkdir_p gen_dir, verbose: false
  # The platform baked into the programs (FmrbConst::PLATFORM, which gates
  # ESP32-only code such as the RTC) follows from the target, so a directory
  # never holds another platform's programs.
  programs = spinel_gen_programs(bin, gen_dir: gen_dir, stage_dir: SPINEL_SRC_DIR,
                                 kernel: FMRB_KERNEL_ENGINE == "spinel",
                                 editor: FMRB_APP_ENGINE_EDITOR == "spinel",
                                 desktop: FMRB_APP_ENGINE_DESKTOP == "spinel",
                                 fft: FMRB_FFT_SPINEL, platform: platform)
  programs.each do |name, cmds, inputs|
    cmds[0..-2].each { |c| sh c }
    key_file = "#{gen_dir}/#{name}.key"
    key = spinel_gen_key(dir, bin, cmds[-1], inputs)
    outputs = %w[c h].map { |ext| "#{gen_dir}/#{name}.#{ext}" }
    if outputs.all? { |f| File.exist?(f) } && File.exist?(key_file) && File.read(key_file) == key
      puts "Spinel #{gen_dir}/#{name}.c is up to date"
      next
    end
    rm_f key_file, verbose: false
    sh cmds[-1]
    File.write(key_file, key)
    puts "Spinel generated #{gen_dir}/#{name}.c"
  end
  @spinel_gen_done[target] = true
end

namespace :spinel do
  desc "Fetch + build the pinned Spinel compiler into vendor/spinel"
  task :setup do
    pin = spinel_pin
    if Dir.exist?(File.join(SPINEL_VENDOR_DIR, ".git"))
      head = `git -C #{SPINEL_VENDOR_DIR} rev-parse HEAD 2>/dev/null`.strip
      unless head == pin["commit"]
        sh "git -C #{SPINEL_VENDOR_DIR} fetch --depth 100 origin #{pin["commit"]}"
        sh "git -C #{SPINEL_VENDOR_DIR} checkout --detach #{pin["commit"]}"
      end
    else
      mkdir_p File.dirname(SPINEL_VENDOR_DIR)
      # Clone then detach at the pin (shallow; deepen if the pin is older
      # than the branch tip).
      sh "git clone --branch #{pin["branch"] || "fmrb-dev"} --depth 100 #{pin["repo"]} #{SPINEL_VENDOR_DIR}"
      sh "git -C #{SPINEL_VENDOR_DIR} checkout --detach #{pin["commit"]}"
    end
    bin = File.join(SPINEL_VENDOR_DIR, "bin/spinel")
    head = `git -C #{SPINEL_VENDOR_DIR} rev-parse HEAD`.strip
    stamp = File.join(SPINEL_VENDOR_DIR, ".built_commit")
    unless File.executable?(bin) && File.exist?(stamp) && File.read(stamp).strip == head
      sh "cd #{SPINEL_VENDOR_DIR} && make deps && make"
      File.write(stamp, head)
    end
    puts "Spinel compiler ready: #{bin} (#{head[0, 12]})"
  end

  # SPINEL_GEN_TARGET picks the build: esp32, linux or hosted (the default,
  # for the native program and the browser build). rake build:esp32,
  # build:linux, native:* and wasm:core/web run it themselves.
  desc "Generate Spinel C on the host into main/ruby/spinel/gen/<SPINEL_GEN_TARGET> (esp32|linux|hosted, default hosted)"
  task :gen do
    spinel_gen!(ENV['SPINEL_GEN_TARGET'].to_s.empty? ? "hosted" : ENV['SPINEL_GEN_TARGET'])
  end

  desc "Lint Spinel-targeted Ruby with spinel-doctor (source-level: unsupported/unresolved/inference)"
  task :doctor do
    dir = SPINEL_DIR
    unless File.executable?(File.join(dir, "bin/spinel"))
      Rake::Task['spinel:setup'].invoke
      dir = SPINEL_VENDOR_DIR
    end
    doctor = File.join(dir, "bin/spinel-doctor")
    abort "spinel-doctor not found at #{doctor}. Run `rake spinel:setup`." unless File.executable?(doctor)
    # A directory of its own, so the lint's Linux programs never stand in for
    # a build's.
    lint_dir = "#{SPINEL_GEN_ROOT}/doctor"
    mkdir_p lint_dir
    # Lint kernel + desktop (both are Spinel-convertible programs). We generate
    # the require-inlined combined Ruby, then run the SOURCE-LEVEL legs only.
    # --skip build,behavior: those legs link/run the combined standalone, which
    # flags the fmruby C shims (fmrb_spx_*) as undefined and reports a CRuby-diff
    # -- both false positives here (the real firmware build links the shims).
    # The gate keys on unsupported/unresolved (error severity); inference notes
    # ("widened to untyped") are informational poly-widen hints.
    targets = [
      ["fmrb_kernel",    "tools/spinel/gen_kernel_combined.rb", nil],
      ["system_desktop", "tools/spinel/gen_app_combined.rb", "system_desktop"],
      ["editor",         "tools/spinel/gen_app_combined.rb", "editor"],
    ]
    # Known-accepted findings: ESP32-only RTC hardware code that is dead on Linux
    # (PLATFORM guard) but still statically analyzed. Its driver classes
    # (RX8900 / RX8130) are not in the Linux Spinel program, so `write_time`
    # can't resolve here. INTERIM allowlist: Phase 5 routes RTC writes through the
    # set_wallclock FFI (decision (b), phase5.md T5-4) and deletes the direct
    # driver instantiation from clock_setting.rb -- then REMOVE this allowlist.
    # `rtc.init` on the same driver object is the same case.
    allow = [
      /unresolved call 'write_time' on .* receiver/,
      /unresolved call 'init' on poly receiver -> NoMethodError \(matching CRuby\)/,
    ]
    failed = []
    # The gems' ext kernels link standalone (no FFI), so they get every leg,
    # build and behavior included. One finding is expected and allowlisted:
    # the behavior leg always reports "compiled output differs from CRuby"
    # for an ext kernel, because the `if __FILE__ == $0` type-inference driver
    # runs under CRuby and not in the compiled program, where $0 is the
    # executable (upstream spinel-doctor, candidate U-14). Any other [ERR]
    # leg fails the gate.
    gems = [
      ["picoruby-fmrb-fft", "fft_kernel", %w[fft_core fft_core_q15]],
      ["picoruby-fmrb-spinel-hello", "spinel_hello_kernel", %w[spinel_hello_core]],
      ["picoruby-fmrb-raycast", "raycast_kernel", %w[raycast_core]],
    ]
    gem_allow_legs = %w[behavior]
    gems.each do |gem, kernel, cores|
      [["spinel", kernel], *cores.map { |c| ["mrblib", c] }].each do |sub, f|
        cp "lib/gems/#{gem}/#{sub}/#{f}.rb", "#{lint_dir}/#{f}.rb"
      end
      puts "== spinel-doctor: #{kernel} =="
      out = `cd #{lint_dir} && SPINEL_DIR=#{dir} #{doctor} #{kernel}.rb 2>&1`
      puts out
      legs = out.scan(/^\[ERR\]\s+(\S+)/).flatten
      unexpected = legs - gem_allow_legs
      puts "  (#{(legs & gem_allow_legs).join(', ')} allowlisted: ext kernel, U-14)" unless (legs & gem_allow_legs).empty?
      failed << kernel unless unexpected.empty?
    end
    targets.each do |name, gen, arg|
      rb = "#{lint_dir}/#{name}_combined.rb"
      sh "#{RbConfig.ruby} #{gen} #{arg ? "#{arg} " : ""}#{rb} linux"
      puts "== spinel-doctor: #{name} =="
      out = `SPINEL_DIR=#{dir} #{doctor} --only unsupported,unresolved #{rb} 2>&1`
      puts out
      findings = out.lines.select { |l| l =~ /warning:|error:/ }
      unexpected = findings.reject { |l| allow.any? { |p| l =~ p } }
      allowed = findings.size - unexpected.size
      puts "  (#{allowed} known finding(s) allowlisted)" if allowed > 0
      failed << name unless unexpected.empty?
    end
    abort "spinel-doctor UNEXPECTED findings in: #{failed.join(', ')}" unless failed.empty?
    puts "spinel-doctor: clean (modulo allowlisted findings)"
  end
end
