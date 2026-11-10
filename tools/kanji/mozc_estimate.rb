#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Estimate what a word dictionary built from mozc's open dictionary data
# (src/data/dictionary_oss/dictionary0*.txt and src/data/single_kanji/
# single_kanji.tsv) holds when it is cut down to the words one font can draw
# and to a byte budget. Measurement only (doc/kanji_input/ K0); the real
# generator is K1.
#
#   ruby tools/kanji/mozc_estimate.rb MOZC_SRC_DATA FONT_LIST [--budget BYTES]
#
#   MOZC_SRC_DATA  the mozc checkout's src/data directory
#   FONT_LIST      code points the font draws, as font_charset.rb --out writes
#
# Size models (bytes; the index is one 4-byte offset per reading):
#   utf8   reading and surfaces in UTF-8, a length byte each, a count byte
#   packed kana in one byte (hiragana/katakana/long mark), any other
#          character in two (an index into the font's table)
#
# Standard library only.

require "set"

module MozcEstimate
  module_function

  HIRA = (0x3041..0x3096).freeze
  KATA = (0x30A1..0x30FA).freeze
  READING_RE = /\A[ぁ-ゖー]+\z/

  def load_font(path)
    File.readlines(path).map { |l| l[/U\+(\h+)/, 1].hex }.to_set
  end

  def kanji?(c)
    (0x4E00..0x9FFF).cover?(c) || (0x3400..0x4DBF).cover?(c) ||
      (0xF900..0xFAFF).cover?(c) || c == 0x3005 || c == 0x3006 || c == 0x4EDD
  end

  # Proper nouns: 名詞,固有名詞,* in id.def.
  def proper_ids(data)
    ids = Set.new
    File.foreach(File.join(data, "dictionary_oss/id.def")) do |l|
      id, pos = l.split(" ", 2)
      ids << id.to_i if pos&.start_with?("名詞,固有名詞")
    end
    ids
  end

  def packed_len(str)
    str.each_codepoint.sum { |c| HIRA.cover?(c) || KATA.cover?(c) || c == 0x30FC ? 1 : 2 }
  end

  def entry_size(model, str)
    model == :utf8 ? str.bytesize + 1 : packed_len(str) + 1
  end

  # Fill a budget from the cheapest (most frequent) entry up.
  def fill(entries, budget, model)
    used = 0
    readings = Set.new
    taken = 0
    entries.each do |reading, surface, _cost|
      add = entry_size(model, surface)
      add += 4 + entry_size(model, reading) + 1 unless readings.include?(reading)
      break if used + add > budget
      used += add
      readings << reading
      taken += 1
    end
    [taken, readings.size, used]
  end

  def total_size(entries, model)
    fill(entries, Float::INFINITY, model)[2]
  end
end

if $PROGRAM_NAME == __FILE__
  args = ARGV.dup
  budget = 1_000_000
  if (i = args.index("--budget"))
    budget = args[i + 1].to_i
    args.slice!(i, 2)
  end
  data, font_path = args
  abort "usage: mozc_estimate.rb MOZC_SRC_DATA FONT_LIST [--budget BYTES]" unless data && font_path
  font = MozcEstimate.load_font(font_path)
  proper = MozcEstimate.proper_ids(data)

  raw = 0
  bad_reading = 0
  same_as_reading = 0
  undrawable = 0
  missing_chars = Hash.new(0)
  best = {} # [reading, surface] => [cost, proper?]
  Dir[File.join(data, "dictionary_oss/dictionary0*.txt")].sort.each do |f|
    File.foreach(f, encoding: "UTF-8") do |line|
      reading, lid, _rid, cost, surface = line.chomp.split("\t")
      next unless surface

      raw += 1
      unless reading.match?(MozcEstimate::READING_RE)
        bad_reading += 1
        next
      end
      if surface == reading
        same_as_reading += 1
        next
      end
      miss = surface.each_codepoint.reject { |c| font.include?(c) }
      unless miss.empty?
        undrawable += 1
        miss.each { |c| missing_chars[c] += 1 }
        next
      end
      key = [reading, surface]
      cost = cost.to_i
      pr = proper.include?(lid.to_i)
      if !best[key] || best[key][0] > cost
        best[key] = [cost, pr]
      end
    end
  end

  all = best.map { |(r, s), (c, pr)| [r, s, c, pr] }.sort_by { |r, s, c, _| [c, r, s] }
  common = all.reject { |e| e[3] }
  kata_only = all.count { |_, s, _, _| s.each_codepoint.all? { |c| MozcEstimate::KATA.cover?(c) || c == 0x30FC } }

  puts "mozc dictionary lines:            #{raw}"
  puts "  reading not plain hiragana:     #{bad_reading}"
  puts "  surface == reading (no-op):     #{same_as_reading}"
  puts "  surface not drawable by font:   #{undrawable}"
  puts "  distinct (reading, surface):    #{all.size} (proper nouns #{all.size - common.size}, katakana-only surfaces #{kata_only})"
  puts "  most missed characters:         " +
       missing_chars.sort_by { |_, n| -n }.first(30).map { |c, n| "#{[c].pack('U')}#{n}" }.join(" ")
  %i[utf8 packed].each do |model|
    [["all", all], ["no proper nouns", common]].each do |label, set|
      taken, nread, used = MozcEstimate.fill(set, budget, model)
      total = MozcEstimate.total_size(set, model)
      puts format("%-6s %-16s whole %9d B (%d words); within %d B: %d words, %d readings, %d B",
                  model, label, total, set.size, budget, taken, nread, used)
    end
  end

  # Single kanji: every kanji the font draws should be reachable from a reading.
  sk = Hash.new { |h, k| h[k] = [] }
  File.foreach(File.join(data, "single_kanji/single_kanji.tsv"), encoding: "UTF-8") do |line|
    reading, kanjis = line.chomp.split("\t")
    next unless kanjis && reading.match?(MozcEstimate::READING_RE)

    kanjis.each_char { |k| sk[k.ord] << reading }
  end
  dict_single = Set.new
  all.each { |r, s, _, _| dict_single << s.ord if s.length == 1 }
  font_kanji = font.select { |c| MozcEstimate.kanji?(c) }
  no_sk = font_kanji.reject { |c| sk.key?(c) }
  none = no_sk.reject { |c| dict_single.include?(c) }
  pairs = font_kanji.sum { |c| sk.key?(c) ? sk[c].uniq.size : 0 }
  sk_bytes = { utf8: 0, packed: 0 }
  by_reading = Hash.new { |h, k| h[k] = [] }
  font_kanji.each { |c| sk[c].uniq.each { |r| by_reading[r] << c } if sk.key?(c) }
  by_reading.each do |r, ks|
    sk_bytes[:utf8] += 4 + r.bytesize + 2 + ks.size * 4
    sk_bytes[:packed] += 4 + MozcEstimate.packed_len(r) + 2 + ks.size * 3
  end
  puts "font kanji:                       #{font_kanji.size}"
  puts "  in single_kanji.tsv:            #{font_kanji.size - no_sk.size} (#{pairs} reading pairs, #{by_reading.size} readings)"
  puts "  only as a 1-char dictionary word: #{no_sk.size - none.size}"
  puts "  no reading at all:              #{none.size} #{none.sort.first(40).map { |c| [c].pack('U') }.join}"
  puts "  single-kanji table size:        utf8 #{sk_bytes[:utf8]} B, packed #{sk_bytes[:packed]} B"
end
