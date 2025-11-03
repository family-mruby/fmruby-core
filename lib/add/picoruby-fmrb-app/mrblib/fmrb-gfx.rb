# FmrbGfx - Graphics API for Family mruby applications
# Core class is defined in C (gfx.c)
# This file provides Ruby-level wrapper methods

class FmrbGfx
  # Color constants (16-bit RGB565 format)
  COLOR_BLACK   = 0x0000
  COLOR_WHITE   = 0xFFFF
  COLOR_RED     = 0xF800
  COLOR_GREEN   = 0x07E0
  COLOR_BLUE    = 0x001F
  COLOR_YELLOW  = 0xFFE0
  COLOR_CYAN    = 0x07FF
  COLOR_MAGENTA = 0xF81F
  COLOR_GRAY    = 0x8410

  # Initialize graphics context
  # @param width [Integer] Screen width
  # @param height [Integer] Screen height
  def initialize(width, height)
    _init(width, height)
  end
end
