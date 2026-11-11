# frozen_string_literal: true
#
# The kana composer (main/drivers/usb/fmrb_kana.c, doc/kanji_input K6) with
# the host cc: the IME spellings and the meanings kept from before, and that
# the dictionary generator's typeable (make_dict.rb) agrees with what the
# composer can type -- every kana it lets into the dictionary has a
# spelling, and the ones it keeps out have none. Run by `rake kanji:test`.

require "tmpdir"
require "open3"
require_relative "../make_dict"

ROOT = File.expand_path("../../..", __dir__)
$failures = 0
$checks = 0

def check(label, cond)
  $checks += 1
  return if cond

  $failures += 1
  puts "FAIL: #{label}"
end

# [mode, keys, what the app sees] (1 hiragana, 2 katakana; '_' is the final
# space, '<' Backspace, '!' Esc).
CASES = [
  # IME spellings
  [1, "xa", "ぁ_"], [1, "li", "ぃ_"], [1, "xu", "ぅ_"], [1, "le", "ぇ_"], [1, "xo", "ぉ_"],
  [1, "xtu", "っ_"], [1, "ltu", "っ_"], [1, "xtsu", "っ_"], [1, "ltsu", "っ_"],
  [1, "xya", "ゃ_"], [1, "lyu", "ゅ_"], [1, "xyo", "ょ_"], [1, "xwa", "ゎ_"], [1, "lwa", "ゎ_"],
  [1, "xka", "ヵ_"], [1, "xke", "ヶ_"],
  [1, "thi", "てぃ_"], [1, "thu", "てゅ_"], [1, "dhi", "でぃ_"], [1, "dhu", "でゅ_"],
  [1, "twu", "とぅ_"], [1, "dwu", "どぅ_"],
  [1, "fa", "ふぁ_"], [1, "fi", "ふぃ_"], [1, "fu", "ふ_"], [1, "fe", "ふぇ_"], [1, "fo", "ふぉ_"], [1, "fyu", "ふゅ_"],
  [1, "va", "ゔぁ_"], [1, "vi", "ゔぃ_"], [1, "vu", "ゔ_"], [1, "ve", "ゔぇ_"], [1, "vo", "ゔぉ_"],
  [1, "she", "しぇ_"], [1, "je", "じぇ_"], [1, "che", "ちぇ_"],
  [1, "tsa", "つぁ_"], [1, "tsi", "つぃ_"], [1, "tse", "つぇ_"], [1, "tso", "つぉ_"],
  [1, "wi", "うぃ_"], [1, "we", "うぇ_"], [1, "whi", "うぃ_"],
  [1, "kwa", "くぁ_"], [1, "gwa", "ぐぁ_"], [1, "qa", "くぁ_"], [1, "qi", "くぃ_"], [1, "qe", "くぇ_"], [1, "qo", "くぉ_"],
  [1, "pa-thi-", "ぱーてぃー_"], [1, "fairu", "ふぁいる_"], [1, "ffa", "っふぁ_"], [1, "Xa", "ぁ_"],
  [2, "pa-thi-", "パーティー_"], [2, "vu", "ヴ_"], [2, "xtu", "ッ_"], [2, "xa", "ァ_"], [2, "xka", "ヵ_"],
  # Kept as they were
  [1, "kanji", "かんじ_"], [1, "kya", "きゃ_"], [1, "shi", "し_"], [1, "chi", "ち_"], [1, "tsu", "つ_"],
  [1, "ja", "じゃ_"], [1, "kka", "っか_"], [1, "nn", "ん_"], [1, "wu", "_"], [1, "ye", "_"], [1, "ca", "_"],
  [1, "x", "x_"], [1, "xk", "x_"], [1, "xka-", "ヵー_"], [1, "xx", "xx_"], [1, "xyz", "x_"], [1, "vvu", "vゔ_"],
  [1, "X1", "X1_"], [1, "xt!", "x!_"], [1, "x<", "_"], [1, "xt<a", "ぁ_"], [1, "th<a", "あ_"], [1, "thk", "_"],
  [1, "lt", "l_"], [1, "q", "q_"], [1, "qq", "qq_"], [1, "kwk", "_"], [1, "tha", "てゃ_"], [1, "dho", "でょ_"]
].freeze

def build(dir)
  cc = ENV["CC"] || "cc"
  abort "#{cc} not found (install a C compiler or set CC)" unless system("which #{cc} > /dev/null 2>&1")
  out = File.join(dir, "kana_c_test")
  incs = %w[main/drivers/usb components/fmrb_common/include tools/kanji/test/c/stub]
  cmd = "#{cc} -std=gnu11 -O1 -Wall -Wextra -Werror -DFMRB_PLATFORM_HOSTED " \
        "#{incs.map { |i| "-I #{i}" }.join(' ')} main/drivers/usb/fmrb_kana.c tools/kanji/test/c/kana_main.c -o #{out}"
  Dir.chdir(ROOT) { system(cmd) or abort "kana C test: compile failed" }
  out
end

Dir.mktmpdir("kana_test") do |dir|
  prog = build(dir)
  input = CASES.map { |m, k, _| "#{m} #{k}" }.join("\n") + "\nALL 4\n"
  out, err, st = Open3.capture3(prog, stdin_data: input)
  check("driver exits cleanly (#{st.inspect} #{err[0, 200]})", st.success? && err.empty?)
  lines = out.force_encoding("UTF-8").lines.map(&:chomp)
  CASES.each_with_index do |(m, k, want), i|
    check("#{m == 2 ? 'katakana' : 'hiragana'} #{k}: got #{lines[i].inspect}, want #{want.inspect}", lines[i] == want)
  end

  # Every kana the composer types from up to four letters, on its own.
  typed = {}
  lines.drop(CASES.size).each do |l|
    romaji, text = l.split("\t")
    kana = text.to_s.delete_suffix("_")
    typed[kana] ||= romaji if kana.length == 1
  end
  ((0x3041..0x3096).map { |c| [c].pack("U") } + ["ー"]).each do |c|
    ok = KanjiMake.typeable(c) == :ok
    check("#{c}: the dictionary #{ok ? 'takes' : 'keeps out'} it, the composer #{typed[c] ? "types it (#{typed[c]})" : 'has no spelling'}",
          ok == !typed[c].nil?)
  end
end

puts "kana composer: #{$checks - $failures}/#{$checks} checks passed"
exit($failures.zero? ? 0 : 1)
