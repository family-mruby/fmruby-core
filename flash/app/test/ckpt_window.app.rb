# A movable app with a window (doc/checkpoint): F1 carries only windowless
# apps, so its image is refused at the safe point (the window's FmrbGfx is C
# data no gem carries yet, F3) and the app goes on counting. The checkpoint
# check (tools/regress/checkpoint.rb) reads the refusal and the next count.

class CkptWindowApp < FmrbApp
  def on_create
    @n = 0
  end

  def on_update
    @n += 1
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4, "n=#{@n}", theme_fg)
    @gfx.present
    Log.info("ckpt_window: n=#{@n}")
    250
  end
end

begin
  app = CkptWindowApp.new
  app.start
rescue => e
  Log.error("ckpt_window: #{e.class}: #{e.message}")
end
