# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.

class FmrbApp
  TITLE_BAR_H = 11

  attr_reader :name, :running, :window_width, :window_height, :pos_x, :pos_y, :platform, :fullscreen

  def initialize()
    Log.debug("initialize")
    @running = false
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

      draw_window_frame
    else
      @gfx = nil
      @bg_gfx = nil
      Log.debug("Headless app: no graphics initialized")
    end

  end

  def draw_window_frame
    return if @fullscreen
    # Draw title bar
    @gfx.fill_rect(0, 0, @window_width, TITLE_BAR_H, 0xC5)
    @gfx.fill_rect(2, 2, 8, 8, 0x60) # menu button
    @gfx.draw_text(12, 2, @name, FmrbGfx::WHITE)

    # Draw close button (X) on the right side of title bar
    close_btn_x = @window_width - 10
    close_btn_y = 2
    @gfx.fill_rect(close_btn_x, close_btn_y, 8, 8, 0xE0) # red background
    # Draw X mark (two diagonal lines)
    @gfx.draw_line(close_btn_x + 2, close_btn_y + 2, close_btn_x + 5, close_btn_y + 5, FmrbGfx::WHITE)
    @gfx.draw_line(close_btn_x + 5, close_btn_y + 2, close_btn_x + 2, close_btn_y + 5, FmrbGfx::WHITE)

    # Draw window border
    @gfx.draw_rect(0, 0, @window_width, @window_height, 0x60)
  end

  # Scroll bar constants and helpers
  SCROLLBAR_W = 10
  SCROLLBAR_BTN_H = 10

  # Draw a vertical scroll bar with up/down arrow buttons
  # scroll: current scroll position (0-based)
  # total: total item count
  # visible: number of visible items
  # x, y, w, h: scroll area (defaults to user area)
  def draw_scrollbar(scroll, total, visible, x = @user_area_x0, y = @user_area_y0, w = @user_area_width, h = @user_area_height)
    return if total <= visible
    bar_x = x + w - SCROLLBAR_W
    btn_h = SCROLLBAR_BTN_H
    border = FmrbConst::THEME_BORDER
    bg = FmrbConst::THEME_WINDOW_BG

    # Separator line between content and scrollbar
    @gfx.draw_line(bar_x - 1, y, bar_x - 1, y + h - 1, border)
    @gfx.draw_line(bar_x    , y, bar_x    , y + h - 1, bg)

    # Up button
    @gfx.fill_rect(bar_x, y, SCROLLBAR_W, btn_h, bg)
    @gfx.draw_rect(bar_x, y, SCROLLBAR_W, btn_h, border)
    # Up arrow triangle
    cx = bar_x + SCROLLBAR_W / 2
    @gfx.draw_line(cx, y + 2, cx - 3, y + 7, border)
    @gfx.draw_line(cx, y + 2, cx + 3, y + 7, border)
    @gfx.draw_line(cx - 3, y + 7, cx + 3, y + 7, border)

    # Down button
    dy = y + h - btn_h
    @gfx.fill_rect(bar_x, dy, SCROLLBAR_W, btn_h, bg)
    @gfx.draw_rect(bar_x, dy, SCROLLBAR_W, btn_h, border)
    # Down arrow triangle
    @gfx.draw_line(cx, dy + 7, cx - 3, dy + 2, border)
    @gfx.draw_line(cx, dy + 7, cx + 3, dy + 2, border)
    @gfx.draw_line(cx - 3, dy + 2, cx + 3, dy + 2, border)

    # Thumb in track area
    track_y = y + btn_h
    track_h = h - btn_h * 2
    if track_h > 4
      thumb_h = [track_h * visible / total, 6].max
      max_scroll = total - visible
      thumb_y = track_y + (max_scroll > 0 ? (track_h - thumb_h) * scroll / max_scroll : 0)
      @gfx.fill_rect(bar_x + 1, thumb_y, SCROLLBAR_W - 2, thumb_h, border)
    end
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
    33 
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
    # Handle close button click (left click)
    if ev[:type] == :mouse_up && ev[:button] == 1
      close_btn_x = @window_width - 10
      close_btn_y = 2
      if ev[:x] >= close_btn_x && ev[:x] < close_btn_x + 8 &&
         ev[:y] >= close_btn_y && ev[:y] < close_btn_y + 8
        stop
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