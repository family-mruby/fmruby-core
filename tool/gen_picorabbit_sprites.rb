#!/usr/bin/env ruby
# Generate PicoRabbit's rabbit and turtle sprites from the pixel art below.
#
# THE FILES IN flash/usr/share/picorabbit ARE THE SOURCE OF TRUTH, NOT THIS
# SCRIPT. The art here is a first draft; the frames are meant to be redrawn by
# hand in the on-device sprite editor (launcher -> Sprite Editor -> open
# /usr/share/picorabbit/<name>.bmp -> edit -> S to save). So this script
# refuses to overwrite a file that already exists. Pass --force only when you
# mean to throw away what is on disk and go back to the draft.
#
# Format, fixed by what the sprite editor and the BMP loader accept:
#
#   * 16x16, one frame per file. The editor opens any BMP whose size is a
#     multiple of 16 as a tile sheet, so a single tile opens as one tile.
#   * 8-bit indexed BMP whose pixel bytes are RGB332 values. The loader
#     ignores the palette and uses the byte as the colour (same as
#     tool/gen_shooter_sprites.rb). The palette is written anyway, expanded
#     to RGB888, so the files open correctly in an ordinary image viewer.
#   * Transparent is 0x00, which the app passes as SpriteImage
#     transparent_color. Pure black is therefore unusable in the art; the
#     outline colour is 0x48 (dark olive) instead.
#   * Only the sprite editor's 16-colour palette is used. A colour outside it
#     cannot be picked again in the editor, so a frame repainted by hand could
#     never be put back the way it was.
#
# The rabbit and the turtle stand on the same strip, so every frame draws them
# facing right (towards the goal) and touching the same ground row, BASELINE.
# A frame whose feet sat one row higher would look like a 3px hop once the
# display scales the sprite by three.
#
# Usage: ruby tool/gen_picorabbit_sprites.rb [--force] [out_dir]
#        (default out_dir: flash/usr/share/picorabbit)

SIZE = 16
BASELINE = 14   # last row that may carry a foot; row 15 is always empty

TRANSPARENT = 0x00

# flash/app/tool/sprite_editor.app.rb SpriteEditorApp::PALETTE.
EDITOR_PALETTE = [
  0x00, 0xE0, 0x1C, 0x03, 0xFC, 0x1F, 0xE3, 0xFF,
  0x6D, 0xF0, 0x88, 0x14, 0x5F, 0xF4, 0x02, 0x48,
].freeze

# One character per colour, shared by both characters.
INK = {
  "o" => 0x48,  # outline (dark olive; 0x00 is the transparent key)
  "W" => 0xFF,  # rabbit fur
  "g" => 0x6D,  # rabbit fur, shaded
  "R" => 0xE0,  # rabbit nose / eye
  "P" => 0xF4,  # rabbit inner ear
  "B" => 0x88,  # turtle shell
  "F" => 0xF4,  # turtle shell, lit facets
  "L" => 0x1C,  # turtle skin
  "D" => 0x14,  # turtle skin, shaded
  "Y" => 0xFC,  # sleep marks
  "C" => 0x5F,  # sweat drop
}.freeze

# ---------------------------------------------------------------- turtle ----
# The clock. Two walking frames alternate once a second, and it throws both
# front legs up when the time runs out.

KAME_WALK1 = [
  "................",
  "................",
  "................",
  "...oooooo.......",
  "..oBBFFBBo.ooo..",
  ".oBBBBBBBBooLLLo",
  "oBFFBBBBBBBLLLLo",
  "oBBBBBBBBBBLLoLo",
  "oBBBBFFBBBo.LLLo",
  "oBBBBBBBBBo.ooo.",
  ".ooooooooo......",
  "..LLL..LLL......",
  "..LLL..LLL......",
  "..LLL..LLL......",
  "..ooo..ooo......",
  "................",
]

KAME_WALK2 = [
  "................",
  "................",
  "................",
  "...oooooo.......",
  "..oBBFFBBo.ooo..",
  ".oBBBBBBBBooLLLo",
  "oBFFBBBBBBBLLLLo",
  "oBBBBBBBBBBLLoLo",
  "oBBBBFFBBBo.LLLo",
  "oBBBBBBBBBo.ooo.",
  ".ooooooooo......",
  ".LLL....LLL.....",
  ".LLL....LLL.....",
  ".LLL....LLL.....",
  ".ooo....ooo.....",
  "................",
]

# Both front legs in the air, head thrown back. The rear legs stay on the
# ground so the frame keeps the same baseline as the walk.
KAME_BANZAI = [
  "..oo....oo......",
  ".oLLo..oLLo.....",
  ".oLLo..oLLo.ooo.",
  "..oLo..oLo.oLLLo",
  "...oooooo.oLLLLo",
  "..oBBFFBBo.LLoLo",
  ".oBBBBBBBBo.LLLo",
  "oBFFBBBBBBo.ooo.",
  "oBBBBBBBBBo.....",
  "oBBBBFFBBBo.....",
  ".ooooooooo......",
  "..LLL..LLL......",
  "..LLL..LLL......",
  "..LLL..LLL......",
  "..ooo..ooo......",
  "................",
]

# ---------------------------------------------------------------- rabbit ----
# The talk. Two running frames while a page has just turned, then one of the
# three standing poses: dozing when the talk is ahead of the clock, hurrying
# when it is behind, running on the spot otherwise.

USAGI_RUN1 = [
  ".........o..o...",
  "........oPo.oPo.",
  "........oPo.oPo.",
  "........oPoooPo.",
  ".......ooWWWWWo.",
  "......oWWWWWWWWo",
  "......oWoWWWWWRo",
  "...oooWWWWWWWWo.",
  "..oWWWWWWWWWoo..",
  ".oWWWWWWWWWo....",
  "oWgWWWWWWWWo....",
  "oWWWWWWWWWWo....",
  ".ooWWWooWWWo....",
  "..oWWo..oWWo....",
  "..ooo....ooo....",
  "................",
]

USAGI_RUN2 = [
  "................",
  ".......o...o....",
  "......oPo.oPo...",
  "......oPo.oPo...",
  ".......ooWWWWWo.",
  "......oWWWWWWWWo",
  "......oWoWWWWWRo",
  "...oooWWWWWWWWo.",
  "..oWWWWWWWWWoo..",
  ".oWWWWWWWWWo....",
  "oWgWWWWWWWWo....",
  "oWWWWWWWWWWo....",
  "oWWWoooWWWWo....",
  "oWWo.....oWWo...",
  "ooo.......ooo...",
  "................",
]

# Mid-hop: ears back, legs stretched out. The app lifts this frame with the
# jump offset, so it keeps the same baseline as the rest.
USAGI_JUMP = [
  "................",
  "................",
  "...o...o........",
  "..oPo.oPo.......",
  "..oPo.oPo.ooooo.",
  "...ooooooWWWWWWo",
  "..oWWWWWWWWoWWRo",
  ".oWWoWWWWWWWWWo.",
  "oWgWWWWWWWWWWo..",
  "oWWWWWWWWWWWo...",
  ".oWWWooWWWWo....",
  ".oWWo..oWWWo....",
  "oWWo.....oWWo...",
  "oWo.......oWo...",
  "ooo.......ooo...",
  "................",
]

# Ahead of the clock: sitting down, ears drooping, eye shut.
USAGI_SLEEP = [
  "............YY..",
  "...........Y....",
  "....oo....YYY...",
  "...oPPo.........",
  "..oPPo..oooooo..",
  "..oPo..oWWWWWWo.",
  "...o..oWWooWWWRo",
  "....ooWWWWWWWWo.",
  "...oWWWWWWWWWo..",
  "..oWWWWWWWWWo...",
  "..oWgWWWWWWWo...",
  ".oWWWWWWWWWWWo..",
  ".oWWWWWWWWWWWo..",
  "..oWWWWWWWWWo...",
  "..ooooooooooo...",
  "................",
]

# Behind the clock: leaning forward, ears pinned back, mouth open.
USAGI_HURRY = [
  "................",
  "................",
  "....oo.......C..",
  "...oPo......CCC.",
  "..oPo...ooooo.C.",
  ".oPo...oWWWWWo..",
  "..o...oWWoWWWWo.",
  "...oooWWWWWWWRo.",
  "..oWWWWWWWWWWo..",
  ".oWWWWWWWWWoo...",
  "oWgWWWWWWWo.....",
  "oWWWWWWWWo......",
  ".oWWoooWWo......",
  ".oWo...oWWo.....",
  ".ooo....ooo.....",
  "................",
]

FRAMES = [
  ["kame_walk1.bmp",  KAME_WALK1],
  ["kame_walk2.bmp",  KAME_WALK2],
  ["kame_banzai.bmp", KAME_BANZAI],
  ["usagi_run1.bmp",  USAGI_RUN1],
  ["usagi_run2.bmp",  USAGI_RUN2],
  ["usagi_jump.bmp",  USAGI_JUMP],
  ["usagi_sleep.bmp", USAGI_SLEEP],
  ["usagi_hurry.bmp", USAGI_HURRY],
].freeze

# RGB332 byte -> BGR0 palette entry, so viewers show the real colour.
def palette_entry(value)
  r = ((value >> 5) & 0x07) * 255 / 7
  g = ((value >> 2) & 0x07) * 255 / 7
  b = (value & 0x03) * 255 / 3
  [b, g, r, 0].pack("C4")
end

def to_pixels(rows, name)
  abort "#{name}: #{rows.size} rows, expected #{SIZE}" if rows.size != SIZE
  rows.each_with_index do |row, y|
    abort "#{name}: row #{y} is #{row.length} wide, expected #{SIZE}" if row.length != SIZE
    row.each_char do |ch|
      next if ch == "."
      abort "#{name}: row #{y} uses '#{ch}', which no ink defines" unless INK[ch]
    end
  end
  rows.each_with_index do |row, y|
    next if row.count(".") == SIZE
    abort "#{name}: row #{y} is below the baseline (#{BASELINE})" if y > BASELINE
  end
  abort "#{name}: nothing touches the baseline row #{BASELINE}" if rows[BASELINE].count(".") == SIZE
  rows.map { |row| row.each_char.map { |ch| ch == "." ? TRANSPARENT : INK[ch] } }
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

INK.each do |ch, value|
  next if EDITOR_PALETTE.include?(value)
  abort "ink '#{ch}' is 0x%02X, which the sprite editor palette does not hold" % value
end
abort "ink must not use 0x00; that is the transparent key" if INK.value?(TRANSPARENT)

args = ARGV.dup
force = args.delete("--force")
out_dir = args[0] || "flash/usr/share/picorabbit"

require "fileutils"
FileUtils.mkdir_p(out_dir)

written = 0
kept = 0
FRAMES.each do |name, rows|
  pixels = to_pixels(rows, name)
  path = File.join(out_dir, name)
  if File.exist?(path) && !force
    puts "#{path} exists, kept (--force to overwrite with the draft)"
    kept += 1
    next
  end
  write_bmp(path, pixels)
  puts "#{path} (#{SIZE}x#{SIZE})"
  written += 1
end
puts "#{written} written, #{kept} kept in #{out_dir}"
