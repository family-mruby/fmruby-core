#!/usr/bin/env ruby
# Generate the shooter's sprite pictures (PNG) from the pixel art below.
#
# The art is the editable source: one string per row, one character per pixel,
# '.' transparent. Each sheet names a palette, so the same drawing can be
# emitted more than once in different colours (that is how the two enemy types
# and the boss are all made from one shape), and a scale, so the boss can be the
# same drawing blown up.
#
# The palette values are RGB332 colours, written as palette PNGs of the colours
# used (tools/rgb332_png.rb, same as tools/gen_icon_png.rb): the sprite loader
# turns each colour back into its RGB332 value, and the files open in any
# image viewer.
#
# Transparent pixels are written as 0x00, which is what the app passes as
# SpriteImage transparent_color. No visible colour here is 0x00.
#
# Widths are kept to a multiple of 4 (from the BMP days, when rows then needed
# no padding; kept so the sprite sizes stay as the app expects). The check runs
# after scaling.
#
# Usage: ruby tools/gen_shooter_sprites.rb [out_dir]   (default flash/usr/share/sprites/shooter)

require_relative "rgb332_png"

TRANSPARENT = 0

PAL_PLAYER = { "W" => 0xB6, "C" => 0x1F, "R" => 0xE0, "F" => 0xFC, "f" => 0xE8 }
PAL_ENEMY1 = { "E" => 0xEC, "Y" => 0xE0 }
PAL_ENEMY2 = { "E" => 0x9B, "Y" => 0xFC }
PAL_BULLET = { "Y" => 0xFC, "W" => 0xFF }
# The boss is the enemy drawing again, blown up five times, so it needs a
# palette of its own -- at that size the enemy colours read as a mistake
# rather than as a bigger enemy.
PAL_BOSS    = { "E" => 0xE9, "Y" => 0xFC }
PAL_EBULLET = { "R" => 0xE0, "W" => 0xFC }
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

# What the boss fires. Falls point down, so the bright core sits low.
EBULLET = [
  ".RR.",
  "RRRR",
  "RWWR",
  "RWWR",
  "RRRR",
  ".RR.",
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

# name, art, palette, and how many times each pixel is repeated. The boss is
# the only sheet that scales: 16x12 art at 5 becomes an 80x60 sheet, which is
# what the app expects for BOSS_W/BOSS_H.
BOSS_SCALE = 5

SHEETS = [
  ["player_a.png", PLAYER_A, PAL_PLAYER,  1],
  ["player_b.png", PLAYER_B, PAL_PLAYER,  1],
  ["enemy1_a.png", ENEMY_A,  PAL_ENEMY1,  1],
  ["enemy1_b.png", ENEMY_B,  PAL_ENEMY1,  1],
  ["enemy2_a.png", ENEMY_A,  PAL_ENEMY2,  1],
  ["enemy2_b.png", ENEMY_B,  PAL_ENEMY2,  1],
  ["boss_a.png",   ENEMY_A,  PAL_BOSS,    BOSS_SCALE],
  ["boss_b.png",   ENEMY_B,  PAL_BOSS,    BOSS_SCALE],
  ["bullet.png",   BULLET,   PAL_BULLET,  1],
  ["ebullet.png",  EBULLET,  PAL_EBULLET, 1],
  ["burst_a.png",  BURST_A,  PAL_BURST,   1],
  ["burst_b.png",  BURST_B,  PAL_BURST,   1],
]

def to_pixels(rows, palette, name)
  width = rows[0].length
  rows.each_with_index do |row, y|
    abort "#{name}: row #{y} is #{row.length} wide, expected #{width}" if row.length != width
    row.each_char do |ch|
      next if ch == "."
      abort "#{name}: row #{y} uses '#{ch}', which the palette does not define" unless palette[ch]
    end
  end
  rows.map { |row| row.each_char.map { |ch| ch == "." ? TRANSPARENT : palette[ch] } }
end

# Nearest-neighbour blow-up: every pixel becomes an n x n block, which is what
# keeps the boss looking like the same drawing rather than a blurred one.
def scale_pixels(pixels, n)
  return pixels if n <= 1
  out = []
  pixels.each do |row|
    wide = []
    row.each { |v| n.times { wide << v } }
    n.times { out << wide.dup }
  end
  out
end

out_dir = ARGV[0] || "flash/usr/share/sprites/shooter"
require "fileutils"
FileUtils.mkdir_p(out_dir)

SHEETS.each do |name, rows, palette, scale|
  pixels = scale_pixels(to_pixels(rows, palette, name), scale)
  unless (pixels[0].size % 4).zero?
    abort "#{name}: width #{pixels[0].size} is not a multiple of 4"
  end
  path = File.join(out_dir, name)
  Rgb332Png.write(path, pixels[0].size, pixels.size, pixels.flatten)
  puts "#{path} (#{pixels[0].size}x#{pixels.size})"
end
puts "#{SHEETS.size} sprites written to #{out_dir}"
