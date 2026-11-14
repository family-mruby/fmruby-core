#---fmrb
# app_screen_name = "AutostartBadName"
# class = "MissingApp"
#---
# Autostart test (doc/codebase_tidy T3): class = (in the #---fmrb header)
# names a class the file does not define. Expected: an error dialog saying
# class = "MissingApp" is not an app class here and listing the two there are.
class UpApp < FmrbApp
  def on_create
    Log.error("AUTOSTART: UpApp was started")
  end
end

class DownApp < FmrbApp
  def on_create
    Log.error("AUTOSTART: DownApp was started")
  end
end
