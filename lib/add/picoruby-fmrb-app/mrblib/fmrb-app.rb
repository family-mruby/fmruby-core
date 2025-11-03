# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.
#
# Lifecycle flow:
#   1. on_create()  - Called once when app is created
#   2. on_update()  - Called every frame (return wait time)
#   3. on_destroy() - Called once when app is destroyed
#
# Event handlers:
#   TBD

class FmrbApp
  attr_reader :name, :running

  def initialize()
    puts "[FmrbApp]initialize"
    @running = false
    puts "[DEBUG] Before _init()"
    _init() # C function, variables are defined here
    puts "[DEBUG] After _init(), @window_width=#{@window_width}, @window_height=#{@window_height}"
    # @gfx = FmrbGfx.new(@window_width, @window_height)  # Phase2: Canvas + messaging
    puts "[DEBUG] FmrbGfx initialization skipped (Canvas feature not implemented yet)"
    puts "[FmrbApp]name=#{@name}"
  end

  # Lifecycle methods (override in subclass)

  def on_create
    # Called once when app is created
    # Initialize your app state here
    # Access @name and @gfx instance variables
    puts "[FmrbApp]on_create"
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
    puts "[FmrbApp]on_destroy"
  end

  def on_event(ev)
    # Called from C
  end

  # Internal methods
  def main_loop
    puts "[DEBUG] main_loop started"
    loop do
      return if !@running
      timeout_ms = on_update
      _spin(timeout_ms)
    end
  end

  def destroy
    puts "[DEBUG] destroy() called"
    # @gfx.destroy  # Phase2: Canvas + messaging
    on_destroy
  end

  def start
    puts "[DEBUG] start() called, @running=#{@running}"
    @running = true
    puts "[DEBUG] Before on_create"
    on_create
    puts "[DEBUG] After on_create, entering main_loop"
    main_loop
    puts "[DEBUG] After main_loop, calling destroy"
    destroy
  end

  def stop
    @running = false
  end

end