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
  # The drawing origin FmrbGfx keeps in C: what every draw call adds, so the
  # log holds canvas coordinates, as the display gets them.
  attr_accessor :ox, :oy

  def initialize(size = 1)
    @current_text_size = size
    @log = []
    @ox = 0
    @oy = 0
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

  def draw_line(x0, y0, x1, y1, c); @log << [:line, x0 + @ox, y0 + @oy, x1 + @ox, y1 + @oy, c]; self; end
  def fill_rect(x, y, *a); @log << [:fill, x + @ox, y + @oy, *a]; self; end
  def draw_rect(x, y, *a); @log << [:rect, x + @ox, y + @oy, *a]; self; end
  def draw_text_mixed(x, y, s, color, _bg = nil)
    @log << [:text, x + @ox, y + @oy, s, @current_text_size, color]
    self
  end
  def present; @log << [:present]; self; end

  def texts; @log.select { |e| e[0] == :text }; end
  def count(kind); @log.count { |e| e[0] == kind }; end
end

# The framework's logger, reduced to a recorder so the warnings FmrbUI emits
# can be read back.
module Log
  @warnings = []
  class << self
    attr_reader :warnings
    def warn(msg); @warnings << msg; nil; end
    def info(msg); nil; end
    def debug(msg); nil; end
    def error(msg); nil; end
  end
end

# Stands in for an app that paints its own ground. It records the rectangles
# it was asked for and draws nothing, so a test can tell "FmrbUI asked the app"
# from "FmrbUI painted a bg rectangle itself".
class FakeBgPainter
  attr_reader :calls

  def initialize
    @calls = []
  end

  def paint_bg_rect(gfx, x, y, w, h)
    @calls << [x, y, w, h]
    nil
  end
end

# The readers FmrbUI takes from the app. The area is a framed window's: its
# origin (1, 11) goes to the gfx, which adds it as FmrbGfx does, so the log
# shows the canvas coordinates real apps produce. Events in the tests are
# given in the window's coordinates and put in the area's by ev (run.rb), as
# app.c does.
class FakeApp
  attr_reader :gfx, :user_area_width, :user_area_height, :window_width, :window_height
  attr_reader :attached_uis

  def initialize(gfx, x0 = 1, y0 = 11, w = 320, h = 240)
    @gfx = gfx
    gfx.ox = x0
    gfx.oy = y0
    @user_area_width = w - 2 * x0
    @user_area_height = h - y0 - x0
    @window_width = w
    @window_height = h
    @attached_uis = []
  end

  # The real base keeps these so clear_user_area can redraw them; here it is
  # only proof that FmrbUI announced itself.
  def attach_ui(ui)
    @attached_uis << ui
    nil
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
