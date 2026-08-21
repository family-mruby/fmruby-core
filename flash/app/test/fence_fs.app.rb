#---fmrb
# default_window_mode = "fullscreen"
# fullscreen_switchable = true
#---
# Comment-embedded toml test (doc/multivm_app/instruction_m1.md T1): this app
# has no .app.toml sidecar, so the fence above is the only place its fullscreen
# attribute is declared. Ctrl+Q quits.
class FenceFsApp < FmrbApp
  def on_create
    Log.info("FENCE: fullscreen app up, area=#{@user_area_width}x#{@user_area_height}")
    draw_screen
  end

  def draw_screen
    clear_user_area
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height,
                   FmrbGfx::BLUE)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 8,
                   "fence_fs: fullscreen from comment toml",
                   FmrbGfx::WHITE, FmrbGfx::BLUE)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 24,
                   "area #{@user_area_width}x#{@user_area_height}",
                   FmrbGfx::WHITE, FmrbGfx::BLUE)
    @gfx.present
  end

  def on_resume
    draw_screen
  end

  def on_update
    100
  end
end

begin
  FenceFsApp.new.start
rescue => e
  Log.error("FENCE: exception #{e.class}: #{e.message}")
end
