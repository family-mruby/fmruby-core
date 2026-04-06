# System Overlay Application
# Topmost layer (z=254): dialogs, launcher, file selector
# Stays idle until activated by messages from other apps.

class SystemOverlayApp < FmrbApp
  def initialize
    super()
    @active = false
  end

  def on_create
    Log.info("SystemOverlay created")
  end

  def on_update
    unless @active
      return 100
    end
    33
  end

  def on_event(ev)
    super(ev)
  end

  def on_destroy
    Log.info("SystemOverlay destroyed")
  end
end

Log.info("SystemOverlayApp.new")
begin
  app = SystemOverlayApp.new
  Log.info("SystemOverlayApp created successfully")
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
