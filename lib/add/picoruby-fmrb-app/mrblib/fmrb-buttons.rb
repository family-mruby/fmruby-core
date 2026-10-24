# FmrbButtons - the click targets of a screen an app draws itself
# (doc/app_refactor R5).
#
# For apps that redraw the whole screen and list the buttons as they go:
# clear before drawing, button (or add, for a target the app draws itself)
# for each one, and at(x, y) for a click. FmrbUI is the other way round --
# widgets made once, each redrawing itself, square with a border, the mixed
# 6x8/8x8 font, acting on release -- and does not fit such a screen.
#
# A target is [id, x, y, w, h, data]: data is whatever the app wants back
# with the click (nil when not given). The first target that holds the
# point wins.
class FmrbButtons
  def initialize(gfx)
    @gfx = gfx
    @list = []
  end

  def clear
    @list.clear
    self
  end

  # A click target without drawing anything.
  def add(id, x, y, w, h, data = nil)
    @list << [id, x, y, w, h, data]
    self
  end

  # A rounded button filled with fill, label centred in fg with the current
  # font, and its target.
  def button(id, x, y, w, h, label, fill, fg, radius = 3)
    add(id, x, y, w, h)
    @gfx.fill_round_rect(x, y, w, h, radius, fill)
    @gfx.draw_text(x + (w - @gfx.text_width(label)) / 2, y + (h - @gfx.font_height) / 2, label, fg)
    self
  end

  # The target [id, x, y, w, h, data] at (x, y), or nil.
  def at(x, y)
    i = 0
    while i < @list.size
      b = @list[i]
      return b if x >= b[1] && x < b[1] + b[3] && y >= b[2] && y < b[2] + b[4]
      i += 1
    end
    nil
  end

  # The id of the target at (x, y), or nil.
  def id_at(x, y)
    b = at(x, y)
    b ? b[0] : nil
  end
end
