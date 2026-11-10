#!/usr/bin/env ruby
# frozen_string_literal: true
#
# List the Unicode code points a u8g2 font (as emitted by bdfconv into a C
# array, the form LovyanGFX / M5GFX carry misaki and efont in) has glyphs for.
#
#   ruby tools/kanji/font_charset.rb FILE.c ARRAY_NAME [--out list.txt]
#   ruby tools/kanji/font_charset.rb --list FILE.c      # array names in FILE
#
# Prints a summary (total glyphs, CJK unified ideographs, kana, ...) and with
# --out writes one code point per line ("U+XXXX<TAB>char").
#
# Standard library only. The C file is read as text: the array body is a run
# of string literals with octal escapes, decoded here to the raw bytes.

module FontCharset
  module_function

  # Bytes of `name`'s initializer in a bdfconv C file.
  def array_bytes(src, name)
    start = src.index(/\b#{Regexp.escape(name)}\s*\[\d*\][^=]*=/)
    raise "array #{name} not found" unless start
    body_start = src.index("=", start) + 1
    body_end = src.index(";", body_start)
    # A ';' can sit inside a literal; walk literal by literal instead.
    pos = body_start
    out = String.new(encoding: Encoding::BINARY)
    loop do
      pos += 1 while src[pos] =~ /\s/
      break unless src[pos] == '"'
      pos += 1
      while src[pos] != '"'
        c = src[pos]
        if c == "\\"
          n = src[pos + 1]
          if n =~ /[0-7]/
            digits = src[pos + 1, 3][/\A[0-7]{1,3}/]
            out << digits.to_i(8).chr
            pos += 1 + digits.size
          elsif n == "x"
            hex = src[pos + 2..][/\A[0-9a-fA-F]+/]
            out << hex.to_i(16).chr
            pos += 2 + hex.size
          else
            out << ({ "n" => "\n", "t" => "\t", "r" => "\r", "a" => "\a",
                      "b" => "\b", "f" => "\f", "v" => "\v" }[n] || n)
            pos += 2
          end
        else
          out << c.b
          pos += 1
        end
      end
      pos += 1
    end
    _ = body_end
    out
  end

  # Code points of a u8g2 font blob.
  def codepoints(font)
    b = font.bytes
    # The C literal's terminating NUL is part of the blob in the firmware but
    # not of the decoded bytes, so read past the end as zero.
    word = ->(i) { ((b[i] || 0) << 8) | (b[i + 1] || 0) }
    cps = []
    # Glyphs 0..255: [encoding][jump][data...], jump 0 ends the list.
    pos = 23
    while b[pos + 1] != 0
      cps << b[pos]
      pos += b[pos + 1]
    end
    upos = 23 + word.call(21)
    # Unicode part: a lookup table (offset, last encoding) ending in 0xffff,
    # whose first offset leads past the table to the glyph run.
    gpos = upos + word.call(upos)
    loop do
      e = word.call(gpos)
      break if e.zero?
      cps << e
      gpos += b[gpos + 2]
    end
    cps
  end

  CLASSES = [
    ["CJK unified (U+4E00-9FFF)", 0x4E00..0x9FFF],
    ["CJK ext A (U+3400-4DBF)", 0x3400..0x4DBF],
    ["CJK compat (U+F900-FAFF)", 0xF900..0xFAFF],
    ["hiragana (U+3040-309F)", 0x3040..0x309F],
    ["katakana (U+30A0-30FF)", 0x30A0..0x30FF],
    ["CJK symbols (U+3000-303F)", 0x3000..0x303F],
    ["fullwidth forms (U+FF00-FFEF)", 0xFF00..0xFFEF],
    ["ASCII (U+0020-007E)", 0x20..0x7E]
  ].freeze

  def summary(cps)
    lines = ["glyphs: #{cps.size}"]
    CLASSES.each do |label, range|
      lines << format("  %-32s %6d", label, cps.count { |c| range.cover?(c) })
    end
    lines.join("\n")
  end
end

if $PROGRAM_NAME == __FILE__
  args = ARGV.dup
  if args[0] == "--list"
    File.read(args[1], mode: "rb").scan(/const uint8_t (\w+)\[\d*\]/) { puts Regexp.last_match(1) }
    exit
  end
  out = nil
  if (i = args.index("--out"))
    out = args[i + 1]
    args.slice!(i, 2)
  end
  file, name = args
  abort "usage: font_charset.rb FILE.c ARRAY_NAME [--out list.txt]" unless file && name
  cps = FontCharset.codepoints(FontCharset.array_bytes(File.read(file, mode: "rb"), name))
  puts "#{name}: #{FontCharset.summary(cps)}"
  if out
    File.open(out, "w") do |f|
      cps.sort.each { |c| f.puts format("U+%04X\t%s", c, [c].pack("U")) }
    end
  end
end
