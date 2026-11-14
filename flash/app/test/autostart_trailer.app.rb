#---fmrb
# app_screen_name = "AutostartTrailer"
# default_window_width = 160
# default_window_height = 60
#---
# Autostart test (doc/codebase_tidy T3): a file that still starts its app at
# its foot, the way every app file used to. The runtime must not start a
# second one once this one ends. The app ends itself after 2 s by leaving
# its loop without FmrbApp#stop, so nothing but the instance flag keeps the
# runtime from starting another. Expected: "AUTOSTART: trailer instance 1"
# and "AUTOSTART: trailer instance 1 ends" in the log, and no instance 2.
class TrailerApp < FmrbApp
  def on_create
    $autostart_trailer_count = ($autostart_trailer_count || 0) + 1
    Log.info("AUTOSTART: trailer instance #{$autostart_trailer_count}")
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "instance #{$autostart_trailer_count}", theme_fg)
    @gfx.present
    @ticks = 0
  end

  def on_update
    @ticks += 1
    if @ticks > 20
      Log.info("AUTOSTART: trailer instance #{$autostart_trailer_count} ends")
      @_running = false
    end
    100
  end
end

begin
  TrailerApp.new.start
rescue => e
  Log.error("TrailerApp: #{e.class}: #{e.message}")
end
