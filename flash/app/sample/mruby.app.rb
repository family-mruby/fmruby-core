# Bouncing Ball Application
# A simple bouncing ball demo app

class BouncingBallApp < FmrbApp
  def initialize
    super()
    @counter = 0
    @bounce_count = 0

    # Ball properties
    @ball_x = @window_width / 2
    @ball_y = @window_height / 2
    @velocity_x = 3
    @velocity_y = 2
    @ball_radius = 8

    # Colors
    @bg_col = FmrbGfx::BLACK
    @ball_col = FmrbGfx::RED
  end

  def on_create()
    Log.info("on_create called")
    Log.info("user_area: x0=#{@user_area_x0}, y0=#{@user_area_y0}, width=#{@user_area_width}, height=#{@user_area_height}")
    Log.info("window: width=#{@window_width}, height=#{@window_height}")

    # Initialize ball position within user area
    @ball_x = @user_area_x0 + @user_area_width / 2
    @ball_y = @user_area_y0 + @user_area_height / 2

    draw_full_screen

    Log.info("load my_lib")
    require "/lib/my_lib"
    Log.info("load my_lib done")
    inspect_env

  end

  def draw_full_screen()
    @gfx.clear(FmrbGfx::WHITE)
    draw_window_frame
    draw_ball
    @gfx.present
  end

  def draw_ball()
    @gfx.fill_circle(@ball_x, @ball_y, @ball_radius, @ball_col)
  end

  def erase_ball()
    @gfx.fill_circle(@ball_x, @ball_y, @ball_radius, FmrbGfx::WHITE)
  end

  def on_update()
    # Erase old ball position
    erase_ball

    # Update ball position
    @ball_x += @velocity_x
    @ball_y += @velocity_y

    # Calculate user area boundaries
    left_boundary = @user_area_x0 + @ball_radius
    right_boundary = @user_area_x0 + @user_area_width - @ball_radius
    top_boundary = @user_area_y0 + @ball_radius
    bottom_boundary = @user_area_y0 + @user_area_height - @ball_radius

    # Check collision with left/right walls
    if @ball_x <= left_boundary || @ball_x >= right_boundary
      @velocity_x = -@velocity_x
      @ball_x += @velocity_x  # Move away from boundary
      @bounce_count += 1
    end

    # Check collision with top/bottom walls
    if @ball_y <= top_boundary || @ball_y >= bottom_boundary
      @velocity_y = -@velocity_y
      @ball_y += @velocity_y  # Move away from boundary
      @bounce_count += 1
    end

    # Draw new ball position
    draw_ball

    @gfx.present
    @counter += 1

    # Return sleep time in milliseconds (33ms = ~30fps)
    33
  end

  def on_event(ev)
  end

  def on_resize(new_width, new_height)
    # Called when window is resized
    # @window_width, @window_height, @user_area_* are already updated by C code
    Log.info("Resize event: #{new_width}x#{new_height}")

    # Keep ball within new boundaries
    left_boundary = @user_area_x0 + @ball_radius
    right_boundary = @user_area_x0 + @user_area_width - @ball_radius
    top_boundary = @user_area_y0 + @ball_radius
    bottom_boundary = @user_area_y0 + @user_area_height - @ball_radius

    @ball_x = left_boundary if @ball_x < left_boundary
    @ball_x = right_boundary if @ball_x > right_boundary
    @ball_y = top_boundary if @ball_y < top_boundary
    @ball_y = bottom_boundary if @ball_y > bottom_boundary

    # Trigger full redraw
    draw_full_screen
  end

  def on_destroy
    Log.info("Destroyed")
  end
end

# Create and start the app
Log.info("Creating BouncingBallApp")
begin
  app = BouncingBallApp.new
  Log.info("App created successfully")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
end
Log.info("Script ended")
