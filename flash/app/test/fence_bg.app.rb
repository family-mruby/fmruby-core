#---fmrb
# default_window_mode = "background"
# task_stack_kb = 32
#---
# Comment-embedded toml test (doc/multivm_app/instruction_m1.md T1): headless,
# no .app.toml sidecar. Logs a few lines and exits, so the only evidence it ran
# with the right attributes is the log (no canvas is created for it).
class FenceBgApp < FmrbApp
  def on_create
    @n = 0
    Log.info("FENCE: background app up (no canvas)")
  end

  def on_update
    @n += 1
    Log.info("FENCE: tick #{@n}")
    stop if @n >= 3
    200
  end

  def on_destroy
    Log.info("FENCE: background app done")
  end
end

begin
  FenceBgApp.new.start
rescue => e
  Log.error("FENCE: exception #{e.class}: #{e.message}")
end
