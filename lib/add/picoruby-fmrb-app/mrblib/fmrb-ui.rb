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

  # Glyph cell height of the mixed renderer at text size 1 (Font0 and
  # misaki_8 are both 8px). A widget multiplies it by its own text size.
  TEXT_H = 8
  # Width of the < and > halves of a Stepper at text size 1.
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
    # Text scale this widget draws at, fixed when it was created. FmrbUI does
    # not follow whatever size the app happens to be in: a widget measured at
    # one size and drawn at another comes out wrong, and the app changes the
    # size for its own drawing between two flushes (the PicoRuby demo runs
    # pages at sizes 1 to 4). flush sets it per widget and puts the app's
    # size back before it returns.
    def text_size
      @ts
    end

    def initialize(id, x, y, w, h, text_size)
      @id = id
      @x = x
      @y = y
      @w = w
      @h = h
      @ts = text_size
      @dirty = true
      @enabled = true
      @visible = true
      @bg = C_BG
    end

    # Glyph height at this widget's size.
    def text_h
      TEXT_H * @ts
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
    def press(px, py); @dirty = true; nil; end
    def release; @dirty = true; nil; end
    # Confirm the operation (mouse released on top of the widget). Returns
    # true when the app should be told about it.
    def activate; false; end
    # A scrollbar acts when you press it and keeps acting while you hold,
    # unlike a button, which acts when you let go over it. Everything else
    # answers false and is reported on release as before.
    def fires_on_press?; false; end
    # Which way a held scrollbar is going: -1 up, 0 not held, 1 down.
    def direction; 0; end
    def set_text(text, gfx); nil; end
    def group; nil; end
    def on?; false; end
    def set_on(flag); nil; end
    def value; 0; end
    def set_value(v); false; end
    def set_range(min, max); nil; end
    # Deliberately not called "text": GfxBlock::Recorder has a four-argument
    # #text (an alias of draw_text), and a call on a base-typed receiver picks
    # its candidates by name. Same reason draw is draw_widget.
    def option_text; nil; end
    # TextField only. Named field_text for the same reason option_text is not
    # text: a poly call picks candidates by name and Recorder#text exists.
    def field_text; nil; end
    def set_field_text(t); nil; end
    def focus(on); nil; end
    def type_key(code, keycode); false; end

    private

    # Width of str at this widget's own text size, whatever size the app is
    # in at the moment. FmrbGfx#text_width multiplies by the current size, so
    # divide that back out first; the division is exact, the raw value being
    # base * size. Measuring with :default (not the current family) is on
    # purpose - FmrbUI always draws with the mixed renderer.
    def measure(gfx, str)
      gfx.text_width(str, :default) / gfx.current_text_size * @ts
    end

    # Text position for a horizontally centered single line.
    def center_text(tw)
      @tx = @x + (@w - tw) / 2
      @ty = @y + (@h - text_h) / 2
      nil
    end
  end

  # Static single-line text on the window background.
  class Label < Widget
    attr_reader :text

    def initialize(id, x, y, w, h, text, align, gfx, text_size)
      super(id, x, y, w, h, text_size)
      @align = align
      @text = text
      @tw = measure(gfx, text)
      relayout
    end

    def set_text(text, gfx)
      return nil if @text == text
      @text = text
      @tw = measure(gfx, text)
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
      @ty = @y + (@h - text_h) / 2
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

    def initialize(id, x, y, w, h, text, gfx, text_size, accent)
      super(id, x, y, w, h, text_size)
      @accent = accent
      @text = text
      @tw = measure(gfx, text)
      @pressed = false
      relayout
    end

    def set_text(text, gfx)
      return nil if @text == text
      @text = text
      @tw = measure(gfx, text)
      relayout
      @dirty = true
      nil
    end

    def relayout
      center_text(@tw)
    end

    def press(px, py)
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
      fill = @accent || C_BUTTON
      if @pressed
        gfx.fill_rect(@x, @y, @w, @h, C_TEXT_LIGHT)
        gfx.draw_rect(@x, @y, @w, @h, C_BORDER)
        gfx.draw_text_mixed(@tx, @ty, @text, fill)
      else
        gfx.fill_rect(@x, @y, @w, @h, fill)
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

    def initialize(id, x, y, w, h, text, group, on, on_text, gfx, text_size,
                   accent)
      super(id, x, y, w, h, text_size)
      @accent = accent
      @text = text
      @on_text = on_text
      @group = group
      @on = on
      @pressed = false
      @tw = measure(gfx, text)
      @on_tw = on_text ? measure(gfx, on_text) : @tw
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
      @tw = measure(gfx, text)
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

    def press(px, py)
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
        # An accent is a saturated colour chosen to mean something, so its
        # text is light; the theme highlight is pale and takes dark text.
        gfx.fill_rect(@x, @y, @w, @h, @accent || C_HIGHLIGHT)
        color = @accent ? C_TEXT_LIGHT : C_TEXT
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

    def initialize(id, x, y, w, h, value, min, max, step, gfx, text_size)
      super(id, x, y, w, h, text_size)
      @min = min
      @max = max
      @step = step
      @value = clamp(value)
      @gfx = gfx
      @suffix = nil
      @text = build_text
      @tw = measure(gfx, @text)
      @pressed = 0
      relayout
    end

    # Unit shown after the number ("%" etc). Usually set once after creation,
    # but it may also carry something that changes with what is loaded (the
    # NSF player puts "/5" there). Unchanged text costs one String compare.
    def suffix=(s)
      return nil if @suffix == s
      @suffix = s
      @text = build_text
      @tw = measure(@gfx, @text)
      relayout
      @dirty = true
      nil
    end

    # Move the ends. The value is pulled inside the new range, and the text is
    # rebuilt only if that actually moved it. For a stepper whose range comes
    # from the data (tracks in the loaded file, pages in the open document).
    def set_range(min, max)
      return nil if @min == min && @max == max
      @min = min
      @max = max
      set_value(@value)
      nil
    end

    def set_value(v)
      nv = clamp(v)
      return false if nv == @value
      @value = nv
      @text = build_text
      @tw = measure(@gfx, @text)
      relayout
      @dirty = true
      true
    end

    # The < and > halves widen with the text, so a stepper asked for text
    # size 2 needs roughly twice the width to keep a value field.
    def arrow_w
      STEP_W * @ts
    end

    def relayout
      aw = arrow_w
      @tx = @x + aw + (@w - aw * 2 - @tw) / 2
      @ty = @y + (@h - text_h) / 2
      nil
    end

    def press(px, py)
      aw = arrow_w
      if px < @x + aw
        @pressed = -1
      elsif px >= @x + @w - aw
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
      aw = arrow_w
      mid_x = @x + aw
      mid_w = @w - aw * 2
      gfx.fill_rect(mid_x, @y, mid_w, @h, @bg)
      gfx.draw_rect(mid_x, @y, mid_w, @h, C_BORDER)
      gfx.draw_text_mixed(@tx, @ty, @text, C_TEXT)
      draw_arrow(gfx, @x, "<", @pressed == -1)
      draw_arrow(gfx, @x + @w - aw, ">", @pressed == 1)
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
      aw = arrow_w
      gfx.fill_rect(ax, @y, aw, @h, bg)
      gfx.draw_rect(ax, @y, aw, @h, C_BORDER)
      gfx.draw_text_mixed(ax + (aw - 6 * @ts) / 2, @y + (@h - text_h) / 2, glyph, fg)
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

  # "< choice >" over a fixed table of strings. A Stepper whose value is an
  # index and whose text comes from the table, so nothing is built when the
  # user moves it -- the strings all exist before the app starts. That makes
  # it lighter than Stepper, which has a number to render.
  #
  # The ends do not wrap. A language or a theme that rolls over from the last
  # entry back to the first is easy to overshoot, and there is no undo.
  class Enum < Stepper
    def initialize(id, x, y, w, h, options, index, gfx, text_size)
      @options = options
      last = options.size - 1
      last = 0 if last < 0
      super(id, x, y, w, h, index, 0, last, 1, gfx, text_size)
    end

    # The string on show. #value is its index.
    def option_text
      @options[@value]
    end

    private

    # Called by Stepper#initialize and by every set_value that moved.
    def build_text
      t = @options[@value]
      t.nil? ? "" : t
    end
  end

  # A vertical scrollbar: two arrow buttons with a track and a thumb between
  # them. Unlike the rest of the set the rect given is the BAR, not the thing
  # being scrolled -- a widget is placed where it is drawn.
  #
  # It knows nothing about the list. Three integers describe the whole job:
  # how many there are, how many fit, and where we are. That is why the seven
  # places in the tree that scrolled something could move onto it without any
  # of them agreeing on what a row is.
  #
  # The app owns the scroll position (its keyboard moves it too); the widget
  # is a view of it. Push it in with set_value, read it back after handle
  # reports.
  class Scrollbar < Widget
    BTN_H = 10       # arrow button height, top and bottom
    MIN_THUMB = 6

    attr_reader :value, :total, :visible_count

    def initialize(id, x, y, w, h, total, visible, scroll, gfx, text_size)
      super(id, x, y, w, h, text_size)
      @total = total
      @visible_count = visible
      @value = clamp(scroll)
      @dir = 0
    end

    # Nothing to show while everything fits, and nothing to click either --
    # the list underneath should get the event.
    def active?
      @total > @visible_count && @h > BTN_H * 2 + 4
    end

    def hit?(px, py)
      return false unless active?
      super(px, py)
    end

    def fires_on_press?; true; end
    def direction; @dir; end

    def set_range(total, visible)
      return nil if @total == total && @visible_count == visible
      @total = total
      @visible_count = visible
      set_value(@value)
      @dirty = true
      nil
    end

    def set_value(v)
      nv = clamp(v)
      return false if nv == @value
      @value = nv
      @dirty = true
      true
    end

    def press(px, py)
      @dir = press_dir(py)
      @dirty = true
      nil
    end

    def release
      @dir = 0
      @dirty = true
      nil
    end

    # One unit per press, the same step the hand-written version took for both
    # the arrows and the track. The app repeats it while direction is not 0.
    def activate
      return false if @dir == 0
      set_value(@value + @dir)
    end

    def draw_widget(gfx)
      unless active?
        gfx.fill_rect(@x, @y, @w, @h, @bg)
        return nil
      end
      gfx.draw_line(@x, @y, @x, @y + @h - 1, C_BORDER)
      draw_arrow(gfx, @y, true, @dir < 0)
      draw_arrow(gfx, @y + @h - BTN_H, false, @dir > 0)
      ty = track_y
      th = track_h
      gfx.fill_rect(@x + 1, ty, @w - 1, th, @bg)
      hh = thumb_h
      gfx.fill_rect(@x + 2, thumb_y, @w - 4, hh, C_BUTTON)
      gfx.draw_rect(@x + 2, thumb_y, @w - 4, hh, C_BORDER)
      nil
    end

    private

    def max_scroll
      m = @total - @visible_count
      m < 0 ? 0 : m
    end

    def clamp(v)
      return 0 if v < 0
      m = max_scroll
      v > m ? m : v
    end

    def track_y; @y + BTN_H; end
    def track_h; @h - BTN_H * 2; end

    def thumb_h
      t = track_h * @visible_count / @total
      t < MIN_THUMB ? MIN_THUMB : t
    end

    def thumb_y
      m = max_scroll
      return track_y if m == 0
      track_y + (track_h - thumb_h) * @value / m
    end

    # Above the thumb scrolls up, below it scrolls down, same as the arrows.
    def press_dir(py)
      return -1 if py < track_y
      return 1 if py >= @y + @h - BTN_H
      ty = thumb_y
      return -1 if py < ty
      return 1 if py >= ty + thumb_h
      0
    end

    # Held arrows fill with the highlight, not with C_TEXT_LIGHT: the app's
    # background is usually the theme's window colour, which IS C_TEXT_LIGHT,
    # and an arrow that goes from white to white says nothing. The highlight
    # reads against a light background and a dark one both.
    def draw_arrow(gfx, ay, up, pressed)
      bg = pressed ? C_HIGHLIGHT : @bg
      gfx.fill_rect(@x, ay, @w, BTN_H, bg)
      gfx.draw_rect(@x, ay, @w, BTN_H, C_BORDER)
      cx = @x + @w / 2
      if up
        gfx.draw_line(cx, ay + 2, cx - 3, ay + 7, C_BORDER)
        gfx.draw_line(cx, ay + 2, cx + 3, ay + 7, C_BORDER)
        gfx.draw_line(cx - 3, ay + 7, cx + 3, ay + 7, C_BORDER)
      else
        gfx.draw_line(cx, ay + 7, cx - 3, ay + 2, C_BORDER)
        gfx.draw_line(cx, ay + 7, cx + 3, ay + 2, C_BORDER)
        gfx.draw_line(cx - 3, ay + 2, cx + 3, ay + 2, C_BORDER)
      end
      nil
    end
  end

  # One line of typed text. Clicking it takes the focus; typing goes to
  # whichever field has it; Enter reports the id.
  #
  # No blinking caret. A blink would mean drawing every frame, which is the
  # one thing this widget set refuses to do, so the line is simply there
  # while the field has the focus.
  #
  # No arrow keys and no insertion point either. Everything goes on the end
  # and backspace takes from the end, which is what the two places that
  # wanted a field (a filename, a search term) actually do. An editing
  # caret can come when something needs it.
  class TextField < Widget
    BACKSPACE = 8
    ENTER_LF = 10
    ENTER_CR = 13
    ESCAPE = 27

    def initialize(id, x, y, w, h, text, max, gfx, text_size)
      super(id, x, y, w, h, text_size)
      @gfx = gfx
      @max = max
      @focused = false
      set_field_text(text)
    end

    def field_text; @text; end

    def set_field_text(t)
      # dup so the field owns a string it may extend; a literal is frozen.
      @text = t.nil? ? "".dup : t.dup
      @dirty = true
      nil
    end

    def focus(on)
      return nil if @focused == on
      @focused = on
      @dirty = true
      nil
    end

    def focused?; @focused; end

    # Clicking the box takes the focus and says nothing: focusing a field is
    # not the same as confirming what is in it, and an app that treats the
    # two alike will act on a half-typed value. Only Enter reports.
    # (The container moves the focus on any hit, so answering false here
    # still focuses.)
    def activate
      false
    end

    # Returns true when the app should be told (Enter). The container turns
    # that into the widget's id.
    def type_key(code, keycode)
      if code == BACKSPACE
        n = @text.length
        @text = @text[0, n - 1] if n > 0
        @dirty = true
        return false
      end
      return true if code == ENTER_LF || code == ENTER_CR
      return false if code < 32 || code > 126
      return false if @text.length >= @max
      @text = @text + code.chr
      @dirty = true
      false
    end

    def draw_widget(gfx)
      gfx.fill_rect(@x, @y, @w, @h, C_TEXT_LIGHT)
      gfx.draw_rect(@x, @y, @w, @h, @focused ? C_HIGHLIGHT : C_BORDER)
      ty = @y + (@h - text_h) / 2
      gfx.draw_text_mixed(@x + 2, ty, @text, C_TEXT)
      if @focused
        cx = @x + 2 + measure(gfx, @text)
        cx = @x + @w - 2 if cx > @x + @w - 2
        gfx.draw_line(cx, ty, cx, ty + text_h - 1, C_TEXT)
      end
      nil
    end
  end

  # ----------------------------------------------------------------------
  # Container
  # ----------------------------------------------------------------------

  # bg is what the widgets paint behind themselves and over a hidden one.
  # It defaults to the theme's window background; an app that clears its user
  # area to something else (the monitor's dark panel) passes that instead.
  #
  # text_size is the default scale for widgets made from here on; a single
  # widget can override it. It is 1 unless said otherwise, and it is never
  # taken from whatever size the app is in at the time - see Widget#text_size.
  def initialize(app, bg: C_BG, text_size: 1)
    @gfx = app.gfx
    @ox = app.user_area_x0
    @oy = app.user_area_y0
    @bg = bg
    @ts = text_size
    @widgets = []
    @pressed = nil
    @focus = nil
  end

  attr_reader :widgets

  # ---- creation (on_create only; keyword arguments are fine here) ----

  def label(id, x, y, w, h, text, align: :left, text_size: nil)
    add(Label.new(id, @ox + x, @oy + y, w, h, text, align, @gfx,
                  text_size || @ts))
  end

  # accent: an RGB332 fill that replaces the theme's button colour, for the
  # rare control whose colour carries meaning (the Yes of a confirm dialog,
  # a transport's Stop). Creation-time only, so nothing is allocated later.
  # Use it sparingly: widgets carrying their own palette is what this set
  # exists to stop.
  def button(id, x, y, w, h, text, text_size: nil, accent: nil)
    add(Button.new(id, @ox + x, @oy + y, w, h, text, @gfx, text_size || @ts,
                   accent))
  end

  # accent: replaces the highlight used while the toggle is on.
  def toggle(id, x, y, w, h, text, group: nil, on: false, on_text: nil,
             text_size: nil, accent: nil)
    add(Toggle.new(id, @ox + x, @oy + y, w, h, text, group, on, on_text, @gfx,
                   text_size || @ts, accent))
  end

  def stepper(id, x, y, w, h, value, min, max, step = 1, text_size: nil)
    add(Stepper.new(id, @ox + x, @oy + y, w, h, value, min, max, step, @gfx,
                    text_size || @ts))
  end

  # One line of typed text. max is how many characters it will take.
  def text_field(id, x, y, w, h, text = "", max: 32, text_size: nil)
    add(TextField.new(id, @ox + x, @oy + y, w, h, text, max, @gfx,
                      text_size || @ts))
  end

  # x, y, w, h are the bar itself. Ten pixels wide is what the rest of the
  # system uses.
  def scrollbar(id, x, y, w, h, total, visible, scroll = 0)
    add(Scrollbar.new(id, @ox + x, @oy + y, w, h, total, visible, scroll,
                      @gfx, @ts))
  end

  # options is an Array of String, read but never written by the widget.
  def enum(id, x, y, w, h, options, index: 0, text_size: nil)
    add(Enum.new(id, @ox + x, @oy + y, w, h, options, index, @gfx,
                 text_size || @ts))
  end

  # ---- run time (allocates nothing) ----

  # Feed one event. Returns the id of the widget whose operation completed,
  # or nil. A press only updates the pressed look; the id comes on release,
  # and only when the release lands on the same widget.
  def handle(ev)
    t = ev[:type]
    # mouse_move arrives at 30 Hz, so leave before touching anything else.
    # A key costs one more comparison than it did before text fields
    # existed, and only reaches a widget while one has the focus.
    if t != :mouse_down && t != :mouse_up
      return nil if @focus.nil?
      return nil if t != :key_down
      return type_into_focus(ev)
    end
    return nil if ev[:button].to_i != 1
    px = ev[:x].to_i
    py = ev[:y].to_i

    if t == :mouse_down
      i = @widgets.size - 1
      while i >= 0
        w = @widgets[i]
        if w.visible && w.enabled && w.hit?(px, py)
          w.press(px, py)
          @pressed = w
          return w.id if w.fires_on_press? && w.activate
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
      set_focus(w)
      # A widget that already acted on the press must not act again here.
      # Without this a single click on a scrollbar arrow scrolled twice --
      # once going down, once coming up -- which is what "too responsive"
      # feels like from the other side of the screen.
      on_press = w.fires_on_press?
      fired = w.activate if on_press == false
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
    # The app draws its own picture between two flushes and may leave the
    # text size anywhere. Note what it was, draw each widget at its own size,
    # and hand the app's size back. current_text_size is tracked in Ruby, so
    # reading it is free and set_text_size only goes out when it differs.
    saved = @gfx.current_text_size
    size = saved
    while i < n
      w = @widgets[i]
      if w.dirty
        if w.visible
          ts = w.text_size
          if ts != size
            @gfx.set_text_size(ts)
            size = ts
          end
          w.draw_widget(@gfx)
        else
          @gfx.fill_rect(w.x, w.y, w.w, w.h, @bg)
        end
        w.dirty = false
        drawn += 1
      end
      i += 1
    end
    @gfx.set_text_size(saved) if size != saved
    return false if drawn == 0
    @gfx.present
    true
  end

  # Mark everything on screen for redraw (after clear_user_area or a resize).
  #
  # Hidden widgets are deliberately left alone. A hidden one is repainted
  # with the background so its hole matches what is behind it, which is only
  # wanted for the widget that has just been taken away -- and set_visible
  # already marks that one. Dirtying them all here instead painted a
  # background-coloured rectangle wherever every hidden widget happened to
  # sit: at (0, 0) for anything not yet moved, and over the middle of the
  # screen for a dialog that had been open earlier.
  def invalidate_all
    n = @widgets.size
    i = 0
    while i < n
      w = @widgets[i]
      w.dirty = true if w.visible
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

  # The string an Enum is showing (its index is #value).
  def option_text(id)
    t = find(id)
    t.nil? ? nil : t.option_text
  end

  # Which way a held scrollbar is going (-1 / 0 / 1), for an app that repeats
  # the scroll while the button is down.
  def direction(id)
    t = find(id)
    t.nil? ? 0 : t.direction
  end

  # Give the focus to a widget by id, or take it away with nil. Only a text
  # field can hold it; anything else clears it, which is what clicking a
  # button next to a field should do.
  def focus(id)
    w = id.nil? ? nil : find(id)
    set_focus(w)
    nil
  end

  # id of the widget holding the focus, or nil.
  def focused
    @focus.nil? ? nil : @focus.id
  end

  def field_text(id)
    t = find(id)
    t.nil? ? nil : t.field_text
  end

  def set_field_text(id, text)
    t = find(id)
    return nil if t.nil?
    t.set_field_text(text)
    nil
  end

  def set_range(id, min, max)
    t = find(id)
    return nil if t.nil?
    t.set_range(min, max)
    nil
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

  # Escape gives the focus back to nobody; Enter reports the field.
  def type_into_focus(ev)
    code = ev[:character].to_i
    if code == TextField::ESCAPE
      set_focus(nil)
      return nil
    end
    w = @focus
    return nil unless w.type_key(code, ev[:keycode].to_i)
    w.id
  end

  # A widget that cannot hold the focus takes it away from whoever had it.
  def set_focus(w)
    nw = (w && w.field_text) ? w : nil
    return nil if nw == @focus
    @focus.focus(false) if @focus
    nw.focus(true) if nw
    @focus = nw
    nil
  end

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
