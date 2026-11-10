#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Generate the kanji dictionary file (doc/kanji_input/ K1; the file format is
# doc/kanji_input/dict_format.md).
#
#   ruby tools/kanji/make_dict.rb [--size BYTES] [--work DIR] [--out DIR]
#                                 [--font FILE.c ARRAY | --font-list LIST]
#                                 [--no-fetch]
#
# Inputs (sources.rb pins and fetches them into the work directory): mozc's
# open dictionary, its single-kanji and symbol tables, and Unihan's Japanese
# readings. The base font (misaki 8 by default) decides which words may go
# in: every character of a candidate must have a glyph. Readings the kana
# composer cannot type are left out, except a trailing small tsu (the stem
# forms 言っ, 良かっ), which a longest-prefix lookup reaches.
#
# Words go in from the cheapest mozc cost up until the file would pass the
# size budget. The single-kanji table (every kanji the font draws, at least
# one reading each) and the character table always go in.
#
# Output: OUT/fmrb_kanji.dic and OUT/LICENSE.
#
# Standard library only.

require "set"
require "fileutils"
require "zlib"
require_relative "dict_format"
require_relative "font_charset"
require_relative "sources"

module KanjiMake
  ROOT = File.expand_path("../..", __dir__)
  DEFAULT_FONT = [File.join(ROOT, "main/drivers/display_local/fonts/misaki/lgfx_misaki.c"), "lgfx_misaki_8"].freeze
  DEFAULT_OUT = File.join(KanjiSources::DEFAULT_WORK, "out")
  DEFAULT_SIZE = 1024 * 1024
  DIC_NAME = "fmrb_kanji.dic"

  READING_RE = /\A[ぁ-ゖー]+\z/
  # Kana the composer (main/drivers/usb/fmrb_kana.c) can put in front of a
  # small ya/yu/yo/i/e: the i-column of every row that takes "y".
  I_DAN = "きしちにひみりぎじぢびぴ"
  NEVER = "ぁぅぉゎゔゐゑゕゖ"
  # What can follow a small tsu: a kana whose romaji starts with a doubled
  # consonant (not n, not a vowel).
  AFTER_SOKUON = "かきくけこさしすせそたちつてとはひふへほまみむめもやゆよらりるれろわをがぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽっ"

  module_function

  # :ok, :sokuon_end (typeable but for the final small tsu), or :no.
  def typeable(reading)
    ch = reading.chars
    ch.each_with_index do |c, i|
      prev = i.positive? ? ch[i - 1] : nil
      nxt = ch[i + 1]
      return :no if NEVER.include?(c)

      case c
      when "ぃ", "ぇ", "ゃ", "ゅ", "ょ"
        return :no unless prev && I_DAN.include?(prev)
      when "っ"
        if nxt.nil?
          return :sokuon_end
        elsif !AFTER_SOKUON.include?(nxt)
          return :no
        end
      end
    end
    :ok
  end

  def kanji?(cp)
    (0x4E00..0x9FFF).cover?(cp) || (0x3400..0x4DBF).cover?(cp) ||
      (0xF900..0xFAFF).cover?(cp) || cp == 0x3005 || cp == 0x3006
  end

  def kata_to_hira(str)
    str.each_codepoint.map { |c| c >= 0x30A1 && c <= 0x30F6 ? c - 0x60 : c }.pack("U*")
  end

  # efontJA sizes the configurations use (12, 12 bold, 16).
  EFONT_ARRAYS = %w[lgfx_efont_ja_12 lgfx_efont_ja_12_b lgfx_efont_ja_16].freeze

  def font_set(file, name)
    FontCharset.codepoints(FontCharset.array_bytes(File.read(file, mode: "rb"), name)).to_set
  end

  # The base character set: by default misaki 8 narrowed to what efontJA
  # also draws, i.e. what every configuration can draw.
  def load_font(opts, paths)
    if opts[:font_list]
      File.readlines(opts[:font_list]).map { |l| l[/U\+(\h+)/, 1].hex }.to_set
    elsif opts[:font]
      font_set(*opts[:font])
    else
      set = font_set(*DEFAULT_FONT)
      src = File.read(paths[:efont], mode: "rb")
      EFONT_ARRAYS.each { |a| set &= FontCharset.codepoints(FontCharset.array_bytes(src, a)).to_set }
      set
    end
  end

  # Read the inputs and decide every candidate. Returns a hash the layout and
  # the writer work from.
  def collect(paths, font)
    stats = Hash.new(0)
    best = {} # [reading, surface] => cost
    one_char = Hash.new { |h, k| h[k] = [] } # kanji cp => readings (dictionary 1-char words)
    paths[:dictionaries].each do |f|
      File.foreach(f, encoding: "UTF-8") do |line|
        reading, _lid, _rid, cost, surface = line.chomp.split("\t")
        next unless surface

        stats[:lines] += 1
        unless reading.match?(READING_RE)
          stats[:bad_reading] += 1
          next
        end
        if surface == reading
          stats[:same_as_reading] += 1
          next
        end
        unless surface.each_codepoint.all? { |c| font.include?(c) && c <= 0xFFFF }
          stats[:undrawable] += 1
          next
        end
        t = typeable(reading)
        if t == :no
          stats[:untypeable] += 1
          next
        end
        stats[:sokuon_end] += 1 if t == :sokuon_end
        cost = cost.to_i
        key = [reading, surface]
        best[key] = cost if !best[key] || best[key] > cost
        one_char[surface.ord] << reading if surface.length == 1 && kanji?(surface.ord)
      end
    end
    words = best.map { |(r, s), c| [r, s, c] }.sort_by { |r, s, c| [c, r, s] }
    stats[:pairs] = words.size

    singles, single_stats = single_table(paths, font, one_char)
    { words: words, singles: singles, stats: stats, single_stats: single_stats,
      font_kanji: font.select { |c| kanji?(c) }.sort }
  end

  # reading => [kanji (string), ...] in mozc's order, covering every kanji of
  # the font. Sources in turn: single_kanji.tsv, the dictionary's 1-char words,
  # Unihan's kJapanese, mozc's symbol table.
  def single_table(paths, font, one_char)
    font_kanji = font.select { |c| kanji?(c) }.to_set
    table = Hash.new { |h, k| h[k] = [] }
    covered = Set.new
    st = Hash.new(0)
    add = lambda do |reading, cp, source|
      return false unless reading.match?(READING_RE) && typeable(reading) != :no

      k = [cp].pack("U")
      unless table[reading].include?(k)
        table[reading] << k
        st[:pairs] += 1
      end
      st[source] += 1 unless covered.include?(cp)
      covered << cp
      true
    end

    File.foreach(paths[:single_kanji], encoding: "UTF-8") do |line|
      reading, kanjis = line.chomp.split("\t")
      next unless kanjis

      kanjis.each_codepoint { |cp| add.call(reading, cp, :single_kanji_tsv) if font_kanji.include?(cp) }
    end

    missing = -> { font_kanji.reject { |c| covered.include?(c) } }
    missing.call.each do |cp|
      one_char[cp].uniq.each { |r| add.call(r, cp, :dictionary_1char) }
    end

    unihan = Hash.new { |h, k| h[k] = [] }
    File.foreach(paths[:unihan_readings], encoding: "UTF-8") do |line|
      next unless line.start_with?("U+")

      code, field, value = line.chomp.split("\t")
      next unless field == "kJapanese"

      unihan[code[2..].hex].concat(value.split(" "))
    end
    missing.call.each do |cp|
      unihan[cp].each { |r| add.call(kata_to_hira(r), cp, :unihan) }
    end

    symbols = Hash.new { |h, k| h[k] = [] }
    File.foreach(paths[:symbol], encoding: "UTF-8") do |line|
      cols = line.chomp.split("\t")
      next unless cols[1] && cols[1].length == 1 && cols[2]

      symbols[cols[1].ord].concat(cols[2].split(" "))
    end
    missing.call.each do |cp|
      symbols[cp].each { |r| add.call(r, cp, :symbol) }
    end

    st[:missing] = missing.call.size
    st[:missing_list] = missing.call.map { |c| [c].pack("U") }.join
    [table, st]
  end

  # reading => candidates for the first n words plus the single-kanji table.
  def entries_for(data, n)
    e = Hash.new { |h, k| h[k] = [] }
    data[:words].first(n).each { |r, s, _| e[r] << s }
    data[:singles].each do |r, ks|
      list = e[r]
      ks.each { |k| list << k unless list.include?(k) }
    end
    e
  end

  def entry_size(reading, cands)
    1 + KanjiDict.packed_len(reading) + KanjiDict.count_len(cands.size) +
      cands.sum { |c| 1 + KanjiDict.packed_len(c) }
  end

  # Lay entries out in blocks. Returns [sorted [reading, cands, size]], block
  # start indexes, character list.
  def layout(entries)
    sorted = entries.map { |r, c| [r, c, entry_size(r, c)] }
    # Packed order is code point order (all readings are hiragana and the
    # long vowel mark, packed one byte each in code point order).
    sorted.sort_by! { |r, _, _| r.codepoints }
    starts = []
    used = KanjiDict::BLOCK_SIZE
    sorted.each_with_index do |(r, _, size), i|
      raise "entry #{r} is #{size} bytes, more than a block" if size > KanjiDict::BLOCK_SIZE

      if used + size > KanjiDict::BLOCK_SIZE
        starts << i
        used = 0
      end
      used += size
    end
    chars = Set.new
    sorted.each { |_, cands, _| cands.each { |c| c.each_codepoint { |cp| chars << cp unless KanjiDict.kana_code(cp) } } }
    [sorted, starts, chars.to_a.sort]
  end

  def front_size(sorted, starts, chars)
    index = starts.size * 4 + starts.sum { |i| 1 + KanjiDict.packed_len(sorted[i][0]) }
    raw = KanjiDict::HEADER_SIZE + chars.size * 2 + index
    bs = KanjiDict::BLOCK_SIZE
    (raw + bs - 1) / bs * bs
  end

  def file_size(data, n)
    sorted, starts, chars = layout(entries_for(data, n))
    front_size(sorted, starts, chars) + starts.size * KanjiDict::BLOCK_SIZE
  end

  # The largest word count that fits the budget.
  def fit(data, budget)
    raise "the single-kanji table alone (#{file_size(data, 0)} B) is over the budget #{budget} B" if file_size(data, 0) > budget

    total = data[:words].size
    return total if file_size(data, total) <= budget

    # A lower bound on the size of n words is the packed bytes alone; it gives
    # an upper bound on n so the search does not lay out the whole source.
    hi = 0
    acc = 0
    seen = Set.new
    data[:words].each do |r, s, _|
      acc += 1 + KanjiDict.packed_len(s)
      acc += 2 + KanjiDict.packed_len(r) if seen.add?(r)
      break if acc > budget

      hi += 1
    end
    lo = 0
    while lo < hi
      mid = (lo + hi + 1) / 2
      if file_size(data, mid) <= budget
        lo = mid
      else
        hi = mid - 1
      end
    end
    lo
  end

  def write(path, data, n)
    sorted, starts, chars = layout(entries_for(data, n))
    raise "#{chars.size} characters, the table holds #{KanjiDict::MAX_CHARS}" if chars.size > KanjiDict::MAX_CHARS
    raise "a character beyond the BMP" if chars.any? { |c| c > 0xFFFF }

    index = chars.each_with_index.to_h
    bs = KanjiDict::BLOCK_SIZE
    body = String.new(encoding: Encoding::BINARY)
    block = String.new(encoding: Encoding::BINARY)
    keys = []
    flush = lambda do
      body << block << ("\0" * (bs - block.bytesize))
      block = String.new(encoding: Encoding::BINARY)
    end
    starts_set = starts.to_set
    sorted.each_with_index do |(r, cands, _), i|
      if starts_set.include?(i)
        flush.call unless i.zero?
        keys << KanjiDict.pack(r, index)
      end
      pr = KanjiDict.pack(r, index)
      raise "reading #{r} is #{pr.bytesize} bytes" if pr.bytesize > 0x7F

      block << pr.bytesize.chr << pr << KanjiDict.count_bytes(cands.size)
      cands.each do |c|
        pc = KanjiDict.pack(c, index)
        raise "candidate #{c} is #{pc.bytesize} bytes" if pc.bytesize > 0xFF

        block << pc.bytesize.chr << pc
      end
    end
    flush.call unless sorted.empty?

    char_bytes = chars.pack("v*")
    key_area = String.new(encoding: Encoding::BINARY)
    offs = []
    base = keys.size * 4
    keys.each do |k|
      offs << base + key_area.bytesize
      key_area << k.bytesize.chr << k
    end
    index_bytes = offs.pack("V*") + key_area
    chars_offset = KanjiDict::HEADER_SIZE
    index_offset = chars_offset + char_bytes.bytesize
    front = char_bytes + index_bytes
    body_offset = (KanjiDict::HEADER_SIZE + front.bytesize + bs - 1) / bs * bs
    pad = "\0" * (body_offset - KanjiDict::HEADER_SIZE - front.bytesize)
    rest = front + pad + body
    header = {
      magic: KanjiDict::MAGIC, version: KanjiDict::VERSION, header_size: KanjiDict::HEADER_SIZE,
      file_size: KanjiDict::HEADER_SIZE + rest.bytesize,
      chars_offset: chars_offset, char_count: chars.size,
      index_offset: index_offset, index_size: index_bytes.bytesize,
      block_count: keys.size, body_offset: body_offset, block_size: bs,
      reading_count: sorted.size, word_count: n,
      single_count: data[:singles].sum { |_, ks| ks.size },
      crc32: Zlib.crc32(rest)
    }
    FileUtils.mkdir_p(File.dirname(path))
    File.binwrite(path, KanjiDict.pack_header(header) + rest)
    header
  end

  # Read the written file back and check it against what went in: every
  # entry round-trips, readings ascend, every candidate is drawable, every
  # font kanji has a reading.
  def verify(path, data, n, font)
    r = KanjiDict::Reader.new(path)
    h = r.header
    rest = File.binread(path, nil, KanjiDict::HEADER_SIZE)
    raise "crc32 mismatch" unless Zlib.crc32(rest) == h[:crc32]

    expect = entries_for(data, n)
    prev = nil
    count = 0
    undrawable = 0
    reached = Set.new
    r.each_entry do |reading, cands, packed, blk|
      raise "readings out of order at #{reading}" if prev && prev >= packed
      raise "block #{blk} key mismatch" if r.keys[blk] > packed

      prev = packed
      count += 1
      raise "entry #{reading} differs" unless expect[reading] == cands

      cands.each do |c|
        undrawable += 1 unless c.each_codepoint.all? { |cp| font.include?(cp) }
        reached << c.ord if c.length == 1
      end
    end
    raise "#{count} readings read back, #{expect.size} written" unless count == expect.size

    lost = data[:font_kanji].reject { |c| reached.include?(c) }
    # Spot check: every reading looks up to its own candidates.
    expect.keys.each_slice(97).map(&:first).each do |k|
      raise "lookup #{k} failed" unless r.lookup(k) == expect[k]
    end
    r.close
    { readings: count, undrawable: undrawable, lost: lost }
  end

  def license_text(paths, header, data)
    mozc = File.read(paths[:mozc_license], encoding: "UTF-8")
    unicode = File.read(paths[:unicode_license], encoding: "UTF-8")
    <<~TEXT
      #{DIC_NAME}: the kanji dictionary of Family mruby
      ==================================================

      This file is generated from the following data. The notices each of
      them requires are reproduced below, unchanged.

      * mozc (commit #{KanjiSources::MOZC_COMMIT}):
        src/data/dictionary_oss/dictionary00.txt .. dictionary09.txt,
        src/data/single_kanji/single_kanji.tsv, src/data/symbol/symbol.tsv.
        The mozc license (BSD 3-clause, Google), and for src/data/dictionary*
        the IPAdic notice (NAIST, including the ICOT Free Software conditions
        and Section 3 "NO WARRANTY") and the Okinawa dictionary
        (public domain): see "mozc LICENSE" below.
      * Unihan #{KanjiSources::UNIHAN_VERSION} (Unihan_Readings.txt, the kJapanese field),
        for the readings of #{data[:single_stats][:unihan]} kanji: see "Unicode License" below.

      Words: #{header[:word_count]}, readings: #{header[:reading_count]}, file size: #{header[:file_size]} bytes.

      ----------------------------------------------------------------------
      mozc LICENSE
      ----------------------------------------------------------------------

      #{mozc.rstrip}

      ----------------------------------------------------------------------
      Unicode License
      ----------------------------------------------------------------------

      #{unicode.rstrip}
    TEXT
  end

  def run(opts)
    paths = opts[:paths] || (opts[:fetch] ? KanjiSources.fetch(opts[:work]) : KanjiSources.layout(opts[:work]))
    font = load_font(opts, paths)
    t0 = Time.now
    data = collect(paths, font)
    n = fit(data, opts[:size])
    out = File.join(opts[:out], DIC_NAME)
    header = write(out, data, n)
    v = verify(out, data, n, font)
    File.write(File.join(opts[:out], "LICENSE"), license_text(paths, header, data))
    { header: header, data: data, verify: v, out: out, seconds: Time.now - t0 }
  end

  def report(res, budget)
    h = res[:header]
    d = res[:data]
    st = d[:stats]
    ss = d[:single_stats]
    words = d[:words]
    n = h[:word_count]
    lines = []
    lines << "#{res[:out]}: #{h[:file_size]} B (budget #{budget} B), #{h[:block_count]} blocks of #{h[:block_size]} B"
    lines << "  words #{n} of #{words.size} (cost cutoff #{n.positive? ? words[n - 1][2] : '-'}), readings #{h[:reading_count]}, characters #{h[:char_count]}"
    lines << "  source lines #{st[:lines]}: reading not hiragana #{st[:bad_reading]}, same as reading #{st[:same_as_reading]}, " \
             "not drawable #{st[:undrawable]}, not typeable #{st[:untypeable]} (kept with a final small tsu: #{st[:sokuon_end]})"
    lines << "  single-kanji table: #{d[:font_kanji].size} font kanji, #{ss[:pairs]} reading pairs; " \
             "from single_kanji.tsv #{ss[:single_kanji_tsv]}, 1-char words #{ss[:dictionary_1char]}, Unihan #{ss[:unihan]}, symbols #{ss[:symbol]}"
    lines << "  font kanji with no reading: #{res[:verify][:lost].size}#{res[:verify][:lost].empty? ? '' : ' ' + res[:verify][:lost].map { |c| [c].pack('U') }.join}"
    lines << "  candidates the font cannot draw: #{res[:verify][:undrawable]}"
    lines << format("  (%.1f s)", res[:seconds])
    lines.join("\n")
  end
end

def parse_size(s)
  m = s.to_s.strip.match(/\A(\d+(?:\.\d+)?)\s*([kKmM]?)i?[bB]?\z/) or abort "bad size: #{s}"
  mult = { "" => 1, "k" => 1024, "m" => 1024 * 1024 }[m[2].downcase]
  (m[1].to_f * mult).to_i
end

if $PROGRAM_NAME == __FILE__
  opts = { size: KanjiMake::DEFAULT_SIZE, work: KanjiSources::DEFAULT_WORK, out: KanjiMake::DEFAULT_OUT,
           fetch: true }
  args = ARGV.dup
  until args.empty?
    a = args.shift
    case a
    when "--size" then opts[:size] = parse_size(args.shift)
    when "--work" then opts[:work] = File.expand_path(args.shift)
    when "--out" then opts[:out] = File.expand_path(args.shift)
    when "--font" then opts[:font] = [args.shift, args.shift]
    when "--font-list" then opts[:font_list] = args.shift
    when "--no-fetch" then opts[:fetch] = false
    else abort "usage: make_dict.rb [--size BYTES] [--work DIR] [--out DIR] [--font FILE.c ARRAY | --font-list LIST] [--no-fetch]"
    end
  end
  res = KanjiMake.run(opts)
  puts KanjiMake.report(res, opts[:size])
  ok = res[:verify][:lost].empty? && res[:verify][:undrawable].zero?
  exit(ok ? 0 : 1)
end
