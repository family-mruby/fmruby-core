# frozen_string_literal: true
#
# Helpers shared by raycast_run.rb and fft_run.rb: shelling out, reading the
# compile flags upstream Spinel would use, building a 32-bit runtime in a
# clone, and object section sizes.

require "fileutils"
require "open3"
require "tmpdir"

HERE = File.expand_path(__dir__)
CORE_ROOT = File.expand_path("../..", HERE)
PINNED = "01521b1e"

def sh!(*cmd, chdir: nil)
  o, s = Open3.capture2e(*cmd, **(chdir ? { chdir: chdir } : {}))
  unless s.success?
    warn o
    abort "failed: #{cmd.join(' ')}"
  end
  o
end

def log(msg)
  puts msg
  $stdout.flush
end

# Commit of the upstream checkout, with a warning when it is not the pinned one.
def upstream_head(sp)
  head = sh!("git", "-C", sp, "rev-parse", "HEAD").strip
  log "upstream: #{sp} @ #{head[0, 12]}"
  warn "WARNING: upstream is not at the pinned #{PINNED}" unless head.start_with?(PINNED)
  abort "#{sp}/bin/spinel not found (build upstream first: make deps && make)" unless File.executable?(File.join(sp, "bin/spinel"))
  head
end

# The C flags `spinel <kernel> --print-build` reports (defines, cflags,
# include dirs), so the ext TU is compiled the way spinel itself would.
def build_flags(spinel, kernel, cc)
  args = [spinel, kernel, "--print-build"]
  args << "--cc=#{cc}" if cc != "cc"
  flags = []
  sh!(*args).each_line do |l|
    k, v = l.strip.split(" ", 2)
    case k
    when "cflag", "define" then flags << v
    when "include" then flags << "-I#{v}"
    end
  end
  flags
end

# A 32-bit libspinel_rt.a of the same upstream commit, built in a clone under
# out/up32 (the checkout itself is never touched). Only the runtime archive is
# made: the i386 bin/spinel would need an i386 libcrypt.
def build_rt32(sp, head, out)
  dir = File.join(out, "up32")
  lib = File.join(dir, "lib/libspinel_rt.a")
  have = File.directory?(File.join(dir, ".git")) && sh!("git", "-C", dir, "rev-parse", "HEAD").strip
  if have != head
    FileUtils.rm_rf(dir)
    sh!("git", "clone", "-q", "--no-checkout", sp, dir)
    sh!("git", "-C", dir, "checkout", "-q", head)
  end
  unless File.exist?(lib)
    log "building the 32-bit runtime in #{dir} (make CC='cc -m32' lib/libspinel_rt.a)"
    File.write(File.join(out, "build_rt32.log"), sh!("make", "-C", dir, "CC=cc -m32", "-j8", "lib/libspinel_rt.a"))
  end
  lib
end

# The same for the fork (fmruby-core/vendor/spinel), for the timing host.
def build_fork_rt32(src, out)
  dir = File.join(out, "fork32")
  lib = File.join(dir, "lib/libspinel_rt.a")
  unless File.exist?(lib)
    FileUtils.rm_rf(dir)
    sh!("git", "clone", "-q", src, dir)
    log "building the fork's 32-bit runtime in #{dir}"
    File.write(File.join(out, "build_fork_rt32.log"), sh!("make", "-C", dir, "CC=cc -m32", "-j8", "lib/libspinel_rt.a"))
  end
  lib
end

# size -A summed per section family. .data.rel.ro is kept apart from .data: it
# exists because the host objects are position independent, and a non-PIC
# device build puts that content in .rodata.
def sections(obj)
  s = {}
  sh!("size", "-A", obj).each_line do |l|
    f = l.split
    next unless f.size >= 2 && f[1] =~ /\A\d+\z/
    key = case f[0]
          when /\A\.text/ then "text"
          when /\A\.data\.rel\.ro/ then "relro"
          when /\A\.data/ then "data"
          when /\A\.rodata/ then "rodata"
          when /\A\.bss/ then "bss"
          end
    s[key] = (s[key] || 0) + f[1].to_i if key
  end
  s
end

def print_sizes(sizes)
  log "object sizes (size -A, -O2, bytes)"
  log format("  %-12s %9s %9s %9s %13s %9s", "", ".text", ".data", ".rodata", ".data.rel.ro", ".bss")
  sizes.each do |n, s|
    log format("  %-12s %9d %9d %9d %13d %9d", n, s["text"] || 0, s["data"] || 0, s["rodata"] || 0,
               s["relro"] || 0, s["bss"] || 0)
  end
end

# Generate the ext C + header for a kernel into dir, compile it and a host, link.
# Returns [kernel object, executable].
def build_ext(spinel:, kernel:, init:, entries:, host_c:, dir:, cc:, rt:)
  FileUtils.mkdir_p(dir)
  base = File.basename(kernel, ".rb")
  gen = [spinel, kernel, "-c", "--ext-init", init, "--ext-entry", entries, "-o", File.join(dir, "#{base}.c")]
  gen << "--cc=#{cc}" if cc != "cc"
  sh!(*gen, chdir: File.dirname(kernel))
  flags = build_flags(spinel, kernel, cc) + ["-O2", "-w", "-ffunction-sections", "-fdata-sections", "-I#{dir}"]
  ccv = cc.split
  kobj = File.join(dir, "#{base}.o")
  hobj = File.join(dir, "#{File.basename(host_c, '.c')}.o")
  sh!(*ccv, *flags, "-c", File.join(dir, "#{base}.c"), "-o", kobj)
  sh!(*ccv, *flags, "-c", host_c, "-o", hobj)
  exe = File.join(dir, File.basename(host_c, ".c"))
  sh!(*ccv, "-Wl,--gc-sections", hobj, kobj, rt, "-lm", "-o", exe)
  [kobj, exe]
end
