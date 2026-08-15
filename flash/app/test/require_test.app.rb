# require test from FmrbApp context

Log.info("require_test: before require")
require "my_lib"
Log.info("require_test: after require")

class RequireTestApp < FmrbApp
  def on_create
    Log.info("require_test: on_create")
    inspect_env
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "require OK", FmrbGfx::GREEN)
    draw_window_frame
    @gfx.present
  end

  def on_event(ev)
    super(ev)
  end

  def on_update
    500
  end
end

begin
  app = RequireTestApp.new
  app.start
rescue => e
  Log.error("require_test: #{e.class}: #{e.message}")
end
