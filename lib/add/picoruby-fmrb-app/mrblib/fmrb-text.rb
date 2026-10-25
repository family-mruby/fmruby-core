# FmrbText - measuring, fitting and wrapping a text in pixels (doc/app_refactor
# R5, R6). Stateless: it remembers nothing between calls.
#
# Which helper (doc/reference/screen_helpers.md):
#   FmrbGfx  draws -- including the text helpers here (FmrbText) and the
#            rounded button picture (FmrbGfx#draw_button).
#   FmrbUI   operates -- widgets made once that keep their state (focus, keys,
#            act on release, theme colours; settings and tool screens, shared
#            with the Spinel programs), and FmrbUI::HitAreas, the click
#            targets of a screen redrawn every time.
#
# Mixed into FmrbGfx, so an app calls it on its @gfx. Everything is measured
# in pixels with #text_width, i.e. with FONT_METRICS for the font and text
# size selected at the time of the call (or for the family passed in) -- the
# same numbers the text is then drawn with. The host needs nothing but
# text_width(str, family) and draw_text(x, y, str, color), which is what lets
# the mahjong overflow checker mix it into its recording stand-in as well.
#
# Iteration is by UTF-8 character with getbyte/byteslice: picoruby has no
# Regexp, and String#[] on a multi-byte string walks from the start each time.
#
# The Spinel programs (desktop, editor) take this file too and include it in
# their own FmrbGfx (tool/spinel/gen_app_combined.rb): FmrbUI fits its
# labels with fit_text. Keep it to what Spinel types: no blocks, no Hash.
module FmrbText
  # str cut so that it fits width pixels. When it had to be cut, mark is
  # appended ("…", "..") and room for it is kept: mark_w pixels, its
  # measured width when nil. A str that fits comes back as it is. family
  # measures in that font (FmrbUI passes :default, the font it draws with)
  # instead of the current one.
  #
  # The to_s / to_i keep the loop in concrete types under Spinel, whatever
  # the caller hands in (FmrbUI's own values are untyped there).
  def fit_text(str, width, mark = "", mark_w = nil, family = nil)
    s = str.to_s
    w = width.to_i
    return s if text_width(s, family).to_i <= w
    mw = mark_w ? mark_w.to_i : text_width(mark, family).to_i
    out = "".dup
    used = 0
    i = 0
    n = s.bytesize
    while i < n
      b = s.getbyte(i)
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2))
      ch = s.byteslice(i, len).to_s
      cw = text_width(ch, family).to_i
      break if used + cw + mw > w
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
      rest = s.byteslice(sp + 1, s.bytesize - sp - 1).to_s
      return [s.byteslice(0, sp).to_s, rest] if text_width(rest) <= width
    end
    [first, fit_text(s.byteslice(first.bytesize, s.bytesize - first.bytesize).to_s, width, "…")]
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
