# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.

class FmrbApp
  attr_reader :name, :running

  def initialize()
    puts "[FmrbApp]initialize"
    @running = false
    _init() # C function, variables are defined here
    puts "[FmrbApp][#{@name}] name=#{@name}"
    puts "[FmrbApp][#{@name}] After _init(), @canvas=#{@canvas}, @window_width=#{@window_width}, @window_height=#{@window_height}"

    # Initialize graphics only for non-headless apps (@canvas is set)
    if @canvas
      @gfx = FmrbGfx.new(@canvas)  # Pass canvas_id to FmrbGfx
      puts "[FmrbApp][#{@name}] FmrbGfx initialized: canvas_id=#{@canvas}"
    else
      @gfx = nil
      puts "[FmrbApp][#{@name}] Headless app: no graphics initialized"
    end

  end

  # Lifecycle methods (override in subclass)

  def on_create
    # Called once when app is created
    # Initialize your app state here
    # Access @name and @gfx instance variables
    puts "[FmrbApp][#{@name}]on_create"
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
    puts "[FmrbApp][#{@name}]on_destroy"
  end

  def on_event(ev)
    # Called from C
    puts "[FmrbApp][#{@name}] on_event called"
  end

  # Internal methods
  def main_loop
    puts "[FmrbApp][#{@name}] main_loop started"
    loop do
      return if !@running
      timeout_ms = on_update
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
    _send_message(dest_pid, msg_type, data)
  end

  def destroy
    puts "[FmrbApp][#{@name}] destroy() called"
    @gfx.destroy if @gfx  # Cleanup graphics resources
    on_destroy
    _cleanup()  # C function: cleanup canvas and message queue
  end

  def start
    puts "[FmrbApp][#{@name}] start() called, @running=#{@running}"
    @running = true
    puts "[FmrbApp][#{@name}] Before on_create"
    on_create
    puts "[FmrbApp][#{@name}] After on_create, entering main_loop"
    main_loop
    puts "[FmrbApp][#{@name}] After main_loop, calling destroy"
    destroy
  end

  def stop
    @running = false
  end

end