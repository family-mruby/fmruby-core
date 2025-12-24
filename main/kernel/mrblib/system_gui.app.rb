# System GUI Application
# This is the default system GUI that provides basic UI elements

class SystemGuiApp < FmrbApp
  def initialize
    super()
    @counter = 0

    @score = 0
    @player_x = 300
    @player_y = 60
    @velocity_x = 5
    @velocity_y = 3
    @ball_radius = 10
    @bg_col = 0xF6

    @st = 0
  end

  def on_create()
    puts "[SystemGUI] on_create called"
    draw_current
  end

  def spawn_app(app_name)
    puts "[SystemGUI] Requesting spawn: #{app_name}"

    # Build message payload: subtype + app_name(FMRB_MAX_PATH_LEN)
    # Convert APP_CTRL_SPAWN constant to byte string (chr is available in mruby)
    data = FmrbApp::APP_CTRL_SPAWN.chr
    data += app_name.ljust(FmrbApp::MAX_PATH_LEN, "\x00")

    # Send to Kernel using constants
    success = send_message(FmrbApp::PROC_ID_KERNEL, FmrbApp::MSG_TYPE_APP_CONTROL, data)

    if success
      puts "[SystemGUI] Spawn request sent successfully"
    else
      puts "[SystemGUI] Failed to send spawn request"
    end
  end

  def draw_current()
    @gfx.clear(@bg_col)
    draw_system_frame
    @gfx.present
  end

  def draw_system_frame()
    @gfx.fill_rect( 0,  0, @window_width, 10, 0xC5)
    @gfx.draw_text( 2,  1, "Family mruby", FmrbGfx::WHITE)
    @gfx.draw_text(@window_width - 80,  1, "Count: #{@score}", FmrbGfx::WHITE)
  end

  def on_update()
    if @counter % 30 == 0
      puts "[SystemGUI] on_update() tick #{@counter / 30}s"
    end

    # Erase old position
    @gfx.fill_circle(@player_x, @player_y, @ball_radius, @bg_col)

    # Update position
    @player_x += @velocity_x
    @player_y += @velocity_y

    # Check collision with window boundaries and reflect
    if @player_x - @ball_radius <= 12 || @player_x + @ball_radius >= @window_width
      @velocity_x = -@velocity_x
      @player_x += @velocity_x  # Move away from boundary
    end

    if @player_y - @ball_radius <= 12 || @player_y + @ball_radius >= @window_height
      @velocity_y = -@velocity_y
      @player_y += @velocity_y  # Move away from boundary
    end

    # Draw new position
    @gfx.fill_circle(@player_x, @player_y, @ball_radius, FmrbGfx::RED)


    @score += 1
    @counter += 1

    draw_system_frame
    @gfx.present

    33
  end

  def on_event(ev)
    puts "on_event: gui app"
    p ev

    # Handle mouse up event
    if ev[:type] == :mouse_up
      puts "[SystemGUI] Mouse button #{ev[:button]} released at (#{ev[:x]}, #{ev[:y]})"
      # Add your mouse up handling logic here
      if @st == 0
        puts "[SystemGUI] spawn default shell app"
        spawn_app("default/shell")
      end

      if @st == 1
        puts "[SystemGUI] spawn sample Lua app"
        spawn_app("/app/sample_app/sample_lua_app.app.lua")
      end

      @st += 1
    end
  end

  def on_destroy
    puts "[SystemGUI] Destroyed"
  end

end

# Create and start the system GUI app instance
puts "[SystemGUI] SystemGuiApp.new"
begin
  app = SystemGuiApp.new
  puts "[SystemGUI] SystemGuiApp created successfully"
  app.start
rescue => e
  puts "[SystemGUI] Exception caught: #{e.class}"
  puts "[SystemGUI] Message: #{e.message}"
  puts "[SystemGUI] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end
puts "[SystemGUI] Script ended"
