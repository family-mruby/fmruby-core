#!/usr/bin/env ruby
# frozen_string_literal: true
#
# P1 prototype driver: build the raycast ext library with upstream Spinel, run
# it from a C test host on 64-bit and 32-bit, and compare every byte it returns
# with CRuby running the same raycast_core.rb. Also records object sizes and a
# rough host-side timing against the fork's current gem. Nothing here is wired
# into the fmrb build.
#
# usage: ruby raycast_run.rb [--out DIR] [--spinel DIR] [--rt32 LIB] [--no-m32]
#                            [--seed N] [--random N] [--no-fork] [--cpu N]
# See README.md.

require "optparse"
require_relative "common"

GEM_DIR = File.join(CORE_ROOT, "lib/add/picoruby-fmrb-raycast")
GAME_APP = File.join(CORE_ROOT, "flash/app/game/raycaster.app.rb")
KERNEL = File.join(HERE, "raycast_kernel.rb")
ENTRIES = "RaycastKernel.load_map,RaycastKernel.cast"

opt = {
  out: File.join(Dir.tmpdir, "spinel_ext_poc"),
  spinel: File.expand_path("~/dev/spinel"),
  rt32: nil,
  m32: true,
  seed: 20260926,
  random: 1200,
  fork: true,
  cpu: 2,
}
OptionParser.new do |o|
  o.on("--out DIR", "where generated C, binaries and logs go") { |v| opt[:out] = File.expand_path(v) }
  o.on("--spinel DIR", "upstream Spinel checkout, already built (read only)") { |v| opt[:spinel] = File.expand_path(v) }
  o.on("--rt32 LIB", "a 32-bit libspinel_rt.a to use instead of building one") { |v| opt[:rt32] = File.expand_path(v) }
  o.on("--no-m32", "skip the 32-bit leg") { opt[:m32] = false }
  o.on("--seed N", Integer) { |v| opt[:seed] = v }
  o.on("--random N", Integer, "random poses on the game map (default 1200)") { |v| opt[:random] = v }
  o.on("--no-fork", "skip the comparison with the fork's generated C") { opt[:fork] = false }
  o.on("--cpu N", Integer, "pin the timing runs to this CPU with taskset (default 2)") { |v| opt[:cpu] = v }
end.parse!

ROOT = opt[:out]
OUT = File.join(ROOT, "raycast")
FileUtils.mkdir_p(OUT)
SP = opt[:spinel]
SPINEL = File.join(SP, "bin/spinel")
# Timing runs are pinned to one CPU when taskset is there: the host is shared.
PIN = system("which taskset > /dev/null 2>&1") ? ["taskset", "-c", opt[:cpu].to_s] : []
head = upstream_head(SP)

# ---- the maps -------------------------------------------------------------

# The game's world, read out of the app itself so this cannot drift from it.
src = File.read(GAME_APP)
gw = src[/^\s*MAP_W\s*=\s*(\d+)/, 1].to_i
gh = src[/^\s*MAP_H\s*=\s*(\d+)/, 1].to_i
cells = src[/^\s*WORLD_MAP\s*=\s*\[(.*?)\]/m, 1].scan(/\d+/).map(&:to_i)
abort "could not read WORLD_MAP from #{GAME_APP}" if gw < 1 || cells.size != gw * gh
GAME = { w: gw, h: gh, bytes: cells.pack("C*") }.freeze

# A small hand-made one: not square (to catch w/h mix-ups), open cells on the
# border (so rays leave the map and hit the solid outside), all wall values.
HAND_ROWS = %w[
  0000000
  0102030
  0000004
  3000100
  0400000
].freeze
HAND = { w: HAND_ROWS[0].size, h: HAND_ROWS.size, bytes: HAND_ROWS.join.bytes.map { |b| b - 48 }.pack("C*") }.freeze

# ---- the cases ------------------------------------------------------------

def map_line(m)
  "map #{m[:w]} #{m[:h]} #{m[:bytes].unpack1('H*')}"
end

C = 256

def fixed_poses(m)
  w = m[:w] * C
  h = m[:h] * C
  poses = []
  # Every whole degree from one open spot.
  cx = C + C / 2
  cy = C + C / 2
  0.step(359, 1) { |a| poses << [cx, cy, a] }
  # Facing walls square-on and diagonally, from inside cells of the map.
  [[C + 10, C + 10], [C * 2 + 200, C + 128], [C + 128, C * 2 + 250]].each do |x, y|
    [0, 45, 90, 135, 180, 225, 270, 315].each { |a| poses << [x, y, a] }
  end
  # Exactly on cell corners and edges (the DDA's zero-distance branches).
  [[C, C], [C * 2, C * 2], [C, C * 2], [C * 3, C]].each do |x, y|
    [0, 1, 30, 89, 90, 91, 180, 269, 270, 359].each { |a| poses << [x, y, a] }
  end
  # The map's own edges and beyond, including negative positions.
  [[0, 0], [w - 1, h - 1], [0, h - 1], [w - 1, 0], [w / 2, 0], [0, h / 2],
   [-1, -1], [-300, 100], [w + 50, h / 2], [w / 2, h + 700]].each do |x, y|
    [0, 60, 135, 200, 300].each { |a| poses << [x, y, a] }
  end
  # Angles outside 0..359.
  [-1, -45, -360, -719, 360, 361, 725, 1080].each { |a| poses << [cx, cy, a] }
  poses
end

def random_poses(m, n, rng)
  w = m[:w] * C
  h = m[:h] * C
  Array.new(n) { [rng.rand(-C..w + C), rng.rand(-C..h + C), rng.rand(-720..720)] }
end

rng = Random.new(opt[:seed])
lines = []
lines << "cast 100 100 0" # before any map: must raise
lines << map_line(GAME)
fixed_poses(GAME).each { |p| lines << "cast #{p.join(' ')}" }
random_poses(GAME, opt[:random], rng).each { |p| lines << "cast #{p.join(' ')}" }
# Swap the map: the next casts must see the new world.
lines << map_line(HAND)
fixed_poses(HAND).each { |p| lines << "cast #{p.join(' ')}" }
random_poses(HAND, 300, rng).each { |p| lines << "cast #{p.join(' ')}" }
# A bad map raises and leaves the previous core in place.
lines << "map 4 4 #{'01' * 3}"
lines << "map 0 3 000000"
random_poses(HAND, 20, rng).each { |p| lines << "cast #{p.join(' ')}" }
# And back to the game map.
lines << map_line(GAME)
random_poses(GAME, 200, rng).each { |p| lines << "cast #{p.join(' ')}" }
cases = File.join(OUT, "cases.txt")
File.write(cases, lines.join("\n") + "\n")
ncast = lines.count { |l| l.start_with?("cast ") }
log "cases: #{lines.size} commands, #{ncast} casts, #{lines.count { |l| l.start_with?('map ') }} map loads"

# Bench: one heavy pose (the long corridor, rays running to MAX_STEPS) and the
# start pose, 1000 calls each on every engine. Each pose is run BENCH_ROUNDS
# times in turn and the fastest round is kept, so a stray GC or scheduler
# hiccup does not decide the figure. The benchtry lines run the same poses
# through Init_raycast_try (ext host only; CRuby and the fork host skip them).
BENCH = [[1000, C + C / 2, C * 15 + C / 2, 0], [1000, C + C / 2, C + C / 2, 45]].freeze
BENCH_ROUNDS = 20
bench_file = File.join(OUT, "bench.txt")
File.write(bench_file, ([map_line(GAME)] + (BENCH.map { |b| "bench #{b.join(' ')}" } * BENCH_ROUNDS) +
                        (BENCH.map { |b| "benchtry #{b.join(' ')}" } * BENCH_ROUNDS)).join("\n") + "\n")

# Best ns/cast per pose from a list of "bench <n> <ns>" lines in file order.
def best_per_pose(lines)
  per = lines.map { |e| e.split[2].to_i / e.split[1].to_i }
  Array.new(BENCH.size) { |k| per.each_slice(BENCH.size).map { |r| r[k] }.compact.min }
end

# ---- CRuby reference ----------------------------------------------------

load KERNEL

def run_cruby(case_path, out_path)
  events = []
  File.open(out_path, "wb") do |out|
    File.foreach(case_path) do |l|
      f = l.split
      case f[0]
      when "map"
        begin
          r = RaycastKernel.load_map([f[3].to_s].pack("H*"), f[1].to_i, f[2].to_i)
          events << "map ok #{r}"
        rescue => e
          events << "map raise #{e.class}: #{e.message}"
        end
      when "cast"
        begin
          b = RaycastKernel.cast(f[1].to_i, f[2].to_i, f[3].to_i)
          out.write(b)
          events << "cast #{b.bytesize}"
        rescue => e
          events << "cast raise #{e.class}: #{e.message}"
        end
      when "bench"
        n = f[1].to_i
        t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond)
        n.times { RaycastKernel.cast(f[2].to_i, f[3].to_i, f[4].to_i) }
        events << "bench #{n} #{Process.clock_gettime(Process::CLOCK_MONOTONIC, :nanosecond) - t0}"
      end
    end
  end
  events
end

ref_bin = File.join(OUT, "ref.bin")
ref_events = run_cruby(cases, ref_bin)
File.write(File.join(OUT, "ref.events"), ref_events.join("\n") + "\n")
cruby_bench = run_cruby(bench_file, File.join(OUT, "ref_bench.bin")).grep(/^bench/)
log "cruby: #{File.size(ref_bin)} bytes of rays"

# ---- one leg (64 or 32) -------------------------------------------------

# The same cases three ways: as is; with the collector forced to run every
# 2 KB (so the map swaps and the host-made map string go through real
# collections); and without the slab allocator, which a device port has to do
# without (it reserves address space with mmap).
RUN_ENVS = [
  ["plain", {}],
  ["gc_stress", { "SPINEL_GC_STRESS" => "1" }],
  ["no_slab", { "SPINEL_GC_SLAB" => "0" }],
].freeze

def leg(name, cc, rt, cases, bench_file)
  dir = File.join(OUT, name)
  kobj, exe = build_ext(spinel: SPINEL, kernel: KERNEL, init: "Init_raycast", entries: ENTRIES,
                        host_c: File.join(HERE, "raycast_host.c"), dir: dir, cc: cc, rt: rt)
  runs = RUN_ENVS.map do |tag, env|
    bin = File.join(dir, "out_#{tag}.bin")
    events = sh!(env, exe, cases, bin).lines.map(&:chomp)
    File.write(File.join(dir, "events_#{tag}"), events.join("\n") + "\n")
    { tag: tag, bin: bin, events: events }
  end
  blines = sh!(*PIN, exe, bench_file, File.join(dir, "bench.bin")).lines.map(&:chomp)
  { name: name, runs: runs, bench: blines.grep(/^bench /),
    benchtry: blines.grep(/^benchtry \d/).map { |l| l.sub("benchtry", "bench") }, obj: kobj, exe: exe }
end

def compare(run, ref_bin, ref_events)
  a = File.binread(run[:bin])
  b = File.binread(ref_bin)
  ok = true
  if run[:events] != ref_events
    ok = false
    i = run[:events].zip(ref_events).index { |x, y| x != y } || [run[:events].size, ref_events.size].min
    log "  events differ at ##{i}: ext=#{run[:events][i].inspect} cruby=#{ref_events[i].inspect}"
  end
  if a != b
    ok = false
    i = (0...[a.bytesize, b.bytesize].min).find { |k| a.getbyte(k) != b.getbyte(k) } || [a.bytesize, b.bytesize].min
    per = RaycastCore::NUM_RAYS * RaycastCore::RAY_BYTES
    log "  bytes differ at offset #{i} (ext #{a.bytesize} bytes, cruby #{b.bytesize} bytes): " \
        "successful cast ##{i / per}, ray #{(i % per) / RaycastCore::RAY_BYTES}"
  end
  ok
end

legs = []
legs << leg("x86_64", "cc", File.join(SP, "lib/libspinel_rt.a"), cases, bench_file)
if opt[:m32]
  rt32 = opt[:rt32] || build_rt32(SP, head, ROOT)
  legs << leg("i386", "cc -m32", rt32, cases, bench_file)
end

all_ok = true
legs.each do |l|
  l[:runs].each do |r|
    ok = compare(r, ref_bin, ref_events)
    all_ok &&= ok
    log "#{l[:name]} #{r[:tag]}: #{ok ? 'MATCH' : 'MISMATCH'} (#{File.size(r[:bin])} bytes, #{ncast} casts, " \
        "#{r[:events].count { |e| e.include?(' raise ') }} raises)"
  end
end
raises = ref_events.select { |e| e.include?(" raise ") }
log "raises seen (cruby): #{raises.uniq.join(' | ')}"

# ---- sizes and the fork's gem --------------------------------------------

sizes = legs.map { |l| ["ext #{l[:name]}", sections(l[:obj])] }

# The fork's generated raycast_entry.c, for comparison. The three gem sources
# are copied into OUT because the fork compiler resolves require_relative next
# to the entry, as rake spinel:gen arranges (copies, never edited).
fork_bin = File.join(CORE_ROOT, "vendor/spinel/bin/spinel")
fork_bench = []
fork_spot_ok = true
if opt[:fork] && File.executable?(fork_bin)
  fdir = File.join(OUT, "fork")
  FileUtils.mkdir_p(fdir)
  %w[spinel/raycast_entry.rb spinel/raycast_ffi.rb mrblib/raycast_core.rb].each do |f|
    FileUtils.cp(File.join(GEM_DIR, f), fdir)
  end
  sh!(fork_bin, "--no-main", "--entry", "raycast_entry", "--persistent-statics", "-I", ".", "-c",
      "raycast_entry.rb", "-o", "raycast_entry.c", chdir: fdir)
  finc = "-I#{File.join(CORE_ROOT, 'vendor/spinel/lib')}"
  [["x86_64", []], ["i386", ["-m32"]]].each do |arch, m|
    next if arch == "i386" && !opt[:m32]
    obj = File.join(fdir, "raycast_entry_#{arch}.o")
    sh!("cc", *m, "-O2", "-w", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off", finc,
        "-c", File.join(fdir, "raycast_entry.c"), "-o", obj)
    sizes << ["fork #{arch}", sections(obj)]

    # Timing host for today's gem shape. x86_64 links the fork checkout's own
    # runtime (read only); i386 needs a 32-bit fork runtime, built in a clone.
    rt = if arch == "x86_64"
           File.join(CORE_ROOT, "vendor/spinel/lib/libspinel_rt.a")
         else
           build_fork_rt32(File.join(CORE_ROOT, "vendor/spinel"), ROOT)
         end
    hobj = File.join(fdir, "fork_host_#{arch}.o")
    sh!("cc", *m, "-O2", "-w", "-c", File.join(HERE, "fork_host.c"), "-o", hobj)
    exe = File.join(fdir, "fork_host_#{arch}")
    sh!("cc", *m, "-Wl,--gc-sections", hobj, obj, rt, "-lm", "-o", exe)
    last = File.join(fdir, "last_#{arch}.bin")
    fork_bench << ["fork #{arch}", sh!(*PIN, exe, bench_file, last).lines.map(&:chomp).grep(/^bench/)]
    # Spot check: the rays of the last bench pose must equal CRuby's.
    b = BENCH.last
    RaycastKernel.load_map(GAME[:bytes], GAME[:w], GAME[:h])
    fork_spot_ok &&= File.binread(last) == RaycastKernel.cast(b[1], b[2], b[3]).b
  end
end

log ""
print_sizes(sizes)

# ---- timing ---------------------------------------------------------------

log ""
log "timing, #{BENCH.size} poses x 1000 casts each (host only, not a device figure)"
log "  best of #{BENCH_ROUNDS} rounds; pose 1 = down the long corridor, pose 2 = the start room at 45 deg"
rows = [["cruby", cruby_bench]] + legs.map { |l| ["ext #{l[:name]}", l[:bench]] } +
       legs.map { |l| ["ext+try #{l[:name]}", l[:benchtry]] } + fork_bench
rows.each do |n, b|
  log format("  %-16s %s", n, best_per_pose(b).map { |x| format("%8d ns/cast", x) }.join("  "))
end
log "  fork spot check (last bench pose vs CRuby): #{fork_spot_ok ? 'match' : 'MISMATCH'}" unless fork_bench.empty?

log ""
log(all_ok ? "RESULT: all legs match CRuby byte for byte" : "RESULT: MISMATCH")
exit(all_ok ? 0 : 1)
