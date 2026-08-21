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

puts(Check.failed.zero? ? "fmrb_ui: all checks passed" : "fmrb_ui: #{Check.failed} FAILED")
exit(Check.failed.zero? ? 0 : 1)
