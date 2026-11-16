#!/usr/bin/env ruby
# frozen_string_literal: true
#
# The Japanese font image for the ESP32-P4's font partition (doc/flash_layout
# F4). The efontJA fonts the P4 draws Japanese with (16 px, 12 px, 12 px bold)
# used to be linked into the firmware; they now sit in a partition of their
# own, written by a full flash and left alone by an app-only one. The firmware
# maps the partition and hands each font's bytes to LovyanGFX as they are
# (main/drivers/display_local/fonts/font_ja/), so the bytes here are exactly
# the arrays M5GFX ships (lgfx_efont_ja.c), declared size and all.
#
#   ruby tools/font_ja/font_ja_image.rb build SRC.c OUT.bin   # write OUT only when it changes
#   ruby tools/font_ja/font_ja_image.rb check OUT.bin         # header, directory, CRC
#   ruby tools/font_ja/font_ja_image.rb info OUT.bin          # one line: version, size, crc (for the flash check)
#
# The format (little endian), read by font_ja_image.c:
#
#   0   char[8]  magic "FMRBFONT"
#   8   u16      format version (VERSION)
#   10  u16      number of fonts
#   12  u32      image size in bytes (header + directory + data)
#   16  u32      CRC-32 (zlib's) of bytes 24 .. image size
#   20  u32      0
#   24  count x { char name[24] (NUL padded), u32 offset, u32 size }
#   ..  the fonts, each at an offset that is a multiple of 4
#
# The firmware checks the magic, the version and that every entry lies inside
# the image; it does not walk the CRC at boot (a megabyte through the flash
# cache on every start). The CRC is what `rake flash:app_sync` compares with
# the board to tell whether the partition holds this build's image.
#
# Standard library only.

require "zlib"

module FontJaImage
  MAGIC = "FMRBFONT".b
  VERSION = 1
  HEADER_SIZE = 24
  ENTRY_SIZE = 32
  NAME_MAX = 24

  # The fonts the P4 carries, as [name in the image, array in lgfx_efont_ja.c].
  # The names are what the firmware looks up (font_ja_esp32.cpp).
  FONTS = [
    ["efont_ja_16",   "lgfx_efont_ja_16"],
    ["efont_ja_12",   "lgfx_efont_ja_12"],
    ["efont_ja_12_b", "lgfx_efont_ja_12_b"],
  ].freeze

  module_function

  # The bytes of `name`'s initializer in a bdfconv C file: a run of string
  # literals with octal escapes, NUL padded to the declared size (the
  # literal's own terminator is part of the array when it fits).
  def array_bytes(src, name)
    m = src.match(/\b#{Regexp.escape(name)}\s*\[(\d+)\][^=;]*=\s*/)
    raise "array #{name} not found" unless m
    declared = m[1].to_i
    out = String.new(capacity: declared, encoding: Encoding::BINARY)
    pos = m.end(0)
    lit = /\G\s*"((?:[^"\\]|\\.)*)"/m
    while (lm = lit.match(src, pos))
      out << unescape(lm[1])
      pos = lm.end(0)
    end
    raise "#{name}: no string literal after the =" if out.empty?
    raise "#{name}: #{out.bytesize} bytes, more than the declared #{declared}" if out.bytesize > declared
    out << ("\0".b * (declared - out.bytesize))
    out
  end

  ESCAPES = { "n" => "\n", "t" => "\t", "r" => "\r", "a" => "\a", "b" => "\b",
              "f" => "\f", "v" => "\v" }.freeze

  def unescape(body)
    body.b.gsub(/\\([0-7]{1,3}|x\h+|.)/mn) do
      e = Regexp.last_match(1)
      if e.match?(/\A[0-7]/)
        (e.to_i(8) & 0xFF).chr
      elsif e.start_with?("x") && e.size > 1
        (e[1..].to_i(16) & 0xFF).chr
      else
        ESCAPES.fetch(e, e)
      end
    end
  end

  # The image of [[name, bytes], ...].
  def build(fonts)
    raise "no fonts" if fonts.empty?
    off = HEADER_SIZE + ENTRY_SIZE * fonts.size
    dir = String.new(encoding: Encoding::BINARY)
    data = String.new(encoding: Encoding::BINARY)
    fonts.each do |name, bytes|
      raise "name too long: #{name}" if name.bytesize >= NAME_MAX
      pad = (-(off + data.bytesize)) % 4
      data << ("\0".b * pad)
      dir << [name.b, off + data.bytesize, bytes.bytesize].pack("a#{NAME_MAX}VV")
      data << bytes.b
    end
    body = dir + data
    size = HEADER_SIZE + body.bytesize
    header = [MAGIC, VERSION, fonts.size, size, Zlib.crc32(body), 0].pack("a8vvVVV")
    header + body
  end

  # {version:, size:, crc:, fonts: [[name, offset, size], ...]}; raises on a
  # malformed image. Checks the CRC too (this is the host side).
  def parse(img)
    img = img.b
    raise "shorter than the header" if img.bytesize < HEADER_SIZE
    magic, version, count, size, crc, = img.unpack("a8vvVVV")
    raise "not a font image (magic #{magic.inspect})" unless magic == MAGIC
    raise "format version #{version}, this tool knows #{VERSION}" unless version == VERSION
    raise "image size #{size} past the file (#{img.bytesize})" if size > img.bytesize
    dir_end = HEADER_SIZE + ENTRY_SIZE * count
    raise "directory past the image" if dir_end > size
    actual = Zlib.crc32(img.byteslice(HEADER_SIZE, size - HEADER_SIZE))
    raise format("CRC %08x, the header says %08x", actual, crc) unless actual == crc
    fonts = Array.new(count) do |i|
      name, o, s = img.byteslice(HEADER_SIZE + ENTRY_SIZE * i, ENTRY_SIZE).unpack("Z#{NAME_MAX}VV")
      raise "#{name}: outside the image" if o < dir_end || o + s > size
      [name, o, s]
    end
    { version: version, size: size, crc: crc, fonts: fonts }
  end

  def font(img, name)
    _, o, s = parse(img)[:fonts].find { |n, _, _| n == name }
    o && img.b.byteslice(o, s)
  end

  # Build the image from M5GFX's lgfx_efont_ja.c and write it to `out` when it
  # differs from what is there. Returns [image, written?].
  def build_file(src_path, out)
    src = File.binread(src_path)
    img = build(FONTS.map { |name, array| [name, array_bytes(src, array)] })
    parse(img)
    return [img, false] if File.exist?(out) && File.binread(out) == img
    tmp = "#{out}.tmp"
    File.binwrite(tmp, img)
    File.rename(tmp, out)
    [img, true]
  end

  def describe(img)
    info = parse(img)
    lines = [format("font image v%d, %d bytes, crc %08x", info[:version], info[:size], info[:crc])]
    info[:fonts].each { |n, o, s| lines << format("  %-14s offset %7d  %7d bytes", n, o, s) }
    lines.join("\n")
  end
end

if $PROGRAM_NAME == __FILE__
  cmd = ARGV.shift
  case cmd
  when "build"
    src, out = ARGV
    abort "usage: font_ja_image.rb build SRC.c OUT.bin" unless src && out
    img, written = FontJaImage.build_file(src, out)
    puts "#{written ? 'wrote' : 'unchanged'} #{out}"
    puts FontJaImage.describe(img)
  when "check"
    file = ARGV.shift or abort "usage: font_ja_image.rb check IMAGE.bin"
    puts FontJaImage.describe(File.binread(file))
  when "info"
    file = ARGV.shift or abort "usage: font_ja_image.rb info IMAGE.bin"
    i = FontJaImage.parse(File.binread(file))
    puts format("version=%d size=%d crc=%08x", i[:version], i[:size], i[:crc])
  else
    abort "usage: font_ja_image.rb build SRC.c OUT.bin | check IMAGE.bin | info IMAGE.bin"
  end
end
