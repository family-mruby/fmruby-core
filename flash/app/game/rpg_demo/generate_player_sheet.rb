#!/usr/bin/env ruby
# Generates eight 16x16 player frame BMPs (player_<dir>_<step>.bmp).
#
# Style references (Dragon Quest 1/2 Famicom hero):
#   - dotartplay.com/dragonquest-dot  (DQ pixel-art conventions)
#   - nyaonyao21.com/entry/2019/04/28/123402  (Famicom 16x16 RPG sprites)
# Common rules followed here: 3-4 color hero silhouette, hooded helmet, eyes
# 2 dots apart on the face, "one foot forward at a time" walking animation,
# black outline so the figure reads on any terrain tile.
#
# Pixel bytes are RGB332 (the WROVER BMP loader ignores the BMP palette and
# reads pixel bytes directly as RRRGGGBB). Palette is emitted self-consistently
# so the BMP also views correctly in normal viewers.

TILE = 16

def rgb332(r, g, b)
  ((r & 0xE0)) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6)
end

BG       = rgb332(  0,   0,   0)   # transparent key
OUTLINE  = rgb332( 32,  32,  32)
SKIN     = rgb332(255, 192, 128)
SKIN_S   = rgb332(192, 128,  64)
HOOD     = rgb332( 96,  64, 192)   # dark purple-blue hood / helmet
HOOD_S   = rgb332( 64,  32, 128)
TUNIC    = rgb332( 64, 160,  32)   # bright green over-tunic
TUNIC_S  = rgb332( 32,  96,  32)
BOOTS    = rgb332(128,  64,  32)   # brown boots / belt
EYE      = rgb332( 32,  32,  64)

# --------------------------------------------------------------------------
# Pixel buffer helpers
# --------------------------------------------------------------------------
def new_tile
  Array.new(TILE * TILE, BG)
end

def px!(t, x, y, c)
  return if x < 0 || y < 0 || x >= TILE || y >= TILE
  t[y * TILE + x] = c
end

def hline!(t, x0, x1, y, c); (x0..x1).each { |x| px!(t, x, y, c) }; end
def vline!(t, x, y0, y1, c); (y0..y1).each { |y| px!(t, x, y, c) }; end
def fill_rect!(t, x0, y0, w, h, c)
  (y0...y0 + h).each { |y| (x0...x0 + w).each { |x| px!(t, x, y, c) } }
end

# --------------------------------------------------------------------------
# Shared parts
# --------------------------------------------------------------------------

# Hood / helmet that covers the upper head. Shape varies with facing so the
# silhouette reads correctly from each angle.
def draw_hood!(t, dir)
  # Top crown common to all directions.
  hline!(t,  5, 10, 0, OUTLINE)
  hline!(t,  5, 10, 1, HOOD)
  hline!(t,  4, 11, 2, HOOD)
  hline!(t,  3, 12, 3, HOOD)
  hline!(t,  3, 12, 4, HOOD)
  # Crown outline corners.
  px!(t,  4, 1, OUTLINE)
  px!(t, 11, 1, OUTLINE)
  px!(t,  3, 2, OUTLINE)
  px!(t, 12, 2, OUTLINE)
  px!(t,  2, 3, OUTLINE)
  px!(t, 13, 3, OUTLINE)

  case dir
  when :down
    # Hood frames the face: cheek tabs descend on both sides.
    vline!(t,  3, 4, 6, HOOD)
    vline!(t, 12, 4, 6, HOOD)
    vline!(t,  2, 4, 6, OUTLINE)
    vline!(t, 13, 4, 6, OUTLINE)
    # Inner shadow line under the hood brim.
    hline!(t,  4, 11, 4, HOOD_S)
  when :up
    # Back of the hood: solid cap, no face cutout.
    fill_rect!(t, 3, 4, 10, 3, HOOD)
    hline!(t,  2, 13, 4, HOOD)  # widen
    px!(t,  2, 4, OUTLINE)
    px!(t, 13, 4, OUTLINE)
    vline!(t,  2, 5, 6, OUTLINE)
    vline!(t, 13, 5, 6, OUTLINE)
    # Shadow seam.
    hline!(t,  4, 11, 6, HOOD_S)
  when :left
    # Hood pulled to the right (the side away from the player) so the face
    # opens on the left.
    fill_rect!(t,  6, 4, 7, 3, HOOD)
    px!(t,  5, 4, OUTLINE)
    px!(t, 13, 4, OUTLINE)
    vline!(t,  5, 5, 6, HOOD)
    vline!(t, 13, 5, 6, OUTLINE)
    px!(t, 12, 6, HOOD_S)
  when :right
    fill_rect!(t,  3, 4, 7, 3, HOOD)
    px!(t,  2, 4, OUTLINE)
    px!(t, 10, 4, OUTLINE)
    vline!(t,  2, 5, 6, OUTLINE)
    vline!(t, 10, 5, 6, HOOD)
    px!(t,  3, 6, HOOD_S)
  end
end

# Face: visible only for down / left / right. Eyes are 2 dots apart on
# down-facing frames (Famicom DQ convention).
def draw_face!(t, dir)
  case dir
  when :down
    fill_rect!(t, 4, 4, 8, 3, SKIN)
    # Hood inner brim shadow already covers row 4 with HOOD_S; reapply.
    px!(t, 6, 5, EYE)
    px!(t, 9, 5, EYE)
    # Mouth.
    px!(t, 7, 6, OUTLINE)
    px!(t, 8, 6, OUTLINE)
    # Chin / jaw line for definition.
    hline!(t, 5, 10, 7, SKIN_S)
  when :left
    fill_rect!(t, 3, 5, 3, 2, SKIN)
    px!(t, 4, 5, EYE)
    px!(t, 5, 6, SKIN_S)
    px!(t, 3, 6, OUTLINE)  # chin/jaw outline
  when :right
    fill_rect!(t, 11, 5, 3, 2, SKIN)
    px!(t, 12, 5, EYE)
    px!(t, 10, 6, SKIN_S)
    px!(t, 13, 6, OUTLINE)
  end
end

# Torso: green tunic with belt at the waist.
def draw_torso!(t)
  fill_rect!(t, 4, 8, 8, 4, TUNIC)
  # Outline shoulders + sides.
  hline!(t,  4, 11, 7, OUTLINE)
  vline!(t,  3, 8, 11, OUTLINE)
  vline!(t, 12, 8, 11, OUTLINE)
  # Tunic shadow on right.
  vline!(t, 11, 8, 11, TUNIC_S)
  # Belt.
  hline!(t, 4, 11, 11, BOOTS)
  px!(t, 3, 11, OUTLINE)
  px!(t, 12, 11, OUTLINE)
end

# Lower body: legs + boots. The step changes which foot is forward (drops 1
# row below). This is the main visual change for walking - made deliberately
# obvious so it's readable on TV.
def draw_legs!(t, step)
  # Tunic skirt continues to y=13.
  fill_rect!(t, 4, 12, 8, 2, TUNIC)
  vline!(t,  3, 12, 13, OUTLINE)
  vline!(t, 12, 12, 13, OUTLINE)
  # A vertical cleft in the skirt suggests two legs.
  vline!(t,  7, 12, 13, TUNIC_S)
  vline!(t,  8, 12, 13, TUNIC_S)

  # Boots. Two states differ by 2 px horizontally so the motion is visible
  # even with subtle TV scaling.
  if step.zero?
    # Standing / both feet planted close to center.
    fill_rect!(t, 5, 14, 2, 2, BOOTS)
    fill_rect!(t, 9, 14, 2, 2, BOOTS)
    px!(t, 4, 14, OUTLINE); px!(t, 7, 14, OUTLINE)
    px!(t, 4, 15, OUTLINE); px!(t, 7, 15, OUTLINE)
    px!(t, 8, 14, OUTLINE); px!(t, 11, 14, OUTLINE)
    px!(t, 8, 15, OUTLINE); px!(t, 11, 15, OUTLINE)
  else
    # Striding / feet spread outward.
    fill_rect!(t, 3, 14, 2, 2, BOOTS)
    fill_rect!(t, 11, 14, 2, 2, BOOTS)
    px!(t, 2, 14, OUTLINE); px!(t, 5, 14, OUTLINE)
    px!(t, 2, 15, OUTLINE); px!(t, 5, 15, OUTLINE)
    px!(t, 10, 14, OUTLINE); px!(t, 13, 14, OUTLINE)
    px!(t, 10, 15, OUTLINE); px!(t, 13, 15, OUTLINE)
  end
end

# --------------------------------------------------------------------------
# Per-direction frames
# --------------------------------------------------------------------------
def draw_down(t, step)
  draw_hood!(t, :down)
  draw_face!(t, :down)
  draw_torso!(t)
  draw_legs!(t, step)
end

def draw_up(t, step)
  draw_hood!(t, :up)
  # No face for back view.
  draw_torso!(t)
  draw_legs!(t, step)
  # A small belt-knot accent at center back.
  px!(t, 7, 11, OUTLINE)
  px!(t, 8, 11, OUTLINE)
end

def draw_left(t, step)
  draw_hood!(t, :left)
  draw_face!(t, :left)
  draw_torso!(t)
  draw_legs!(t, step)
end

def draw_right(t, step)
  draw_hood!(t, :right)
  draw_face!(t, :right)
  draw_torso!(t)
  draw_legs!(t, step)
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

# --------------------------------------------------------------------------
# BMP writer
# --------------------------------------------------------------------------
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
