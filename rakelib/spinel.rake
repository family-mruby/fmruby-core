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
    # Guard against compiler/runtime-snapshot divergence: the generated C and
    # components/fmrb_spinel_rt/spinel_rt must come from the same fork commit.
    import_info = File.expand_path("components/fmrb_spinel_rt/spinel_rt/IMPORT_INFO", ROOT_DIR)
    if File.exist?(import_info)
      snap = File.read(import_info)[/^fork_commit: (\h+)/, 1]
      head = `git -C #{dir} rev-parse HEAD 2>/dev/null`.strip
      if snap && !head.empty? && snap != head
        warn "WARNING: spinel compiler at #{head[0, 12]} but runtime snapshot is #{snap[0, 12]};" \
             " regenerate the snapshot (import_from_fork.rb) or align the checkouts."
      end
    end
    mkdir_p SPINEL_GEN_DIR
    # Platform passed to the gen scripts (sets PLATFORM in the combined Ruby, which
    # gates ESP32-only code like RTC HW access). Defaults to linux; build:esp32 sets
    # SPINEL_GEN_PLATFORM=esp32. Getting this wrong silently compiles the wrong
    # PLATFORM branch (esp32 build with linux gen = RTC/HW code dropped).
    platform = ENV['SPINEL_GEN_PLATFORM'] || 'linux'
    # Kernel: concatenate the kernel Ruby into one combined program (the Spinel
    # compiler needs a single translation unit; require_relative is stripped)
    # and compile to C (library mode, entry fmrb_kernel_entry). Host-generated
    # into gen/ (gitignored). Skipped when the kernel stays on mruby.
    if FMRB_KERNEL_ENGINE == "spinel"
      combined_rb = "#{SPINEL_GEN_DIR}/fmrb_kernel_combined.rb"
      out_c       = "#{SPINEL_GEN_DIR}/fmrb_kernel_combined.c"
      sh "#{RbConfig.ruby} tool/spinel/gen_kernel_combined.rb #{combined_rb} #{platform}"
      sh "#{bin} --no-main --entry fmrb_kernel_entry -I #{SPINEL_SRC_DIR} -c #{combined_rb} -o #{out_c}"
      puts "Spinel generated #{out_c}"
    end
    # Editor: same, for the editor (entry editor_entry).
    if FMRB_APP_ENGINE_EDITOR == "spinel"
      e_rb = "#{SPINEL_GEN_DIR}/editor_combined.rb"
      e_c  = "#{SPINEL_GEN_DIR}/editor_combined.c"
      sh "#{RbConfig.ruby} tool/spinel/gen_app_combined.rb editor #{e_rb} #{platform}"
      sh "#{bin} --no-main --entry editor_entry -I #{SPINEL_SRC_DIR} -c #{e_rb} -o #{e_c}"
      puts "Spinel generated #{e_c}"
    end
    # Desktop: same, for system_desktop (entry system_desktop_entry).
    if FMRB_APP_ENGINE_DESKTOP == "spinel"
      d_rb = "#{SPINEL_GEN_DIR}/system_desktop_combined.rb"
      d_c  = "#{SPINEL_GEN_DIR}/system_desktop_combined.c"
      sh "#{RbConfig.ruby} tool/spinel/gen_app_combined.rb system_desktop #{d_rb} #{platform}"
      sh "#{bin} --no-main --entry system_desktop_entry -I #{SPINEL_SRC_DIR} -c #{d_rb} -o #{d_c}"
      puts "Spinel generated #{d_c}"
    end
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
