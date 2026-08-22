#!/usr/bin/env ruby
# Host tests for FmrbUI (lib/add/picoruby-fmrb-app/mrblib/fmrb-ui.rb).
#
# The widget set is pure Ruby over a handful of FmrbGfx calls, so the real
# file runs here against a recording stand-in. That buys a regression net for
# the geometry, the allocation rules and the event logic without docker, a
# firmware build or a device -- the things that make the sim tier slow enough
# that nobody runs it after a small change.
#
# What this cannot see: how it looks, and whether the graphics backend draws
# what the log says. Those stay with the simulator and the hardware.
#
#   ruby test/fmrb_ui/run.rb        (or: rake ui:test)

require_relative "fake_gfx"
load File.expand_path("../../lib/add/picoruby-fmrb-app/mrblib/fmrb-ui.rb", __dir__)

# --- geometry and the text size ------------------------------------------

g = FakeGfx.new(4)                      # the app left the canvas at size 4
ui = FmrbUI.new(FakeApp.new(g))
b = ui.button(:b, 0, 0, 60, 16, "GO")
check("default text size is 1", b.text_size, 1)
g.log.clear
ui.flush
# "GO" is 12px at size 1, centred in 60: x = 1 + (60 - 12) / 2
check("measured at its own size, not the app's", g.texts[0][1], 1 + (60 - 12) / 2)
check("drawn at its own size", g.texts[0][4], 1)
check("the app's size is handed back", g.current_text_size, 4)
check("one present", g.count(:present), 1)

g2 = FakeGfx.new(1)
ui2 = FmrbUI.new(FakeApp.new(g2))
big = ui2.label(:t, 0, 0, 100, 20, "Hi", align: :center, text_size: 2)
check("per-widget size", big.text_size, 2)
g2.log.clear
ui2.flush
t = g2.texts[0]
check("width scales", t[1], 1 + (100 - 24) / 2)
check("height scales", t[2], 11 + (20 - 16) / 2)
check("drawn at 2", t[4], 2)
check("restored to 1", g2.current_text_size, 1)

g3 = FakeGfx.new(1)
ui3 = FmrbUI.new(FakeApp.new(g3), text_size: 2)
check("container default", ui3.button(:x, 0, 0, 40, 20, "A").text_size, 2)
check("widget beats container", ui3.button(:y, 0, 0, 40, 20, "A", text_size: 1).text_size, 1)

g4 = FakeGfx.new(1)
ui4 = FmrbUI.new(FakeApp.new(g4))
ui4.button(:a, 0, 0, 40, 16, "A")
ui4.button(:b, 50, 0, 40, 16, "B")
g4.log.clear
ui4.flush
check("no size command when nothing differs", g4.count(:size), 0)

# --- events ---------------------------------------------------------------

def ev(type, x, y, button = 1) = { type: type, x: x, y: y, button: button }

g5 = FakeGfx.new(1)
ui5 = FmrbUI.new(FakeApp.new(g5))
ui5.button(:go, 0, 0, 40, 16, "GO")
check("press reports nothing", ui5.handle(ev(:mouse_down, 10, 15)), nil)
check("release on the widget reports it", ui5.handle(ev(:mouse_up, 10, 15)), :go)
ui5.handle(ev(:mouse_down, 10, 15))
check("release elsewhere reports nothing", ui5.handle(ev(:mouse_up, 200, 200)), nil)
check("a second release has nothing pressed", ui5.handle(ev(:mouse_up, 10, 15)), nil)
g5.log.clear
check("mouse_move is ignored", ui5.handle({ type: :mouse_move, x: 10, y: 15, button: 0 }), nil)
check("and draws nothing", g5.log.length, 0)
check("button 3 is ignored", ui5.handle(ev(:mouse_down, 10, 15, 3)), nil)

# --- toggles and groups ---------------------------------------------------

g6 = FakeGfx.new(1)
ui6 = FmrbUI.new(FakeApp.new(g6))
ui6.toggle(:t1, 0, 0, 40, 16, "1", group: :g, on: true)
ui6.toggle(:t2, 50, 0, 40, 16, "2", group: :g)
ui6.toggle(:free, 0, 20, 40, 16, "F")
ui6.handle(ev(:mouse_down, 60, 15))
check("picking one reports it", ui6.handle(ev(:mouse_up, 60, 15)), :t2)
check("the group's other one went off", ui6.on?(:t1), false)
check("the picked one is on", ui6.on?(:t2), true)
ui6.handle(ev(:mouse_down, 60, 15))
ui6.handle(ev(:mouse_up, 60, 15))
check("pressing it again keeps it on", ui6.on?(:t2), true)
ui6.handle(ev(:mouse_down, 10, 35))
ui6.handle(ev(:mouse_up, 10, 35))
check("an ungrouped toggle flips", ui6.on?(:free), true)
ui6.set_on(:t1, true)
check("set_on applies the group too", ui6.on?(:t2), false)

# --- stepper --------------------------------------------------------------

g7 = FakeGfx.new(1)
ui7 = FmrbUI.new(FakeApp.new(g7))
st = ui7.stepper(:s, 0, 0, 66, 14, 3, 1, 9)
st.suffix = "/9"
check("stepper text", st.text, "3/9")
ui7.handle(ev(:mouse_down, 3, 15))              # left arrow
check("left half steps down", ui7.handle(ev(:mouse_up, 3, 15)), :s)
check("value moved", st.value, 2)
ui7.handle(ev(:mouse_down, 63, 15))             # right arrow
ui7.handle(ev(:mouse_up, 63, 15))
check("right half steps up", st.value, 3)
ui7.handle(ev(:mouse_down, 33, 15))             # the value field, neither half
check("the middle does nothing", ui7.handle(ev(:mouse_up, 33, 15)), nil)
st.set_value(9)
ui7.handle(ev(:mouse_down, 63, 15))
check("no report at the end of the range", ui7.handle(ev(:mouse_up, 63, 15)), nil)
st.set_range(1, 2)
check("set_range pulls the value in", st.value, 2)
check("and rebuilds the text", st.text, "2/9")

# --- enum -----------------------------------------------------------------

OPTS = ["light", "dark", "classic"].freeze
g8 = FakeGfx.new(1)
ui8 = FmrbUI.new(FakeApp.new(g8))
en = ui8.enum(:theme, 0, 0, 90, 14, OPTS, index: 1)
check("enum starts at the index given", en.value, 1)
check("enum shows that entry", en.option_text, "dark")
check("the container can read it", ui8.option_text(:theme), "dark")
check("its range comes from the table", [en.min, en.max], [0, 2])
ui8.handle(ev(:mouse_down, 87, 15))
check("stepping reports it", ui8.handle(ev(:mouse_up, 87, 15)), :theme)
check("moved on", en.value, 2)
check("text followed", en.option_text, "classic")
ui8.handle(ev(:mouse_down, 87, 15))
check("no wrap at the last entry", ui8.handle(ev(:mouse_up, 87, 15)), nil)
# The point of Enum over Stepper: moving it builds no string.
check("shows the table's own object", en.option_text.equal?(OPTS[2]), true)
en0 = ui8.enum(:empty, 0, 20, 90, 14, [].freeze)
check("an empty table is survivable", [en0.value, en0.min, en0.max], [0, 0, 0])

# --- background, visibility, redraw ---------------------------------------

g9 = FakeGfx.new(1)
ui9 = FmrbUI.new(FakeApp.new(g9), bg: 0x12)
lb = ui9.label(:l, 0, 0, 30, 10, "x")
check("bg reaches the widget", lb.bg, 0x12)
g9.log.clear
ui9.flush
check("a label paints that bg", g9.log[0], [:fill, 1, 11, 30, 10, 0x12])
ui9.set_visible(:l, false)
g9.log.clear
ui9.flush
check("a hidden widget's hole uses it too", g9.log[0], [:fill, 1, 11, 30, 10, 0x12])
g9.log.clear
check("a clean flush draws nothing", ui9.flush, false)
check("and does not present", g9.count(:present), 0)
ui9.set_text(:l, "x")
check("the same text is not a change", ui9.flush, false)
ui9.set_visible(:l, true)
ui9.set_text(:l, "y")
check("a different text is", ui9.flush, true)

# --- accent ----------------------------------------------------------------

g14 = FakeGfx.new(1)
ui14 = FmrbUI.new(FakeApp.new(g14))
ui14.button(:plain, 0, 0, 40, 16, "P")
ui14.button(:green, 0, 20, 40, 16, "G", accent: 0x34)
ui14.toggle(:tog, 0, 40, 40, 16, "T", on: true, accent: 0xC0)
g14.log.clear
ui14.flush
fills = g14.log.select { |e| e[0] == :fill }
check("a plain button uses the theme", fills[0][5], FmrbConst::THEME_BUTTON)
check("an accented one uses its colour", fills[1][5], 0x34)
check("an accented toggle replaces the highlight", fills[2][5], 0xC0)
# On an accent the text goes light; on the pale theme highlight it stays dark,
# which is the whole reason the colour is worth having.
check("text on an accented toggle is light", g14.texts[2][5], FmrbConst::THEME_TEXT_LIGHT)
g14.log.clear
ui14.toggle(:tog2, 0, 60, 40, 16, "T2", on: true)
ui14.flush
check("text on the plain highlight stays dark", g14.texts[0][5], FmrbConst::THEME_TEXT)

# --- scrollbar ------------------------------------------------------------

g10 = FakeGfx.new(1)
ui10 = FmrbUI.new(FakeApp.new(g10))
# The rect is the bar: 10 wide, 100 tall, at user-area (0, 0) -> (1, 11).
sb = ui10.scrollbar(:sb, 0, 0, 10, 100, 50, 10)
check("starts where told", sb.value, 0)
check("knows the list size", [sb.total, sb.visible_count], [50, 10])
check("it is needed", sb.active?, true)
# At the top there is nowhere to go, so pressing up moves nothing and, like
# every other widget, says nothing. It is still held, which is what an app
# repeating the scroll watches.
check("pressing up at the top reports nothing", ui10.handle(ev(:mouse_down, 5, 15)), nil)
check("and moves nothing", sb.value, 0)
check("direction is up while held", ui10.direction(:sb), -1)
ui10.handle(ev(:mouse_up, 5, 15))
# Away from the top it reports on the press, unlike a button.
sb.set_value(5)
check("pressing up reports at once", ui10.handle(ev(:mouse_down, 5, 15)), :sb)
check("and has already moved", sb.value, 4)
ui10.handle(ev(:mouse_up, 5, 15))
sb.set_value(0)
check("released, no direction", ui10.direction(:sb), 0)
check("the bottom arrow scrolls down", (ui10.handle(ev(:mouse_down, 5, 105)); sb.value), 1)
check("direction is down while held", ui10.direction(:sb), 1)
ui10.handle(ev(:mouse_up, 5, 105))
check("release reports nothing", ui10.handle(ev(:mouse_up, 5, 105)), nil)
# Below the thumb pages down, above it pages up.
sb.set_value(25)
check("clicking below the thumb goes down", (ui10.handle(ev(:mouse_down, 5, 100)); sb.value), 26)
ui10.handle(ev(:mouse_up, 5, 100))
sb.set_value(25)
check("clicking above it goes up", (ui10.handle(ev(:mouse_down, 5, 25)); sb.value), 24)
ui10.handle(ev(:mouse_up, 5, 25))
check("cannot go past the end", (sb.set_value(999); sb.value), 40)
check("nor before the start", (sb.set_value(-5); sb.value), 0)
# Everything fits: no bar, and clicks fall through to whatever is underneath.
sb.set_range(8, 10)
check("not needed when it all fits", sb.active?, false)
check("and it stops taking clicks", sb.hit?(5, 15), false)
check("a click passes through", ui10.handle(ev(:mouse_down, 5, 15)), nil)
sb.set_range(50, 10)
check("needed again", sb.active?, true)
check("range change pulls the value in", (sb.set_value(45); sb.set_range(20, 10); sb.value), 10)

# Drawing: the bar paints its arrows and thumb, and paints nothing but the
# background when it is not needed. (draw_line was missing from FakeGfx until
# a text field's caret hit it -- the bar's drawing had never been exercised.)
sb.set_range(50, 10)
g10.log.clear
sb.dirty = true
ui10.flush
check("the bar draws arrows and a thumb", g10.count(:fill) >= 3 && g10.count(:line) >= 7, true)
sb.set_range(5, 10)
g10.log.clear
sb.dirty = true
ui10.flush
check("and only the background when it fits", g10.count(:fill), 1)
check("with no arrows", g10.count(:line), 0)
sb.set_range(50, 10)

# The other widgets still report on release, not on press.
g11 = FakeGfx.new(1)
ui11 = FmrbUI.new(FakeApp.new(g11))
ui11.button(:b, 0, 0, 40, 16, "B")
check("a button still says nothing on press", ui11.handle(ev(:mouse_down, 10, 15)), nil)

# --- text field ------------------------------------------------------------

def key(ch, keycode = 0) = { type: :key_down, character: ch, keycode: keycode, x: 0, y: 0, button: 0 }

g12 = FakeGfx.new(1)
ui12 = FmrbUI.new(FakeApp.new(g12))
tf = ui12.text_field(:name, 0, 0, 100, 12, "song", max: 8)
ui12.button(:save, 0, 20, 40, 12, "Save")
check("starts with the text given", ui12.field_text(:name), "song")
check("nobody has the focus yet", ui12.focused, nil)
# A key with nothing focused is ignored, exactly as before text fields existed.
check("a key goes nowhere unfocused", ui12.handle(key(65)), nil)
check("and changed nothing", ui12.field_text(:name), "song")

ui12.handle(ev(:mouse_down, 10, 15))
# Focusing is not confirming: the click must not look like Enter, or an app
# acts on a half-typed value.
check("clicking a field reports nothing", ui12.handle(ev(:mouse_up, 10, 15)), nil)
check("but it does focus it", ui12.focused, :name)
check("typing appends", (ui12.handle(key(115)); ui12.field_text(:name)), "songs")
check("a printable key reports nothing", ui12.handle(key(116)), nil)
check("backspace takes from the end", (ui12.handle(key(8)); ui12.field_text(:name)), "songs")
check("Enter reports the field", ui12.handle(key(13)), :name)
check("the text survives Enter", ui12.field_text(:name), "songs")
check("control codes are ignored", (ui12.handle(key(7)); ui12.field_text(:name)), "songs")
# max is a limit, not a suggestion.
ui12.handle(key(122)); ui12.handle(key(122)); ui12.handle(key(122)); ui12.handle(key(122))
check("stops at max", ui12.field_text(:name), "songszzz")
check("escape drops the focus", (ui12.handle(key(27)); ui12.focused), nil)
check("and the keys stop landing", (ui12.handle(key(97)); ui12.field_text(:name)), "songszzz")

# Clicking something that cannot hold the focus takes it away.
ui12.focus(:name)
check("focus by id works", ui12.focused, :name)
ui12.handle(ev(:mouse_down, 10, 35))
ui12.handle(ev(:mouse_up, 10, 35))
check("clicking a button clears the focus", ui12.focused, nil)

# Two fields: the focus moves, it does not spread.
g13 = FakeGfx.new(1)
ui13 = FmrbUI.new(FakeApp.new(g13))
a = ui13.text_field(:a, 0, 0, 60, 12, "")
b = ui13.text_field(:b, 0, 20, 60, 12, "")
ui13.focus(:a)
ui13.handle(key(120))
ui13.handle(ev(:mouse_down, 10, 35))
ui13.handle(ev(:mouse_up, 10, 35))
check("the second field has it", ui13.focused, :b)
check("the first gave it up", a.focused?, false)
ui13.handle(key(121))
check("typing lands in the focused one", [a.field_text, b.field_text], ["x", "y"])

# The caret is drawn only while focused, and never blinks: one draw per change.
g13.log.clear
ui13.flush
check("the focused field draws one caret", g13.count(:line), 1)
check("a settled field redraws nothing", ui13.flush, false)
ui13.focus(nil)
g13.log.clear
ui13.flush
check("no caret once the focus is gone", g13.count(:line), 0)

puts(Check.failed.zero? ? "fmrb_ui: all checks passed" : "fmrb_ui: #{Check.failed} FAILED")
exit(Check.failed.zero? ? 0 : 1)
