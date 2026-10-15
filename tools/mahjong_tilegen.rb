#!/usr/bin/env ruby
# frozen_string_literal: true
#
# mahjong_tilegen.rb - Pixel-art riichi mahjong tile generator (pure Ruby, no gems)
#
# Generates PNG tiles drawn directly at their final pixel size, so they stay
# crisp on small screens (e.g. Family mruby on ESP32-P4 at 640x360).
#
#   ruby mahjong_tilegen.rb [OUT_DIR]
#   ruby mahjong_tilegen.rb --sheets DIR [--force]
#
# Output (OUT_DIR):
#   OUT_DIR/hand/   42x56 tiles  (hand: 14 + 1 tiles across 640 px)
#   OUT_DIR/river/  24x32 tiles  (discards, same pictures drawn small)
#   OUT_DIR/preview.png          (all tiles, 3x, for checking)
#
# Output (--sheets DIR): the sprite sheets Family mruby loads
# (SpriteImage#load_bmp + FmrbGfx#draw_tile), 8-bit BMPs whose pixel values
# are RGB332 colours (the palette is the RGB332 decode, so a paint program
# shows the right colours; Family mruby reads the index as the colour):
#   DIR/tiles_hand_0.bmp   210x224  m and p, 42x56 cells
#   DIR/tiles_hand_1.bmp   210x224  s and z (and back), 42x56 cells
#   DIR/tiles_river.bmp    240x128  every tile, 24x32 cells
#   DIR/sheets_preview.png          the three sheets at 3x, for checking
# Grids without gaps: the cell of tile number i (TILES below) is
#   hand:  sheet i / 20, k = i % 20, src_x = (k % 5) * 42, src_y = (k / 5) * 56
#   river: src_x = (i % 10) * 24, src_y = (i / 10) * 32
# Tile numbers: 0..8 1m..9m, 9 0m, 10..18 1p..9p, 19 0p, 20..28 1s..9s, 29 0s,
# 30..36 1z..7z, 37 back (the rest of the last row is empty).
# The transparent colour (rounded corners, the empty cells) is RGB332 0xE3
# (magenta, TRANSPARENT below); no tile colour rounds to it.
#
# **The sheets are the source of truth once edited by hand**: Family mruby
# reads them and nothing else. --sheets refuses to overwrite an existing sheet
# unless --force is given, so a run of this script does not lose the edits.
#
# File names use MPSZ notation: 1m..9m 1p..9p 1s..9s, 1z..7z (E S W N Wh G R),
# 0m 0p 0s (red fives), back.
#
# Glyphs are plain text bitmaps below ('#' = ink). Edit them freely.
# License of this script and its output: CC0 (public domain).

require "zlib"
require "fileutils"

# ---------------------------------------------------------------- colors

def rgb(hex, a = 255)
  [hex[1, 2].to_i(16), hex[3, 2].to_i(16), hex[5, 2].to_i(16), a]
end

C = {
  clear:   [0, 0, 0, 0],
  outline: rgb("#4a4a52"),
  face:    rgb("#f7f3e6"),
  side:    rgb("#e0a33c"),   # tile back / thickness (amber)
  side_dk: rgb("#b47a22"),
  back:    rgb("#e0a33c"),
  back_dk: rgb("#c48a2c"),
  ink:     rgb("#1e2240"),   # kanji / digits
  red:     rgb("#c8302a"),
  green:   rgb("#24873a"),
  green_dk: rgb("#16602a"),
  green_lt: rgb("#7cc48a"),
  blue:    rgb("#2a55b0"),
  red_dk:  rgb("#8e1f1a"),
  red_lt:  rgb("#e58a84"),
}.freeze

# ---------------------------------------------------------------- glyphs
# Number kanji (manzu), 11 wide. Rows of '#' / '.'.

def g(str) = str.strip.lines.map { |l| l.strip.chars.map { |c| c == "#" } }

KANJI_NUM = {
  1 => g(<<~G),
    ...........
    ...........
    ...........
    ...........
    ###########
    ...........
    ...........
    ...........
    ...........
  G
  2 => g(<<~G),
    ...........
    .#########.
    ...........
    ...........
    ...........
    ...........
    ###########
    ...........
    ...........
  G
  3 => g(<<~G),
    .#########.
    ...........
    ...........
    ..#######..
    ...........
    ...........
    ...........
    ###########
    ...........
  G
  4 => g(<<~G),
    ###########
    #...#.#...#
    #...#.#...#
    #...#.#...#
    #..#..#...#
    #.#....##.#
    ##........#
    #.........#
    ###########
  G
  5 => g(<<~G),
    .#########.
    ....#......
    ....#......
    .########..
    ....#...#..
    ...#....#..
    ...#....#..
    ..#.....#..
    ###########
  G
  6 => g(<<~G),
    .....#.....
    ......#....
    ###########
    ...........
    ...#...#...
    ..#.....#..
    .#.......#.
    .#.......#.
    #.........#
  G
  7 => g(<<~G),
    ...#.......
    ...#.......
    ...#..#####
    #######....
    ...#.......
    ...#.......
    ...#......#
    ...#......#
    ....######.
  G
  8 => g(<<~G),
    ....#.#....
    ....#..#...
    ....#..#...
    ...#....#..
    ...#....#..
    ..#......#.
    ..#......#.
    .#........#
    #.........#
  G
  9 => g(<<~G),
    ...#.......
    ...#.......
    #######....
    ...#..#....
    ...#..#....
    ..#...#....
    ..#...#...#
    .#....#...#
    #......###.
  G
}.freeze

MAN = g(<<~G)
  .#.......#.
  ###########
  .#.......#.
  .#########.
  .#...#...#.
  .#########.
  .#...#...#.
  .#########.
  .....#.....
  ###########
  #...#.#...#
  #..#...#.##
G

HONOR = {
  1 => g(<<~G), # East
    .....#.....
    ###########
    .....#.....
    .#########.
    .#...#...#.
    .#########.
    .#...#...#.
    .#########.
    ....###....
    ..##.#.##..
    ##...#...##
  G
  2 => g(<<~G), # South
    .....#.....
    ###########
    .....#.....
    ###########
    #..#...#..#
    #...#.#...#
    #.#######.#
    #....#....#
    #.#######.#
    #....#....#
    #....#...##
  G
  3 => g(<<~G), # West
    ###########
    ...#...#...
    .#########.
    .#.#...#.#.
    .#.#...#.#.
    .##.....##.
    .#.......#.
    .#.......#.
    .#.......#.
    .#########.
    .#.......#.
  G
  4 => g(<<~G), # North
    ...#...#...
    ...#...#...
    ...#...#..#
    ...#...#.#.
    ####...##..
    ...#...#...
    ...#...#...
    ...#...#...
    ..##...#...
    .#.#...#..#
    #..#...####
  G
  6 => g(<<~G), # Green dragon
    .#.#...#.#.
    ##..#.#..##
    .....#.....
    ###########
    ..#.....#..
    .####.#####
    #..#.#..#..
    ..#..#.#...
    .#...#..#..
    #...#.#..#.
    ...#...#..#
  G
  7 => g(<<~G), # Red dragon
    .....#.....
    .....#.....
    ###########
    #....#....#
    #....#....#
    #....#....#
    ###########
    .....#.....
    .....#.....
    .....#.....
    .....#.....
  G
}.freeze

DIGIT = {
  1 => g(".#. ##. .#. .#. .#. .#. ###".tr(" ", "\n")),
  2 => g("### ..# ..# ### #.. #.. ###".tr(" ", "\n")),
  3 => g("### ..# ..# ### ..# ..# ###".tr(" ", "\n")),
  4 => g("#.# #.# #.# ### ..# ..# ..#".tr(" ", "\n")),
  5 => g("### #.. #.. ### ..# ..# ###".tr(" ", "\n")),
  6 => g("### #.. #.. ### #.# #.# ###".tr(" ", "\n")),
  7 => g("### ..# ..# .#. .#. .#. .#.".tr(" ", "\n")),
  8 => g("### #.# #.# ### #.# #.# ###".tr(" ", "\n")),
  9 => g("### #.# #.# ### ..# ..# ###".tr(" ", "\n")),
}.freeze

# 1s: a peacock on a bamboo perch. Palette letters, '.' = transparent.
BIRD = <<~B.lines.map(&:chomp).freeze
  ...R.R.R..........
  ....RRR...........
  ....GGG...........
  ...GKGGG..........
  ..YYGGGG..........
  ....GGGG..........
  .....GGG..........
  .....GGGG.........
  ....BBGGGG........
  ...BBBBGGGG.......
  ...BBBBBGGGG......
  ...BBBBBGGGGG.....
  ....BBBBGGGGGg....
  .....BBGGGGGgGg...
  ......GGGGgGGgGg..
  .......g.GGgRGgRg.
  .......g..gRBRgRBR
  ......g.g..gRgGgRg
  .gggggggggg.g..g..
  .GGGGGGGGGG.......
  .gggggggggg.......
B

SPRITE_PALETTE = {
  "G" => :green, "g" => :green_dk, "R" => :red, "B" => :blue, "K" => :ink, "Y" => :side,
}.freeze

# ---------------------------------------------------------------- canvas

class Canvas
  attr_reader :w, :h

  def initialize(w, h, color = C[:clear])
    @w = w
    @h = h
    @px = Array.new(w * h) { color }
  end

  def set(x, y, c)
    return if x.negative? || y.negative? || x >= @w || y >= @h
    @px[y * @w + x] = c
  end

  def get(x, y) = @px[y * @w + x]

  def rect(x0, y0, x1, y1, c)
    (y0..y1).each { |y| (x0..x1).each { |x| set(x, y, c) } }
  end

  # Filled circle centered on a pixel grid position (cx, cy may be .5)
  def disc(cx, cy, r, c)
    (cy - r - 1).floor.upto((cy + r + 1).ceil) do |y|
      (cx - r - 1).floor.upto((cx + r + 1).ceil) do |x|
        dx = x + 0.5 - cx
        dy = y + 0.5 - cy
        set(x, y, c) if dx * dx + dy * dy <= r * r + r * 0.5 # rounder small circles
      end
    end
  end

  def glyph(bits, x0, y0, scale, c)
    bits.each_with_index do |row, y|
      row.each_with_index do |on, x|
        next unless on
        rect(x0 + x * scale, y0 + y * scale, x0 + x * scale + scale - 1, y0 + y * scale + scale - 1, c)
      end
    end
  end

  # Multi-color sprite (array of strings) centered in a box
  def sprite_in(rows, bx, by, bw, bh, scale, palette)
    gw = rows.map(&:size).max * scale
    gh = rows.size * scale
    x0 = bx + (bw - gw) / 2
    y0 = by + (bh - gh) / 2
    rows.each_with_index do |row, y|
      row.each_char.with_index do |ch, x|
        key = palette[ch] or next
        rect(x0 + x * scale, y0 + y * scale, x0 + x * scale + scale - 1, y0 + y * scale + scale - 1, C[key])
      end
    end
  end

  # Center a glyph inside a box
  def glyph_in(bits, bx, by, bw, bh, scale, c)
    gw = bits.map(&:size).max * scale
    gh = bits.size * scale
    glyph(bits, bx + (bw - gw) / 2, by + (bh - gh) / 2, scale, c)
  end

  def blit(src, ox, oy, scale = 1)
    src.h.times do |y|
      src.w.times do |x|
        c = src.get(x, y)
        next if c[3].zero?
        rect(ox + x * scale, oy + y * scale, ox + x * scale + scale - 1, oy + y * scale + scale - 1, c)
      end
    end
  end

  def to_png
    raw = +"".b
    @h.times do |y|
      raw << "\x00".b
      row = @px[y * @w, @w]
      raw << row.flatten.pack("C*")
    end
    chunk = lambda do |type, data|
      [data.bytesize].pack("N") + type + data + [Zlib.crc32(type + data)].pack("N")
    end
    "\x89PNG\r\n\x1a\n".b +
      chunk.call("IHDR", [@w, @h, 8, 6, 0, 0, 0].pack("NNCCCCC")) +
      chunk.call("IDAT", Zlib::Deflate.deflate(raw, 9)) +
      chunk.call("IEND", "")
  end
end

# ---------------------------------------------------------------- tile body

Profile = Struct.new(:name, :w, :h, :side, :margin, keyword_init: true)

HAND  = Profile.new(name: "hand",  w: 42, h: 56, side: 4, margin: 2)
RIVER = Profile.new(name: "river", w: 24, h: 32, side: 2, margin: 1)

# Draws the tile body and returns the inner drawing box [x, y, w, h]
def body(cv, pf, face: true)
  w, h = pf.w, pf.h
  cv.rect(0, 0, w - 1, h - 1, C[:outline])
  # thickness (bottom side)
  cv.rect(1, h - 1 - pf.side, w - 2, h - 2, C[:side])
  cv.rect(1, h - 2, w - 2, h - 2, C[:side_dk])
  # face
  fy1 = h - 2 - pf.side
  if face
    cv.rect(1, 1, w - 2, fy1, C[:face])
  else
    cv.rect(1, 1, w - 2, fy1, C[:back])
    cv.rect(3, 3, w - 4, fy1 - 2, C[:back_dk])
    cv.rect(4, 4, w - 5, fy1 - 3, C[:back])
  end
  # rounded corners
  [[0, 0], [w - 1, 0], [0, h - 1], [w - 1, h - 1]].each { |x, y| cv.set(x, y, C[:clear]) }
  m = pf.margin
  [1 + m, 1 + m, (w - 2) - 2 * m, fy1 - 2 * m]
end

# ---------------------------------------------------------------- pips

def pin_disc(cv, cx, cy, r, color)
  # Snap to a pixel center so small rings stay symmetric
  cx = cx.floor + 0.5
  cy = cy.floor + 0.5
  if r >= 4
    cv.disc(cx, cy, r, color)
    cv.disc(cx, cy, r - 1.6, C[:face])
    cv.disc(cx, cy, [r - 3.2, 1.0].max, color)
  else
    cv.disc(cx, cy, r, color)
    cv.set(cx.floor, cy.floor, C[:face]) if r >= 3.0
  end
end

PIN_LAYOUT = {
  2 => [[0.5, 0.25], [0.5, 0.75]],
  3 => [[0.22, 0.18], [0.5, 0.5], [0.78, 0.82]],
  4 => [[0.27, 0.25], [0.73, 0.25], [0.27, 0.75], [0.73, 0.75]],
  5 => [[0.22, 0.18], [0.78, 0.18], [0.5, 0.5], [0.22, 0.82], [0.78, 0.82]],
  6 => [[0.27, 0.15], [0.73, 0.15], [0.27, 0.5], [0.73, 0.5], [0.27, 0.85], [0.73, 0.85]],
  7 => [[0.2, 0.1], [0.5, 0.2], [0.8, 0.3], [0.27, 0.6], [0.73, 0.6], [0.27, 0.88], [0.73, 0.88]],
  8 => [[0.27, 0.11], [0.73, 0.11], [0.27, 0.37], [0.73, 0.37],
        [0.27, 0.63], [0.73, 0.63], [0.27, 0.89], [0.73, 0.89]],
  9 => [[0.2, 0.15], [0.5, 0.15], [0.8, 0.15], [0.2, 0.5], [0.5, 0.5], [0.8, 0.5],
        [0.2, 0.85], [0.5, 0.85], [0.8, 0.85]],
}.freeze

PIN_COLORS = {
  2 => %i[green blue],
  3 => %i[blue red green],
  4 => %i[blue green green blue],
  5 => %i[blue green red green blue],
  6 => %i[green green red red red red],
  7 => %i[green green green red red red red],
  8 => %i[blue] * 8,
  9 => %i[blue blue blue red red red green green green],
}.freeze

PIN_R = { 2 => 7.0, 3 => 5.5, 4 => 6.0, 5 => 5.0, 6 => 5.0, 7 => 4.5, 8 => 4.6, 9 => 4.6 }.freeze

# rscale shrinks the pips for the river size
def draw_pin(cv, box, n, red5, rscale = 1.0)
  bx, by, bw, bh = box
  if n == 1
    cx = bx + bw / 2.0
    cy = by + bh / 2.0
    r = [bw, bh].min / 2.0
    k = r / 18.0
    colors = %i[red face green face red face red]
    [0, 2, 4, 6, 8, 11, 13].each_with_index do |step, i|
      cv.disc(cx, cy, [r - step * k, 1.0].max, C[colors[i]])
    end
    return
  end
  PIN_LAYOUT[n].each_with_index do |(fx, fy), i|
    color = red5 ? C[:red] : C[PIN_COLORS[n][i]]
    pin_disc(cv, bx + fx * bw, by + fy * bh, PIN_R[n] * rscale, color)
  end
end

def stick(cv, cx, cy, sw, sh, red)
  body = red ? C[:red] : C[:green]
  dark = red ? C[:red_dk] : C[:green_dk]
  lite = red ? C[:red_lt] : C[:green_lt]
  x0 = (cx - sw / 2.0).round
  y0 = (cy - sh / 2.0).round
  x1 = x0 + sw - 1
  y1 = y0 + sh - 1
  cv.rect(x0, y0, x1, y1, body)
  cv.rect(x0, y0, x0, y1, lite) if sw >= 3
  cv.rect(x1, y0, x1, y1, dark) if sw >= 3
  # nodes
  [y0, (y0 + y1) / 2, y1].each { |y| cv.rect(x0, y, x1, y, dark) }
end

SOU_LAYOUT = {
  # [fx, fy, red?]
  2 => [[0.5, 0.25], [0.5, 0.75]],
  3 => [[0.5, 0.25], [0.27, 0.75], [0.73, 0.75]],
  4 => [[0.27, 0.25], [0.73, 0.25], [0.27, 0.75], [0.73, 0.75]],
  5 => [[0.18, 0.25], [0.82, 0.25], [0.5, 0.5, true], [0.18, 0.75], [0.82, 0.75]],
  6 => [[0.2, 0.25], [0.5, 0.25], [0.8, 0.25], [0.2, 0.75], [0.5, 0.75], [0.8, 0.75]],
  7 => [[0.5, 0.15, true], [0.2, 0.5], [0.5, 0.5], [0.8, 0.5], [0.2, 0.84], [0.5, 0.84], [0.8, 0.84]],
  8 => [[0.14, 0.25], [0.38, 0.25], [0.62, 0.25], [0.86, 0.25],
        [0.14, 0.75], [0.38, 0.75], [0.62, 0.75], [0.86, 0.75]],
  9 => [[0.2, 0.16], [0.5, 0.16, true], [0.8, 0.16], [0.2, 0.5], [0.5, 0.5, true], [0.8, 0.5],
        [0.2, 0.84], [0.5, 0.84, true], [0.8, 0.84]],
}.freeze

def draw_sou(cv, box, n, red5, sw: 5, pad: 4)
  bx, by, bw, bh = box
  if n == 1
    scale = [[bw / 18, bh / BIRD.size].min, 1].max
    cv.sprite_in(BIRD, bx, by, bw, bh, scale, SPRITE_PALETTE)
    return
  end
  rows = SOU_LAYOUT[n].map { |p| p[1] }.uniq.size
  rows = 2 if n == 5 # the center stick sits between the two rows
  sh = (bh / rows) - pad
  w = n == 8 ? [sw - 1, 3].max : sw
  SOU_LAYOUT[n].each do |fx, fy, red|
    stick(cv, bx + fx * bw, by + fy * bh, w, sh, red || red5)
  end
end

def draw_man(cv, box, n, red5, scale)
  bx, by, bw, bh = box
  num = KANJI_NUM[n]
  num_h = num.size * scale
  man_h = MAN.size * scale
  gap = [bh - num_h - man_h, 0].max
  cv.glyph_in(num, bx, by, bw, num_h, scale, red5 ? C[:red] : C[:ink])
  cv.glyph_in(MAN, bx, by + num_h + gap, bw, man_h, scale, C[:red])
end

def draw_honor(cv, box, n, scale)
  bx, by, bw, bh = box
  return if n == 5 # White dragon: plain white face
  color = { 6 => C[:green], 7 => C[:red] }.fetch(n, C[:ink])
  cv.glyph_in(HONOR[n], bx, by, bw, bh, scale, color)
end

# ---------------------------------------------------------------- build

TILES = (%w[m p s].flat_map { |s| (1..9).map { |n| "#{n}#{s}" } + ["0#{s}"] } +
         (1..7).map { |n| "#{n}z" } + ["back"]).freeze

def make_tile(pf, id)
  cv = Canvas.new(pf.w, pf.h)
  if id == "back"
    body(cv, pf, face: false)
    return cv
  end
  box = body(cv, pf)
  n = id[0].to_i
  suit = id[1]
  red5 = n.zero?
  n = 5 if red5
  river = pf == RIVER
  case suit
  when "m" then draw_man(cv, box, n, red5, river ? 1 : 2)
  when "p" then draw_pin(cv, box, n, red5, river ? 0.56 : 1.0)
  when "s" then river ? draw_sou(cv, box, n, red5, sw: 3, pad: 2) : draw_sou(cv, box, n, red5)
  when "z" then draw_honor(cv, box, n, river ? 1 : 3)
  end
  cv
end

def preview(tiles_by_profile, scale: 3)
  cols = 10
  pad = 4
  rows_per = (TILES.size / cols.to_f).ceil
  height = tiles_by_profile.sum { |pf, _| rows_per * (pf.h + pad) * scale + pad * scale }
  width = cols * (HAND.w + pad) * scale + pad * scale
  sheet = Canvas.new(width, height, rgb("#3a6b4f"))
  y = pad * scale
  tiles_by_profile.each do |pf, tiles|
    TILES.each_with_index do |id, i|
      cx = pad * scale + (i % cols) * (HAND.w + pad) * scale
      cy = y + (i / cols) * (pf.h + pad) * scale
      sheet.blit(tiles[id], cx, cy, scale)
    end
    y += rows_per * (pf.h + pad) * scale + pad * scale
  end
  sheet
end

# ---------------------------------------------------------------- sheets

TRANSPARENT = 0xE3

# RGB332 of an RGBA colour (nearest level per channel); clear is TRANSPARENT.
def rgb332(c)
  return TRANSPARENT if c[3] < 128
  r = (c[0] * 7 + 127) / 255
  g = (c[1] * 7 + 127) / 255
  b = (c[2] * 3 + 127) / 255
  v = (r << 5) | (g << 2) | b
  raise "a tile colour rounds to the transparent colour: #{c.inspect}" if v == TRANSPARENT
  v
end

# The RGB a device shows for an RGB332 value (the BMP palette).
def rgb332_to_rgb(v)
  [((v >> 5) & 7) * 255 / 7, ((v >> 2) & 7) * 255 / 7, (v & 3) * 255 / 3]
end

# An 8-bit uncompressed BMP, bottom-up, palette = the RGB332 decode.
def bmp8(w, h, px)
  row = (w + 3) & ~3
  palette = (0..255).map { |v| r, g, b = rgb332_to_rgb(v); [b, g, r, 0].pack("C4") }.join
  data = +"".b
  (h - 1).downto(0) do |y|
    data << px[y * w, w].pack("C*") << ("\x00" * (row - w)).b
  end
  off = 14 + 40 + palette.bytesize
  ["BM", off + data.bytesize, 0, 0, off].pack("a2VvvV") +
    [40, w, h, 1, 8, 0, data.bytesize, 2835, 2835, 256, 0].pack("VVVvvVVVVVV") +
    palette + data
end

SHEETS = [
  # [file, profile, columns, rows, first tile number, count]
  ["tiles_hand_0.bmp", HAND, 5, 4, 0, 20],
  ["tiles_hand_1.bmp", HAND, 5, 4, 20, 18],
  ["tiles_river.bmp", RIVER, 10, 4, 0, 38],
].freeze

def sheet_pixels(pf, cols, rows, first, count, tiles)
  w = cols * pf.w
  h = rows * pf.h
  px = Array.new(w * h, TRANSPARENT)
  count.times do |k|
    cv = tiles[TILES[first + k]]
    ox = (k % cols) * pf.w
    oy = (k / cols) * pf.h
    pf.h.times { |y| pf.w.times { |x| px[(oy + y) * w + ox + x] = rgb332(cv.get(x, y)) } }
  end
  [w, h, px]
end

def write_sheets(dir, force)
  FileUtils.mkdir_p(dir)
  existing = SHEETS.map { |f, *| File.join(dir, f) }.select { |p| File.exist?(p) }
  if !existing.empty? && !force
    warn "not overwriting #{existing.join(', ')}: the sheets are the source of truth once"
    warn "edited by hand. Move them away or pass --force to write them again."
    exit 1
  end
  tiles = { HAND => TILES.to_h { |id| [id, make_tile(HAND, id)] },
            RIVER => TILES.to_h { |id| [id, make_tile(RIVER, id)] } }
  scale = 3
  shots = []
  SHEETS.each do |file, pf, cols, rows, first, count|
    w, h, px = sheet_pixels(pf, cols, rows, first, count, tiles[pf])
    File.binwrite(File.join(dir, file), bmp8(w, h, px))
    shots << [w, h, px]
    puts "wrote #{file} (#{w}x#{h}, #{count} tiles)"
  end
  pw = shots.map { |w, _, _| w }.max * scale
  ph = shots.sum { |_, h, _| h + 4 } * scale
  prev = Canvas.new(pw, ph, rgb("#3a6b4f"))
  y0 = 0
  shots.each do |w, h, px|
    h.times do |y|
      w.times do |x|
        v = px[y * w + x]
        next if v == TRANSPARENT
        prev.rect(x * scale, (y0 + y) * scale, x * scale + scale - 1, (y0 + y) * scale + scale - 1,
                  rgb332_to_rgb(v) + [255])
      end
    end
    y0 += h + 4
  end
  File.binwrite(File.join(dir, "sheets_preview.png"), prev.to_png)
end

if $PROGRAM_NAME == __FILE__
  if ARGV[0] == "--sheets"
    abort "usage: #{$PROGRAM_NAME} --sheets DIR [--force]" unless ARGV[1]
    write_sheets(ARGV[1], ARGV.include?("--force"))
    exit
  end
  out = ARGV[0] || "mahjong_tiles"
  result = {}
  [HAND, RIVER].each do |pf|
    dir = File.join(out, pf.name)
    FileUtils.mkdir_p(dir)
    result[pf] = TILES.to_h do |id|
      cv = make_tile(pf, id)
      File.binwrite(File.join(dir, "#{id}.png"), cv.to_png)
      [id, cv]
    end
  end
  File.binwrite(File.join(out, "preview.png"), preview(result).to_png)
  puts "wrote #{TILES.size * 2} tiles to #{out}/ (hand #{HAND.w}x#{HAND.h}, river #{RIVER.w}x#{RIVER.h})"
end
