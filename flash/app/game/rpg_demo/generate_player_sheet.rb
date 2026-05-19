#!/usr/bin/env ruby
# Generates eight 16x16 player frame BMPs (player_<dir>_<step>.bmp).
#
# Pixel bytes are RGB332 values (the renderer ignores the BMP palette and
# reads pixels as RGB332 directly). See generate_tiles.rb for the rationale.
#
# Output frames:
#   player_down_0.bmp   player_down_1.bmp
#   player_up_0.bmp     player_up_1.bmp
#   player_left_0.bmp   player_left_1.bmp
#   player_right_0.bmp  player_right_1.bmp

TILE = 16

def rgb332(r, g, b)
  ((r & 0xE0)) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6)
end

BG      = rgb332(  0,   0,   0)   # transparent key
OUTLINE = rgb332( 32,  32,  32)
SKIN    = rgb332(255, 192, 160)
HAIR    = rgb332(255, 192,  32)
HAIR_S  = rgb332(192, 128,  32)
TUNIC   = rgb332(192,  32,  32)
TUNIC_S = rgb332(128,  16,  16)
BOOTS   = rgb332( 64,  32,  16)
EYEW    = rgb332(255, 255, 255)
PUPIL   = rgb332( 32,  32,  64)

def new_tile
  Array.new(TILE * TILE, BG)
end

def px!(tile, x, y, col)
  return if x < 0 || y < 0 || x >= TILE || y >= TILE
  tile[y * TILE + x] = col
end

def hline!(tile, x0, x1, y, col)
  (x0..x1).each { |x| px!(tile, x, y, col) }
end

def vline!(tile, x, y0, y1, col)
  (y0..y1).each { |y| px!(tile, x, y, col) }
end

def fill_rect!(tile, x0, y0, w, h, col)
  (y0...y0 + h).each { |y| (x0...x0 + w).each { |x| px!(tile, x, y, col) } }
end

def draw_body!(t, step)
  fill_rect!(t, 5, 8, 6, 5, TUNIC)
  vline!(t, 10, 8, 12, TUNIC_S)
  hline!(t, 5, 10, 12, BOOTS)
  fill_rect!(t, 5, 13, 6, 2, TUNIC)
  if step == 0
    fill_rect!(t, 5, 14, 2, 1, BOOTS)
    fill_rect!(t, 9, 14, 2, 1, BOOTS)
  else
    fill_rect!(t, 4, 14, 2, 1, BOOTS)
    fill_rect!(t, 10, 14, 2, 1, BOOTS)
  end
  vline!(t,  4, 8, 14, OUTLINE)
  vline!(t, 11, 8, 14, OUTLINE)
  hline!(t,  5, 10,  7, OUTLINE)
  hline!(t,  5, 10, 15, OUTLINE)
end

def draw_head_box!(t)
  fill_rect!(t, 4, 1, 8, 6, SKIN)
  hline!(t,  4, 11, 0, OUTLINE)
  hline!(t,  4, 11, 7, OUTLINE)
  vline!(t,  3, 1, 6, OUTLINE)
  vline!(t, 12, 1, 6, OUTLINE)
end

def draw_down(t, step)
  draw_body!(t, step)
  draw_head_box!(t)
  fill_rect!(t, 4, 1, 8, 2, HAIR)
  px!(t, 4, 3, HAIR_S)
  px!(t, 11, 3, HAIR_S)
  px!(t, 6, 4, EYEW)
  px!(t, 6, 5, PUPIL)
  px!(t, 9, 4, EYEW)
  px!(t, 9, 5, PUPIL)
  px!(t, 7, 6, OUTLINE)
  px!(t, 8, 6, OUTLINE)
end

def draw_up(t, step)
  draw_body!(t, step)
  draw_head_box!(t)
  fill_rect!(t, 4, 1, 8, 5, HAIR)
  hline!(t, 4, 11, 6, HAIR_S)
end

def draw_left(t, step)
  draw_body!(t, step)
  draw_head_box!(t)
  fill_rect!(t, 4, 1, 8, 2, HAIR)
  fill_rect!(t, 4, 3, 2, 2, HAIR_S)
  px!(t, 7, 4, EYEW)
  px!(t, 7, 5, PUPIL)
  px!(t, 4, 5, OUTLINE)
end

def draw_right(t, step)
  draw_body!(t, step)
  draw_head_box!(t)
  fill_rect!(t, 4, 1, 8, 2, HAIR)
  fill_rect!(t, 10, 3, 2, 2, HAIR_S)
  px!(t, 8, 4, EYEW)
  px!(t, 8, 5, PUPIL)
  px!(t, 11, 5, OUTLINE)
end

FRAMES = [
  ["down",  0, :draw_down,  0],
  ["down",  1, :draw_down,  1],
  ["up",    0, :draw_up,    0],
  ["up",    1, :draw_up,    1],
  ["left",  0, :draw_left,  0],
  ["left",  1, :draw_left,  1],
  ["right", 0, :draw_right, 0],
  ["right", 1, :draw_right, 1],
].freeze

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

FRAMES.each do |dir, step, drawer, step_arg|
  t = new_tile
  send(drawer, t, step_arg)
  path = File.join(__dir__, "player_#{dir}_#{step}.bmp")
  write_bmp(path, TILE, TILE, t)
  puts "Wrote #{path}"
end

# Remove the now-unused sheet file if it exists, so stale assets don't get
# transferred to the WROVER cache.
sheet_path = File.join(__dir__, "player.bmp")
if File.exist?(sheet_path)
  File.delete(sheet_path)
  puts "Deleted obsolete #{sheet_path}"
end
