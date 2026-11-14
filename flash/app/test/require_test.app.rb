# require test from FmrbApp context

Log.info("require_test: before require")
require "my_lib"
Log.info("require_test: after require")

class RequireTestApp < FmrbApp
  def on_create
    Log.info("require_test: on_create")
    inspect_env
    clear_user_area
    @gfx.draw_text(4, 4,
                   "require OK", theme_fg)
    draw_window_frame
    @gfx.present
  end

  def on_update
    500
  end
end
