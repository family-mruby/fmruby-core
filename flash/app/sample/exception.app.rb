# Exception Test - Triggers an uncaught runtime exception after 3 seconds

class ExceptionApp < FmrbApp
  def on_create
    @tick = 0
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4, "Exception in 3s...", FmrbGfx::YELLOW)
    draw_window_frame
    @gfx.present
  end

  def on_update
    @tick += 1
    if @tick >= 30
      # This will raise an uncaught exception
      nil.no_such_method
    end
    100
  end
end

app = ExceptionApp.new
app.start
