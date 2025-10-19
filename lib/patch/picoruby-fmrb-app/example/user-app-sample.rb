# Sample User Application for Family mruby OS
# This demonstrates how to create an app using FmrbApp framework

require 'fmrb-app'

class SampleApp < FmrbApp
  def initialize
    super("Sample Application")
    @frame_count = 0
    @x = 100
    @y = 100
    @color_index = 0
    @colors = [
      FmrbGfx::COLOR_RED,
      FmrbGfx::COLOR_GREEN,
      FmrbGfx::COLOR_BLUE,
      FmrbGfx::COLOR_YELLOW,
      FmrbGfx::COLOR_CYAN,
      FmrbGfx::COLOR_MAGENTA
    ]
  end

  def on_create
    puts "#{@name}: on_create called"

    # Initialize graphics
    FmrbHAL.init

    # Clear screen with black background
    FmrbGfx.clear(FmrbGfx::COLOR_BLACK)

    # Draw welcome message
    FmrbGfx.draw_string("Welcome to Family mruby OS!", 10, 10, FmrbGfx::COLOR_WHITE)
    FmrbGfx.draw_string("Press keys or move mouse to interact", 10, 30, FmrbGfx::COLOR_WHITE)

    FmrbGfx.present
  end

  def on_update(delta_time_ms)
    @frame_count += 1

    # Update every 10 frames to reduce processing
    return unless (@frame_count % 10) == 0

    # Clear previous frame
    FmrbGfx.clear(FmrbGfx::COLOR_BLACK)

    # Draw title
    FmrbGfx.draw_string("Sample App - Frame: #{@frame_count}", 10, 10, FmrbGfx::COLOR_WHITE)
    FmrbGfx.draw_string("Position: (#{@x}, #{@y})", 10, 30, FmrbGfx::COLOR_WHITE)

    # Draw animated circle
    anim_offset = (@frame_count / 10) % 100
    FmrbGfx.draw_circle(@x + anim_offset, @y, 20, @colors[@color_index])
    FmrbGfx.fill_circle(@x + anim_offset, @y, 10, FmrbGfx::COLOR_WHITE)

    # Draw rectangle
    FmrbGfx.draw_rect(50, 150, 100, 80, FmrbGfx::COLOR_CYAN)
    FmrbGfx.fill_rect(60 + anim_offset / 2, 160, 30, 30, FmrbGfx::COLOR_RED)

    # Present the frame
    FmrbGfx.present
  end

  def on_pause
    puts "#{@name}: on_pause called"
  end

  def on_resume
    puts "#{@name}: on_resume called"
  end

  def on_destroy
    puts "#{@name}: on_destroy called"
    FmrbHAL.deinit
  end

  def on_key_down(key_code)
    puts "#{@name}: Key pressed: #{key_code}"

    # Change color on key press
    @color_index = (@color_index + 1) % @colors.length

    # Move position based on arrow keys
    case key_code
    when 0x50  # Up arrow
      @y -= 10
    when 0x51  # Down arrow
      @y += 10
    when 0x4F  # Right arrow
      @x += 10
    when 0x50  # Left arrow
      @x -= 10
    end

    # Clamp position to screen bounds
    @x = [[0, @x].max, 450].min
    @y = [[0, @y].max, 290].min
  end

  def on_key_up(key_code)
    puts "#{@name}: Key released: #{key_code}"
  end

  def on_mouse_move(x, y)
    # Update position to follow mouse
    @x = x
    @y = y
  end

  def on_mouse_click(x, y, button)
    puts "#{@name}: Mouse clicked at (#{x}, #{y}), button: #{button}"

    # Change color on click
    @color_index = (@color_index + 1) % @colors.length

    # Draw a marker at click position
    FmrbGfx.fill_circle(x, y, 5, FmrbGfx::COLOR_WHITE)
    FmrbGfx.present
  end
end

# Create and start the app
# (This would normally be called by the kernel)
if __FILE__ == $0
  app = SampleApp.new
  app.start

  # Simulate frame updates
  last_time = FmrbHAL.time_ms
  loop do
    current_time = FmrbHAL.time_ms
    delta_time = current_time - last_time
    last_time = current_time

    app.update(delta_time)

    # Target 60 FPS = ~16.67ms per frame
    sleep_time = 17 - delta_time
    FmrbHAL.delay_ms(sleep_time) if sleep_time > 0
  end
end
