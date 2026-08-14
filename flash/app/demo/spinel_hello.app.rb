# Minimal demo for the SpinelHello sample gem. It shows the string that
# Spinel-compiled Ruby produced, reached through the gem from this mruby app --
# proof that "Spinel as a gem" works end to end
# (doc/spinel_aot/adding_a_spinel_gem.md).
class SpinelHelloApp < FmrbApp
  def initialize
    super()
    @text = nil
  end

  def on_create
    hello = Fmrb::SpinelHello.new
    @text = hello.greet
    hello.close
    draw_screen
  end

  def on_update
    500   # nothing changes; idle
  end

  def draw_screen
    clear_user_area(FmrbGfx::BLACK)
    @gfx.draw_text(@user_area_x0 + 6, @user_area_y0 + 10, @text.to_s, FmrbGfx::GREEN)
    @gfx.draw_text(@user_area_x0 + 6, @user_area_y0 + 26,
                   "built by Spinel", FmrbGfx::GRAY)
    draw_window_frame
    @gfx.present
  end
end

begin
  app = SpinelHelloApp.new
  app.start
rescue => e
  Log.error("SpinelHello: #{e.class}: #{e.message}")
end
