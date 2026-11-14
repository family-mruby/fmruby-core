#---fmrb
# app_screen_name = "AutostartTwo"
#---
# Autostart test (doc/codebase_tidy T3): two app classes and no class =.
# Expected: an error dialog naming both and asking for class = in the
# .app.toml; neither is started.
class LeftApp < FmrbApp
  def on_create
    Log.error("AUTOSTART: LeftApp was started")
  end
end

class RightApp < FmrbApp
  def on_create
    Log.error("AUTOSTART: RightApp was started")
  end
end
