#!/usr/bin/env ruby
# frozen_string_literal: true
#
# P1 prototype driver, T4: the FFT gem's cores as an upstream Spinel ext
# library, checked against CRuby running the same fft_core.rb / fft_core_q15.rb
# on 64-bit and 32-bit. Nothing here is wired into the fmrb build.
#
# Tolerance: the magnitudes (int16 bytes, both cores) must match byte for byte.
# level_db returns a Float; it passes when |ext - cruby| <= 1e-9 dB, and the
# summary says how many were bit-identical.
#
# usage: ruby fft_run.rb [--out DIR] [--spinel DIR] [--rt32 LIB] [--no-m32]
#                        [--seed N] [--random N] [--no-fork]
# See README.md.

require "optparse"
require_relative "common"

GEM_DIR = File.join(CORE_ROOT, "lib/add/picoruby-fmrb-fft")
KERNEL = File.join(HERE, "fft_kernel.rb")
ENTRIES = "FftKernel.run,FftKernel.run_q15,FftKernel.level_db"
LEVEL_TOL = 1e-9

opt = {
  out: File.join(Dir.tmpdir, "spinel_ext_poc"),
  spinel: File.expand_path("~/dev/spinel"),
  rt32: nil,
  m32: true,
  seed: 20260926,
  random: 200,
  fork: true,
}
OptionParser.new do |o|
  o.on("--out DIR", "where generated C, binaries and logs go") { |v| opt[:out] = File.expand_path(v) }
  o.on("--spinel DIR", "upstream Spinel checkout, already built (read only)") { |v| opt[:spinel] = File.expand_path(v) }
  o.on("--rt32 LIB", "a 32-bit libspinel_rt.a to use instead of building one") { |v| opt[:rt32] = File.expand_path(v) }
  o.on("--no-m32", "skip the 32-bit leg") { opt[:m32] = false }
  o.on("--seed N", Integer) { |v| opt[:seed] = v }
  o.on("--random N", Integer, "random transforms (default 200)") { |v| opt[:random] = v }
  o.on("--no-fork", "skip the size comparison with the fork's generated C") { opt[:fork] = false }
end.parse!

ROOT = opt[:out]
OUT = File.join(ROOT, "fft")
FileUtils.mkdir_p(OUT)
SP = opt[:spinel]
SPINEL = File.join(SP, "bin/spinel")
head = upstream_head(SP)

# ---- the cases ------------------------------------------------------------

def pcm(vals)
  vals.map { |v| v.clamp(-32768, 32767) }.pack("s<*")
end

def signals(n, rng)
  pi = Math::PI
  {
    "zeros" => pcm(Array.new(n, 0)),
    "dc" => pcm(Array.new(n, 12_000)),
    "impulse" => pcm(Array.new(n) { |i| i.zero? ? 32_767 : 0 }),
    "square_fs" => pcm(Array.new(n) { |i| (i / 4).even? ? 32_767 : -32_768 }),
    "sine_bin5" => pcm(Array.new(n) { |i| (12_000 * Math.sin(2 * pi * 5 * i / n)).round }),
    "two_tone" => pcm(Array.new(n) { |i| (9000 * Math.sin(2 * pi * 3.3 * i / n) + 7000 * Math.cos(2 * pi * (n / 5) * i / n)).round }),
    "noise_fs" => pcm(Array.new(n) { rng.rand(-32_768..32_767) }),
  }
end

rng = Random.new(opt[:seed])
lines = []
lines << "level 0 -96.0" # before any run: must raise
[64, 128, 256, 512, 1024].each do |n|
  signals(n, rng).each do |_name, s|
    hex = s.unpack1("H*")
    lines << "run #{n} 1 #{hex}"
    [0, 1, 5, n / 4, n / 2 - 1].each { |i| lines << "level #{i} -96.0" }
    lines << "level 3 -120.25"
    lines << "q15 #{n} 1 #{hex}"
  end
end
# Iterating the transform in place (iters > 1) as the bench does.
s = signals(256, rng)["two_tone"].unpack1("H*")
lines << "run 256 3 #{s}"
lines << "q15 256 3 #{s}"
# Random sizes and samples; the cores are rebuilt whenever the size changes.
opt[:random].times do
  n = [64, 128, 256, 512].sample(random: rng)
  hex = pcm(Array.new(n) { rng.rand(-32_768..32_767) }).unpack1("H*")
  lines << "run #{n} 1 #{hex}"
  lines << "level #{rng.rand(0...n / 2)} -80.5"
  lines << "q15 #{n} 1 #{hex}"
end
# Bad requests raise ArgumentError and leave the cached cores alone.
lines << "run 100 1 #{'00' * 200}"
lines << "run 32 1 #{'00' * 64}"
lines << "q15 64 0 #{'00' * 128}"
lines << "q15 128 1 #{'00' * 100}"
lines << "level 99999 -96.0"
lines << "level 2 -96.0"
cases = File.join(OUT, "cases.txt")
File.write(cases, lines.join("\n") + "\n")
log "cases: #{lines.size} commands (#{lines.count { |l| l.start_with?('run ') }} run, " \
    "#{lines.count { |l| l.start_with?('q15 ') }} q15, #{lines.count { |l| l.start_with?('level ') }} level)"

# ---- CRuby reference ----------------------------------------------------

load KERNEL

def run_cruby(case_path, out_path)
  events = []
  File.open(out_path, "wb") do |out|
    File.foreach(case_path) do |l|
      f = l.split
      begin
        case f[0]
        when "run", "q15"
          bytes = [f[3].to_s].pack("H*")
          r = f[0] == "run" ? FftKernel.run(bytes, f[1].to_i, f[2].to_i) : FftKernel.run_q15(bytes, f[1].to_i, f[2].to_i)
          out.write(r)
          events << "#{f[0]} #{r.bytesize}"
        when "level"
          events << format("level %.17g", FftKernel.level_db(f[1].to_i, Float(f[2])))
        end
      rescue => e
        events << "#{f[0]} raise #{e.class}: #{e.message}"
      end
    end
  end
  events
end

ref_bin = File.join(OUT, "ref.bin")
ref_events = run_cruby(cases, ref_bin)
File.write(File.join(OUT, "ref.events"), ref_events.join("\n") + "\n")
log "cruby: #{File.size(ref_bin)} bytes of magnitudes"

# ---- legs ------------------------------------------------------------------

def compare(name, bin, events, ref_bin, ref_events)
  ok = true
  if File.binread(bin) != File.binread(ref_bin)
    a = File.binread(bin)
    b = File.binread(ref_bin)
    i = (0...[a.bytesize, b.bytesize].min).find { |k| a.getbyte(k) != b.getbyte(k) } || [a.bytesize, b.bytesize].min
    log "  #{name}: magnitude bytes differ at offset #{i}"
    ok = false
  end
  exact = 0
  nlevel = 0
  worst = 0.0
  if events.size != ref_events.size
    log "  #{name}: #{events.size} events vs #{ref_events.size}"
    return [false, 0, 0, 0.0]
  end
  events.zip(ref_events).each_with_index do |(x, y), i|
    if x.start_with?("level ") && y.start_with?("level ") && !x.include?(" raise ") && !y.include?(" raise ")
      nlevel += 1
      d = (Float(x.split[1]) - Float(y.split[1])).abs
      exact += 1 if x == y
      worst = d if d > worst
      next if d <= LEVEL_TOL
    end
    next if x == y
    log "  #{name}: event ##{i} ext=#{x.inspect} cruby=#{y.inspect}"
    ok = false
  end
  [ok, exact, nlevel, worst]
end

legs = [["x86_64", "cc", File.join(SP, "lib/libspinel_rt.a")]]
legs << ["i386", "cc -m32", opt[:rt32] || build_rt32(SP, head, ROOT)] if opt[:m32]
sizes = []
all_ok = true
legs.each do |name, cc, rt|
  dir = File.join(OUT, name)
  kobj, exe = build_ext(spinel: SPINEL, kernel: KERNEL, init: "Init_fft", entries: ENTRIES,
                        host_c: File.join(HERE, "fft_host.c"), dir: dir, cc: cc, rt: rt)
  sizes << ["ext #{name}", sections(kobj)]
  bin = File.join(dir, "out.bin")
  events = sh!(exe, cases, bin).lines.map(&:chomp)
  File.write(File.join(dir, "events"), events.join("\n") + "\n")
  ok, exact, nlevel, worst = compare(name, bin, events, ref_bin, ref_events)
  all_ok &&= ok
  log "#{name}: #{ok ? 'MATCH' : 'MISMATCH'} (magnitudes byte-exact: #{File.binread(bin) == File.binread(ref_bin)}; " \
      "level_db #{exact}/#{nlevel} bit-identical, worst |diff| #{worst} dB; " \
      "#{events.count { |e| e.include?(' raise ') }} raises)"
end
log "raises seen (cruby): #{ref_events.select { |e| e.include?(' raise ') }.uniq.join(' | ')}"
log "boundary types (from the generated header):"
File.readlines(File.join(OUT, "x86_64", "fft_kernel.h")).grep(/sp_FftKernel_s_/).each { |l| log "  #{l.strip}" }

# The fork's generated fft_spinel.c, for the size table (compiled only).
fork_bin = File.join(CORE_ROOT, "vendor/spinel/bin/spinel")
if opt[:fork] && File.executable?(fork_bin)
  fdir = File.join(OUT, "fork")
  FileUtils.mkdir_p(fdir)
  %w[spinel/fft_spinel.rb spinel/fmrb_fft_ffi.rb mrblib/fft_core.rb mrblib/fft_core_q15.rb].each do |f|
    FileUtils.cp(File.join(GEM_DIR, f), fdir)
  end
  sh!(fork_bin, "--no-main", "--entry", "fmrb_fft_spinel_entry", "--persistent-statics", "-I", ".", "-c",
      "fft_spinel.rb", "-o", "fft_spinel.c", chdir: fdir)
  finc = "-I#{File.join(CORE_ROOT, 'vendor/spinel/lib')}"
  [["x86_64", []], ["i386", ["-m32"]]].each do |arch, m|
    next if arch == "i386" && !opt[:m32]
    obj = File.join(fdir, "fft_spinel_#{arch}.o")
    sh!("cc", *m, "-O2", "-w", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off", finc,
        "-c", File.join(fdir, "fft_spinel.c"), "-o", obj)
    sizes << ["fork #{arch}", sections(obj)]
  end
end

log ""
print_sizes(sizes)
log ""
log(all_ok ? "RESULT: all legs match CRuby (magnitudes byte for byte, level_db within #{LEVEL_TOL} dB)" : "RESULT: MISMATCH")
exit(all_ok ? 0 : 1)
