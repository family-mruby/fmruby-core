# Asks for the largest task stack an app may have (64 KB, the
# FMRB_USER_APP_TASK_STACK_MAX cap). Its use is to prove the spawn-time memory
# gate: with a few apps already running there is plenty of free internal RAM
# left in total, but not one contiguous 72 KB, and this is what shows that the
# refusal happens for the right reason.

class BigStackApp < FmrbApp
  def on_create
    clear_user_area
    @gfx.draw_text(8, 8, "Big stack app running", FmrbGfx::WHITE)
    @gfx.present
    Log.info("BigStack: started")
  end

  def on_update
    500
  end
end

begin
  app = BigStackApp.new
  app.start
rescue => e
  Log.error("BigStack: #{e}")
end
