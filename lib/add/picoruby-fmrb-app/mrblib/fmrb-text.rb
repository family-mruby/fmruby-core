# FmrbText - fitting a text into a width, for apps that lay out their own
# screen (doc/app_refactor R5).
#
# Mixed into FmrbGfx, so an app calls it on its @gfx. Everything is measured
# in pixels with #text_width, i.e. with FONT_METRICS for the font and text
# size selected at the time of the call -- the same numbers the text is then
# drawn with. The host needs nothing but text_width(str) and draw_text(x, y,
# str, color), which is what lets the mahjong overflow checker mix it into
# its recording stand-in as well.
#
# Iteration is by UTF-8 character with getbyte/byteslice: picoruby has no
# Regexp, and String#[] on a multi-byte string walks from the start each time.
#
# Spinel programs (desktop, editor) have their own FmrbGfx and do not see
# this module.
module FmrbText
  # str cut so that it fits width pixels. When it had to be cut, mark is
  # appended ("…", "..") and room for it is kept: mark_w pixels, its
  # measured width when nil. A str that fits comes back as it is.
  def fit_text(str, width, mark = "", mark_w = nil)
    s = str.to_s
    return s if text_width(s) <= width
    mw = mark_w || text_width(mark)
    out = ""
    used = 0
    i = 0
    n = s.bytesize
    while i < n
      b = s.getbyte(i)
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2))
      ch = s.byteslice(i, len)
      cw = text_width(ch)
      break if used + cw + mw > width
      out << ch
      used += cw
      i += len
    end
    out << mark
  end

  # str in at most two lines of width pixels. The break goes at the last
  # space of the first line when the rest then fits on one line; otherwise
  # at the width, and the second line ends in "…" if it is still too long.
  def wrap_text(str, width)
    s = str.to_s
    return [s] if text_width(s) <= width
    first = fit_text(s, width)
    sp = -1
    i = 0
    n = first.bytesize
    while i < n
      sp = i if first.getbyte(i) == 32
      i += 1
    end
    if sp > 0
      rest = s.byteslice(sp + 1, s.bytesize - sp - 1)
      return [s.byteslice(0, sp), rest] if text_width(rest) <= width
    end
    [first, fit_text(s.byteslice(first.bytesize, s.bytesize - first.bytesize), width, "…")]
  end

  # The lines of wrap_text(str, width) drawn from (x, y) down, line_h apart.
  # Answers the y under the last one.
  def draw_text_lines(x, y, str, width, color, line_h)
    ls = wrap_text(str, width)
    i = 0
    while i < ls.size
      draw_text(x, y + i * line_h, ls[i], color)
      i += 1
    end
    y + ls.size * line_h
  end
end

class FmrbGfx
  include FmrbText
end
