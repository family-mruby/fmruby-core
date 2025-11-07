class ShellApp < FmrbApp
  def initialize
    super()
  end

  def on_create()
    @gfx.clear(FmrbGfx::BLACK)
    puts "[ShellApp] on_create called"
  end

  def draw_current()
    # @gfx.fill_circle(@player_x, @player_y, 10, FmrbGfx::RED)
    # @gfx.fill_rect( 0,  0, 480, 10+10, FmrbGfx::BLACK)
    # @gfx.draw_text(10, 10, "Score: #{@score}", FmrbGfx::WHITE)
    # @gfx.present
  end

  def on_update()
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
