# Dies on purpose two seconds after it starts, for testing what happens to an
# app that fails: the single-app boot (system_conf boot_mode = "app") starts
# it again startup_app_retries times and then gives up with a message on the
# screen. Shows a red screen and a countdown until then.

class CrashSoonApp < FmrbApp
  def on_create
    @t0 = Machine.board_millis
    Log.info("crash_soon: started, failing in 2 s")
    @gfx.fill_rect(0, 0, @window_width, @window_height, FmrbGfx::COLOR_RED)
    @gfx.draw_text(4, 4, "crash_soon", FmrbGfx::COLOR_WHITE)
    @gfx.present
  end

  def on_update
    left = 2000 - (Machine.board_millis - @t0)
    raise "crash_soon: failing on purpose" if left <= 0
    @gfx.fill_rect(4, 16, 60, 10, FmrbGfx::COLOR_RED)
    @gfx.draw_text(4, 16, "#{left} ms", FmrbGfx::COLOR_WHITE)
    @gfx.present
    200
  end
end

# No rescue here, unlike an ordinary app: the exception is meant to reach the
# runtime, which reports it to the kernel as this app's error.
app = CrashSoonApp.new
app.start
