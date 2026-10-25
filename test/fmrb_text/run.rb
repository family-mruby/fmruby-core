#!/usr/bin/env ruby
# Host tests for FmrbText (fitting a text into a width), FmrbGfx#draw_button
# and FmrbUI::HitAreas (click targets): lib/add/picoruby-fmrb-app/mrblib/
# fmrb-text.rb, fmrb-gfx.rb and fmrb-ui.rb.
#
# All are plain Ruby over FmrbGfx#text_width, so the real gem files run
# here -- fmrb-gfx.rb included, with its C half stubbed -- and the answers
# are compared with the helpers the apps had before (doc/app_refactor R5):
# the othello table's column counting (a wide character 2 columns of 6 px)
# and the mahjong table's pixel fit with "..".
#
#   ruby test/fmrb_text/run.rb        (or: rake text:test)

module FmrbConst
  HW_FAMILY = "modern"
  FONT_JA = true
  # Read by fmrb-ui.rb when it loads.
  THEME_WINDOW_BG = 0xFF
  THEME_TEXT = 0x00
  THEME_TEXT_LIGHT = 0xFF
  THEME_HIGHLIGHT = 0x1F
  THEME_BORDER = 0x6D
  THEME_BUTTON = 0x6D
end

GEM = File.expand_path("../../lib/add/picoruby-fmrb-app/mrblib", __dir__)

# The C half of FmrbGfx (gfx.c): record what is drawn.
class FmrbGfx
  attr_reader :log

  def _init(_id) = (@log = [])
  def _set_font(*) = nil
  def _set_text_size(_n) = nil
  def _draw_text(x, y, s, c, *) = @log << [:text, x, y, s, c]
  def fill_round_rect(x, y, w, h, r, c) = @log << [:round, x, y, w, h, r, c]
end
load File.join(GEM, "fmrb-gfx.rb")
load File.join(GEM, "fmrb-text.rb")
load File.join(GEM, "fmrb-ui.rb")

$failed = 0
$n = 0
def check(name, got, want)
  $n += 1
  return if got == want
  $failed += 1
  puts "FAIL #{name}: got #{got.inspect}, want #{want.inspect}"
end

# ---------------------------------------------------------------- the old helpers

# othello.app.rb before R5 (the same fit is in board.app.rb).
module Old
  def self.text_w(s)
    w = 0
    i = 0
    while i < s.length
      w += s[i, 1].bytesize > 1 ? 12 : 6
      i += 1
    end
    w
  end

  def self.fit(s, cols)
    s = s.to_s
    out = ""
    used = 0
    i = 0
    while i < s.length
      ch = s[i, 1]
      w = ch.bytesize > 1 ? 2 : 1
      break if used + w > cols
      out += ch
      used += w
      i += 1
    end
    out
  end

  def self.fit_dots(s, cols)
    s = s.to_s
    return s if text_w(s) <= cols * 6
    return "" if cols < 2
    fit(s, cols - 2) + "…"
  end

  def self.wrap2(s, cols)
    s = s.to_s
    return [s] if text_w(s) <= cols * 6
    first = fit(s, cols)
    sp = -1
    i = 0
    while i < first.length
      sp = i if first[i, 1] == " "
      i += 1
    end
    if sp > 0 && text_w(s[sp + 1, s.length].to_s) <= cols * 6
      return [s[0, sp], s[sp + 1, s.length].to_s]
    end
    [first, fit_dots(s[first.length, s.length].to_s, cols)]
  end

  # mahjong.app.rb before R5, with its tw (@fw: the font's px x the size).
  def self.tw(s, fw)
    w = 0
    s.each_char { |ch| w += ch.bytesize == 1 ? fw / 2 : fw }
    w
  end

  def self.mj_fit(s, w, fw)
    return s if tw(s, fw) <= w
    out = ""
    s.each_char do |ch|
      break if tw(out, fw) + tw(ch, fw) + 12 > w
      out << ch
    end
    "#{out}.."
  end

  # walk_scroller.app.rb before R5 (the default 6x8 font).
  def self.ws_fit(t, n)
    t.length > n ? t[0, n] : t
  end
end

# ---------------------------------------------------------------- the strings

TEXTS = [
  "", "a", "abc", "あ", "席に着いた (色は始めるときに決まる)", "観戦中: 空いた席に座れる",
  "卓の主 (*) が始める", "残り 30 秒 (切れたら CPU が打つ)", "OwnLongName-abcdefgh",
  "LongPlayerName-abcdefghijklmnopq", "ながいなまえのプレイヤーさんabcdefg",
  "オセロ (卓): 卓を選ぶ (Enter で入る)", "ゲームホストを探しています (games/othello*)",
  "r4agames/games/othello1  対局中  fmruby-a1b2c3 / Jev", "a b c d e f g h i j k l m n o p",
  " leading space and more words", "nospaceatallbutquitealongwordindeed", "ｱｲｳｴｵ ﾊﾝｶｸ", "…already…",
  "東 ながいなまえ 25000", "絵文字なし mixed 日本語 and English words here"
]
srand(5)
pool = ["a", "b", " ", "あ", "漢", "ｱ", "-", "1", "…"]
200.times { TEXTS << Array.new(rand(0..40)) { pool.sample }.join }

g = FmrbGfx.new(0)
g.set_font(:ja, 12)
g.set_text_size(1)

# ---------------------------------------------------------------- fit_text

TEXTS.each do |s|
  (0..40).each do |cols|
    w = cols * 6
    check("fit #{s.inspect} #{cols}", g.fit_text(s, w), Old.fit(s, cols))
    # A width between two columns gives the same (every width is a multiple of 6).
    check("fit #{s.inspect} #{w + 5}px", g.fit_text(s, w + 5), Old.fit(s, cols))
    check("fit_dots #{s.inspect} #{cols}", g.fit_text(s, w, "…"), Old.fit_dots(s, cols)) if cols >= 2
    check("wrap2 #{s.inspect} #{cols}", g.wrap_text(s, w), Old.wrap2(s, cols)) if cols >= 2
  end
end

# The mahjong table's old fit (a fixed 12 px for "..") through mark_w, and
# the measured mark it uses now, at both text sizes.
[[12, 1], [12, 2], [16, 1]].each do |px, size|
  g.set_font(:ja, px)
  g.set_text_size(size)
  fw = px * size
  TEXTS.each do |s|
    [0, 6, 11, 12, 24, 40, 80, 100, 141, 200].each do |w|
      check("mahjong fit #{s.inspect} #{w} at #{px}x#{size}", g.fit_text(s, w, "..", 12), Old.mj_fit(s, w, fw))
      # With the mark measured (mahjong since R5), a cut line never passes w.
      r = g.fit_text(s, w, "..")
      check("mahjong fit #{s.inspect} #{w} at #{px}x#{size} stays inside", g.text_width(r) <= w || w < g.text_width(".."), true)
    end
  end
end

# walk_scroller: the default font, ASCII lines.
g.set_font(:default)
g.set_text_size(1)
["TUNE (no sensor yet)", "TUNE abcdefghijklmnopqrstuvwxyz0123456789abcdef",
 "offline: already on the network (waiting to reconnect after the old session)", "waiting for a step or script node"].each do |s|
  check("walk fit #{s}", g.fit_text(s, 38 * 6), Old.ws_fit(s, 38))
end

# FmrbUI measures in the mixed renderer's font whatever font is selected:
# family :default, ASCII 6 px and a wide character 8 px.
g.set_font(:ja, 12)
check("family :default fits", g.fit_text("あいう abc", 48, "..", nil, :default), "あいう abc")
check("family :default cuts", g.fit_text("あいう abcdef", 42, "..", nil, :default), "あいう ..")
check("family :default, current font", g.fit_text("あいう abcdef", 42, ".."), "あい..")

# ---------------------------------------------------------------- draw_text_lines

g.set_font(:ja, 12)
g.log.clear
y = g.draw_text_lines(10, 20, "席に着いた (色は始めるときに決まる)", 23 * 6, 0x6D, 15)
check("two lines answer the y under them", y, 50)
check("two lines drawn", g.log, [[:text, 10, 20, "席に着いた (色は始める", 0x6D], [:text, 10, 35, "ときに決まる)", 0x6D]])
g.log.clear
check("one line", g.draw_text_lines(0, 0, "short", 60, 1, 15), 15)
check("nothing to wrap", g.log.size, 1)

# ---------------------------------------------------------------- draw_button, HitAreas

b = FmrbUI::HitAreas.new
g.log.clear
g.draw_button(210, 180, 60, 16, "立つ[S]", 0x13, 0xFF)
b.add(:stand, 210, 180, 60, 16)
g.draw_button(300, 100, 27, 15, "戻", 0x13, 0xFF)
b.add(:undo, 300, 100, 27, 15)
check("round box", g.log[0], [:round, 210, 180, 60, 16, 3, 0x13])
# "立つ[S]" is 2 x 12 + 3 x 6 = 42 px: centred in 60, 2 px down in 16 (the othello button).
check("label centred (16 high)", g.log[1], [:text, 210 + (60 - 42) / 2, 182, "立つ[S]", 0xFF])
# The board's 15 px button: 1 px down.
check("label centred (15 high)", g.log[3], [:text, 300 + (27 - 12) / 2, 101, "戻", 0xFF])
check("hit inside", b.id_at(210, 180), :stand)
check("hit last pixel", b.id_at(269, 195), :stand)
check("miss right edge", b.id_at(270, 190), nil)
check("miss below", b.id_at(220, 196), nil)
b.add(:row, 0, 0, 400, 20, [3])
check("data comes back", b.at(5, 5), [:row, 0, 0, 400, 20, [3]])
b.add(:under, 0, 0, 400, 20)
check("the first target wins", b.id_at(5, 5), :row)
b.clear
check("cleared", b.id_at(5, 5), nil)

puts($failed.zero? ? "fmrb_text: all #{$n} checks passed" : "fmrb_text: #{$failed} of #{$n} FAILED")
exit($failed.zero? ? 0 : 1)
