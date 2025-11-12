class ShellApp < FmrbApp
  def initialize
    super()
    @count = 0
    @player_x = 100
    @player_y = 100
  end

  def on_create()
    @gfx.clear(FmrbGfx::GREEN)
    puts "[ShellApp] on_create called"
  end

  def on_update()
    @gfx.fill_rect( 0,  0, @window_width, 10+10, FmrbGfx::BLACK)
    @gfx.draw_text(10, 10, "Update: #{@count}", FmrbGfx::WHITE)

    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::GREEN)
    #@player_x += (RNG.random_int % 7) - 3
    #@player_y += (RNG.random_int % 7) - 3
    @player_x += 3
    @player_x = 100 if @player_x > 200
    @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::RED)

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
