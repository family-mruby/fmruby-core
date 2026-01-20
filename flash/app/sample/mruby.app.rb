# Bouncing Balls Application
# Multiple bouncing balls demo app

class BouncingBallApp < FmrbApp
  BALL_COUNT = 8  # Number of balls to display

  def initialize
    super()
    @counter = 0
    @bounce_count = 0
    @balls = []

    # Available colors for balls
    @colors = [
      FmrbGfx::RED,
      FmrbGfx::GREEN,
      FmrbGfx::BLUE,
      FmrbGfx::YELLOW,
      FmrbGfx::CYAN,
      FmrbGfx::MAGENTA
    ]
  end

  def on_create()
    Log.info("on_create called")
    Log.info("user_area: x0=#{@user_area_x0}, y0=#{@user_area_y0}, width=#{@user_area_width}, height=#{@user_area_height}")
    Log.info("window: width=#{@window_width}, height=#{@window_height}")

    # Initialize balls with random positions and velocities
    initialize_balls

    draw_full_screen

    # Log.info("load my_lib")
    # require "/lib/my_lib"
    # Log.info("load my_lib done")
    # inspect_env

  end

  def initialize_balls()
    @balls = []

    BALL_COUNT.times do |i|
      # Random position within user area
      radius = 6 + (i % 3) * 2  # Radius: 6, 8, or 10

      # Use RNG.random_int for random position
      x = @user_area_x0 + radius + (RNG.random_int % (@user_area_width - radius * 2))
      y = @user_area_y0 + radius + (RNG.random_int % (@user_area_height - radius * 2))

      # Random velocity (avoid zero velocity)
      vx = (RNG.random_int % 7) - 3  # -3 to 3
      vx = 2 if vx == 0
      vy = (RNG.random_int % 7) - 3  # -3 to 3
      vy = 2 if vy == 0

      # Random color
      color = @colors[RNG.random_int % @colors.length]

      @balls << {
        x: x,
        y: y,
        vx: vx,
        vy: vy,
        radius: radius,
        color: color
      }
    end

    Log.info("Initialized #{@balls.length} balls")
  end

  def draw_full_screen()
    @gfx.clear(FmrbGfx::WHITE)
    draw_window_frame
    draw_balls
    @gfx.present
  end

  def draw_balls()
    @balls.each do |ball|
      @gfx.fill_circle(ball[:x], ball[:y], ball[:radius], ball[:color])
    end
  end

  def erase_balls()
    @balls.each do |ball|
      @gfx.fill_circle(ball[:x], ball[:y], ball[:radius], FmrbGfx::WHITE)
    end
  end

  def on_update()
    # Erase old ball positions
    erase_balls

    # Update each ball
    @balls.each do |ball|
      # Update position
      ball[:x] += ball[:vx]
      ball[:y] += ball[:vy]

      # Calculate boundaries for this ball
      left_boundary = @user_area_x0 + ball[:radius]
      right_boundary = @user_area_x0 + @user_area_width - ball[:radius]
      top_boundary = @user_area_y0 + ball[:radius]
      bottom_boundary = @user_area_y0 + @user_area_height - ball[:radius]

      # Check collision with left/right walls
      if ball[:x] <= left_boundary || ball[:x] >= right_boundary
        ball[:vx] = -ball[:vx]
        ball[:x] += ball[:vx]  # Move away from boundary
        @bounce_count += 1
      end

      # Check collision with top/bottom walls
      if ball[:y] <= top_boundary || ball[:y] >= bottom_boundary
        ball[:vy] = -ball[:vy]
        ball[:y] += ball[:vy]  # Move away from boundary
        @bounce_count += 1
      end
    end

    # Draw new ball positions
    draw_balls

    @gfx.present
    @counter += 1

    # Return sleep time in milliseconds (33ms = ~30fps)
    33
  end

  # def on_event(ev)
  #   # Call parent class handler (for close button, etc.)
  #   super(ev)
  # end

  def on_resize(new_width, new_height)
    # Called when window is resized
    # @window_width, @window_height, @user_area_* are already updated by C code
    Log.info("Resize event: #{new_width}x#{new_height}")

    # Keep all balls within new boundaries
    @balls.each do |ball|
      left_boundary = @user_area_x0 + ball[:radius]
      right_boundary = @user_area_x0 + @user_area_width - ball[:radius]
      top_boundary = @user_area_y0 + ball[:radius]
      bottom_boundary = @user_area_y0 + @user_area_height - ball[:radius]

      ball[:x] = left_boundary if ball[:x] < left_boundary
      ball[:x] = right_boundary if ball[:x] > right_boundary
      ball[:y] = top_boundary if ball[:y] < top_boundary
      ball[:y] = bottom_boundary if ball[:y] > bottom_boundary
    end

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
