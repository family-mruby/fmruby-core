# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.

class FmrbApp
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
      @user_area_x0 = 1
      @user_area_y0 = 10
      @user_area_x1 = @window_width - 1
      @user_area_y1 = @window_height  - 1
      @user_area_width = @window_width - 2
      @user_area_height = @window_height - 12

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
    # Draw title bar
    @gfx.fill_rect(0, 0, @window_width, 11, 0xC5)
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
    # Handle close button click
    if ev[:type] == :mouse_up && ev[:button] == 1
      close_btn_x = @window_width - 10
      close_btn_y = 2
      # Check if click is within close button area
      if ev[:x] >= close_btn_x && ev[:x] < close_btn_x + 8 &&
         ev[:y] >= close_btn_y && ev[:y] < close_btn_y + 8
        stop
      end
    end
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

  def subscribe(from,type,name, &blk)
  end

  def publish()
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

end