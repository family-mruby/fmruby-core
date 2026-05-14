# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.

class FmrbApp
  TITLE_BAR_H = 11
  CORNER_R = 4
  TRANSPARENT_COLOR = 0x01
  SCROLLBAR_W = 10
  SCROLLBAR_BTN_H = 10

  attr_reader :name, :running, :window_width, :window_height, :pos_x, :pos_y, :platform, :fullscreen

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

      unless @fullscreen
        _build_frame_block
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
    if @gfx
      saved_font = @gfx.current_font
      saved_size = @gfx.current_text_size
      @gfx.set_font(:default)
      @gfx.set_text_size(1)
    end
    if @frame_block
      @frame_block.draw(w: @window_width, h: @window_height)
    end
    if @gfx
      @gfx.set_font(*saved_font) unless saved_font == [:default]
      @gfx.set_text_size(saved_size) unless saved_size == 1
    end
  end

  # Clear the user area (region inside the window frame, excluding the title
  # bar) with the given color. Use this instead of @gfx.clear to keep the
  # title bar and close button intact.
  # @param color [Integer] RGB332 color (default: FmrbGfx::BLACK)
  def clear_user_area(color = FmrbGfx::BLACK)
    return unless @gfx
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, color)
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
  end

  # Build a scrollbar GfxBlock for a fixed geometry. Static parts (separator,
  # up/down buttons, arrows) are baked into bytecode; only the thumb rectangle
  # is driven by kwargs (thumb_y, thumb_h) and updated per draw.
  def _build_scrollbar_block(x, y, w, h)
    bar_x = x + w - SCROLLBAR_W
    btn_h = SCROLLBAR_BTN_H
    border = FmrbConst::THEME_BORDER
    bg = FmrbConst::THEME_WINDOW_BG
    cx = bar_x + SCROLLBAR_W / 2
    dy = y + h - btn_h

    GfxBlock.new(@gfx, thumb_y: y + btn_h, thumb_h: 6) do |r, thumb_y:, thumb_h:|
      # Separator column between content and scrollbar
      r.draw_line(bar_x , y, bar_x , y + h - 1, border)
      # Up button + arrow
      r.fill_rect(bar_x, y, SCROLLBAR_W, btn_h, bg)
      r.draw_rect(bar_x, y, SCROLLBAR_W, btn_h, border)
      r.draw_line(cx, y + 2, cx - 3, y + 7, border)
      r.draw_line(cx, y + 2, cx + 3, y + 7, border)
      r.draw_line(cx - 3, y + 7, cx + 3, y + 7, border)
      # Down button + arrow
      r.fill_rect(bar_x, dy, SCROLLBAR_W, btn_h, bg)
      r.draw_rect(bar_x, dy, SCROLLBAR_W, btn_h, border)
      r.draw_line(cx, dy + 7, cx - 3, dy + 2, border)
      r.draw_line(cx, dy + 7, cx + 3, dy + 2, border)
      r.draw_line(cx - 3, dy + 2, cx + 3, dy + 2, border)
      # Thumb (dynamic)
      r.fill_rect(bar_x + 2, thumb_y, SCROLLBAR_W - 3, thumb_h, border)
    end
  end

  public

  # Draw a vertical scroll bar with up/down arrow buttons
  # scroll: current scroll position (0-based)
  # total: total item count
  # visible: number of visible items
  # x, y, w, h: scroll area (defaults to user area)
  def draw_scrollbar(scroll, total, visible, x = @user_area_x0, y = @user_area_y0, w = @user_area_width, h = @user_area_height)
    return if total <= visible
    btn_h = SCROLLBAR_BTN_H
    track_y = y + btn_h
    track_h = h - btn_h * 2
    return if track_h <= 4

    thumb_h = [track_h * visible / total, 6].max
    max_scroll = total - visible
    thumb_y = track_y + (max_scroll > 0 ? (track_h - thumb_h) * scroll / max_scroll : 0)

    @scrollbar_blocks ||= {}
    key = [x, y, w, h]
    block = (@scrollbar_blocks[key] ||= _build_scrollbar_block(x, y, w, h))
    block.draw(thumb_y: thumb_y, thumb_h: thumb_h)
  end

  # Hit test for scroll bar click
  # Returns :up, :down, or nil
  def scrollbar_hit(click_x, click_y, x = @user_area_x0, y = @user_area_y0, w = @user_area_width, h = @user_area_height)
    bar_x = x + w - SCROLLBAR_W - 1
    return nil unless click_x >= bar_x && click_y >= y && click_y < y + h
    btn_h = SCROLLBAR_BTN_H
    mid_y = y + h / 2
    if click_y < y + btn_h
      :up
    elsif click_y >= y + h - btn_h
      :down
    elsif click_y < mid_y
      :up
    else
      :down
    end
  end

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
    if ev[:button] == 1 && (ev[:type] == :mouse_down || ev[:type] == :mouse_up)
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
        elsif hit
          stop  # safety net: down event missed but click landed on button
        end
      end
    end
    # Handle title bar right click (reload for file-based apps)
    if ev[:type] == :mouse_up && ev[:button] == 3 && ev[:y] < 11
      request_reload if _is_file_app
    end
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
        next
      end
      timeout_ms = on_update
      Task.pass  # Yield control to other tasks
      _spin(timeout_ms)
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
    end
  end

  def set_timer(interval, &blk)
    return timer_id
  end

  def clear_time(timer_id)
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

  def send_message(dest_pid, msg_type, data)
    # Auto-serialize all data to msgpack binary
    binary_data = MessagePack.pack(data)
    _send_message(dest_pid, msg_type, binary_data)
  end

  def set_window_position(x, y)
    _set_window_param(:pos_x, x)
    _set_window_param(:pos_y, y)
    @gfx.present if @gfx  # Immediately reflect position change
    self
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
    if @scrollbar_blocks
      @scrollbar_blocks.each_value { |b| b.destroy }
      @scrollbar_blocks = nil
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

  # Convert virtual path (e.g. "/home/music/x.nsf") to OS file path for File.open.
  # Strips the leading "/" since HAL adds the flash prefix.
  def to_file_path(virtual_path)
    virtual_path.start_with?("/") ? virtual_path[1..-1] : virtual_path
  end

  # Convert virtual path to OS directory path for Dir.open.
  # Linux: "flash/home/music", ESP32: "/flash/home/music"
  def to_os_dir_path(virtual_path)
    fs_root = @platform == :linux ? "flash" : "/flash"
    if virtual_path == "/"
      fs_root
    else
      "#{fs_root}#{virtual_path}"
    end
  end

end