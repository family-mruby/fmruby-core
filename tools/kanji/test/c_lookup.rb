# frozen_string_literal: true
#
# The board's C lookup (components/fmrb_kanji, doc/kanji_input K2a) against
# the Ruby reference (dict_format.rb's Reader, which tools/kanji/lookup.rb
# uses), built with the host cc over a stand-in file HAL and pool
# (tools/kanji/test/c/). Run by `rake kanji:test` (and `rake test`).
#
#   1. made-up dictionaries (fixtures.rb, one block and several): every
#      reading, longest leading parts, misses -- always run, nothing fetched
#   2. broken files made from them: refused at open, or (a damaged body)
#      refused at lookup without reading outside the block; read errors;
#      nothing left in the pool after close
#   3. the conversion's candidate list (fmrb_kanji_cands.c, K6): the
#      dictionary's first, the katakana of the whole reading when only a
#      leading part matched (it replaces everything), the katakana of the
#      matched part, the rest -- against the same order made from the reader
#   4. the generated dictionary (tools/kanji/work/out/fmrb_kanji.dic, from
#      `rake kanji:dict`) when it is there: every reading and thousands of
#      longest-match queries. Skipped, with the reason, when it is not.
#
# The program is built with AddressSanitizer and UBSan when the compiler has
# them, so a read past the block shows up as a failure.

require "tmpdir"
require "fileutils"
require "open3"
require_relative "fixtures"

ROOT = File.expand_path("../../..", __dir__)
DICT = File.expand_path("../work/out/fmrb_kanji.dic", __dir__)
OK = 0
ERR_INVALID_PARAM = -1
ERR_NOT_SUPPORTED = -4
ERR_FAILED = -6
ERR_NOT_FOUND = -7
ERR_INVALID_STATE = -8
FMRB_READING_MAX = 127

$failures = 0
$checks = 0

def check(label, cond)
  $checks += 1
  return true if cond

  $failures += 1
  puts "FAIL: #{label}"
  false
end

def build_program(dir)
  cc = ENV["CC"] || "cc"
  abort "#{cc} not found (install a C compiler or set CC)" unless system("which #{cc} > /dev/null 2>&1")
  srcs = %W[components/fmrb_kanji/fmrb_kanji.c components/fmrb_kanji/fmrb_kanji_cands.c tools/kanji/test/c/hal_stub.c tools/kanji/test/c/main.c]
  incs = %W[components/fmrb_kanji tools/kanji/test/c/stub components/fmrb_hal components/fmrb_common/include]
  out = File.join(dir, "kanji_c_test")
  base = "#{cc} -std=gnu11 -O1 -g -Wall -Wextra -Werror #{incs.map { |i| "-I #{i}" }.join(' ')} " \
         "#{srcs.join(' ')} -o #{out}"
  san = "-fsanitize=address,undefined -fno-sanitize-recover=all"
  Dir.chdir(ROOT) do
    if system("#{base} #{san} 2>/dev/null")
      return [out, true]
    end
    system(base) or abort "kanji C test: compile failed"
  end
  [out, false]
end

# Runs the driver over a list of command lines; returns its answer lines.
def run_program(prog, commands)
  out, err, st = Open3.capture3(prog, stdin_data: commands.join("\n") + "\n")
  unless st.success? && err.empty?
    check("driver exits cleanly (#{st.inspect})", false)
    puts err.lines.first(20).join
  end
  out.force_encoding("UTF-8").lines.map(&:chomp)
end

def expect_exact(cands)
  cands.empty? ? "E\t#{ERR_NOT_FOUND}" : (["E", OK.to_s] + cands).join("\t")
end

# Expected longest leading part from the reference's answers by reading.
def expect_prefix(map, q)
  q.length.downto(1) do |n|
    c = map[q[0, n]]
    return (["P", OK.to_s, (n * 3).to_s] + c).join("\t") if c
  end
  "P\t#{ERR_NOT_FOUND}"
end

def compare(label, got, want)
  bad = got.zip(want).each_with_index.reject { |(g, w), _| g == w }
  bad += [[[nil, nil], -1]] if got.size != want.size
  bad.first(3).each { |(g, w), i| puts "  #{label} ##{i}: got #{g.inspect[0, 160]}\n    want #{w.inspect[0, 160]}" }
  check("#{label}: #{want.size - bad.size}/#{want.size} lines agree", bad.empty?)
end

# Every reading of a dictionary, exact and with a kana or two after it, and
# misses; the expected lines come from the Ruby reader.
def agree(prog, path, label, rng, prefix_samples: 1000)
  r = KanjiDict::Reader.new(path)
  map = {}
  r.each_entry { |rd, cands, _, _| map[rd] = cands }
  readings = map.keys
  kana = ("あ".."ん").to_a + ["ー"]
  misses = Array.new(3000) { Array.new(rng.rand(1..6)) { kana.sample(random: rng) }.join }
  odd = ["", "a", "かa", "カンジ", "漢字", "かんじ漢", "ー", "ん" * 130, "か" * 127, "\xE3\x81".b.force_encoding("UTF-8"),
         "\xE3\x81\x8B\xE3".b.force_encoding("UTF-8")]
  exact = readings + misses + odd
  prefix = readings.map { |q| q + "た" } + readings.first(2000).map { |q| q + "んじ" } +
           readings.select { |q| q.length >= 3 }.map { |q| q[0..-2] } + misses + odd

  cmds = ["open #{path}", "stats"] + exact.map { |q| "E #{q}" } + ["stats"] + prefix.map { |q| "P #{q}" } + ["stats", "close"]
  out = run_program(prog, cmds)
  check("#{label}: open", out[0] == "open\t0")
  e_out = out[2, exact.size]
  _, e_reads, e_us = out[2 + exact.size].split("\t").map(&:to_i)
  p_out = out[3 + exact.size, prefix.size]
  _, p_reads, p_us = out[3 + exact.size + prefix.size].split("\t").map(&:to_i)
  compare("#{label}: exact", e_out, exact.map { |q| q.valid_encoding? ? expect_exact(map[q] || []) : "E\t#{ERR_NOT_FOUND}" })
  compare("#{label}: longest prefix", p_out,
          prefix.map { |q| q.valid_encoding? ? expect_prefix(map, q) : expect_prefix(map, q.b.byteslice(0, (q.bytesize / 3) * 3).force_encoding("UTF-8")) })
  check("#{label}: nothing left in the pool", out.last == "close\t0 0")

  # The map stands in for Reader#lookup; check it against the reader itself
  # (lookup.rb's code path) on a sample.
  sample = (readings.sample(prefix_samples, random: rng) + misses.first(200))
  sample_ok = sample.all? { |q| r.lookup(q) == (map[q] || []) } &&
              prefix.sample(prefix_samples, random: rng).all? do |q|
                next true unless q.valid_encoding? && q.length <= 127

                lp = r.longest_prefix(q)
                lp.nil? ? expect_prefix(map, q) == "P\t#{ERR_NOT_FOUND}" : expect_prefix(map, q) == (["P", "0", (lp[0].length * 3).to_s] + lp[1]).join("\t")
              end
  check("#{label}: the reader agrees with its own entries on a sample", sample_ok)
  r.close
  { readings: readings.size, exact: exact.size, prefix: prefix.size, e_reads: e_reads, p_reads: p_reads,
    e_us: e_us, p_us: p_us }
end

def katakana(s)
  s.tr("ぁ-ゖ", "ァ-ヶ")
end

# The candidate list a conversion shows, from the reader: C rc matched whole cands.
def expect_conv(r, q)
  hq = q.tr("ァ-ヶ", "ぁ-ゖ")
  lp = r.longest_prefix(hq)
  return ["C", ERR_NOT_FOUND, hq.bytesize, -1, katakana(hq)].join("\t") if lp.nil?

  rd, cands = lp
  partial = rd.bytesize < hq.bytesize
  extra = []
  extra << katakana(hq) if partial
  extra << katakana(rd) unless cands.include?(katakana(rd))
  at = cands.empty? ? 0 : 1
  (["C", OK, rd.bytesize, partial ? at : -1] + cands.dup.insert(at, *extra)).join("\t")
end

def conversions(prog, path, label, rng, samples)
  r = KanjiDict::Reader.new(path)
  readings = []
  r.each_entry { |rd, _, _, _| readings << rd }
  kana = ("あ".."ん").to_a + ["ー"]
  qs = readings.sample(samples, random: rng)
  qs += qs.first(samples / 2).map { |q| q + kana.sample(random: rng) + kana.sample(random: rng) }
  qs += qs.first(samples / 4).map { |q| katakana(q) }
  qs += Array.new(samples / 2) { Array.new(rng.rand(1..6)) { kana.sample(random: rng) }.join }
  qs = qs.select { |q| q.length <= FMRB_READING_MAX }
  out = run_program(prog, ["open #{path}"] + qs.map { |q| "C #{q}" } + ["close"])
  compare("#{label}: conversion lists", out[1, qs.size], qs.map { |q| expect_conv(r, q) })
  check("#{label}: conversions leave nothing in the pool", out.last == "close\t0 0")
  r.close
end

# The cases the K6 follow-up was for, on the generated dictionary.
def k6_cases(prog, path)
  qs = %w[ふぁいる ぱーてぃー ずずず かんじへんかん]
  out = run_program(prog, ["open #{path}"] + qs.map { |q| "C #{q}" } + ["close"]).drop(1)
  conv = qs.zip(out.map { |l| l.split("\t") }).to_h { |q, (_, rc, m, w, *c)| [q, { rc: rc.to_i, m: m.to_i, w: w.to_i, c: c }] }
  f = conv["ふぁいる"]
  check("fairu: only ふぁ matches (#{f[:m]} bytes)", f[:rc] == OK && f[:m] == 6)
  check("fairu: ファイル is 2nd and replaces all of it (#{f[:c].first(3).inspect}, whole #{f[:w]})",
        f[:c][1] == "ファイル" && f[:w] == 1)
  check("fairu: the partial candidates are still there (ファ 3rd)", f[:c][2] == "ファ" && f[:c].size >= 3)
  p = conv["ぱーてぃー"]
  check("pa-thi-: the whole reading matches, nothing replaces more (#{p[:c].first(2).inspect}, whole #{p[:w]})",
        p[:rc] == OK && p[:m] == "ぱーてぃー".bytesize && p[:w] == -1 && p[:c][1] == "パーティー")
  z = conv["ずずず"]
  check("zuzuzu: ズズズ 2nd, replacing all (#{z[:c].first(3).inspect}, whole #{z[:w]})",
        z[:m] == 3 && z[:c][1] == "ズズズ" && z[:w] == 1 && z[:c][2] == "ズ")
  k = conv["かんじへんかん"]
  check("kanjihenkan: a partial match keeps the dictionary's candidates (#{k[:c].first(4).inspect})",
        k[:m] == "かんじ".bytesize && k[:c].include?("漢字") && k[:c][1] == "カンジヘンカン" && k[:c].include?("カンジ"))
end

def patched(dir, src, name)
  bytes = File.binread(src)
  bytes = yield(bytes.dup)
  path = File.join(dir, name)
  File.binwrite(path, bytes)
  path
end

def put32(bytes, off, v)
  bytes[off, 4] = [v].pack("V")
  bytes
end

def broken_files(prog, dir, src)
  h = KanjiDict.unpack_header(File.binread(src, 64))
  cases = {
    "missing file" => [File.join(dir, "no_such.dic"), ERR_NOT_FOUND],
    "bad magic" => [patched(dir, src, "magic.dic") { |b| b[0, 4] = "FMKX"; b }, ERR_INVALID_STATE],
    "version 2" => [patched(dir, src, "v2.dic") { |b| b[4, 2] = [2].pack("v"); b }, ERR_NOT_SUPPORTED],
    "short copy" => [patched(dir, src, "short.dic") { |b| b[0, b.bytesize - 4096] }, ERR_INVALID_STATE],
    "header only part" => [patched(dir, src, "tiny.dic") { |b| b[0, 10] }, ERR_INVALID_STATE],
    "empty file" => [patched(dir, src, "empty.dic") { |_| +"" }, ERR_INVALID_STATE],
    "index offset off" => [patched(dir, src, "idx.dic") { |b| put32(b, 20, h[:index_offset] + 1) }, ERR_INVALID_STATE],
    "block size" => [patched(dir, src, "bs.dic") { |b| put32(b, 36, 2048) }, ERR_INVALID_STATE],
    "header size" => [patched(dir, src, "hs.dic") { |b| b[6, 2] = [32].pack("v"); b }, ERR_INVALID_STATE],
    "block count" => [patched(dir, src, "bc.dic") { |b| put32(b, 28, h[:block_count] + 1) }, ERR_INVALID_STATE],
    "huge tables" => [patched(dir, src, "huge.dic") { |b| put32(b, 24, 0x7FFF0000) }, ERR_INVALID_STATE],
    "index entry out of range" => [patched(dir, src, "ioff.dic") { |b| put32(b, h[:index_offset], h[:index_size]) }, ERR_INVALID_STATE],
    "index keys out of order" => [patched(dir, src, "iord.dic") do |b|
      put32(b, h[:index_offset] + 4, b[h[:index_offset], 4].unpack1("V"))
    end, ERR_INVALID_STATE]
  }
  cmds = cases.values.flat_map { |path, _| ["open #{path}", "close"] }
  out = run_program(prog, cmds)
  cases.each_with_index do |(name, (_, want)), i|
    check("broken: #{name} -> #{want} (got #{out[i * 2]})", out[i * 2] == "open\t#{want}")
    check("broken: #{name} leaves nothing in the pool", out[i * 2 + 1] == "close\t0 0")
  end

  # The CRC: right on the file as made, wrong after one byte of the body
  # changes (which open does not look at).
  flip = patched(dir, src, "flip.dic") { |b| b[h[:body_offset] + 5] = (b.getbyte(h[:body_offset] + 5) ^ 0x40).chr; b }
  out = run_program(prog, ["open #{src}", "verify", "open #{flip}", "verify", "close"])
  check("crc: verify of the file as made", out[1] == "verify\t0")
  check("crc: a changed byte opens", out[2] == "open\t0")
  check("crc: and fails verify", out[3] == "verify\t#{ERR_INVALID_STATE}")

  # Damaged bodies: every reading looked up must answer found, not found or
  # broken, never read outside the block (the sanitizers watch), and found
  # answers decode or say the entry is broken.
  r = KanjiDict::Reader.new(src)
  readings = []
  r.each_entry { |rd, _, _, _| readings << rd }
  r.close
  rng = Random.new(7)
  bodies = {
    "body of 0xFF" => ->(b) { b[h[:body_offset]..] = "\xFF".b * (b.bytesize - h[:body_offset]); b },
    "random body" => ->(b) { b[h[:body_offset]..] = rng.bytes(b.bytesize - h[:body_offset]); b },
    "lengths bumped" => ->(b) { (h[:body_offset]...b.bytesize).step(7) { |o| b.setbyte(o, b.getbyte(o) | 0x7F) }; b },
    "zero counts" => ->(b) { (h[:body_offset]...b.bytesize).step(3) { |o| b.setbyte(o, 0) if b.getbyte(o) < 0x10 }; b },
    # The zero padding after the last entry turned into more entries, which
    # run up to and past the end of the block: only the bound checks stop them.
    "padding of 0x03" => ->(b) { (h[:body_offset]...b.bytesize).each { |o| b.setbyte(o, 3) if b.getbyte(o).zero? }; b },
    "padding of 0x05" => ->(b) { (h[:body_offset]...b.bytesize).each { |o| b.setbyte(o, 5) if b.getbyte(o).zero? }; b },
    "padding of 0x7F" => ->(b) { (h[:body_offset]...b.bytesize).each { |o| b.setbyte(o, 0x7F) if b.getbyte(o).zero? }; b }
  }
  bodies.each do |name, fn|
    path = patched(dir, src, "body_#{name.tr(' ', '_')}.dic") { |b| fn.call(b) }
    qs = readings + readings.map { |q| q + "た" }
    out = run_program(prog, ["open #{path}"] + readings.map { |q| "E #{q}" } + readings.map { |q| "R #{q}" } +
                            qs.map { |q| "P #{q}" } + ["close"])
    rcs = out[1..-2].map { |l| l.split("\t")[1].to_i }
    check("damaged #{name}: opens", out[0] == "open\t0")
    check("damaged #{name}: answers are found / not found / broken",
          rcs.all? { |c| [OK, ERR_NOT_FOUND, ERR_INVALID_STATE].include?(c) })
    check("damaged #{name}: nothing left in the pool", out.last == "close\t0 0")
  end

  # Read errors: in the tables at open, and in a block at lookup.
  out = run_program(prog, ["failreads 1", "open #{src}", "close", "open #{src}", "failreads 0", "E かんじ", "P かんじ",
                           "failreads -1", "E かんじ", "close"])
  check("read error at open -> #{ERR_FAILED} (got #{out[0]})", out[0] == "open\t#{ERR_FAILED}")
  check("read error at open leaves nothing", out[1] == "close\t0 0")
  check("read error at lookup -> #{ERR_FAILED}", out[3] == "E\t#{ERR_FAILED}" && out[4] == "P\t#{ERR_FAILED}")
  check("lookup works again after the error", out[5].start_with?("E\t0\t"))

  # Candidates read last to first agree with first to last; a buffer too
  # small for the candidate is refused.
  out = run_program(prog, ["open #{src}", "E おおい", "R おおい", "S かんじ", "close"])
  fwd = out[1].split("\t")[2..]
  rev = out[2].split("\t")[2..]
  check("200 candidates", fwd.size == 200)
  check("candidates read backwards", rev == fwd.reverse)
  check("small buffer refused", out[3] == "S\t#{ERR_INVALID_PARAM}")
end

Dir.mktmpdir("kanji_c") do |dir|
  prog, sanitized = build_program(dir)
  rng = Random.new(1)

  # 1. made-up dictionaries
  paths = write_inputs(dir)
  small = KanjiMake.run(paths: paths, font_list: File.join(dir, "font.txt"), size: 64 * 1024, out: File.join(dir, "small"))
  agree(prog, small[:out], "small (1 block)", rng, prefix_samples: 50)
  extra, kanji, = filler_inputs
  paths = write_inputs(dir, extra, kanji.uniq)
  full = KanjiMake.run(paths: paths, font_list: File.join(dir, "font.txt"), size: 1024 * 1024, out: File.join(dir, "full"))
  agree(prog, full[:out], "made-up (#{full[:header][:block_count]} blocks)", rng, prefix_samples: 200)

  # 2. broken files
  broken_files(prog, dir, full[:out])

  # 3. conversion lists
  conversions(prog, small[:out], "small (1 block)", rng, 40)
  conversions(prog, full[:out], "made-up", rng, 400)

  # 4. the generated dictionary
  if File.exist?(DICT)
    s = agree(prog, DICT, "generated", rng)
    conversions(prog, DICT, "generated", rng, 3000)
    k6_cases(prog, DICT)
    info = run_program(prog, ["open #{DICT}", "info", "close"])[1].split("\t")[1].split.map(&:to_i)
    puts format("generated dictionary: %d readings; exact %d queries (%d block reads, %.1f us each on the host); " \
                "longest prefix %d queries (%d block reads, %.2f per query, %.1f us each); held while open %d bytes",
                s[:readings], s[:exact], s[:e_reads], s[:e_us].to_f / s[:exact], s[:prefix], s[:p_reads],
                s[:p_reads].to_f / s[:prefix], s[:p_us].to_f / s[:prefix], info[4])
  else
    puts "generated dictionary: skipped (no #{DICT.sub("#{ROOT}/", '')}; `rake kanji:dict` makes it)"
  end
  puts "kanji C lookup: built #{sanitized ? 'with' : 'without'} AddressSanitizer/UBSan"
end

puts "kanji C lookup: #{$checks - $failures}/#{$checks} checks passed"
exit($failures.zero? ? 0 : 1)
