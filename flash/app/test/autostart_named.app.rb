# Autostart test (doc/codebase_tidy T3): two app classes in one file, and the
# .app.toml names the one to start (class = "SecondApp"). The file has no
# start code at its foot. Expected: the window shows "SecondApp" and the log
# says "AUTOSTART: SecondApp up"; FirstApp is never made.
class FirstApp < FmrbApp
  def on_create
    Log.error("AUTOSTART: FirstApp up (wrong class)")
  end
end

class SecondApp < FmrbApp
  def on_create
    Log.info("AUTOSTART: SecondApp up")
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4, "SecondApp", theme_fg)
    @gfx.present
  end
end
