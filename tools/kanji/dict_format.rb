# frozen_string_literal: true
#
# The kanji dictionary file (doc/kanji_input/dict_format.md): the character
# packing, the header, and a reader. Shared by the generator (make_dict.rb),
# the reference lookup (lookup.rb) and the tests.
#
# Standard library only.

module KanjiDict
  MAGIC = "FMKD"
  VERSION = 1
  HEADER_SIZE = 64
  BLOCK_SIZE = 4096

  # One-byte codes: hiragana U+3041..U+3096 -> 0x01..0x56, katakana
  # U+30A1..U+30FA -> 0x57..0xB0, the long vowel mark U+30FC -> 0xB1.
  HIRA_FIRST = 0x3041
  HIRA_LAST = 0x3096
  KATA_FIRST = 0x30A1
  KATA_LAST = 0x30FA
  CHOON = 0x30FC
  HIRA_BASE = 0x01
  KATA_BASE = 0x57
  CHOON_CODE = 0xB1
  # Two-byte codes: lead 0xC0..0xFF, index into the character table =
  # (lead - 0xC0) << 8 | second byte.
  LEAD_BASE = 0xC0
  MAX_CHARS = 64 * 256

  module_function

  def kana_code(cp)
    if cp >= HIRA_FIRST && cp <= HIRA_LAST
      HIRA_BASE + (cp - HIRA_FIRST)
    elsif cp >= KATA_FIRST && cp <= KATA_LAST
      KATA_BASE + (cp - KATA_FIRST)
    elsif cp == CHOON
      CHOON_CODE
    end
  end

  def kana_cp(code)
    if code >= HIRA_BASE && code < KATA_BASE
      HIRA_FIRST + (code - HIRA_BASE)
    elsif code >= KATA_BASE && code < CHOON_CODE
      KATA_FIRST + (code - KATA_BASE)
    elsif code == CHOON_CODE
      CHOON
    end
  end

  # Packed length of a string: kana 1 byte, anything else 2.
  def packed_len(str)
    str.each_codepoint.sum { |c| kana_code(c) ? 1 : 2 }
  end

  # Pack a string; `index` maps a code point to its character table index.
  def pack(str, index)
    out = String.new(encoding: Encoding::BINARY)
    str.each_codepoint do |c|
      if (k = kana_code(c))
        out << k.chr
      else
        i = index.fetch(c) { raise "character U+#{c.to_s(16).upcase} not in the table" }
        out << (LEAD_BASE + (i >> 8)).chr << (i & 0xFF).chr
      end
    end
    out
  end

  def unpack(bytes, chars)
    b = bytes.bytes
    out = +""
    i = 0
    while i < b.size
      c = b[i]
      if c >= LEAD_BASE
        out << [chars.fetch(((c - LEAD_BASE) << 8) | b[i + 1])].pack("U")
        i += 2
      else
        cp = kana_cp(c) or raise format("bad code 0x%02X", c)
        out << [cp].pack("U")
        i += 1
      end
    end
    out
  end

  # The candidate count: one byte below 0x80, else two (big-endian, top bit set).
  def count_bytes(n)
    raise "too many candidates (#{n})" if n >= 0x8000

    n < 0x80 ? n.chr.b : ((0x80 | (n >> 8)).chr + (n & 0xFF).chr).b
  end

  def count_len(n)
    n < 0x80 ? 1 : 2
  end

  # Header fields in order; all little-endian.
  HEADER_FIELDS = [
    [:magic, "a4"],
    [:version, "v"],
    [:header_size, "v"],
    [:file_size, "V"],
    [:chars_offset, "V"],
    [:char_count, "v"],
    [:flags, "v"],
    [:index_offset, "V"],
    [:index_size, "V"],
    [:block_count, "V"],
    [:body_offset, "V"],
    [:block_size, "V"],
    [:reading_count, "V"],
    [:word_count, "V"],
    [:single_count, "V"],
    [:crc32, "V"],
    [:reserved0, "V"],
    [:reserved1, "V"]
  ].freeze
  HEADER_PACK = HEADER_FIELDS.map(&:last).join

  def pack_header(h)
    s = HEADER_FIELDS.map { |k, _| h.fetch(k, 0) }.pack(HEADER_PACK)
    raise "header is #{s.bytesize} bytes" if s.bytesize != HEADER_SIZE

    s
  end

  def unpack_header(bytes)
    HEADER_FIELDS.map(&:first).zip(bytes.unpack(HEADER_PACK)).to_h
  end

  # Reads a dictionary file the way the board will: the header, the character
  # table and the block index up front, then one block per lookup.
  class Reader
    attr_reader :header, :chars, :keys

    def initialize(path)
      @io = File.open(path, "rb")
      @header = KanjiDict.unpack_header(@io.read(HEADER_SIZE) || "")
      h = @header
      raise "#{path}: not a kanji dictionary" unless h[:magic] == MAGIC
      raise "#{path}: format version #{h[:version]}, this reader knows #{VERSION}" unless h[:version] == VERSION
      raise "#{path}: file size #{File.size(path)} != header #{h[:file_size]}" unless File.size(path) == h[:file_size]

      consistent = h[:header_size] == HEADER_SIZE && h[:block_size] == BLOCK_SIZE &&
                   h[:index_offset] == h[:chars_offset] + h[:char_count] * 2 &&
                   h[:index_offset] + h[:index_size] <= h[:body_offset] &&
                   h[:body_offset] + h[:block_count] * h[:block_size] == h[:file_size]
      raise "#{path}: inconsistent header" unless consistent

      @io.seek(h[:chars_offset])
      @chars = @io.read(h[:char_count] * 2).unpack("v*")
      @io.seek(h[:index_offset])
      idx = @io.read(h[:index_size])
      offs = idx.unpack("V#{h[:block_count]}")
      @keys = offs.map { |o| idx.byteslice(o + 1, idx.getbyte(o)) }
      @index = @chars.each_with_index.to_h
      @reads = 0
    end

    attr_reader :reads

    def close
      @io.close
    end

    def pack(str)
      KanjiDict.pack(str, @index)
    rescue RuntimeError
      nil
    end

    # The block that would hold `key` (packed): the last block whose first
    # reading is <= key.
    def block_for(key)
      lo = 0
      hi = @keys.size - 1
      return nil if hi.negative? || key < @keys[0]

      while lo < hi
        mid = (lo + hi + 1) / 2
        if @keys[mid] <= key
          lo = mid
        else
          hi = mid - 1
        end
      end
      lo
    end

    def read_block(n)
      @reads += 1
      @io.seek(@header[:body_offset] + n * @header[:block_size])
      @io.read(@header[:block_size])
    end

    # Entries of one block as [reading_bytes, [candidate_bytes...]].
    def self.entries(block)
      out = []
      pos = 0
      while pos < block.bytesize
        rlen = block.getbyte(pos)
        break if rlen.zero?

        reading = block.byteslice(pos + 1, rlen)
        pos += 1 + rlen
        n = block.getbyte(pos)
        if n >= 0x80
          n = ((n & 0x7F) << 8) | block.getbyte(pos + 1)
          pos += 2
        else
          pos += 1
        end
        cands = Array.new(n) do
          clen = block.getbyte(pos)
          c = block.byteslice(pos + 1, clen)
          pos += 1 + clen
          c
        end
        out << [reading, cands]
      end
      out
    end

    # Candidates (UTF-8 strings) for a reading, or [] when there are none.
    def lookup(reading)
      key = pack(reading)
      return [] unless key

      b = block_for(key)
      return [] unless b

      Reader.entries(read_block(b)).each do |r, cands|
        return cands.map { |c| KanjiDict.unpack(c, @chars) } if r == key
        break if r > key
      end
      []
    end

    # The longest leading part of `reading` that has candidates:
    # [prefix, candidates], or nil.
    def longest_prefix(reading)
      reading.length.downto(1) do |n|
        p = reading[0, n]
        c = lookup(p)
        return [p, c] unless c.empty?
      end
      nil
    end

    # Every entry in file order, decoded: [reading, [candidates]].
    def each_entry
      return enum_for(:each_entry) unless block_given?

      @header[:block_count].times do |n|
        Reader.entries(read_block(n)).each do |r, cands|
          yield KanjiDict.unpack(r, @chars), cands.map { |c| KanjiDict.unpack(c, @chars) }, r, n
        end
      end
    end
  end
end
