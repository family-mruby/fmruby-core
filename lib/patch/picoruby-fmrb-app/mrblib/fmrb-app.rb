# Family mruby OS - Ruby App Framework
# User app should inherit this class and override lifecycle methods.
#
# Lifecycle flow:
#   1. on_create()  - Called once when app is created
#   2. on_update()  - Called every frame (60Hz)
#   3. on_pause()   - Called when app goes to background
#   4. on_resume()  - Called when app comes to foreground
#   5. on_destroy() - Called once when app is destroyed
#
# Event handlers:
#   - on_key_down(key_code)
#   - on_key_up(key_code)
#   - on_mouse_move(x, y)
#   - on_mouse_click(x, y, button)

class FmrbApp
  attr_reader :name, :running

  def initialize(name = "FmrbApp")
    @name = name
    @running = false
    @paused = false
  end

  # Lifecycle methods (override in subclass)

  def on_create
    # Called once when app starts
    # Initialize your app state here
  end

  def on_update(delta_time_ms)
    # Called every frame (60Hz)
    # delta_time_ms: time since last update in milliseconds
    # Update your app logic here
  end

  def on_pause
    # Called when app goes to background
    # Save state or pause animations here
  end

  def on_resume
    # Called when app comes back to foreground
    # Resume animations or reload state here
  end

  def on_destroy
    # Called once when app is destroyed
    # Cleanup resources here
  end

  # Input event handlers (override in subclass)

  def on_key_down(key_code)
    # Called when a key is pressed
    # key_code: integer representing the key
  end

  def on_key_up(key_code)
    # Called when a key is released
    # key_code: integer representing the key
  end

  def on_mouse_move(x, y)
    # Called when mouse moves
    # x, y: mouse coordinates
  end

  def on_mouse_click(x, y, button)
    # Called when mouse button is clicked
    # x, y: click coordinates
    # button: 0=left, 1=right, 2=middle
  end

  # Internal methods (called by C layer)

  def start
    @running = true
    @paused = false
    on_create
  end

  def stop
    @running = false
    on_destroy
  end

  def pause
    @paused = true
    on_pause
  end

  def resume
    @paused = false
    on_resume
  end

  def update(delta_time_ms)
    return if @paused || !@running
    on_update(delta_time_ms)
  end

  # Input dispatch (called by C layer)

  def dispatch_key_down(key_code)
    on_key_down(key_code) if @running
  end

  def dispatch_key_up(key_code)
    on_key_up(key_code) if @running
  end

  def dispatch_mouse_move(x, y)
    on_mouse_move(x, y) if @running
  end

  def dispatch_mouse_click(x, y, button)
    on_mouse_click(x, y, button) if @running
  end
end