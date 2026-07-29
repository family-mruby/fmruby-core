#!/usr/bin/env ruby
# Convert the launcher's .icon sources into the BMPs the sprite loader reads.
#
# The launcher used to parse the .icon text on the device and push the bitmap
# to the graphics side one pixel at a time as GFX commands, which cost seconds
# of boot. graphics-audio can decode a BMP itself (SpriteImage#load_bmp), so
# the artwork is converted here instead and shipped alongside the source.
#
# .icon is a 12x12 grid of '1' (set) and '.' (clear) plus a "color=0xNN"
# comment carrying the RGB332 colour. The BMP is written at SCALE so it comes
# out the size the launcher drew before (see LAUNCHER_ICON_W/H in
# system_desktop/launcher.rb: the old code scaled to fit the cell, which worked
# out to 2x for a 12x12 source).
#
# Pixel bytes are RGB332 values, not palette indices: the WROVER BMP loader
# ignores the palette and takes the byte as the colour directly. The palette is
# written anyway, expanded to RGB888, so the file also opens correctly in an
# ordinary image viewer.
#
# Usage: ruby tool/gen_icon_bmp.rb [icon_dir]

SCALE = 2
SET_CHAR = "1"
TRANSPARENT = 0  # matches SpriteImage transparent_color: 0 in the launcher

def parse_icon(path)
  color = 0xFF
  rows = []
  File.read(path).split("\n").each do |line|
    line = line.strip
    if line.start_with?("#")
      if (at = line.index("color="))
        token = line[(at + 6)..-1].to_s.strip.split(" ")[0].to_s
        color = token.start_with?("0x") ? token[2..-1].to_i(16) : token.to_i
      end
    elsif !line.empty?
      rows << line
    end
  end
  [rows, color]
end

# RGB332 byte -> BGR0 palette entry, so viewers show the real colour.
def palette_entry(v)
  r = ((v >> 5) & 0x07) * 255 / 7
  g = ((v >> 2) & 0x07) * 255 / 7
  b = (v & 0x03) * 255 / 3
  [b, g, r, 0].pack("C4")
end

def write_bmp(path, pixels)
  height = pixels.size
  width  = pixels[0].size
  pad = (4 - (width % 4)) % 4
  palette = (0..255).map { |v| palette_entry(v) }.join
  pixel_offset = 14 + 40 + palette.bytesize
  # BMP scanlines run bottom-up.
  body = (height - 1).downto(0).map { |y| pixels[y].pack("C*") + ("\0" * pad) }.join
  header = "BM".b + [pixel_offset + body.bytesize, 0, pixel_offset].pack("VVV")
  dib = [40, width, height].pack("Vll") +
        [1, 8].pack("vv") +
        [0, body.bytesize, 2835, 2835, 256, 0].pack("V6")
  File.binwrite(path, header + dib + palette + body)
end

def convert(icon_path)
  rows, color = parse_icon(icon_path)
  abort "#{icon_path}: no pixel rows" if rows.empty?
  width = rows[0].length
  abort "#{icon_path}: ragged rows" unless rows.all? { |r| r.length == width }

  pixels = []
  rows.each do |row|
    line = []
    row.each_char do |ch|
      v = (ch == SET_CHAR) ? color : TRANSPARENT
      SCALE.times { line << v }
    end
    SCALE.times { pixels << line.dup }
  end

  bmp_path = icon_path.sub(/\.icon\z/, ".bmp")
  write_bmp(bmp_path, pixels)
  puts "#{bmp_path}: #{pixels[0].size}x#{pixels.size} color=0x#{color.to_s(16).upcase}"
end

dir = ARGV[0] || "flash/usr/share/icon"
icons = Dir.glob("#{dir}/*.icon").sort
abort "no .icon files under #{dir}" if icons.empty?
icons.each { |p| convert(p) }
