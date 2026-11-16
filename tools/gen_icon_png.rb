#!/usr/bin/env ruby
# Convert the launcher's .icon sources into the pictures the sprite loader reads.
#
# The launcher used to parse the .icon text on the device and push the bitmap
# to the graphics side one pixel at a time as GFX commands, which cost seconds
# of boot. The display side decodes a picture itself (SpriteImage#load_bmp), so
# the artwork is converted here instead and shipped alongside the source.
#
# .icon is a 12x12 grid of '1' (set) and '.' (clear) plus a "color=0xNN"
# comment carrying the RGB332 colour. The picture is written at SCALE so it comes
# out the size the launcher drew before (see LAUNCHER_ICON_W/H in
# system_desktop/launcher.rb: the old code scaled to fit the cell, which worked
# out to 2x for a 12x12 source).
#
# Written as a palette PNG of the icon's colours (tools/rgb332_png.rb): the
# sprite loader turns each colour back into the RGB332 value it came from, and
# at 100-odd bytes an icon fits inline in littlefs (no 4 KB block of its own).
#
# Usage: ruby tools/gen_icon_png.rb [icon_dir]

require_relative "rgb332_png"

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

  png_path = icon_path.sub(/\.icon\z/, ".png")
  size = Rgb332Png.write(png_path, pixels[0].size, pixels.size, pixels.flatten)
  puts "#{png_path}: #{pixels[0].size}x#{pixels.size} color=0x#{color.to_s(16).upcase} #{size} B"
end

dir = ARGV[0] || "flash/usr/share/icon"
icons = Dir.glob("#{dir}/*.icon").sort
abort "no .icon files under #{dir}" if icons.empty?
icons.each { |p| convert(p) }
