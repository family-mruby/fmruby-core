# frozen_string_literal: true
#
# Tests of the kanji dictionary generator and reader (doc/kanji_input/ K1)
# on small made-up inputs written to a temporary directory: no source data
# is fetched. Run by `rake kanji:test` (and `rake test`).

require "tmpdir"
require "fileutils"
require_relative "../make_dict"

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

KANA = ("ぁ".."ゖ").to_a + ("ァ".."ヺ").to_a + ["ー"]
FONT_KANJI = %w[漢 字 感 幹 事 変 換 返 還 言 行 一 学 校 日 本 語 亠 々 〆 呎 生 先 明].freeze

# A made-up mozc tree: dictionary lines are reading, left id, right id,
# cost, surface.
def write_inputs(dir, extra_lines = [], extra_kanji = [])
  dic = File.join(dir, "dictionary00.txt")
  lines = [
    "かんじ\t1\t1\t100\t漢字",
    "かんじ\t1\t1\t50\t感じ",
    "かんじ\t2\t2\t40\t感じ", # same pair, cheaper: the cheaper cost wins
    "かんじ\t1\t1\t300\t幹事",
    "へんかん\t1\t1\t200\t変換",
    "へんかん\t1\t1\t9000\t返還", # over the small budget's cutoff
    "いっ\t1\t1\t10\t言っ",      # final small tsu: kept
    "いっ\t1\t1\t20\t行っ",
    "がっこう\t1\t1\t30\t学校",
    "にほんご\t1\t1\t60\t日本語",
    "てぃっしゅ\t1\t1\t5\tティッシュ", # てぃ cannot be typed: dropped
    "ゔぁ\t1\t1\t5\tヴァ",               # ゔ: dropped
    "かんじ\t1\t1\t5\tかんじ",           # same as the reading: dropped
    "かんじ\t1\t1\t5\t漢児",             # 児 not in the font: dropped
    "abc\t1\t1\t5\tエービーシー",        # reading not hiragana: dropped
    "せんせい\t1\t1\t70\t先生",
    "せんせい\t1\t1\t80\tセンセイ"
  ] + extra_lines
  File.write(dic, lines.join("\n") + "\n")
  File.write(File.join(dir, "single_kanji.tsv"), [
    "かん\t漢感幹換還",
    "じ\t字事",
    "こう\t校行",
    "ふぃーと\t呎", # untypeable: 呎 has to come from Unihan
    "いち\t一",
    "がく\t学",
    "ご\t語",
    "ほん\t本",
    "にち\t日",
    "へん\t変返",
    "げん\t言",
    "せん\t先",
    "せい\t生",
    "めい\t明"
  ].join("\n") + "\n")
  File.write(File.join(dir, "symbol.tsv"),
             "POS\tCHAR\tReading\n記号\t々\tおなじ くりかえし\t\n記号\t〆\tしめ\t\n")
  File.write(File.join(dir, "Unihan_Readings.txt"),
             "# test\nU+4EA0\tkJapanese\tトウ なべぶた\nU+544E\tkJapanese\tフィート\nU+544E\tkJapaneseOn\tX\n")
  File.write(File.join(dir, "LICENSE"), "MOZC LICENSE TEXT\n")
  File.write(File.join(dir, "license.txt"), "UNICODE LICENSE V3\nTEXT\n")
  font = KANA + FONT_KANJI + extra_kanji + %w[A B C]
  File.write(File.join(dir, "font.txt"), font.map { |c| format("U+%04X\t%s\n", c.ord, c) }.join)
  {
    mozc_license: File.join(dir, "LICENSE"),
    dictionaries: [dic],
    single_kanji: File.join(dir, "single_kanji.tsv"),
    symbol: File.join(dir, "symbol.tsv"),
    unihan_readings: File.join(dir, "Unihan_Readings.txt"),
    unicode_license: File.join(dir, "license.txt")
  }
end

def build(dir, paths, size)
  KanjiMake.run(paths: paths, font_list: File.join(dir, "font.txt"), size: size, out: File.join(dir, "out"))
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
  extra = []
  kanji = []
  # Filler words: reading of 3 kana, surface of 2 kanji from CJK, costs 1000+.
  base = 0x4E00
  i = 0
  ("か".."ご").each do |a|
    ("さ".."ぞ").each do |b|
      ("な".."の").each do |c|
        k1 = [base + (i % 500)].pack("U")
        k2 = [base + 500 + (i % 300)].pack("U")
        kanji << k1 << k2
        extra << "#{a}#{b}#{c}\t1\t1\t#{1000 + i}\t#{k1}#{k2}"
        i += 1
      end
    end
  end
  # A reading with many candidates exercises the two-byte count.
  many = (0...200).map { |j| [0x5000 + j].pack("U") }
  kanji.concat(many)
  many.each_with_index { |k, j| extra << "おおい\t1\t1\t#{10 + j}\t#{k}" }
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
