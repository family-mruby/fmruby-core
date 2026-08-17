#!/usr/bin/env ruby
# Generates the sprites for the Python breakout demo.
#
#   paddle.bmp     32x6   the bat
#   paddle_wide.bmp 48x6  the bat after the widening item
#   ball.bmp       4x4    the ball
#   item_wide.bmp  8x8    widen the bat
#   item_slow.bmp  8x8    slow the ball
#   item_life.bmp  8x8    one more life
#
# Pixel bytes are RGB332: the loader on the graphics side ignores the BMP
# palette and reads the pixel bytes as RRRGGGBB. The palette is written to
# match, so the files also look right in an ordinary image viewer.

def rgb332(r, g, b)
  (r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6)
end

KEY     = rgb332(0, 0, 0)         # colour key: drawn as transparent
WHITE   = rgb332(255, 255, 255)
CYAN    = rgb332(64, 224, 255)
CYAN_D  = rgb332(0, 128, 192)
YELLOW  = rgb332(255, 224, 64)
GREEN   = rgb332(64, 224, 96)
PINK    = rgb332(255, 96, 160)
DARK    = rgb332(32, 32, 32)

def buffer(w, h, fill = KEY)
  Array.new(w * h, fill)
end

def px!(buf, w, h, x, y, c)
  return if x < 0 || y < 0 || x >= w || y >= h
  buf[y * w + x] = c
end

def rect!(buf, w, h, x0, y0, rw, rh, c)
  (y0...y0 + rh).each { |y| (x0...x0 + rw).each { |x| px!(buf, w, h, x, y, c) } }
end

# The bat: a rounded bar with a lit top edge, so it reads as a solid object
# rather than a line.
def paddle(width)
  h = 6
  buf = buffer(width, h)
  rect!(buf, width, h, 1, 0, width - 2, 1, WHITE)
  rect!(buf, width, h, 0, 1, width, 3, CYAN)
  rect!(buf, width, h, 1, 4, width - 2, 2, CYAN_D)
  # Grip marks in the middle, so the bat's centre is visible while it moves.
  rect!(buf, width, h, width / 2 - 3, 2, 2, 2, WHITE)
  rect!(buf, width, h, width / 2 + 1, 2, 2, 2, WHITE)
  [buf, width, h]
end

def ball
  w = 4
  buf = buffer(w, w)
  rect!(buf, w, w, 1, 0, 2, 4, WHITE)
  rect!(buf, w, w, 0, 1, 4, 2, WHITE)
  px!(buf, w, w, 1, 1, YELLOW)
  [buf, w, w]
end

# The items are 8x8 badges: a filled square with a dark border and a mark
# that says which one it is. Colour carries the meaning; the mark is there
# for the colours the eye confuses on a small screen.
def item(colour, mark)
  w = 8
  buf = buffer(w, w)
  rect!(buf, w, w, 0, 0, 8, 8, DARK)
  rect!(buf, w, w, 1, 1, 6, 6, colour)
  case mark
  when :wide     # a horizontal bar with arrow heads
    rect!(buf, w, w, 2, 3, 4, 2, DARK)
    px!(buf, w, w, 1, 3, DARK)
    px!(buf, w, w, 6, 4, DARK)
  when :slow     # a downward arrow
    rect!(buf, w, w, 3, 2, 2, 3, DARK)
    rect!(buf, w, w, 2, 4, 4, 1, DARK)
    px!(buf, w, w, 3, 5, DARK)
    px!(buf, w, w, 4, 5, DARK)
  when :life     # a plus sign
    rect!(buf, w, w, 3, 2, 2, 4, DARK)
    rect!(buf, w, w, 2, 3, 4, 2, DARK)
  end
  [buf, w, w]
end

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
    (height - 1).downto(0) { |y| f.write(pixels_top_down[y * width, width].pack("C*")) }
  end
  puts "Wrote #{path} (#{width}x#{height})"
end

ASSETS = {
  "paddle.bmp"      => paddle(32),
  "paddle_wide.bmp" => paddle(48),
  "ball.bmp"        => ball,
  "item_wide.bmp"   => item(CYAN, :wide),
  "item_slow.bmp"   => item(GREEN, :slow),
  "item_life.bmp"   => item(PINK, :life),
}

ASSETS.each do |name, (buf, w, h)|
  write_bmp(File.join(__dir__, name), w, h, buf)
end
