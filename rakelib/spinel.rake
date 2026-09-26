# rakelib/spinel.rake
# Spinel AOT compiler: fetch/build, host codegen, and the doctor lint.
# Part of the Rakefile split: shared constants, helper defs, and the
# docker command strings live in the top-level Rakefile, which Rake
# loads before every file in rakelib/.

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

  desc "Generate Spinel C from PreBuild Ruby on the host (before docker build)"
  task :gen do
    dir = SPINEL_DIR
    # Self-provision on a fresh clone: no usable compiler anywhere -> fetch and
    # build the pinned vendor copy.
    unless File.executable?(File.join(dir, "bin/spinel"))
      Rake::Task['spinel:setup'].invoke
      dir = SPINEL_VENDOR_DIR
    end
    bin = File.join(dir, "bin/spinel")
    abort "spinel not found at #{bin}. Run `rake spinel:setup` or set SPINEL_DIR." unless File.executable?(bin)
    # Flags every generated program gets. --no-inline-hot: the compiler forces
    # small leaf methods inline by default, and a large dispatcher that calls
    # many of them absorbs them all into one C frame. The kernel's
    # handle_app_control grew from 4,192 to 12,448 bytes that way and
    # overflowed the kernel task's 16 KB stack at boot (Stack protection fault
    # on the P4, doc/spinel_upstream_ext/report/p2b2.md). The sim does not
    # notice: its pthread stacks are far larger than the device's task stacks.
    # Must match main/prebuild_scripts/compile_ruby_to_spinel.cmake.
    gen_flags = "--no-inline-hot"
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
    mkdir_p SPINEL_GEN_DIR
    # Platform passed to the gen scripts (sets PLATFORM in the combined Ruby, which
    # gates ESP32-only code like RTC HW access). Defaults to linux; build:esp32 sets
    # SPINEL_GEN_PLATFORM=esp32. Getting this wrong silently compiles the wrong
    # PLATFORM branch (esp32 build with linux gen = RTC/HW code dropped).
    platform = ENV['SPINEL_GEN_PLATFORM'] || 'linux'
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
    vm = lambda do |rb, init|
      c = rb.sub(/\.rb\z/, ".c")
      sh "#{bin} #{gen_flags} -I #{SPINEL_SRC_DIR} -c #{rb} --ext-init #{init} -o #{c}"
      puts "Spinel generated #{c}"
    end
    if FMRB_KERNEL_ENGINE == "spinel"
      combined_rb = "#{SPINEL_GEN_DIR}/fmrb_kernel_combined.rb"
      sh "#{RbConfig.ruby} tool/spinel/gen_kernel_combined.rb #{combined_rb} #{platform}"
      vm.call(combined_rb, "Init_fmrb_kernel")
    end
    if FMRB_APP_ENGINE_EDITOR == "spinel"
      e_rb = "#{SPINEL_GEN_DIR}/editor_combined.rb"
      sh "#{RbConfig.ruby} tool/spinel/gen_app_combined.rb editor #{e_rb} #{platform}"
      vm.call(e_rb, "Init_editor")
    end
    if FMRB_APP_ENGINE_DESKTOP == "spinel"
      d_rb = "#{SPINEL_GEN_DIR}/system_desktop_combined.rb"
      sh "#{RbConfig.ruby} tool/spinel/gen_app_combined.rb system_desktop #{d_rb} #{platform}"
      vm.call(d_rb, "Init_system_desktop")
    end
    # The gems (FFT, SpinelHello, Raycast): libraries an mruby task calls. Each
    # gem's spinel/<name>_kernel.rb names its init and its entries in two
    # comment lines (`# spinel-ext-init:` / `# spinel-ext-entry:`), read here
    # and by main/CMakeLists.txt, so the names live in one place. The entries
    # become typed C functions in the generated header, which the gem's
    # native/ receiver includes. The kernel and the core it requires are staged
    # into SPINEL_SRC_DIR so require_relative resolves in one dir (the staged
    # copies are gitignored; the gem holds the originals, so the :ruby and
    # :spinel backends can never run different code).
    gem = lambda do |kernel, *cores|
      name = File.basename(kernel, ".rb")
      [kernel, *cores].each do |src|
        abort "#{src} is missing" unless File.exist?(src)
        cp src, "#{SPINEL_SRC_DIR}/#{File.basename(src)}"
      end
      spec = File.read(kernel)
      init = spec[/^# spinel-ext-init: *(\S+)/, 1] or abort "#{kernel}: no `# spinel-ext-init:` line"
      entries = spec[/^# spinel-ext-entry: *(\S+)/, 1] or abort "#{kernel}: no `# spinel-ext-entry:` line"
      c = "#{SPINEL_GEN_DIR}/#{name}.c"
      sh "#{bin} #{gen_flags} -I #{SPINEL_SRC_DIR} -c #{SPINEL_SRC_DIR}/#{name}.rb " \
         "--ext-init #{init} --ext-entry #{entries} -o #{c}"
      puts "Spinel generated #{c}"
    end
    # FFT: not built with FMRB_FFT_SPINEL=0 (doc/mic_spectrum).
    if FMRB_FFT_SPINEL
      gem.call("lib/add/picoruby-fmrb-fft/spinel/fft_kernel.rb",
               "lib/add/picoruby-fmrb-fft/mrblib/fft_core.rb",
               "lib/add/picoruby-fmrb-fft/mrblib/fft_core_q15.rb")
    end
    # SpinelHello: the minimal sample gem. Always built (no flag).
    gem.call("lib/add/picoruby-fmrb-spinel-hello/spinel/spinel_hello_kernel.rb",
             "lib/add/picoruby-fmrb-spinel-hello/mrblib/spinel_hello_core.rb")
    # Raycast: the raycaster's ray loop as a gem (doc/raycast_spinel). Always
    # built. Its core is shared with the :ruby backend.
    gem.call("lib/add/picoruby-fmrb-raycast/spinel/raycast_kernel.rb",
             "lib/add/picoruby-fmrb-raycast/mrblib/raycast_core.rb")
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
    mkdir_p SPINEL_GEN_DIR
    # Lint kernel + desktop (both are Spinel-convertible programs). We generate
    # the require-inlined combined Ruby, then run the SOURCE-LEVEL legs only.
    # --skip build,behavior: those legs link/run the combined standalone, which
    # flags the fmruby C shims (fmrb_spx_*) as undefined and reports a CRuby-diff
    # -- both false positives here (the real firmware build links the shims).
    # The gate keys on unsupported/unresolved (error severity); inference notes
    # ("widened to untyped") are informational poly-widen hints.
    targets = [
      ["fmrb_kernel",    "tool/spinel/gen_kernel_combined.rb", nil],
      ["system_desktop", "tool/spinel/gen_app_combined.rb", "system_desktop"],
      ["editor",         "tool/spinel/gen_app_combined.rb", "editor"],
    ]
    # Known-accepted findings: ESP32-only RTC hardware code that is dead on Linux
    # (PLATFORM guard) but still statically analyzed. Its driver classes
    # (RX8900 / RX8130) are not in the Linux Spinel program, so `write_time`
    # can't resolve here. INTERIM allowlist: Phase 5 routes RTC writes through the
    # set_wallclock FFI (decision (b), phase5.md T5-4) and deletes the direct
    # driver instantiation from clock_setting.rb -- then REMOVE this allowlist.
    allow = [/unresolved call 'write_time' on .* receiver/]
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
        cp "lib/add/#{gem}/#{sub}/#{f}.rb", "#{SPINEL_GEN_DIR}/#{f}.rb"
      end
      puts "== spinel-doctor: #{kernel} =="
      out = `cd #{SPINEL_GEN_DIR} && SPINEL_DIR=#{dir} #{doctor} #{kernel}.rb 2>&1`
      puts out
      legs = out.scan(/^\[ERR\]\s+(\S+)/).flatten
      unexpected = legs - gem_allow_legs
      puts "  (#{(legs & gem_allow_legs).join(', ')} allowlisted: ext kernel, U-14)" unless (legs & gem_allow_legs).empty?
      failed << kernel unless unexpected.empty?
    end
    targets.each do |name, gen, arg|
      rb = "#{SPINEL_GEN_DIR}/#{name}_combined.rb"
      sh "#{RbConfig.ruby} #{gen} #{arg ? "#{arg} " : ""}#{rb} linux"
      puts "== spinel-doctor: #{name} =="
      out = `SPINEL_DIR=#{dir} #{doctor} --only unsupported,unresolved #{rb} 2>&1`
      puts out
      findings = out.lines.select { |l| l =~ /warning:|error:/ }
      unexpected = findings.reject { |l| allow.any? { |p| l =~ p } }
      allowed = findings.size - unexpected.size
      puts "  (#{allowed} known ESP32-only finding(s) allowlisted)" if allowed > 0
      failed << name unless unexpected.empty?
    end
    abort "spinel-doctor UNEXPECTED findings in: #{failed.join(', ')}" unless failed.empty?
    puts "spinel-doctor: clean (modulo allowlisted ESP32-only findings)"
  end
end
