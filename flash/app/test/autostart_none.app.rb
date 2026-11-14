#---fmrb
# app_screen_name = "AutostartNone"
#---
# Autostart test (doc/codebase_tidy T3): the file defines a class, but not
# under FmrbApp. Expected: an error dialog and a log line saying
# "No app class in /app/test/autostart_none.app.rb", not a silent nothing.
class NoneApp
  def on_create
    Log.error("AUTOSTART: NoneApp was started")
  end
end
