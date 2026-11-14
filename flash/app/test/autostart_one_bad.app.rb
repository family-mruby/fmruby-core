#---fmrb
# app_screen_name = "AutostartOneBad"
# class = "OtherApp"
#---
# Autostart test (doc/codebase_tidy T3): one app class, but class = names
# another. class = always decides when it is there, so this is not started.
# Expected: an error dialog saying class = "OtherApp" is not an app class
# here (App classes: OnlyApp).
class OnlyApp < FmrbApp
  def on_create
    Log.error("AUTOSTART: OnlyApp was started")
  end
end
