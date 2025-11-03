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
    @gfx.clear(FmrbGfx::BLACK)
  end

  def on_update()
    if @counter % 33 == 0  # Every 1 second at 60Hz
      puts "[SystemGUI] Running... (#{@counter / 33}s)"
    end
    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::BLACK)
    @player_x += 1
    @player_y += 1
    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::RED)
    @gfx.draw_text(10, 10, "Score: #{@score}", FmrbGfx::WHITE)
    @gfx.present

    @counter += 1
    33
  end

  def on_destroy
    puts "[SystemGUI] Destroyed"
  end

end

# Create and start the system GUI app instance
puts "[SystemGUI] SystemGuiApp.new"
app = SystemGuiApp.new
app.start
