# FmrbUI - small widget set for apps that draw their own window
#
# Goal: make a tool with a screen short to write. Apps used to build their own
# button tables, hit tests and pressed-state drawing; this holds one copy of
# that.
#
# The two rules the implementation is built around (see doc/ui_widgets/plan.md):
#
#   1. Nothing is allocated in the steady state. Widgets are created in
#      on_create; the event and redraw paths allocate nothing at all (no Hash
#      or Array literals, no string interpolation, no keyword arguments). The
#      single exception is the value string a Stepper rebuilds when the user
#      actually changed the value.
#   2. Nothing is drawn per frame. Each widget carries a dirty flag and #flush
#      draws only those, calling present once - and not at all when nothing was
#      dirty.
#
# Iteration is therefore always `while` (a block call costs ~0.4 ms here), the
# widget state lives in instance variables rather than Hashes (so the Spinel
# AOT build keeps concrete types), and no widget stores a callback block
# (Spinel cannot capture the enclosing locals). #handle returns the id of the
# widget the user operated, and the app dispatches with a case.
#
# Coordinates passed to the factory methods are relative to the app's user
# area; FmrbUI adds the user-area origin once, when the widget is created.
class FmrbUI
  # Theme colors, resolved once into class constants so no widget carries its
  # own palette.
  C_BG         = FmrbConst::THEME_WINDOW_BG
  C_TEXT       = FmrbConst::THEME_TEXT
  C_TEXT_LIGHT = FmrbConst::THEME_TEXT_LIGHT
  C_HIGHLIGHT  = FmrbConst::THEME_HIGHLIGHT
  C_BORDER     = FmrbConst::THEME_BORDER
  C_BUTTON     = FmrbConst::THEME_BUTTON

  # Glyph cell height of the mixed renderer (Font0 and misaki_8 are both 8px).
  TEXT_H = 8
  # Width of the < and > halves of a Stepper.
  STEP_W = 14

  # Common state and geometry. Subclasses add their own contents and drawing.
  class Widget
    attr_reader :id, :x, :y, :w, :h
    attr_accessor :dirty, :enabled, :visible
    # What a widget paints where it is not itself: a Label's whole box, a
    # Stepper's value field, and the hole left behind when it is hidden.
    # FmrbUI sets it from its own bg when the widget is added, so an app on a
    # dark background does not get white patches.
    attr_accessor :bg

    def initialize(id, x, y, w, h)
      @id = id
      @x = x
      @y = y
      @w = w
      @h = h
      @dirty = true
      @enabled = true
      @visible = true
      @bg = C_BG
    end

    def place(x, y, w, h)
      @x = x
      @y = y
      @w = w
      @h = h
      @dirty = true
      relayout
      nil
    end

    def hit?(px, py)
      px >= @x && px < @x + @w && py >= @y && py < @y + @h
    end

    # Hooks. The base implementations exist so a call on a mixed widget array
    # always resolves, and so subclasses only override what they use.
    #
    # draw_widget is not called draw on purpose. FmrbUI#flush calls it on a
    # base-typed receiver, and Spinel answers such a call by compiling every
    # method of that name in the whole program -- including GfxBlock#draw,
    # which takes **kwargs and does not compile at all. A widely used method
    # name is a liability under a poly receiver; this one is unique.
    def relayout; nil; end
    def draw_widget(gfx); nil; end
    def press(px); @dirty = true; nil; end
    def release; @dirty = true; nil; end
    # Confirm the operation (mouse released on top of the widget). Returns
    # true when the app should be told about it.
    def activate; false; end
    def set_text(text, gfx); nil; end
    def group; nil; end
    def on?; false; end
    def set_on(flag); nil; end
    def value; 0; end
    def set_value(v); false; end

    private

    # Text position for a horizontally centered single line.
    def center_text(tw)
      @tx = @x + (@w - tw) / 2
      @ty = @y + (@h - TEXT_H) / 2
      nil
    end
  end

  # Static single-line text on the window background.
  class Label < Widget
    attr_reader :text

    def initialize(id, x, y, w, h, text, align, gfx)
      super(id, x, y, w, h)
      @align = align
      @text = text
      @tw = gfx.text_width(text, :default)
      relayout
    end

    def set_text(text, gfx)
      return nil if @text == text
      @text = text
      @tw = gfx.text_width(text, :default)
      relayout
      @dirty = true
      nil
    end

    def relayout
      if @align == :center
        @tx = @x + (@w - @tw) / 2
      elsif @align == :right
        @tx = @x + @w - @tw
      else
        @tx = @x
      end
      @ty = @y + (@h - TEXT_H) / 2
      nil
    end

    def draw_widget(gfx)
      gfx.fill_rect(@x, @y, @w, @h, @bg)
      color = @enabled ? C_TEXT : C_BORDER
      gfx.draw_text_mixed(@tx, @ty, @text, color)
      nil
    end
  end

  # Momentary push button. Inverted while held, reports its id on release.
  class Button < Widget
    attr_reader :text

    def initialize(id, x, y, w, h, text, gfx)
      super(id, x, y, w, h)
      @text = text
      @tw = gfx.text_width(text, :default)
      @pressed = false
      relayout
    end

    def set_text(text, gfx)
      return nil if @text == text
      @text = text
      @tw = gfx.text_width(text, :default)
      relayout
      @dirty = true
      nil
    end

    def relayout
      center_text(@tw)
    end

    def press(px)
      @pressed = true
      @dirty = true
      nil
    end

    def release
      @pressed = false
      @dirty = true
      nil
    end

    def activate
      true
    end

    def draw_widget(gfx)
      if @pressed
        gfx.fill_rect(@x, @y, @w, @h, C_TEXT_LIGHT)
        gfx.draw_rect(@x, @y, @w, @h, C_BORDER)
        gfx.draw_text_mixed(@tx, @ty, @text, C_BUTTON)
      else
        gfx.fill_rect(@x, @y, @w, @h, C_BUTTON)
        gfx.draw_rect(@x, @y, @w, @h, C_BORDER)
        color = @enabled ? C_TEXT_LIGHT : C_BORDER
        gfx.draw_text_mixed(@tx, @ty, @text, color)
      end
      nil
    end
  end

  # On/off button. With a group it behaves as a radio button: activating it
  # switches it on and FmrbUI switches the rest of the group off.
  class Toggle < Widget
    attr_reader :text, :on_text

    def initialize(id, x, y, w, h, text, group, on, on_text, gfx)
      super(id, x, y, w, h)
      @text = text
      @on_text = on_text
      @group = group
      @on = on
      @pressed = false
      @tw = gfx.text_width(text, :default)
      @on_tw = on_text ? gfx.text_width(on_text, :default) : @tw
      relayout
    end

    def group; @group; end
    def on?; @on; end

    def set_on(flag)
      return nil if @on == flag
      @on = flag
      relayout
      @dirty = true
      nil
    end

    def set_text(text, gfx)
      return nil if @text == text
      @text = text
      @tw = gfx.text_width(text, :default)
      relayout
      @dirty = true
      nil
    end

    def relayout
      if @on && @on_text
        center_text(@on_tw)
      else
        center_text(@tw)
      end
    end

    def press(px)
      @pressed = true
      @dirty = true
      nil
    end

    def release
      @pressed = false
      @dirty = true
      nil
    end

    def activate
      if @group
        # Radio behaviour: the pressed one is always the selected one.
        @on = true
      else
        @on = @on ? false : true
      end
      relayout
      @dirty = true
      true
    end

    def draw_widget(gfx)
      label = (@on && @on_text) ? @on_text : @text
      if @on
        gfx.fill_rect(@x, @y, @w, @h, C_HIGHLIGHT)
        color = C_TEXT
      else
        gfx.fill_rect(@x, @y, @w, @h, C_BUTTON)
        color = @enabled ? C_TEXT_LIGHT : C_BORDER
      end
      gfx.draw_rect(@x, @y, @w, @h, C_BORDER)
      # Pressed state is an inner border rather than an inversion: inverting
      # would read as the on/off state instead of "you are holding this". It
      # is drawn in the label color because the default theme has
      # THEME_BORDER == THEME_BUTTON, so a border-colored line on an off
      # toggle would be invisible.
      if @pressed && @w > 2 && @h > 2
        gfx.draw_rect(@x + 1, @y + 1, @w - 2, @h - 2, color)
      end
      gfx.draw_text_mixed(@tx, @ty, label, color)
      nil
    end
  end

  # "< value >" integer stepper. The pressed half is inverted; the id is
  # reported only when the value actually moved.
  class Stepper < Widget
    attr_reader :value, :min, :max, :step, :text

    def initialize(id, x, y, w, h, value, min, max, step, gfx)
      super(id, x, y, w, h)
      @min = min
      @max = max
      @step = step
      @value = clamp(value)
      @gfx = gfx
      @suffix = nil
      @text = build_text
      @tw = gfx.text_width(@text, :default)
      @pressed = 0
      relayout
    end

    # Unit shown after the number ("%" etc). Set once, at creation time.
    def suffix=(s)
      @suffix = s
      @text = build_text
      @tw = @gfx.text_width(@text, :default)
      relayout
      @dirty = true
      nil
    end

    def set_value(v)
      nv = clamp(v)
      return false if nv == @value
      @value = nv
      @text = build_text
      @tw = @gfx.text_width(@text, :default)
      relayout
      @dirty = true
      true
    end

    def relayout
      @tx = @x + STEP_W + (@w - STEP_W * 2 - @tw) / 2
      @ty = @y + (@h - TEXT_H) / 2
      nil
    end

    def press(px)
      if px < @x + STEP_W
        @pressed = -1
      elsif px >= @x + @w - STEP_W
        @pressed = 1
      else
        @pressed = 0
      end
      @dirty = true
      nil
    end

    def release
      @pressed = 0
      @dirty = true
      nil
    end

    def activate
      return false if @pressed == 0
      set_value(@value + @step * @pressed)
    end

    def draw_widget(gfx)
      # Value box.
      mid_x = @x + STEP_W
      mid_w = @w - STEP_W * 2
      gfx.fill_rect(mid_x, @y, mid_w, @h, @bg)
      gfx.draw_rect(mid_x, @y, mid_w, @h, C_BORDER)
      gfx.draw_text_mixed(@tx, @ty, @text, C_TEXT)
      draw_arrow(gfx, @x, "<", @pressed == -1)
      draw_arrow(gfx, @x + @w - STEP_W, ">", @pressed == 1)
      nil
    end

    private

    def draw_arrow(gfx, ax, glyph, pressed)
      if pressed
        bg = C_TEXT_LIGHT
        fg = C_BUTTON
      else
        bg = C_BUTTON
        fg = @enabled ? C_TEXT_LIGHT : C_BORDER
      end
      gfx.fill_rect(ax, @y, STEP_W, @h, bg)
      gfx.draw_rect(ax, @y, STEP_W, @h, C_BORDER)
      gfx.draw_text_mixed(ax + (STEP_W - 6) / 2, @y + (@h - TEXT_H) / 2, glyph, fg)
      nil
    end

    def clamp(v)
      return @min if v < @min
      return @max if v > @max
      v
    end

    def build_text
      # The only allocation on an operated path, and only when the value moved.
      @suffix ? "#{@value}#{@suffix}" : "#{@value}"
    end
  end

  # ----------------------------------------------------------------------
  # Container
  # ----------------------------------------------------------------------

  # bg is what the widgets paint behind themselves and over a hidden one.
  # It defaults to the theme's window background; an app that clears its user
  # area to something else (the monitor's dark panel) passes that instead.
  def initialize(app, bg: C_BG)
    @gfx = app.gfx
    @ox = app.user_area_x0
    @oy = app.user_area_y0
    @bg = bg
    @widgets = []
    @pressed = nil
  end

  attr_reader :widgets

  # ---- creation (on_create only; keyword arguments are fine here) ----

  def label(id, x, y, w, h, text, align: :left)
    add(Label.new(id, @ox + x, @oy + y, w, h, text, align, @gfx))
  end

  def button(id, x, y, w, h, text)
    add(Button.new(id, @ox + x, @oy + y, w, h, text, @gfx))
  end

  def toggle(id, x, y, w, h, text, group: nil, on: false, on_text: nil)
    add(Toggle.new(id, @ox + x, @oy + y, w, h, text, group, on, on_text, @gfx))
  end

  def stepper(id, x, y, w, h, value, min, max, step = 1)
    add(Stepper.new(id, @ox + x, @oy + y, w, h, value, min, max, step, @gfx))
  end

  # ---- run time (allocates nothing) ----

  # Feed one event. Returns the id of the widget whose operation completed,
  # or nil. A press only updates the pressed look; the id comes on release,
  # and only when the release lands on the same widget.
  def handle(ev)
    t = ev[:type]
    # mouse_move arrives at 30 Hz, so leave before touching anything else.
    return nil if t != :mouse_down && t != :mouse_up
    return nil if ev[:button].to_i != 1
    px = ev[:x].to_i
    py = ev[:y].to_i

    if t == :mouse_down
      i = @widgets.size - 1
      while i >= 0
        w = @widgets[i]
        if w.visible && w.enabled && w.hit?(px, py)
          w.press(px)
          @pressed = w
          return nil
        end
        i -= 1
      end
      return nil
    end

    w = @pressed
    return nil if w.nil?
    @pressed = nil
    fired = false
    if w.hit?(px, py)
      fired = w.activate
    end
    w.release
    return nil if fired == false
    g = w.group
    apply_group(w, g) if g
    w.id
  end

  # Draw every dirty widget and present once. Returns true when anything was
  # drawn. This is the only place that calls present.
  def flush
    n = @widgets.size
    i = 0
    drawn = 0
    while i < n
      w = @widgets[i]
      if w.dirty
        if w.visible
          w.draw_widget(@gfx)
        else
          @gfx.fill_rect(w.x, w.y, w.w, w.h, @bg)
        end
        w.dirty = false
        drawn += 1
      end
      i += 1
    end
    return false if drawn == 0
    @gfx.present
    true
  end

  # Mark everything for redraw (after clear_user_area or a resize).
  def invalidate_all
    n = @widgets.size
    i = 0
    while i < n
      @widgets[i].dirty = true
      i += 1
    end
    nil
  end

  def find(id)
    n = @widgets.size
    i = 0
    while i < n
      w = @widgets[i]
      return w if w.id == id
      i += 1
    end
    nil
  end

  # Reposition a widget. Coordinates are user-area relative, as at creation.
  def move(id, x, y, w, h)
    t = find(id)
    return nil if t.nil?
    t.place(@ox + x, @oy + y, w, h)
    nil
  end

  def on?(id)
    t = find(id)
    t.nil? ? false : t.on?
  end

  def set_on(id, on)
    t = find(id)
    return nil if t.nil?
    t.set_on(on)
    if on
      g = t.group
      apply_group(t, g) if g
    end
    nil
  end

  def set_text(id, text)
    t = find(id)
    return nil if t.nil?
    t.set_text(text, @gfx)
    nil
  end

  def set_value(id, value)
    t = find(id)
    return false if t.nil?
    t.set_value(value)
  end

  def value(id)
    t = find(id)
    t.nil? ? 0 : t.value
  end

  def set_enabled(id, flag)
    t = find(id)
    return nil if t.nil?
    if t.enabled != flag
      t.enabled = flag
      t.dirty = true
    end
    nil
  end

  def set_visible(id, flag)
    t = find(id)
    return nil if t.nil?
    if t.visible != flag
      t.visible = flag
      t.dirty = true
    end
    nil
  end

  # Called after the app moved the window origin (resize); widgets keep their
  # absolute coordinates, so re-place them from the app if that changed.
  def set_origin(x0, y0)
    @ox = x0
    @oy = y0
    nil
  end

  private

  def add(w)
    w.bg = @bg
    @widgets << w
    w
  end

  # Switch every other member of the group off.
  def apply_group(widget, g)
    n = @widgets.size
    i = 0
    while i < n
      o = @widgets[i]
      if o != widget && o.group == g && o.on?
        o.set_on(false)
      end
      i += 1
    end
    nil
  end
end
