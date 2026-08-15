#!/usr/bin/env ruby
# Generate the shooter's sprite BMPs from the pixel art below.
#
# The art is the editable source: one string per row, one character per pixel,
# '.' transparent. Each sheet names a palette, so the same drawing can be
# emitted twice in different colours (that is how the two enemy types are made
# from one shape).
#
# Pixel bytes are RGB332 values, not palette indices: the BMP loader on the
# graphics side ignores the palette and takes the byte as the colour directly
# (same as tool/gen_icon_bmp.rb). The palette is written anyway, expanded to
# RGB888, so the files also open correctly in an ordinary image viewer.
#
# Transparent pixels are written as 0x00, which is what the app passes as
# SpriteImage transparent_color. No visible colour here is 0x00.
#
# Widths are kept to a multiple of 4 so BMP rows need no padding; the loader is
# only exercised with unpadded rows elsewhere.
#
# Usage: ruby tool/gen_shooter_sprites.rb [out_dir]   (default flash/usr/share/sprites/shooter)

TRANSPARENT = 0

PAL_PLAYER = { "W" => 0xB6, "C" => 0x1F, "R" => 0xE0, "F" => 0xFC, "f" => 0xE8 }
PAL_ENEMY1 = { "E" => 0xEC, "Y" => 0xE0 }
PAL_ENEMY2 = { "E" => 0x9B, "Y" => 0xFC }
PAL_BULLET = { "Y" => 0xFC, "W" => 0xFF }
PAL_BURST  = { "W" => 0xFF, "F" => 0xFC, "O" => 0xE8 }

# W body, C cockpit, R trim, F/f exhaust. Two frames: the exhaust flickers.
PLAYER_A = [
  ".......WW.......",
  ".......WW.......",
  "......WWWW......",
  "......WCCW......",
  ".....WWCCWW.....",
  ".....WWCCWW.....",
  "....WWWWWWWW....",
  "...WWWWWWWWWW...",
  "..WWRWWWWWWRWW..",
  "..WWRWWWWWWRWW..",
  ".WW.RWWWWWWR.WW.",
  ".W..RRWWWWRR..W.",
  "....WW....WW....",
  "....FF....FF....",
  ".....f....f.....",
  "................",
]
PLAYER_B = [
  ".......WW.......",
  ".......WW.......",
  "......WWWW......",
  "......WCCW......",
  ".....WWCCWW.....",
  ".....WWCCWW.....",
  "....WWWWWWWW....",
  "...WWWWWWWWWW...",
  "..WWRWWWWWWRWW..",
  "..WWRWWWWWWRWW..",
  ".WW.RWWWWWWR.WW.",
  ".W..RRWWWWRR..W.",
  "....WW....WW....",
  "....FF....FF....",
  "....FF....FF....",
  ".....f....f.....",
]

# E body, Y eyes. Two frames: the wings beat.
ENEMY_A = [
  ".....EE..EE.....",
  "......EEEE......",
  "....EEEEEEEE....",
  "...EEYYEEYYEE...",
  "..EEEEEEEEEEEE..",
  ".EE.EEEEEEEE.EE.",
  "EE...EEEEEE...EE",
  "E.....EEEE.....E",
  "......EEEE......",
  ".....E.EE.E.....",
  "....E......E....",
  "...E........E...",
]
ENEMY_B = [
  "................",
  ".....EE..EE.....",
  "....EEEEEEEE....",
  "...EEYYEEYYEE...",
  "..EEEEEEEEEEEE..",
  "..E.EEEEEEEE.E..",
  ".E...EEEEEE...E.",
  "......EEEE......",
  ".....EEEEEE.....",
  "....EE.EE.EE....",
  "...E...EE...E...",
  "..E..........E..",
]

BULLET = [
  ".YY.",
  ".WW.",
  ".WW.",
  "YWWY",
  ".YY.",
  ".YY.",
]

BURST_A = [
  "................",
  "................",
  "................",
  "................",
  ".....O....O.....",
  "......OFFO......",
  ".....OFWWFO.....",
  "....OFWWWWFO....",
  "....OFWWWWFO....",
  ".....OFWWFO.....",
  "......OFFO......",
  ".....O....O.....",
  "................",
  "................",
  "................",
  "................",
]
BURST_B = [
  "................",
  "..O..........O..",
  "................",
  "....O..OO..O....",
  "...O.FF..FF.O...",
  "....FF....FF....",
  "..O.F......F.O..",
  "...O........O...",
  "...O........O...",
  "..O.F......F.O..",
  "....FF....FF....",
  "...O.FF..FF.O...",
  "....O..OO..O....",
  "................",
  "..O..........O..",
  "................",
]

SHEETS = [
  ["player_a.bmp", PLAYER_A, PAL_PLAYER],
  ["player_b.bmp", PLAYER_B, PAL_PLAYER],
  ["enemy1_a.bmp", ENEMY_A,  PAL_ENEMY1],
  ["enemy1_b.bmp", ENEMY_B,  PAL_ENEMY1],
  ["enemy2_a.bmp", ENEMY_A,  PAL_ENEMY2],
  ["enemy2_b.bmp", ENEMY_B,  PAL_ENEMY2],
  ["bullet.bmp",   BULLET,   PAL_BULLET],
  ["burst_a.bmp",  BURST_A,  PAL_BURST],
  ["burst_b.bmp",  BURST_B,  PAL_BURST],
]

# RGB332 byte -> BGR0 palette entry, so viewers show the real colour.
def palette_entry(v)
  r = ((v >> 5) & 0x07) * 255 / 7
  g = ((v >> 2) & 0x07) * 255 / 7
  b = (v & 0x03) * 255 / 3
  [b, g, r, 0].pack("C4")
end

def to_pixels(rows, palette, name)
  width = rows[0].length
  rows.each_with_index do |row, y|
    abort "#{name}: row #{y} is #{row.length} wide, expected #{width}" if row.length != width
    row.each_char do |ch|
      next if ch == "."
      abort "#{name}: row #{y} uses '#{ch}', which the palette does not define" unless palette[ch]
    end
  end
  abort "#{name}: width #{width} is not a multiple of 4" unless (width % 4).zero?
  rows.map { |row| row.each_char.map { |ch| ch == "." ? TRANSPARENT : palette[ch] } }
end

def write_bmp(path, pixels)
  height = pixels.size
  width  = pixels[0].size
  palette = (0..255).map { |v| palette_entry(v) }.join
  pixel_offset = 14 + 40 + palette.bytesize
  # BMP scanlines run bottom-up.
  body = (height - 1).downto(0).map { |y| pixels[y].pack("C*") }.join
  header = "BM".b + [pixel_offset + body.bytesize, 0, pixel_offset].pack("VVV")
  dib = [40, width, height].pack("Vll") +
        [1, 8].pack("vv") +
        [0, body.bytesize, 2835, 2835, 256, 0].pack("V6")
  File.binwrite(path, header + dib + palette + body)
end

out_dir = ARGV[0] || "flash/usr/share/sprites/shooter"
require "fileutils"
FileUtils.mkdir_p(out_dir)

SHEETS.each do |name, rows, palette|
  pixels = to_pixels(rows, palette, name)
  path = File.join(out_dir, name)
  write_bmp(path, pixels)
  puts "#{path} (#{pixels[0].size}x#{pixels.size})"
end
puts "#{SHEETS.size} sprites written to #{out_dir}"
