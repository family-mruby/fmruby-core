# frozen_string_literal: true
#
# Tests of the kanji dictionary generator and reader (doc/kanji_input/ K1)
# on small made-up inputs written to a temporary directory: no source data
# is fetched. Run by `rake kanji:test` (and `rake test`).

require "tmpdir"
require "fileutils"
require_relative "../make_dict"
require_relative "fixtures"

$failures = 0
$checks = 0

def check(label, cond)
  $checks += 1
  return if cond

  $failures += 1
  puts "FAIL: #{label}"
end

def eq(label, got, want)
  check("#{label}: got #{got.inspect}, want #{want.inspect}", got == want)
end

# typeable
eq("typeable かんじ", KanjiMake.typeable("かんじ"), :ok)
eq("typeable きゃく", KanjiMake.typeable("きゃく"), :ok)
eq("typeable しぇ", KanjiMake.typeable("しぇる"), :ok)
eq("typeable いっ", KanjiMake.typeable("いっ"), :sokuon_end)
eq("typeable いった", KanjiMake.typeable("いった"), :ok)
eq("typeable ってぃ", KanjiMake.typeable("てぃー"), :no)
eq("typeable っあ", KanjiMake.typeable("っあ"), :no)
eq("typeable っん", KanjiMake.typeable("かっん"), :no)
eq("typeable ふぁ", KanjiMake.typeable("ふぁん"), :no)
eq("typeable ゔ", KanjiMake.typeable("ゔぃ"), :no)

# packing
idx = { "漢".ord => 0, "字".ord => 300 }
eq("pack kana", KanjiDict.pack("あア", idx).bytes, [0x02, 0x58])
eq("pack long mark", KanjiDict.pack("ー", idx).bytes, [0xB1])
eq("pack 2-byte", KanjiDict.pack("漢字", idx).bytes, [0xC0, 0x00, 0xC1, 0x2C])
eq("unpack", KanjiDict.unpack(KanjiDict.pack("ぁゖァヺー漢字", idx), [ "漢".ord ] + [0] * 299 + ["字".ord]), "ぁゖァヺー漢字")
eq("count 1 byte", KanjiDict.count_bytes(127).bytes, [127])
eq("count 2 bytes", KanjiDict.count_bytes(300).bytes, [0x81, 0x2C])

Dir.mktmpdir("kanji_test") do |dir|
  paths = write_inputs(dir)
  res = build(dir, paths, 64 * 1024)
  dic = res[:out]
  r = KanjiDict::Reader.new(dic)
  h = r.header
  eq("magic", h[:magic], "FMKD")
  eq("version", h[:version], 1)
  eq("file size", File.size(dic), h[:file_size])
  eq("body aligned", h[:body_offset] % 4096, 0)
  eq("blocks", h[:block_count], 1)
  eq("all words in (budget is ample)", h[:word_count], 11)
  eq("かんじ order by cost, then single kanji none", r.lookup("かんじ"), %w[感じ 漢字 幹事])
  eq("へんかん", r.lookup("へんかん"), %w[変換 返還])
  eq("いっ kept", r.lookup("いっ"), %w[言っ 行っ])
  eq("てぃっしゅ dropped", r.lookup("てぃっしゅ"), [])
  eq("longest prefix いった", r.longest_prefix("いった"), ["いっ", %w[言っ 行っ]])
  eq("longest prefix none", r.longest_prefix("ぬ"), nil)
  eq("not in the dictionary", r.lookup("ぬぬぬ"), [])
  eq("katakana surface", r.lookup("せんせい"), %w[先生 センセイ])
  eq("single kanji かん", r.lookup("かん"), %w[漢 感 幹 換 還])
  eq("Unihan fills 亠", r.lookup("なべぶた"), %w[亠])
  eq("Unihan on reading (katakana to hiragana)", r.lookup("とう"), %w[亠])
  eq("untypeable Unihan reading dropped", r.lookup("ふぃーと"), [])
  eq("symbol fills 々", r.lookup("くりかえし"), %w[々])
  eq("symbol fills 〆", r.lookup("しめ"), %w[〆])
  eq("lost kanji", res[:verify][:lost].map { |c| [c].pack("U") }, %w[呎])
  eq("undrawable", res[:verify][:undrawable], 0)
  check("LICENSE carries mozc", File.read(File.join(dir, "out/LICENSE")).include?("MOZC LICENSE TEXT"))
  check("LICENSE carries Unicode", File.read(File.join(dir, "out/LICENSE")).include?("UNICODE LICENSE V3"))
  r.close

  # The budget cuts by cost: 2 blocks of body can hold everything, 1 cannot
  # once many filler words are added.
  extra, kanji, i = filler_inputs
  paths = write_inputs(dir, extra, kanji.uniq)
  full = build(dir, paths, 1024 * 1024)
  eq("full: every word", full[:header][:word_count], 11 + i + 200)
  check("full: more than one block", full[:header][:block_count] > 1)
  r = KanjiDict::Reader.new(full[:out])
  eq("200 candidates read back", r.lookup("おおい").size, 200)
  # Every filler word looks up through the block index.
  ok = true
  r.each_entry { |rd, cands, _, _| ok &&= (r.lookup(rd) == cands) }
  check("every entry looks up to itself", ok)
  blocks = full[:header][:block_count]
  r.close

  # A budget one block smaller than the full file drops the costliest words.
  cut = build(dir, paths, full[:header][:file_size] - 4096)
  check("cut: fewer words", cut[:header][:word_count] < full[:header][:word_count])
  check("cut: within budget", cut[:header][:file_size] <= full[:header][:file_size] - 4096)
  eq("cut: one block fewer at most", cut[:header][:block_count] <= blocks - 1, true)
  r = KanjiDict::Reader.new(cut[:out])
  eq("cut: cheap words stay", r.lookup("かんじ"), %w[感じ 漢字 幹事])
  eq("cut: single kanji stay", r.lookup("なべぶた"), %w[亠])
  r.close

  # A budget below the single-kanji table is an error, not a smaller table.
  begin
    build(dir, paths, 4096)
    check("tiny budget raises", false)
  rescue RuntimeError => e
    check("tiny budget raises (#{e.message})", e.message.include?("over the budget"))
  end
end

puts "kanji dictionary: #{$checks - $failures}/#{$checks} checks passed"
exit($failures.zero? ? 0 : 1)
