class ShellApp < FmrbApp
  def initialize
    super()
  @count = 0
  end

  def on_create()
    @gfx.clear(FmrbGfx::GREEN)
    puts "[ShellApp] on_create called"
  end

  def on_update()
    @gfx.fill_rect( 0,  0, @window_width, 10+10, FmrbGfx::BLACK)
    @gfx.draw_text(10, 10, "Update: #{@count}", FmrbGfx::WHITE)
    @gfx.present
    @count += 1
    1000
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
