# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.

class FmrbApp
  attr_reader :name, :running, :window_width, :window_height, :pos_x, :pos_y

  def initialize()
    Log.debug("initialize")
    @running = false
    _init() # C function, variables are defined here
    Log.debug("name=#{@name}")
    Log.debug("After _init(), @canvas=#{@canvas}, @window_width=#{@window_width}, @window_height=#{@window_height}")

    # Initialize graphics only for non-headless apps (@canvas is set)
    if @canvas
      @gfx = FmrbGfx.new(@canvas)  # Pass canvas_id to FmrbGfx
      Log.debug("FmrbGfx initialized: canvas_id=#{@canvas}")
      @user_area_x0 = 1
      @user_area_y0 = 10
      @user_area_x1 = @window_width - 1
      @user_area_y1 = @window_height  - 1
      @user_area_width = @window_width - 2
      @user_area_height = @window_height - 12

      draw_window_frame
    else
      @gfx = nil
      Log.debug("Headless app: no graphics initialized")
    end

  end

  def draw_window_frame    
    # Draw title bar
    @gfx.fill_rect(0, 0, @window_width, 11, 0xC5)
    @gfx.fill_rect(2, 2, 8, 8, 0x60) # menu button
    @gfx.draw_text(12, 2, @name, FmrbGfx::WHITE)
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

  def on_event(ev)
    # Called from C
    #Log.debug("on_event called")
  end

  # Internal methods
  def main_loop
    Log.debug("main_loop started")
    loop do
      return if !@running
      timeout_ms = on_update
      Task.pass  # Yield control to other tasks
      _spin(timeout_ms)
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
    @gfx.destroy if @gfx  # Cleanup graphics resources
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