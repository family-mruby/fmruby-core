# System GUI Application
# This is the default system GUI that provides basic UI elements

class SystemGuiApp < FmrbApp
  def initialize
    super()
    @counter = 0

    @score = 0
    @player_x = 100
    @player_y = 100
  end

  def on_create()
    @gfx.clear(FmrbGfx::BLUE)  # Phase2: Canvas + messaging
    puts "[SystemGUI] on_create called (graphics disabled temporarily)"
  end

  def on_update()
    if @counter % 30 == 0  
      puts "[SystemGUI] Running... (#{@counter / 30}s)"
    end
    # Graphics commands disabled temporarily (Phase2: Canvas + messaging)
    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::BLUE)
    @player_x += 1
    @player_y += 1
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
  puts "[ERROR] Exception caught: #{e.class}"
  puts "[ERROR] Message: #{e.message}"
  puts "[ERROR] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end
puts "[SystemGUI] Script ended"
