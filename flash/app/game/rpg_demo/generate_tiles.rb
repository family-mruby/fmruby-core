#!/usr/bin/env ruby
# Generates world.bmp: 64x32, 8bpp indexed, 4x2 grid of 16x16 tiles.
#
# The graphics-audio side ignores the BMP palette and interprets each pixel
# byte directly as an RGB332 value (RRRGGGBB). So we:
#   1. Express tile colors via rgb332(r, g, b) and write the resulting byte
#      straight into the pixel buffer.
#   2. Emit a self-consistent palette (palette[i] expands to the RGB332 value
#      that i encodes) just so the file opens cleanly in normal viewers.
#
# Tile IDs (row-major, sheet cols=4 rows=2):
#   0 plain   1 forest  2 desert  3 sea
#   4 hill    5 mountain 6 cave   7 castle

TILE   = 16
COLS   = 4
ROWS   = 2
WIDTH  = COLS * TILE
HEIGHT = ROWS * TILE

def rgb332(r, g, b)
  ((r & 0xE0)) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6)
end

# Index 0 is the transparency color key, kept at pure black.
BG       = rgb332(  0,   0,   0)
# Greens / browns / yellows / blues / greys, plus a few accents.
GRASS    = rgb332( 64, 192,  64)
GRASS_D  = rgb332( 32, 128,  32)
FOREST   = rgb332( 32,  96,  32)
TRUNK    = rgb332(128,  64,  32)
SAND     = rgb332(224, 192,  96)
SAND_D   = rgb332(192, 128,  64)
SEA      = rgb332( 32,  64, 192)
SEA_L    = rgb332( 96, 128, 224)
WHITE    = rgb332(224, 224, 224)
HILL     = rgb332(160, 160,  64)
HILL_D   = rgb332( 96, 128,  32)
ROCK     = rgb332(160, 160, 160)
ROCK_D   = rgb332( 96,  96,  96)
CAVE_R   = rgb332( 96,  64,  32)
CAVE_DK  = rgb332( 32,  32,  32)
STONE    = rgb332(192, 192, 192)
STONE_D  = rgb332(128, 128, 128)
ROOF     = rgb332(192,  32,  32)
DIRT     = rgb332( 64,  32,   0)

def new_tile
  Array.new(TILE * TILE, BG)
end

def px!(tile, x, y, col)
  return if x < 0 || y < 0 || x >= TILE || y >= TILE
  tile[y * TILE + x] = col
end

def fill_rect!(tile, x0, y0, w, h, col)
  (y0...y0 + h).each { |y| (x0...x0 + w).each { |x| px!(tile, x, y, col) } }
end

def fill_tile!(tile, col)
  fill_rect!(tile, 0, 0, TILE, TILE, col)
end

def speckle!(tile, col, count, seed)
  rng = Random.new(seed)
  count.times { px!(tile, rng.rand(TILE), rng.rand(TILE), col) }
end

# ---------- per-tile drawers ----------

def tile_plain
  t = new_tile
  fill_tile!(t, GRASS)
  speckle!(t, GRASS_D, 18, 0x1010)
  t
end

def tile_forest
  t = new_tile
  fill_tile!(t, GRASS)
  speckle!(t, GRASS_D, 10, 0x2020)
  draw_tree!(t, 3, 2)
  draw_tree!(t, 9, 8)
  t
end

def draw_tree!(t, cx, cy)
  (0...5).each { |dx| (0...4).each { |dy| px!(t, cx + dx, cy + dy, FOREST) } }
  px!(t, cx,     cy,     GRASS)
  px!(t, cx + 4, cy,     GRASS)
  px!(t, cx,     cy + 3, GRASS)
  px!(t, cx + 4, cy + 3, GRASS)
  px!(t, cx + 2, cy + 4, TRUNK)
  px!(t, cx + 2, cy + 5, TRUNK)
end

def tile_desert
  t = new_tile
  fill_tile!(t, SAND)
  speckle!(t, SAND_D, 24, 0x3030)
  [[2, 10], [9, 4], [11, 12]].each do |x, y|
    px!(t, x,     y,     SAND_D)
    px!(t, x + 1, y,     SAND_D)
    px!(t, x + 2, y,     SAND_D)
    px!(t, x + 1, y - 1, SAND_D)
  end
  t
end

def tile_sea
  t = new_tile
  fill_tile!(t, SEA)
  (0...TILE).each do |y|
    (0...TILE).each do |x|
      px!(t, x, y, SEA_L) if ((x + y) % 6).zero?
    end
  end
  foam = [[2, 3], [10, 2], [4, 8], [12, 9], [6, 12], [1, 14], [13, 14]]
  foam.each do |x, y|
    px!(t, x,     y, WHITE)
    px!(t, x + 1, y, WHITE)
  end
  t
end

def tile_hill
  t = new_tile
  fill_tile!(t, GRASS)
  speckle!(t, GRASS_D, 8, 0x4040)
  rows = [
    [6, 9], [4, 11], [3, 12], [2, 13], [1, 14],
    [1, 14], [0, 15], [0, 15], [0, 15], [0, 15], [0, 15],
  ]
  rows.each_with_index do |(x0, x1), i|
    y = 5 + i
    (x0..x1).each { |x| px!(t, x, y, HILL) }
    px!(t, x0, y, HILL_D)
    px!(t, x1, y, HILL_D)
  end
  px!(t, 8,  7, GRASS_D)
  px!(t, 4, 11, GRASS_D)
  px!(t, 11, 12, GRASS_D)
  t
end

def tile_mountain
  t = new_tile
  fill_tile!(t, GRASS)
  speckle!(t, GRASS_D, 6, 0x5050)
  rows = [
    nil, nil, nil,
    [7, 8], [6, 9], [5, 10], [4, 11], [3, 12],
    [2, 13], [1, 14], [1, 14], [0, 15], [0, 15],
    [0, 15], [0, 15], [0, 15],
  ]
  rows.each_with_index do |range, y|
    next if range.nil?
    x0, x1 = range
    (x0..x1).each { |x| px!(t, x, y, ROCK) }
    shadow_start = x0 + ((x1 - x0) * 2 / 3)
    (shadow_start..x1).each { |x| px!(t, x, y, ROCK_D) }
  end
  (3..5).each do |y|
    range = rows[y]
    (range[0]..range[1]).each { |x| px!(t, x, y, WHITE) }
  end
  t
end

def tile_cave
  t = new_tile
  fill_tile!(t, GRASS)
  (4..15).each { |y| (1..14).each { |x| px!(t, x, y, CAVE_R) } }
  (3..12).each { |x| px!(t, x, 3, CAVE_R) }
  (2..13).each { |x| px!(t, x, 2, CAVE_R) }
  fill_rect!(t, 6, 8, 4, 7, CAVE_DK)
  px!(t, 6, 8, CAVE_R)
  px!(t, 9, 8, CAVE_R)
  fill_rect!(t, 5, 14, 6, 1, DIRT)
  t
end

def tile_castle
  t = new_tile
  fill_tile!(t, GRASS)
  fill_rect!(t, 2, 5, 12, 11, STONE)
  fill_rect!(t, 2, 14, 12, 2, STONE_D)
  [2, 5, 8, 11, 14].each do |x|
    fill_rect!(t, x - 1, 3, 2, 2, STONE)
  end
  fill_rect!(t, 6, 1, 4, 4, STONE)
  [[7, 8], [6, 9], [5, 10]].each_with_index do |range, i|
    x0, x1 = range
    (x0..x1).each { |x| px!(t, x, i, ROOF) }
  end
  px!(t, 7, 3, CAVE_DK)
  px!(t, 8, 3, CAVE_DK)
  fill_rect!(t, 7, 12, 2, 4, CAVE_DK)
  fill_rect!(t, 7, 15, 2, 1, DIRT)
  [7, 11].each { |y| (2..13).each { |x| px!(t, x, y, STONE_D) } }
  t
end

TILE_DRAWERS = [
  :tile_plain, :tile_forest, :tile_desert, :tile_sea,
  :tile_hill,  :tile_mountain, :tile_cave, :tile_castle,
].freeze

def build_sheet
  sheet = Array.new(WIDTH * HEIGHT, BG)
  TILE_DRAWERS.each_with_index do |drawer, idx|
    tile = send(drawer)
    col = idx % COLS
    row = idx / COLS
    ox = col * TILE
    oy = row * TILE
    TILE.times do |ty|
      TILE.times do |tx|
        sheet[(oy + ty) * WIDTH + (ox + tx)] = tile[ty * TILE + tx]
      end
    end
  end
  sheet
end

# ---------- BMP writer ----------
#
# Pixel bytes are interpreted as RGB332 by the renderer. The palette below is
# just to keep the file viewable: palette[i] is the 24-bit expansion of the
# RGB332 value encoded by index i.

def write_bmp(path, width, height, pixels_top_down)
  raise "bad pixel size" unless pixels_top_down.size == width * height
  raise "row not aligned to 4" unless (width % 4).zero?
  pixel_data_size = width * height
  header_bytes    = 14 + 40 + 256 * 4
  file_size       = header_bytes + pixel_data_size
  File.open(path, "wb") do |f|
    f.write("BM")
    f.write([file_size].pack("V"))
    f.write([0, 0].pack("v2"))
    f.write([header_bytes].pack("V"))
    f.write([40, width, height].pack("Vl<l<"))
    f.write([1, 8].pack("v2"))
    f.write([0, pixel_data_size].pack("V2"))
    f.write([2835, 2835].pack("l<l<"))
    f.write([256, 0].pack("V2"))
    256.times do |i|
      r = ((i >> 5) & 0x7) * 255 / 7
      g = ((i >> 2) & 0x7) * 255 / 7
      b =  (i       & 0x3) * 255 / 3
      f.write([b, g, r, 0].pack("C4"))
    end
    (height - 1).downto(0) do |y|
      f.write(pixels_top_down[y * width, width].pack("C*"))
    end
  end
end

out_path = File.join(__dir__, "world.bmp")
write_bmp(out_path, WIDTH, HEIGHT, build_sheet)
puts "Wrote #{out_path} (#{WIDTH}x#{HEIGHT}, #{File.size(out_path)} bytes)"
