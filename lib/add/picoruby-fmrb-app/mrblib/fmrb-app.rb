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

  # :gfx is here so apps can write gfx.draw_text as well as @gfx.draw_text --
  # the editor's completion answers both spellings, and the method form is the
  # one the type signatures in sig/ describe.
  attr_reader :name, :running, :window_width, :window_height, :pos_x, :pos_y, :platform, :fullscreen, :rounded_corners, :gfx
  # User area (inside the window frame, below the title bar). Exposed so
  # helper libraries such as FmrbUI can place widgets in window-local
  # coordinates without reaching into the app's instance variables.
  attr_reader :user_area_x0, :user_area_y0, :user_area_width, :user_area_height
  # Whether a click on the close-button area may stop the app. System apps
  # that own the screen (the desktop) must set this to false: for them the
  # top-right corner is ordinary UI, not a close button.
  attr_accessor :closable

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
    @running = false
    @close_btn_pressed = false
    @closable = true
    @_timers = []
    _init() # C function, variables are defined here
    Log.debug("name=#{@name}")
    Log.debug("After _init(), @canvas=#{@canvas}, @window_width=#{@window_width}, @window_height=#{@window_height}")

    # Initialize graphics only for non-headless apps (@canvas is set)
    if @canvas
      @gfx = FmrbGfx.new(@canvas, width: @window_width, height: @window_height)
      Log.debug("FmrbGfx initialized: canvas_id=#{@canvas}")
      if @fullscreen
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

      # Background canvas (desktop only)
      if @bg_canvas
        @bg_gfx = FmrbGfx.new(@bg_canvas, width: @window_width, height: @window_height)
        Log.debug("Background GFX initialized: canvas_id=#{@bg_canvas}")
      else
        @bg_gfx = nil
      end

      _apply_user_area_sprite_clip

      unless @fullscreen
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
    return if @fullscreen
    # An app spawned fullscreen never built the frame block (nothing to frame),
    # so build it on the first draw after it becomes a window at runtime -- F11
    # on an editor started with `edit -f` used to leave it with no title bar,
    # no border and no close button.
    _build_frame_block if @gfx && !@frame_block
    if @gfx
      saved_font = @gfx.current_font
      saved_size = @gfx.current_text_size
      @gfx.set_font(:default)
      @gfx.set_text_size(1)
    end
    if @frame_block
      @frame_block.draw(w: @window_width, h: @window_height)
    end
    # Re-stamp the 3 outer pixels of each rounded corner with the canvas
    # color key, so resize / clear-induced opaque pixels there composite
    # as transparent again. Runs after the frame draw so the round-rect
    # border (drawn last by the frame block) stays intact.
    if @corner_clear_block
      @corner_clear_block.draw(w: @window_width, h: @window_height)
    end
    if @gfx
      @gfx.set_font(*saved_font) unless saved_font == [:default]
      @gfx.set_text_size(saved_size) unless saved_size == 1
    end
    # Re-apply composite regions after every frame redraw so resize updates
    # take effect. Internally cached on (w, h); a no-op when size is unchanged.
    _apply_rounded_corner_regions
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

  # Clear the user area (region inside the window frame, excluding the title
  # bar) with the given color. Use this instead of @gfx.clear to keep the
  # title bar and close button intact.
  # @param color [Integer] RGB332 color. Defaults to the theme's window
  #   background, so an app that never names a colour follows the system
  #   theme; pass one only when the picture calls for it.
  def clear_user_area(color = FmrbConst::THEME_WINDOW_BG)
    return unless @gfx
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, color)
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
    return if @bg_canvas
    if @fullscreen
      @gfx.clear_sprite_clip
    else
      @gfx.set_sprite_clip(@user_area_x0, @user_area_y0,
                           @user_area_width, @user_area_height)
    end
  end

  private

  # Build the window-frame GfxBlock once. The block captures @name as a closure
  # so the title text is interned into the bytecode's strtable at new time
  # (strings are immutable after that).
  def _build_frame_block
    title = @name
    @frame_block = GfxBlock.new(@gfx, w: @window_width, h: @window_height) do |r, w:, h:|
      # Title bar with rounded top corners. Draw full-height rounded rect then
      # overwrite the bottom portion so the bottom edge is straight.
      r.fill_round_rect 0, 0, w, TITLE_BAR_H, CORNER_R, 0xC5
      r.fill_rect       0, CORNER_R, w, TITLE_BAR_H - CORNER_R, 0xC5
      # Menu button (hamburger: 3 horizontal lines) + title text
      r.fill_rect       3, 3, 9, 1, 0xFB
      r.fill_rect       3, 5, 9, 1, 0xFB
      r.fill_rect       3, 7, 9, 1, 0xFB
      r.draw_text       15, 2, title, FmrbGfx::WHITE
      # Close button (red circle with white X)
      r.fill_circle     w - 6, 5, 3, 0xFF
      # Rounded window border. Outer edge rows/columns stay transparent because
      # app content never fills them (user_area excludes x=0, x=w-1, y=h-1).
      r.draw_round_rect 0, 0, w, h, CORNER_R, 0x60
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
    return if @fullscreen
    return unless @gfx
    return unless @rounded_corners
    # Apps that own a bg_canvas (system_desktop) manage their own composite
    # regions and toggle them on/off around the boot animation, so don't
    # clobber them with the generic rounded-corner layout.
    return if @bg_canvas
    w = @window_width
    h = @window_height
    return if @composite_region_w == w && @composite_region_h == h
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
    @composite_region_w = w
    @composite_region_h = h
  end

  # CORNER_R=4 の弧外側 3 px ずつ (= 12 px / window) を canvas color key
  # (0x01) で塗り直す。canvas resize やアプリの clear で角丸の外側ピクセルが
  # 不透明色に書き換わると compositing で透けなくなるための保険。
  # 別 GfxBlock に分離しているのは _build_frame_block と合体させると
  # DEFINE_PROG_MAX_PAYLOAD (220B) を超えるリスクがあるため。
  def _build_corner_clear_block
    t = TRANSPARENT_COLOR
    @corner_clear_block = GfxBlock.new(@gfx, w: @window_width, h: @window_height) do |r, w:, h:|
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
    # Access @name and @gfx instance variables
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

  def on_event(ev)
    # Called from C
    # Handle close button press feedback + click
    if @closable && ev[:button] == 1 && (ev[:type] == :mouse_down || ev[:type] == :mouse_up)
      cx = @window_width - CLOSE_BTN_CX_OFFSET
      cy = CLOSE_BTN_CY
      hit = (ev[:x] - cx).abs <= CLOSE_BTN_HIT_R &&
            (ev[:y] - cy).abs <= CLOSE_BTN_HIT_R

      case ev[:type]
      when :mouse_down
        if hit && !@fullscreen && @gfx
          @close_btn_pressed = true
          @gfx.fill_circle(cx, cy, CLOSE_BTN_R, CLOSE_BTN_PRESSED_COLOR)
          @gfx.present
        end
      when :mouse_up
        if @close_btn_pressed
          @close_btn_pressed = false
          if hit
            stop
          elsif @gfx
            # Released outside the button — restore the normal circle.
            @gfx.fill_circle(cx, cy, CLOSE_BTN_R, CLOSE_BTN_NORMAL_COLOR)
            @gfx.present
          end
        elsif hit && !@fullscreen && @gfx
          # Safety net: down event missed but click landed on button. Must
          # carry the same guards as the down path -- without them a plain
          # click on the top-right corner of a fullscreen app (which draws
          # no close button at all) silently stopped it. That is how the
          # desktop was once killed from the launcher.
          stop
        end
      end
    end
    # Handle title bar right click (reload for file-based apps)
    if ev[:type] == :mouse_up && ev[:button] == 3 && ev[:y] < 11
      request_reload if _is_file_app
    end
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
    @suspended = false
    loop do
      return if !@running
      if @suspended
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
      @suspended = true
      on_suspend
      Log.info("App #{@name} suspended")
    when "resume"
      @suspended = false
      on_resume
      Log.info("App #{@name} resumed")
    when "stop"
      Log.info("App #{@name} received stop command")
      stop
    when "clear_and_stop"
      Log.info("App #{@name} clearing canvas and stopping")
      if @gfx
        @gfx.clear(0x00)
        @gfx.present
      end
      stop
    when "quit_request"
      # Ctrl+Q on a built-in app: the kernel asks instead of stopping it, so an
      # app holding unsaved state can put a question to the user first.
      on_quit_request
    end
  end

  # Ctrl+Q. Override to confirm before closing; the default is to close.
  def on_quit_request
    Log.info("App #{@name} quit request")
    stop
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

  def subscribe(topic)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "subscribe", "topic" => topic})
  end

  def unsubscribe(topic)
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

  # Ask the kernel to switch this app between windowed and fullscreen. The VM
  # keeps running, so app state survives; the answer arrives as on_resize with
  # @fullscreen and the user area already updated.
  def request_fullscreen(on)
    cmd = on ? "enter_fullscreen" : "exit_fullscreen"
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => cmd })
  end

  def toggle_fullscreen
    request_fullscreen(!@fullscreen)
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
    @idle_gc = enable
  rescue => e
    # Not fatal: the app runs exactly as it did before.
    Log.warn("idle_gc unavailable: #{e.message}")
    @idle_gc = false
  end

  def idle_gc
    @idle_gc ? true : false
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
    FmrbGfx.new(id, width: width, height: height)
  end

  def delete_canvas_gfx(gfx)
    _delete_canvas(gfx.canvas_id)
  end

  def destroy
    Log.debug("destroy() called")

    # Send exit notification to kernel BEFORE cleanup
    begin
      exit_data = MessagePack.pack({"cmd" => "exit"})
      _send_message(0, FmrbConst::MSG_TYPE_APP_CONTROL, exit_data)
      Log.debug("Exit notification sent to kernel")
    rescue => e
      Log.error("Failed to send exit notification: #{e}")
    end

    # Release the window-frame program before the canvas it belongs to is freed.
    if @frame_block
      @frame_block.destroy
      @frame_block = nil
    end
    if @corner_clear_block
      @corner_clear_block.destroy
      @corner_clear_block = nil
    end
    if @gfx
      @gfx.destroy  # Cleanup graphics resources
      @gfx = nil    # Prevent finalizer from running during mrb_close
    end
    on_destroy
    _cleanup()  # C function: cleanup canvas and message queue
  end

  def start
    Log.debug("start() called, @running=#{@running}")
    @running = true
    Log.debug("Before on_create")
    on_create
    Log.debug("After on_create, entering main_loop")
    main_loop
    Log.debug("After main_loop, calling destroy")
    destroy
  end

  def stop
    @running = false
  end

end