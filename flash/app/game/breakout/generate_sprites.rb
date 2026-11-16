#!/usr/bin/env ruby
# Generates the sprites for the Python breakout demo.
#
#   paddle.png     32x6   the bat
#   paddle_wide.png 48x6  the bat after the widening item
#   ball.png       4x4    the ball
#   item_wide.png  8x8    widen the bat
#   item_slow.png  8x8    slow the ball
#   item_life.png  8x8    one more life
#
# Colours are RGB332 values, written as palette PNGs of the colours used
# (tools/rgb332_png.rb): the sprite loader turns each colour back into its
# RGB332 value, and the files open in any image viewer.

require_relative "../../../../tools/rgb332_png"

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

def write_png(path, width, height, pixels_top_down)
  size = Rgb332Png.write(path, width, height, pixels_top_down)
  puts "Wrote #{path} (#{width}x#{height}, #{size} B)"
end

ASSETS = {
  "paddle.png"      => paddle(32),
  "paddle_wide.png" => paddle(48),
  "ball.png"        => ball,
  "item_wide.png"   => item(CYAN, :wide),
  "item_slow.png"   => item(GREEN, :slow),
  "item_life.png"   => item(PINK, :life),
}

ASSETS.each do |name, (buf, w, h)|
  write_png(File.join(__dir__, name), w, h, buf)
end
