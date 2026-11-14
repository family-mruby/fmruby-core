#---fmrb
# app_screen_name = "AutostartBase"
# default_window_width = 160
# default_window_height = 60
#---
# Autostart test (doc/codebase_tidy T3): a shared base and the app derived
# from it. The base is not counted, so the one app class is started without
# class =. Expected: the window shows "GameApp" and the log says
# "AUTOSTART: GameApp up".
class GameBase < FmrbApp
  def label
    "base"
  end

  def on_create
    Log.info("AUTOSTART: #{self.class} up")
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4, label, theme_fg)
    @gfx.present
  end
end

class GameApp < GameBase
  def label
    "GameApp"
  end
end
