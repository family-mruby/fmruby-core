# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.

class FmrbApp
  # UI language ("en" / "ja"). Here it is simply the constant const.c filled in
  # from system_conf when this VM started; the Spinel build backs the same call
  # with an FFI read, because its constants are baked at compile time. FmrbI18n
  # asks through this so both engines see the setting the user chose.
  def self.language
    FmrbConst::LANGUAGE.to_s
  rescue
    "en"
  end

  TITLE_BAR_H = 11
  CORNER_R = 4
  TRANSPARENT_COLOR = 0x01
  SCROLLBAR_W = 10
  SCROLLBAR_BTN_H = 10
  # The resize mark: a right triangle in the bottom-right corner of a window
  # the kernel resizes from there (its 10x10 corner), legs this long.
  RESIZE_MARK = 8

  # :gfx is here so apps can write gfx.draw_text as well as @gfx.draw_text --
  # the editor's completion answers both spellings, and the method form is the
  # one the type signatures in sig/ describe.
  attr_reader :window_width, :window_height, :pos_x, :pos_y, :gfx

  # Which machine this is: :esp32 or :linux (the browser build says :linux
  # too; FmrbConst::BOARD tells it apart). Asked of FmrbConst each time
  # rather than kept in an instance variable: an app moved to another machine
  # (doc/checkpoint) carries its instance variables with it, while FmrbConst
  # is the VM's own, defined by the machine it runs on.
  def platform
    FmrbConst::PLATFORM == "esp32" ? :esp32 : :linux
  end

  # @_running and @_name are prefixed even though 6 files read them, because
  # they are the two whose collision hurts most: an app that innocently sets
  # its own @_running silently exits (main_loop reads it), and both names are
  # exactly what an app author would pick. The readers moved to these
  # methods; running? is a predicate like the others, name stays a plain
  # noun because it is not a boolean.
  def running?; @_running; end
  def name; @_name; end

  # Predicates, not attr_readers: the backing ivars carry the @_ prefix
  # (base internals, kept out of the namespace apps write in), and a
  # boolean reads better as a question. Nothing called the old readers
  # (measured 2026-09-06), so the rename costs nothing.
  def fullscreen?; @_fullscreen; end
  def rounded_corners?; @_rounded_corners; end
  def closable?; @_closable; end
  # Whether the kernel resizes this window from its bottom-right corner (the
  # app's .app.toml resizable, or a built-in's); the frame shows the mark.
  def resizable?; @_resizable; end
  # Whether the window has no frame (the app's .app.toml window_frame =
  # "none", doc/frameless_window/): no title bar, border or close button, the
  # user area is the whole window and its ground is the colour key, so only
  # what the app draws is seen.
  def frameless?; @_frameless; end
  # Pixels an app keeps free at the right and bottom of its user area when it
  # draws into the bottom-right corner (a status line, a scrollbar): the
  # resize mark and the rest of the kernel's resize grab (the window's last
  # 10x10, 9x9 inside the border), so the mark shows and no control sits under
  # the grab. 0 for a fullscreen window or one that cannot be resized.
  def resize_corner
    return 0 unless @_resizable
    return 0 if @_fullscreen
    RESIZE_MARK + 1
  end
  # User area (inside the window frame, below the title bar). Exposed so
  # helper libraries such as FmrbUI can place widgets in window-local
  # coordinates without reaching into the app's instance variables.
  attr_reader :user_area_x0, :user_area_y0, :user_area_width, :user_area_height
  # Whether a click on the close-button area may stop the app. System apps
  # that own the screen (the desktop) must set this to false: for them the
  # top-right corner is ordinary UI, not a close button.
  def closable=(v); @_closable = v; end

  # Close-button hit zone (used by both draw and event-handler paths).
  CLOSE_BTN_CX_OFFSET = 6   # distance from right edge to circle center
  CLOSE_BTN_CY        = 5   # vertical center of circle in title bar
  CLOSE_BTN_R         = 3   # circle radius
  CLOSE_BTN_HIT_R     = 5   # hit-zone radius (slightly bigger for tolerance)
  CLOSE_BTN_NORMAL_COLOR  = 0xFF  # white
  CLOSE_BTN_PRESSED_COLOR = 0x49  # dark gray, gives an "inset" feel on the
                                  # ruby title bar (still visible, clearly
                                  # distinct from the released state)

  def initialize()
    Log.debug("initialize")
    @_running = false
    @_close_btn_pressed = false
    @_closable = true
    @_spin_break = false
    @_timers = []
    @_attached_uis = []
    _init() # C function, variables are defined here
    Log.debug("name=#{@_name}")
    Log.debug("After _init(), @_canvas=#{@_canvas}, @window_width=#{@window_width}, @window_height=#{@window_height}")

    # Initialize graphics only for non-headless apps (@_canvas is set)
    if @_canvas
      @gfx = FmrbGfx.new(@_canvas, width: @window_width, height: @window_height)
      Log.debug("FmrbGfx initialized: canvas_id=#{@_canvas}")
      _layout_user_area

      # Background canvas (desktop only)
      if @_bg_canvas
        @bg_gfx = FmrbGfx.new(@_bg_canvas, width: @window_width, height: @window_height)
        Log.debug("Background GFX initialized: canvas_id=#{@_bg_canvas}")
      else
        @bg_gfx = nil
      end

      _apply_user_area_sprite_clip

      if @_fullscreen
        # nothing to frame
      elsif @_frameless
        # The ground is the key: nothing shows until the app draws.
        @gfx.clear(TRANSPARENT_COLOR)
        _apply_rounded_corner_regions
      else
        _build_frame_block
        _apply_rounded_corner_regions
      end
    else
      @gfx = nil
      @bg_gfx = nil
      Log.debug("Headless app: no graphics initialized")
    end

  end

  # The system title bar is always rendered with the default 8px ASCII
  # font. The caller's font / text-size selection is saved on entry and
  # restored on exit, so apps using set_font(:ja, ...) never need to
  # re-apply the font after every frame call.
  def draw_window_frame
    return if @_fullscreen
    # A frameless window has nothing to draw; only its whole-window region is
    # kept (cached, a no-op unless the size changed).
    if @_frameless
      _apply_rounded_corner_regions
      return
    end
    # An app spawned fullscreen never built the frame block (nothing to frame),
    # so build it on the first draw after it becomes a window at runtime -- F11
    # on an editor started with `edit -f` used to leave it with no title bar,
    # no border and no close button.
    _build_frame_block if @gfx && !@_frame_block
    if @gfx
      # what the app asked for, not what was picked, so that the frame
      # does not turn the ask into this machine's choice
      saved_font = @gfx._asked_font
      saved_size = @gfx.current_text_size
      @gfx.set_font(:default)
      @gfx.set_text_size(1)
    end
    # The resize mark goes under the frame's border, which is drawn next.
    draw_resize_mark(@window_width, @window_height) if @_resizable && @gfx
    if @_frame_block
      @_frame_block.draw(w: @window_width, h: @window_height)
    end
    # Re-stamp the 3 outer pixels of each rounded corner with the canvas
    # color key, so resize / clear-induced opaque pixels there composite
    # as transparent again. Runs after the frame draw so the round-rect
    # border (drawn last by the frame block) stays intact.
    if @_corner_clear_block
      @_corner_clear_block.draw(w: @window_width, h: @window_height)
    end
    if @gfx
      @gfx.set_font(*saved_font) unless saved_font == [:default]
      @gfx.set_text_size(saved_size) unless saved_size == 1
    end
    # Re-apply composite regions after every frame redraw so resize updates
    # take effect. Internally cached on (w, h); a no-op when size is unchanged.
    _apply_rounded_corner_regions
  end


  # The resize mark of a w x h window: a right triangle filling the
  # bottom-right corner inside the border, in the title bar's colour (the
  # theme's, never the key 0x01). draw_window_frame draws it for a resizable
  # window; a window that draws its own frame (the Linux window's stand-in)
  # calls it before the border.
  def draw_resize_mark(w, h)
    x = w - 2
    y = h - 2
    m = RESIZE_MARK - 1
    @gfx.fill_triangle(x, y - m, x, y, x - m, y, FmrbConst::THEME_MENU_BG)
  end

  # Rows one wheel event asks for, signed: positive scrolls towards the start
  # of a list, which is the wheel pushed away from the user. nil when the
  # event is not a wheel, so an app reads it as
  #
  #   rows = wheel_rows(ev)
  #   if rows
  #     @scroll -= rows
  #     ...
  #
  # How far a notch reaches is the machine's setting (system_conf.toml
  # wheel_lines), not each app's opinion. The scrollbar widget deliberately
  # does NOT answer the wheel: in every app here the bar is a picture of the
  # app's own scroll position, written on each draw, so a value changed inside
  # the widget would be overwritten before it was seen.
  def wheel_rows(ev)
    return nil unless ev[:type] == :mouse_wheel
    d = ev[:delta].to_i
    return nil if d == 0
    d * FmrbConst::WHEEL_LINES
  end


  # The raw notch count, for a list whose rows are not text rows: the
  # launcher's are tiles as tall as several lines, and wheel_lines (a text
  # setting) sends it flying. nil when the event is not a wheel.
  def wheel_notches(ev)
    return nil unless ev[:type] == :mouse_wheel
    d = ev[:delta].to_i
    return nil if d == 0
    d
  end

  # ---- System colours ----
  #
  # The [theme] section of system_conf.toml, as FmrbConst::THEME_*. Apps that
  # show text and controls take their background and ink from here, so one
  # edit to the file restyles the desktop and every app together, and a white
  # page on a Retro NTSC screen can be toned down in one place. A game or a
  # visual piece that owns its whole picture passes explicit colours instead.
  def theme_bg;     FmrbConst::THEME_WINDOW_BG; end   # page background
  def theme_fg;     FmrbConst::THEME_TEXT; end        # ink on theme_bg
  def theme_accent; FmrbConst::THEME_HIGHLIGHT; end   # selection, emphasis
  def theme_border; FmrbConst::THEME_BORDER; end      # rules, boxes, muted text
  def theme_fg_light; FmrbConst::THEME_TEXT_LIGHT; end # ink on accent / button

  # ---- This app's own settings ----
  #
  # /home/<app_handle_name>.toml, read and written through FmrbToml: a Hash
  # ({} when there is no file, or it is not TOML -- that one is logged), and
  # save_app_settings("rise_mg" => 130) rewrites only the lines of the keys
  # given (see FmrbToml.update for what it returns). Methods only, no
  # instance variables: subclasses lay theirs out after the base's.
  def app_settings
    FmrbToml.load(app_settings_path) || {}
  end

  def save_app_settings(values)
    FmrbToml.update(app_settings_path, values)
  end

  def app_settings_path
    "/home/" + app_handle_name + ".toml"
  end

  # ---- This app's remembered state ----
  #
  # Small values an app remembers between openings (the File Manager's last
  # folder): /tmp/<name>.state.toml, a flat table of strings, integers and
  # true/false. /tmp is flat (no subdirectories, fmrb_tmpfs.h) and lasts until
  # the machine restarts: the power on a device, the program on the PC, the
  # page in the browser. Not the user's settings -- those are app_settings in
  # /home. Methods only, no instance variables (same reason as above).
  #
  #   st = app_state                       #=> {} when nothing is remembered
  #   save_app_state({"dir" => "/home"})   # writes only when a value changed
  #
  # who defaults to app_handle_name; a part of a program that has its own
  # state (the desktop's File Manager) passes a fixed one. A file that is
  # missing or is not TOML reads as {} and is replaced on the next save. The
  # Spinel base has the same three methods (fmrb_app_base_spinel.rb).
  def app_state(who = app_handle_name)
    h = FmrbToml.load(app_state_path(who))
    out = {}
    return out unless h.is_a?(Hash)
    h.each do |k, v|
      out[k] = v if v.is_a?(String) || v.is_a?(Integer) || v == true || v == false
    end
    out
  end

  # Merges values into the remembered table. true when the file holds them
  # (written now, or already the same), false when it could not be written.
  def save_app_state(values, who = app_handle_name)
    cur = app_state(who)
    changed = false
    values.each do |k, v|
      next unless v.is_a?(String) || v.is_a?(Integer) || v == true || v == false
      key = k.to_s
      next if cur[key] == v
      cur[key] = v
      changed = true
    end
    return true unless changed
    text = "# remembered by #{who}; delete this file to forget\n".dup
    cur.each { |k, v| text << FmrbToml.dump_key(k) << " = " << FmrbToml.dump_value(v) << "\n" }
    begin
      File.open(app_state_path(who), "w") { |f| f.write(text) }
    rescue => e
      Log.warn("app_state: #{app_state_path(who)}: #{e.message}")
      return false
    end
    true
  end

  def app_state_path(who = app_handle_name)
    "/tmp/" + who + ".state.toml"
  end

  # app_handle_name from the app's .app.toml, else the file name without
  # .app.rb (/app/test/step_sensor.app.rb -> "step_sensor"), else the name.
  def app_handle_name
    path = FmrbApp.script_path.to_s
    return name.to_s if path.empty?
    dot = path.rindex(".")
    if dot
      conf = path[0, dot] + ".toml"
      if File.exist?(conf)
        h = FmrbToml.load(conf)
        n = h ? h["app_handle_name"] : nil
        return n if n.is_a?(String) && !n.empty?
      end
    end
    base = path
    slash = base.rindex("/")
    base = base[slash + 1, base.length - slash - 1] if slash
    if base.end_with?(".app.rb")
      base = base[0, base.length - 7]
    elsif dot && base.rindex(".")
      base = base[0, base.rindex(".")]
    end
    base
  end

  # Clear the user area (region inside the window frame, excluding the title
  # bar) with the given color. Use this instead of @gfx.clear to keep the
  # title bar and close button intact.
  # @param color [Integer] RGB332 color. Defaults to the theme's window
  #   background, so an app that never names a colour follows the system
  #   theme; pass one only when the picture calls for it.
  # Wipe the user area. Putting the frame and the widgets back is part of the
  # job, not something the caller has to remember: the user area reaches the
  # rounded corners' outline and the transparent key pixels at the bottom two
  # corners, so a wipe without a frame redraw squares them off (both players
  # did exactly that), and any widget standing in the wiped area is now gone
  # from the screen while FmrbUI still believes it is drawn.
  #   A frameless window's default is the colour key, its ground.
  def clear_user_area(color = (@_frameless ? TRANSPARENT_COLOR : FmrbConst::THEME_WINDOW_BG))
    return unless @gfx
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, color)
    # @_bg_canvas is the desktop, which has no frame -- letting it through here
    # builds the frame block and draws a title bar across the wallpaper.
    # @_fullscreen is refused by draw_window_frame too; it is named here so the
    # reason is visible at the call site.
    draw_window_frame unless @_bg_canvas || @_fullscreen
    _invalidate_attached_uis
    nil
  end

  # FmrbUI calls this on itself when it is created, so clear_user_area can put
  # the widgets back without the app wiring anything up. An app with one page
  # per screen may hold several.
  def attach_ui(ui)
    @_attached_uis << ui
    nil
  end


  # The other half of attach_ui, for an app whose window can be resized: the
  # widgets are anchored to the user area at the moment they are built, so a
  # resize means building them again -- and the old set has to go, or
  # clear_user_area keeps repainting it at yesterday's coordinates.
  def detach_ui(ui)
    out = []
    i = 0
    n = @_attached_uis.size
    while i < n
      out << @_attached_uis[i] unless @_attached_uis[i] == ui
      i += 1
    end
    @_attached_uis = out
    nil
  end

  def _invalidate_attached_uis
    n = @_attached_uis.size
    i = 0
    while i < n
      @_attached_uis[i].invalidate_all
      i += 1
    end
    nil
  end

  # Sprites are composited above everything the canvas drew, frame included,
  # so a windowed app's sprites would paint over its own title bar and border.
  # Bound them to the user area by default; apps wanting a narrower area (e.g.
  # keeping a playfield out of their own score bar) call set_sprite_clip after
  # startup. Public because app.c calls it from the resize path, which drops
  # the clip on the backend.
  def _apply_user_area_sprite_clip
    return unless @gfx
    # system_desktop (the only app with a bg_canvas) draws no window frame and
    # places its own sprites - menu bar indicators, launcher icons - across the
    # whole canvas, so there is nothing to protect and a user-area rect would
    # only cut them.
    return if @_bg_canvas
    if @_fullscreen
      @gfx.clear_sprite_clip
    else
      @gfx.set_sprite_clip(@user_area_x0, @user_area_y0,
                           @user_area_width, @user_area_height)
    end
  end

  private

  # Build the window-frame GfxBlock once. The block captures @_name as a closure
  # so the title text is interned into the bytecode's strtable at new time
  # (strings are immutable after that).
  def _build_frame_block
    title = @_name
    @_frame_block = GfxBlock.new(@gfx, w: @window_width, h: @window_height) do |r, w:, h:|
      # The frame is the system's, not the app's, so it takes the system's
      # colours: one edit to [theme] in system_conf.toml restyles every
      # window at once. The values it used to carry (0xC5 and 0x60) were the
      # light theme's menu_bg and border spelled out, which is why a changed theme
      # left every title bar behind.
      bar    = FmrbConst::THEME_MENU_BG
      on_bar = FmrbConst::THEME_TEXT_LIGHT
      # Title bar with rounded top corners. Draw full-height rounded rect then
      # overwrite the bottom portion so the bottom edge is straight.
      r.fill_round_rect 0, 0, w, TITLE_BAR_H, CORNER_R, bar
      r.fill_rect       0, CORNER_R, w, TITLE_BAR_H - CORNER_R, bar
      # Menu button (hamburger: 3 horizontal lines) + title text
      r.fill_rect       3, 3, 9, 1, on_bar
      r.fill_rect       3, 5, 9, 1, on_bar
      r.fill_rect       3, 7, 9, 1, on_bar
      r.draw_text       15, 2, title, on_bar
      # Close button
      r.fill_circle     w - 6, 5, 3, on_bar
      # Rounded window border. Outer edge rows/columns stay transparent because
      # app content never fills them (user_area excludes x=0, x=w-1, y=h-1).
      r.draw_round_rect 0, 0, w, h, CORNER_R, FmrbConst::THEME_BORDER
    end
    _build_corner_clear_block
  end

  # Declare composite regions so the GA-side compositor only does the
  # per-pixel transparent compare on the 4 small CORNER_R x CORNER_R corner
  # squares; the rest of the canvas takes the opaque memcpy fast path.
  # Visually identical to a full-area transparent push (rounded corners
  # remain rounded), but the transparent-compare load drops from
  # window_w * window_h to 4 * CORNER_R^2 pixels per frame.
  #
  # Cached on (w, h) so re-running on every draw_window_frame call is a
  # no-op unless the window was just resized.
  def _apply_rounded_corner_regions
    return if @_fullscreen
    return unless @gfx
    return unless @_rounded_corners || @_frameless
    # Apps that own a bg_canvas (system_desktop) manage their own composite
    # regions and toggle them on/off around the boot animation, so don't
    # clobber them with the generic rounded-corner layout.
    return if @_bg_canvas
    w = @window_width
    h = @window_height
    return if @_composite_region_w == w && @_composite_region_h == h
    if @_frameless
      # No frame: the whole window is one colour-keyed region, so whatever
      # the app leaves in the key shows what is behind it.
      @gfx.set_composite_regions([
        { dst_x: 0, dst_y: 0, w: w, h: h, transparent: true }
      ])
      @_composite_region_w = w
      @_composite_region_h = h
      return
    end
    c = CORNER_R
    @gfx.set_composite_regions([
      # 4 corner squares: per-pixel transparent compare so the rounded
      # shape composites correctly against whatever sits underneath.
      { dst_x: 0,     dst_y: 0,     w: c,         h: c,         transparent: true  },
      { dst_x: w - c, dst_y: 0,     w: c,         h: c,         transparent: true  },
      { dst_x: 0,     dst_y: h - c, w: c,         h: c,         transparent: true  },
      { dst_x: w - c, dst_y: h - c, w: c,         h: c,         transparent: true  },
      # 3 opaque strips covering the rest of the canvas (memcpy fast path).
      { dst_x: c,     dst_y: 0,     w: w - 2 * c, h: c,         transparent: false },
      { dst_x: c,     dst_y: h - c, w: w - 2 * c, h: c,         transparent: false },
      { dst_x: 0,     dst_y: c,     w: w,         h: h - 2 * c, transparent: false },
    ])
    @_composite_region_w = w
    @_composite_region_h = h
  end

  # CORNER_R=4 の弧外側 3 px ずつ (= 12 px / window) を canvas color key
  # (0x01) で塗り直す。canvas resize やアプリの clear で角丸の外側ピクセルが
  # 不透明色に書き換わると compositing で透けなくなるための保険。
  # 別 GfxBlock に分離しているのは _build_frame_block と合体させると
  # DEFINE_PROG_MAX_PAYLOAD (220B) を超えるリスクがあるため。
  def _build_corner_clear_block
    t = TRANSPARENT_COLOR
    @_corner_clear_block = GfxBlock.new(@gfx, w: @window_width, h: @window_height) do |r, w:, h:|
      # top-left: (0,0)(1,0)(0,1)
      r.draw_line 0,     0,     1,     0,     t
      r.draw_line 0,     1,     0,     1,     t
      # top-right: (w-2,0)(w-1,0)(w-1,1)
      r.draw_line w - 2, 0,     w - 1, 0,     t
      r.draw_line w - 1, 1,     w - 1, 1,     t
      # bottom-left: (0,h-2)(0,h-1)(1,h-1)
      r.draw_line 0,     h - 2, 0,     h - 1, t
      r.draw_line 1,     h - 1, 1,     h - 1, t
      # bottom-right: (w-1,h-2)(w-1,h-1)(w-2,h-1)
      r.draw_line w - 1, h - 2, w - 1, h - 1, t
      r.draw_line w - 2, h - 1, w - 2, h - 1, t
    end
  end


  public



  # ---- Modifier key helpers (for use inside on_event) ----
  #
  # The modifier byte uses the project-specific FMRB_KEYMAP_MOD_* layout
  # (see fmrb_keymap.h), NOT the USB HID standard byte:
  #   bit0=LSHIFT 0x01, bit1=RSHIFT 0x02, bit2=LCTRL 0x04, bit3=RCTRL 0x08,
  #   bit4=LALT   0x10, bit5=RALT   0x20.
  # Pair with `ev[:scancode]` (USB HID Usage ID) when matching letter keys --
  # `ev[:keycode]` differs across platforms (SDL2 reports ASCII for letters),
  # while scancode is uniform.
  def ev_ctrl?(ev)
    ((ev[:modifier] || 0) & 0x0C) != 0
  end

  def ev_shift?(ev)
    ((ev[:modifier] || 0) & 0x03) != 0
  end

  def ev_alt?(ev)
    ((ev[:modifier] || 0) & 0x30) != 0
  end

  # Lifecycle methods (override in subclass)

  def on_create
    # Called once when app is created
    # Initialize your app state here
    # Access @_name and @gfx instance variables
    Log.debug("on_create")
  end

  def on_update
    # Called by user defined cycle
    # Update your app logic here
    # Return: sleep cycle(msec)
    330
  end

  def on_destroy
    # Called once when app is destroyed
    # Cleanup resources here
    Log.debug("on_destroy")
  end

  def on_suspend
    # Called when app is suspended (fullscreen app taking over)
    Log.debug("on_suspend")
  end

  def on_resume
    # Called when app is resumed (fullscreen app exited)
    Log.debug("on_resume")
  end

  # Moving the app (doc/checkpoint). Only an app whose .app.toml says
  # movable = true can be written to an image, and only on the builds that
  # carry mruby-checkpoint.

  def on_checkpoint
    # Called once the image is written, just before the app closes (it does
    # not get on_destroy: it moved, it did not end). What is done here is not
    # in the image.
  end

  def on_restore
    # Called in the app started from an image, before it goes on from where
    # it was written. The base has made the window again (an empty user area
    # in its frame) and subscribed to the topics again; draw what the window
    # showed (an app that draws all of it in on_update need not) and open
    # again what else the app kept outside the VM (sprites, files, sockets).
  end

  # Called from C in an app started from a checkpoint image, before it goes
  # on (doc/checkpoint F3, D8). What lived outside the VM for the app and the
  # base can make again from the image is made again here: the window's
  # canvases (from the context, as for a fresh app), every FmrbGfx bound to
  # the canvas it drew on, every GfxBlock of the app defined again, the
  # frame built again from this machine's theme, the rounded corners and the sprite clip, the fonts, the topics; the sprites
  # are forgotten. Then the app's on_restore, which draws what the window
  # showed (nothing drawn is in the image) and makes its sprites again. The
  # keyboard focus and the window's place are the kernel's, as for any app it
  # starts. On another machine (F4) the window may have another size (a
  # smaller screen, a fullscreen app on a screen of another size): the user
  # area is laid out again and the app gets on_resize before on_restore, as
  # after a resize; a font this machine lacks becomes the nearest one.
  def _checkpoint_restored
    # The file associations are this machine's (/etc and /home tables), read
    # once per VM and cached on the module, which the image carries: forget
    # them, so the next lookup reads this machine's. FmrbI18n's language is
    # the same: cached on the module the first time it was asked, so the
    # words looked up from here on are in this machine's language.
    FmrbAssoc.forget
    FmrbI18n.forget
    @_suspended = false
    @_close_btn_pressed = false
    @_spin_break = false
    resized = false
    if @_canvas
      old_w = @window_width
      old_h = @window_height
      _checkpoint_canvases
      resized = @window_width != old_w || @window_height != old_h
      _layout_user_area if resized
      gfxs = []
      ObjectSpace.each_object(FmrbGfx) { |g| gfxs << g }
      i = 0
      while i < gfxs.size
        g = gfxs[i]
        if (id = g._checkpoint_rebind)
          g._canvas_size(@window_width, @window_height) if resized && (id == @_canvas || id == @_bg_canvas)
          # the font picked again from this machine's fonts, from what the
          # app asked for (not what the writing side picked)
          g._checkpoint_font
        end
        i += 1
      end
      # The frame's blocks bake the system's [theme] colours and the corner
      # key into their programs. They are the base's, not the app's, so they
      # are built again from this machine's theme (as a fresh window builds
      # them) instead of defined again with the writing side's colours.
      @_frame_block._checkpoint_drop if @_frame_block
      @_corner_clear_block._checkpoint_drop if @_corner_clear_block
      @_frame_block = nil
      @_corner_clear_block = nil
      blocks = []
      ObjectSpace.each_object(GfxBlock) { |b| blocks << b }
      i = 0
      while i < blocks.size
        blocks[i]._checkpoint_redefine
        i += 1
      end
      # Sprites live on the display side under ids of the writing program,
      # which may name another app's here: forget them (destroy does nothing,
      # a move raises) for on_restore to make them again.
      sprites = []
      ObjectSpace.each_object(SpriteImage) { |o| sprites << o }
      ObjectSpace.each_object(SpriteInstance) { |o| sprites << o }
      i = 0
      while i < sprites.size
        sprites[i]._checkpoint_forget
        i += 1
      end
      if @gfx
        # FmrbUI widgets left on the theme's window background take this
        # machine's (the ones the app gave a colour keep it); clear_user_area
        # below marks them for on_restore to draw.
        uis = []
        ObjectSpace.each_object(FmrbUI) { |u| uis << u }
        i = 0
        while i < uis.size
          uis[i]._checkpoint_ground
          i += 1
        end
        # A frameless window gets no frame here either: clear_user_area
        # below grounds it in the key and the region is the whole window,
        # so it arrives see-through without the app doing anything.
        _build_frame_block unless @_fullscreen || @_frameless
        @_composite_region_w = nil
        @_composite_region_h = nil
        _apply_user_area_sprite_clip
        _apply_rounded_corner_regions
        clear_user_area
      end
    end
    # The hit shape came in the image; the kernel here has none for this
    # slot. The sprites were forgotten above, so their marks go: on_restore
    # makes the sprites again and marks them again.
    if @_hit_rects || @_hit_mask || @_hit_sprites
      @_hit_sprites = nil
      _send_hit_shape
    end
    topics = @_topics
    if topics
      i = 0
      while i < topics.size
        send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
          {"cmd" => "subscribe", "topic" => topics[i]})
        i += 1
      end
    end
    on_resize(@window_width, @window_height) if resized
    on_restore
    @gfx.present if @gfx
    nil
  end

  # The user area of a window of @window_width x @window_height (the split
  # initialize and app.c's resize make): a fullscreen or frameless window
  # owns it all, a framed one loses the border and the title bar.
  def _layout_user_area
    if @_fullscreen || @_frameless
      @user_area_x0 = 0
      @user_area_y0 = 0
      @user_area_x1 = @window_width
      @user_area_y1 = @window_height
      @user_area_width = @window_width
      @user_area_height = @window_height
    else
      @user_area_x0 = 1
      @user_area_y0 = TITLE_BAR_H
      @user_area_x1 = @window_width - 1
      @user_area_y1 = @window_height - 1
      @user_area_width = @window_width - 2
      @user_area_height = @window_height - TITLE_BAR_H - 1
    end
  end

  def on_checkpoint_refused(reason)
    # Called when the app asked for its own image (checkpoint) and it
    # could not be written; the app goes on.
    Log.warn("App #{@_name}: checkpoint refused: #{reason}")
  end

  # The frame's own event handling: the close button and the title-bar
  # right click (the window menu). Called on every event ahead of the app's
  # hook (see _dispatch_event), so overriding on_event without super can no
  # longer lose the close button.
  def _frame_event(ev)
    # A frameless window has no close button and no title bar. A right click
    # anywhere is its way to the window menu (on_frameless_right_click).
    if @_frameless
      if ev[:type] == :mouse_up && ev[:button] == 3 && !@_fullscreen
        on_frameless_right_click(ev)
      end
      return
    end
    # Handle close button press feedback + click
    if @_closable && ev[:button] == 1 && (ev[:type] == :mouse_down || ev[:type] == :mouse_up)
      cx = @window_width - CLOSE_BTN_CX_OFFSET
      cy = CLOSE_BTN_CY
      hit = (ev[:x] - cx).abs <= CLOSE_BTN_HIT_R &&
            (ev[:y] - cy).abs <= CLOSE_BTN_HIT_R

      case ev[:type]
      when :mouse_down
        if hit && !@_fullscreen && @gfx
          @_close_btn_pressed = true
          @gfx.fill_circle(cx, cy, CLOSE_BTN_R, CLOSE_BTN_PRESSED_COLOR)
          @gfx.present
        end
      when :mouse_up
        if @_close_btn_pressed
          @_close_btn_pressed = false
          if hit
            stop
          elsif @gfx
            # Released outside the button — restore the normal circle.
            # The frame drew this circle in THEME_TEXT_LIGHT, so putting it
            # back in CLOSE_BTN_NORMAL_COLOR (white) changed its colour
            # under any theme whose light ink is not white.
            @gfx.fill_circle(cx, cy, CLOSE_BTN_R, FmrbConst::THEME_TEXT_LIGHT)
            @gfx.present
          end
        elsif hit && !@_fullscreen && @gfx
          # Safety net: down event missed but click landed on button. Must
          # carry the same guards as the down path -- without them a plain
          # click on the top-right corner of a fullscreen app (which draws
          # no close button at all) silently stopped it. That is how the
          # desktop was once killed from the launcher.
          stop
        end
      end
    end
    # A right click on the title bar opens the window menu on the desktop
    # (doc/window_menu: Send to, Reload for a file app, Close). A fullscreen
    # app has no title bar -- y < 11 is just the top of its picture -- so the
    # guard the close button always had applies here too.
    if ev[:type] == :mouse_up && ev[:button] == 3 && ev[:y] < 11 && !@_fullscreen
      request_window_menu(ev[:x], ev[:y])
    end
  end

  # Called from C for every event: the frame first, then the app's hook.
  # This split is what removed the old contract "override on_event and you
  # must call super(ev), or the close button dies".
  def _dispatch_event(ev)
    _frame_event(ev)
    on_event(ev)
  end

  def on_event(ev)
    # App hook, overridden freely. The base needs nothing here, so the
    # super(ev) calls existing apps still make are harmless no-ops.
  end

  # A right click (its release) on a frameless window, or a left press held
  # still on it for a second (ev[:type] == :long_press, the touch-only way).
  # The default opens the window menu (request_window_menu), as a right click
  # on a framed window's title bar does: Send to, Reload, Close.
  # An app that uses the right button itself overrides this (an empty method
  # keeps the menu away; call super(ev) to have the menu as well); a right
  # click still reaches on_event either way, a long press does not (the
  # kernel held it back).
  def on_frameless_right_click(ev)
    request_window_menu(ev[:x], ev[:y])
  end

  # Ask the desktop for this window's menu at (x, y) of the window
  # (doc/window_menu). The kernel adds what the desktop needs (the place on
  # the screen, the name, whether it can be reloaded, whether sending is on)
  # and the desktop draws it on its overlay. movable: the app can be written
  # to an image (movable = true in .app.toml, or started from one), which is
  # what Send to needs.
  def request_window_menu(x, y)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "window_menu", "x" => x.to_i, "y" => y.to_i, "movable" => _window_movable?})
  end

  def _window_movable?
    g = @gfx
    return false unless g && g.respond_to?(:_checkpoint_keep?)
    g._checkpoint_keep? ? true : false
  end

  # Ask the desktop to put up "Close <this app>?". Yes comes back as the
  # quit request (on_quit_request); No or a click outside does nothing.
  def request_close_confirm
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "close_confirm"})
  end

  # ---- hit shape (frameless windows, doc/frameless_window/ W3b) ----
  # A frameless window takes a press only inside the shape it declares here;
  # a press elsewhere goes to what is behind it. The shape is the union of a
  # list of rectangles, one mask and the sprites marked for it, in window
  # coordinates. With nothing declared the whole window rectangle is hit.
  # The kernel reads the shape on its own (no round trip to the drawing
  # side); a framed window ignores it. The lists are the app's state, so a
  # move (doc/checkpoint) takes them along and the base declares them again
  # on arrival -- except the sprite marks: on_restore makes the sprites again
  # and marks them again.

  # rects: [[x, y, w, h], ...]. Replaces the rectangles ([] drops them).
  def set_hit_shape(rects)
    list = []
    i = 0
    while i < rects.size
      r = rects[i]
      list << [r[0], r[1], r[2], r[3]]
      i += 1
    end
    @_hit_rects = list
    _send_hit_shape
  end

  # The mask (FmrbHitMask) with its top-left at (x, y); nil drops it.
  def set_hit_mask(mask, x = 0, y = 0)
    @_hit_mask = mask
    @_hit_mask_x = x
    @_hit_mask_y = y
    _send_hit_shape
  end

  # A mask from dot-art rows: every character not in transparent is hit.
  def hit_mask_from_art(rows, transparent: ".", scale: 1)
    FmrbHitMask.from_art(rows, transparent: transparent, scale: scale)
  end

  # Mark a sprite: its rectangle (or mask, an FmrbHitMask over the sprite's
  # top-left) is hit wherever the sprite is, while it is visible.
  def add_hit_sprite(sprite, mask = nil)
    list = (@_hit_sprites ||= [])
    list << sprite unless list.index(sprite)
    sprite._hit_mark(true, mask)
    _send_hit_shape
  end

  def remove_hit_sprite(sprite)
    list = @_hit_sprites
    return unless list
    i = list.index(sprite)
    return unless i
    list.delete_at(i)
    sprite._hit_mark(false, nil)
    _send_hit_shape
  end

  # Back to the window rectangle.
  def clear_hit_shape
    list = @_hit_sprites
    if list
      i = 0
      while i < list.size
        list[i]._hit_mark(false, nil)
        i += 1
      end
    end
    @_hit_rects = nil
    @_hit_mask = nil
    @_hit_sprites = nil
    _send_hit_shape
  end

  # Packs the lists into the kernel's layout (FmrbHitMask) and declares it.
  def _send_hit_shape
    hdr = FmrbHitMask::ENTRY_HDR
    rects = @_hit_rects
    mask = @_hit_mask
    sprites = @_hit_sprites
    total = 0
    total += rects.size * hdr if rects
    total += hdr + mask.bits.bytesize if mask
    if sprites
      i = 0
      while i < sprites.size
        sp = sprites[i]
        if sp.id
          m = sp._hit_mask
          total += hdr + (m ? m.bits.bytesize : 0)
        end
        i += 1
      end
    end
    if total > FmrbHitMask::SHAPE_MAX
      Log.warn("App #{@_name}: hit shape of #{total} bytes is over #{FmrbHitMask::SHAPE_MAX}; not declared")
      return false
    end
    buf = "\x00" * total
    off = 0
    if rects
      i = 0
      while i < rects.size
        r = rects[i]
        off = FmrbHitMask._put_entry(buf, off, FmrbHitMask::KIND_RECT, 1, r[0], r[1], r[2], r[3], 0)
        i += 1
      end
    end
    off = mask._put(buf, off, @_hit_mask_x, @_hit_mask_y, 0, false) if mask
    if sprites
      i = 0
      while i < sprites.size
        sp = sprites[i]
        if sp.id
          m = sp._hit_mask
          if m
            off = m._put(buf, off, sp.x, sp.y, sp.id, !sp.visible?)
          else
            kind = FmrbHitMask::KIND_RECT
            kind |= FmrbHitMask::HIDDEN unless sp.visible?
            off = FmrbHitMask._put_entry(buf, off, kind, 1, sp.x, sp.y, sp.width, sp.height, sp.id)
          end
        end
        i += 1
      end
    end
    FmrbApp._set_hit_shape(buf)
  end

  def on_resize(new_width, new_height)
    # Called from C
  end

  def request_reload
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "reload_confirm"})
  end

  # Internal methods
  def main_loop
    Log.debug("main_loop started")
    @_suspended = false
    loop do
      return if !@_running
      if @_suspended
        _spin(500)  # Sleep longer while suspended, still process messages
        _run_timers # Spinel runs timers inside _spin; keep the engines equal
        next
      end
      timeout_ms = on_update
      Task.pass  # Yield control to other tasks
      _spin(timeout_ms)
      _run_timers
    end
  end

  # Called by C _spin via on_control for system commands
  def _handle_system_control(msg)
    case msg["cmd"]
    when "suspend"
      @_suspended = true
      on_suspend
      Log.info("App #{@_name} suspended")
    when "resume"
      @_suspended = false
      on_resume
      Log.info("App #{@_name} resumed")
    when "stop"
      Log.info("App #{@_name} received stop command")
      stop
    when "clear_and_stop"
      Log.info("App #{@_name} clearing canvas and stopping")
      if @gfx
        @gfx.clear(0x00)
        @gfx.present
      end
      stop
    when "quit_request"
      # Ctrl+Q on a built-in app: the kernel asks instead of stopping it, so an
      # app holding unsaved state can put a question to the user first.
      on_quit_request
    when "long_press"
      # The kernel saw a left press held still for a second on this frameless
      # window (doc/frameless_window/ W3b): the touch-only way to the window
      # menu, the same hook as a right click.
      if @_frameless && !@_fullscreen
        lx = msg["x"]
        ly = msg["y"]
        on_frameless_right_click({ type: :long_press, button: 1,
                                   x: lx.is_a?(Integer) ? lx : 0,
                                   y: ly.is_a?(Integer) ? ly : 0 })
      end
    end
  end

  # Ctrl+Q. Override to confirm before closing; the default is to close.
  def on_quit_request
    Log.info("App #{@_name} quit request")
    stop
  end

  # End the current _spin now, so main_loop reaches on_update without waiting
  # out the timeout the app asked for.
  #
  # _spin keeps delivering messages for its whole wait but never returns early
  # on its own, so an app that learns from a message that it must act sooner
  # than it planned cannot act on it: the service host is told, from inside
  # on_control, to call a service back in 200 ms while it is part-way through
  # a 30 second sleep, and without this the callback waits out the sleep.
  #
  # Only meaningful from inside a callback _spin dispatched (on_control,
  # on_event) -- the flag is cleared when _spin starts, so asking from
  # on_update, where the next sleep is about to be recomputed anyway, does
  # nothing. Apps that never call it pay one ivar read per message.
  #
  # It is also what lets an idle app sleep for a long time at all: without a
  # way out, "sleep until the next deadline" has to be capped at whatever the
  # app might later be asked to do in a hurry, which is a poll by another
  # name (doc/user_extension/services/plan.md, the idle-sleep paragraph).
  def request_early_update
    @_spin_break = true
    nil
  end

  # ---- timers (Ruby-side; C cannot call a Ruby block) ----
  # Same shape and semantics as the Spinel base (fmrb_app_base_spinel.rb):
  # one-shot, checked once per main_loop cycle, so the resolution is the
  # app's own update interval.
  def set_timer(interval, &blk)
    id = (@_timer_seq ||= 0) + 1
    @_timer_seq = id
    @_timers << { id: id, at: Machine.board_millis + interval, interval: interval, blk: blk }
    id
  end

  def clear_time(timer_id)
    @_timers.reject! { |t| t[:id] == timer_id } if @_timers
    nil
  end

  # Runs on every turn of the app loop, so it is written to cost nothing when
  # nothing is due: no blocks (passing one costs ~0.4 ms here) and no arrays
  # until there is actually a timer to fire.
  def _run_timers
    timers = @_timers
    return if timers.nil?
    count = timers.size
    return if count == 0

    now = Machine.board_millis
    i = 0
    due_count = 0
    while i < count
      due_count += 1 if timers[i][:at] <= now
      i += 1
    end
    return if due_count == 0

    due = []
    keep = []
    i = 0
    while i < count
      t = timers[i]
      if t[:at] <= now
        due << t
      else
        keep << t
      end
      i += 1
    end
    # Swap the list in before running anything: a callback that arms a new
    # timer has to land in the list that survives, not in the one being
    # replaced.
    @_timers = keep
    i = 0
    while i < due.size
      blk = due[i][:blk]
      blk.call if blk
      i += 1
    end
  end

  # The topics are also kept here (@_topics), so that an app started from a
  # checkpoint image subscribes to them again (_checkpoint_restored): the
  # kernel's table names the writing side's process.
  def subscribe(topic)
    topics = (@_topics ||= [])
    topics << topic unless topics.include?(topic)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "subscribe", "topic" => topic})
  end

  def unsubscribe(topic)
    @_topics.delete(topic) if @_topics
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "unsubscribe", "topic" => topic})
  end

  def publish(topic, data = nil)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "publish", "topic" => topic, "data" => data})
  end

  def request_file_select(mode = "open")
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "file_select", "mode" => mode })
  end

  # Ask the kernel to run a file (.rb / .bas) as an app.
  #
  # An app cannot spawn another app, so this is a request: the kernel stops the
  # instance a previous request started (prev_pid, may be nil), spawns the file,
  # and gives it the keyboard. The new pid comes back as an APP_CONTROL message
  # {"cmd" => "run_result", "path" => ..., "pid" => ... (nil on failure)}.
  # Paths are limited to /app and /home by the kernel.
  def request_run(path, prev_pid = nil)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "run", "path" => path, "prev_pid" => prev_pid })
  end

  # Ask for this app's own checkpoint image (doc/checkpoint): the kernel asks
  # the app back, the way a tool or the shell would, and the image is written
  # at the first safe point after the message wait -- never inside on_event
  # or on_update, so asking from there is fine. Once written the app closes
  # (on_checkpoint); with restart the kernel starts the image again
  # (on_restore). A refusal arrives as on_checkpoint_refused. Returns nil when
  # asked, or a String saying why it cannot be asked (this build cannot write
  # images, ...).
  def checkpoint(restart: false)
    FmrbApp.checkpoint(FmrbApp.pid, restart: restart)
  end

  # Ask to move this app to another machine over Asterism (doc/checkpoint
  # F5), named by its Asterism node ID: the kernel asks that machine first,
  # has the app written and held at the next safe point after the message
  # wait, carries the image there and closes the app here once it is up
  # there (on_checkpoint is the last hook here, on_restore the first there).
  # When it stays (refused there, or no answer), on_checkpoint_refused tells
  # why and the app goes on. Returns nil when asked, or a String saying why
  # it cannot be asked.
  def move_to(to, hold: 0)
    FmrbApp.move(FmrbApp.pid, to, hold: hold)
  end

  # Ask the kernel to switch this app between windowed and fullscreen. The VM
  # keeps running, so app state survives; the answer arrives as on_resize with
  # @_fullscreen and the user area already updated.
  def request_fullscreen(on)
    cmd = on ? "enter_fullscreen" : "exit_fullscreen"
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => cmd })
  end

  def toggle_fullscreen
    request_fullscreen(!@_fullscreen)
  end

  # Make sure the graphics/audio side's copy of a file matches this one,
  # transferring it only when it differs (size + CRC32). FmrbGfx#sync_file is
  # the same thing for apps that have a canvas; this one is here so a headless
  # app can do it too (play_wav from the service host).
  def sync_file(path, dest: nil)
    _sync_file(path, dest || path)
  end

  def send_message(dest_pid, msg_type, data)
    # Auto-serialize all data to msgpack binary
    binary_data = MessagePack.pack(data)
    _send_message(dest_pid, msg_type, binary_data)
  end

  # How far behind collection may fall before the allocation path forces a
  # step anyway. It only comes into play when an app never gives _spin any
  # slack; then this is what stops the heap from growing without bound, at
  # the cost of the pause it would have had before idle_gc. Measured in
  # objects allocated past due (the app's live set is 2000-3000).
  IDLE_GC_DEBT_LIMIT = 4096

  # How much work one collector step may do. It decides how long a single
  # step runs, and a step is the one thing that can still make an app late:
  # it cannot be interrupted once started. Measured in the simulation over
  # the same 24,000 objects, the longest single step was 6-10 ms at mruby's
  # own 2000 and 4.7-5.3 ms at 512, and dropping the limit further (128, 32)
  # did not shorten it again - one phase per cycle is not divisible, and that
  # is the floor. Below 512 only the step count multiplies (195 -> 598 ->
  # 1885 for the same total collector time), so this is the knee
  # (doc/midi/report/p7.md).
  IDLE_GC_STEP_LIMIT = 512

  # Byte-pressure trigger for scheduler-driven GC. Without it the collector
  # only reacts to object-count debt, which badly under-counts Hash/String
  # heavy workloads (their payload lives in malloc'd buffers, not object
  # slots): the desktop grew its 800KB pool to the brim, then paid with one
  # multi-second full GC. 32KB of malloc growth per step keeps collection
  # continuous instead.
  IDLE_GC_MALLOC_THRESHOLD = 32 * 1024

  # Collect while this app waits in _spin, instead of leaving it to whatever
  # call happens to allocate.
  #
  # Worth turning on for an app that has to hold a rhythm - playing a song,
  # animating - and cannot get its allocation down to nothing. Collection
  # then runs in pieces, taken while the app has nothing to do, instead of
  # stopping it for 100-205 ms in the middle of a phrase
  # (doc/midi/report/p7.md).
  #
  # Costs: generational mode goes off (it cannot be split into steps) and
  # does not come back by itself, and a step can delay a message by its own
  # length. Both are measured in the report. An app that is always busy gets
  # its old behaviour back through IDLE_GC_DEBT_LIMIT.
  def idle_gc=(enable)
    if enable
      GC.debt_limit = IDLE_GC_DEBT_LIMIT
      GC.step_limit = IDLE_GC_STEP_LIMIT
      GC.malloc_threshold = IDLE_GC_MALLOC_THRESHOLD
      GC.scheduler_driven = true
    else
      GC.scheduler_driven = false
      GC.step_limit = 0
    end
    @_idle_gc = enable
  rescue => e
    # Not fatal: the app runs exactly as it did before.
    Log.warn("idle_gc unavailable: #{e.message}")
    @_idle_gc = false
  end

  def idle_gc
    @_idle_gc ? true : false
  end

  def set_window_position(x, y)
    _set_window_param(:pos_x, x)
    _set_window_param(:pos_y, y)
    @gfx.present if @gfx  # Immediately reflect position change
    self
  end

  # Create an extra canvas owned by this app and return an FmrbGfx bound to
  # it. Deleted automatically on app exit (also on crash/kill). Intended for
  # fullscreen apps (the window manager does not track extra canvases across
  # focus changes). Position and show it with gfx.present(x, y); combine
  # with gfx.set_viewport for hardware-scrolled layers (Modern/P4 only).
  def create_canvas_gfx(width:, height:, z_offset: 1, transparent: false, transparent_color: 0)
    id = _create_canvas(width, height, z_offset, transparent ? 1 : 0, transparent_color)
    gfx = FmrbGfx.new(id, width: width, height: height)
    # how it was made, for a checkpoint image to make it again (gfx.c)
    gfx._extra_canvas(z_offset, transparent ? 1 : 0, transparent_color)
    gfx
  end

  def delete_canvas_gfx(gfx)
    _delete_canvas(gfx.canvas_id)
  end

  def destroy
    Log.debug("destroy() called")

    # Send exit notification to kernel BEFORE cleanup.
    #
    # "expected" is true because this method is only reached when main_loop
    # returned, and main_loop only returns when stop was called -- an app that
    # dies on an exception never gets here. The C task wrapper sends its own
    # notification afterwards carrying the context's mark; both say the same
    # thing, and the kernel ignores the second (the slot is already gone).
    begin
      exit_data = MessagePack.pack({"cmd" => "exit", "expected" => true})
      _send_message(0, FmrbConst::MSG_TYPE_APP_CONTROL, exit_data)
      Log.debug("Exit notification sent to kernel")
    rescue => e
      Log.error("Failed to send exit notification: #{e}")
    end

    # Release the window-frame program before the canvas it belongs to is freed.
    if @_frame_block
      @_frame_block.destroy
      @_frame_block = nil
    end
    if @_corner_clear_block
      @_corner_clear_block.destroy
      @_corner_clear_block = nil
    end
    if @gfx
      @gfx.destroy  # Cleanup graphics resources
      @gfx = nil    # Prevent finalizer from running during mrb_close
    end
    on_destroy
    _cleanup()  # C function: cleanup canvas and message queue
  end

  def start
    Log.debug("start() called, @_running=#{@_running}")
    @_running = true
    Log.debug("Before on_create")
    on_create
    Log.debug("After on_create, entering main_loop")
    main_loop
    Log.debug("After main_loop, calling destroy")
    destroy
  end

  # Every intended end goes through here: the close button, Ctrl+Q, the
  # kernel's kill, and a script that has simply finished. Marking it is what
  # lets the kernel tell this from a crash -- an app that dies on an exception
  # never reaches this method, and to C the two look identical (the rescue at
  # the foot of the script means the task ends normally either way).
  def stop
    @_running = false
    _mark_expected_stop
    nil
  end

end