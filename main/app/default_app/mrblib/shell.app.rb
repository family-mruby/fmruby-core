class ShellApp < FmrbApp
  def initialize
    super()
    @count = 0
    @player_x = 100
    @player_y = 100
    @velocity_x = 3
    @velocity_y = 2
    @ball_radius = 10
  end

  def on_create()
    @gfx.clear(FmrbGfx::GREEN)
    puts "[ShellApp] on_create called"
  end

  def on_update()
    @gfx.fill_rect( 0,  0, @window_width, 10+10, FmrbGfx::BLACK)
    @gfx.draw_text(10, 10, "Update: #{@count}", FmrbGfx::WHITE)

    # Erase old position
    @gfx.fill_circle(@player_x, @player_y, @ball_radius, FmrbGfx::GREEN)

    # Update position
    @player_x += @velocity_x
    @player_y += @velocity_y

    # Check collision with window boundaries and reflect
    if @player_x - @ball_radius <= 0 || @player_x + @ball_radius >= @window_width
      @velocity_x = -@velocity_x
      @player_x += @velocity_x  # Move away from boundary
    end

    if @player_y - @ball_radius <= 0 || @player_y + @ball_radius >= @window_height
      @velocity_y = -@velocity_y
      @player_y += @velocity_y  # Move away from boundary
    end

    # Draw new position
    @gfx.fill_circle(@player_x, @player_y, @ball_radius, FmrbGfx::RED)

    @gfx.present
    @count += 1
    #1000
    33
  end

  def on_destroy
    puts "[ShellApp] Destroyed"
  end

end

# Create and start the system GUI app instance
puts "[ShellApp] ShellApp.new"
begin
  app = ShellApp.new
  puts "[ShellApp] ShellApp created successfully"
  app.start
rescue => e
  puts "[ShellApp] Exception caught: #{e.class}"
  puts "[ShellApp] Message: #{e.message}"
  puts "[ShellApp] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end
puts "[ShellApp] Script ended"
