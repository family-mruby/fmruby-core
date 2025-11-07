# System GUI Application
# This is the default system GUI that provides basic UI elements

class SystemGuiApp < FmrbApp
  def initialize
    super()
    @counter = 0

    @score = 0
    @player_x = 200
    @player_y = 160
  end

  def on_create()
    @gfx.clear(FmrbGfx::BLUE)  # Phase2: Canvas + messaging
    draw_current
    puts "[SystemGUI] on_create called"
    #puts "[SystemGUI] spawn default shell app"

    # Spawn shell application via Kernel
    #spawn_app("shell")
  end

  def spawn_app(app_name)
    puts "[SystemGUI] Requesting spawn: #{app_name}"

    # Build message payload: subtype + app_name(32)
    # Convert APP_CTRL_SPAWN constant to byte string (chr is available in mruby)
    data = FmrbApp::APP_CTRL_SPAWN.chr
    data += app_name.ljust(32, "\x00")  # app_name padded to 32 bytes

    # Send to Kernel using constants
    success = send_message(FmrbApp::PROC_ID_KERNEL, FmrbApp::MSG_TYPE_APP_CONTROL, data)

    if success
      puts "[SystemGUI] Spawn request sent successfully"
    else
      puts "[SystemGUI] Failed to send spawn request"
    end
  end

  def draw_current()
    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::RED)
    @gfx.fill_rect( 0,  0, 480, 10+10, FmrbGfx::BLACK)
    @gfx.draw_text(10, 10, "Score: #{@score}", FmrbGfx::WHITE)
    @gfx.present
  end

  def on_update()
    if @counter % 30 == 0  
      puts "[SystemGUI] Running... (#{@counter / 30}s)"
    end
    # Graphics commands disabled temporarily (Phase2: Canvas + messaging)
    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::BLUE)
    @player_x += (RNG.random_int % 7) - 3  # -3 to +3 random movement
    @player_y += (RNG.random_int % 7) - 3  # -3 to +3 random movement
    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::RED)
    @gfx.fill_rect( 0,  0, 480, 10+10, FmrbGfx::BLACK)
    @gfx.draw_text(10, 10, "Score: #{@score}", FmrbGfx::WHITE)
    @gfx.present

    @score += 1
    @counter += 1
    33
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
