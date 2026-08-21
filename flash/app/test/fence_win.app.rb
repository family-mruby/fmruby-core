#---fmrb
# default_window_mode = "fullscreen"
#---
# Precedence test (doc/multivm_app/instruction_m1.md T1): the fence above asks
# for fullscreen, the .app.toml sidecar next to it asks for a 180x90 window.
# The sidecar must win -- if this comes up fullscreen, the fallback order is
# wrong.
class FenceWinApp < FmrbApp
  def on_create
    Log.info("FENCE: sidecar app up, area=#{@user_area_width}x#{@user_area_height}")
    draw_screen
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "sidecar wins", theme_fg)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 20,
                   "#{@user_area_width}x#{@user_area_height}", theme_accent)
    draw_window_frame
    @gfx.present
  end

  def on_update
    100
  end
end

begin
  FenceWinApp.new.start
rescue => e
  Log.error("FENCE: exception #{e.class}: #{e.message}")
end
