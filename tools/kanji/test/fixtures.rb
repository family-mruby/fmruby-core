# frozen_string_literal: true
#
# Made-up inputs for the kanji dictionary tests (run.rb, c_lookup.rb):
# a tiny mozc tree, a font list and the filler words that make a dictionary
# of several blocks. Nothing is fetched.

require_relative "../make_dict"

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

# Filler words that spread the dictionary over several blocks, and one
# reading with 200 candidates (the two-byte count). Returns [lines, kanji, n]
# where n is the number of filler words (not counting the 200).
def filler_inputs
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
  [extra, kanji, i]
end
