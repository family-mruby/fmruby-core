# Host-side stand-ins for the two objects FmrbUI talks to.
#
# fmrb-ui.rb is pure Ruby and calls nothing but FmrbGfx drawing methods and a
# few readers on FmrbApp, so it runs unchanged under host Ruby once those two
# are faked. That is what makes this suite possible without docker, a
# firmware build, or a device: the file under test is the real one.

module FmrbConst
  THEME_WINDOW_BG = 0xFF
  THEME_TEXT      = 0x00
  THEME_TEXT_LIGHT = 0xFF
  THEME_HIGHLIGHT = 0xEE
  THEME_BORDER    = 0x60
  THEME_BUTTON    = 0x60
end

# Records what was drawn instead of drawing it, and reproduces FmrbGfx's text
# metrics: ASCII 6px, multi-byte 8px, both multiplied by the current text size.
class FakeGfx
  attr_reader :current_text_size, :log

  def initialize(size = 1)
    @current_text_size = size
    @log = []
  end

  def set_text_size(s)
    @current_text_size = s
    @log << [:size, s]
    self
  end

  def text_width(str, _family = nil, _size = nil)
    base = 0
    b = str.bytes
    i = 0
    while i < b.length
      if b[i] < 0x80    then base += 6; i += 1
      elsif b[i] < 0xE0 then base += 8; i += 2
      elsif b[i] < 0xF0 then base += 8; i += 3
      else                   base += 8; i += 4
      end
    end
    base * @current_text_size
  end

  def draw_line(*a); @log << [:line, *a]; self; end
  def fill_rect(*a); @log << [:fill, *a]; self; end
  def draw_rect(*a); @log << [:rect, *a]; self; end
  def draw_text_mixed(x, y, s, _color, _bg = nil)
    @log << [:text, x, y, s, @current_text_size]
    self
  end
  def present; @log << [:present]; self; end

  def texts; @log.select { |e| e[0] == :text }; end
  def count(kind); @log.count { |e| e[0] == kind }; end
end

# The three readers FmrbUI takes from the app. The user area origin is the
# windowed one (1, 11), so the tests see the same offsetting real apps do.
class FakeApp
  attr_reader :gfx, :user_area_x0, :user_area_y0
  def initialize(gfx, x0 = 1, y0 = 11)
    @gfx = gfx
    @user_area_x0 = x0
    @user_area_y0 = y0
  end
end

# Minimal assertion helpers; a failure prints both sides and sets the exit code.
module Check
  @failed = 0
  class << self
    attr_reader :failed
    def ok(name, got, want)
      if got == want
        puts "ok   #{name}"
      else
        puts "FAIL #{name}"
        puts "       got  #{got.inspect}"
        puts "       want #{want.inspect}"
        @failed += 1
      end
    end
  end
end

def check(name, got, want) = Check.ok(name, got, want)
