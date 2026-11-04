# A windowless app that can be moved and calls a method only some builds have
# (doc/checkpoint F4, doc/checkpoint/movable_apps.md). FmrbApp.linux_settings
# is the native program's (the Linux settings window); the simulation has no
# such method. The app guards the call with respond_to? and never makes it
# (its flag is never set): the call is in its code all the same, so an image
# names it (SND0) and a build without it says so when the image arrives, and
# lets it go on. ckpt_requires.app.rb is the same with the name declared as
# required in its .app.toml: there the build refuses it.

class CkptCallsApp < FmrbApp
  def on_create
    @main = 0
    @open_settings = false
    Log.info("ckpt_calls: start")
  end

  def on_update
    @main += 1
    have = FmrbApp.respond_to?(:linux_settings)
    FmrbApp.linux_settings if @open_settings && have
    Log.info("ckpt_calls: main=#{@main} pack=#{[65].pack('C')} linux_settings=#{have}")
    250
  end

  def on_restore
    Log.info("ckpt_calls: on_restore main=#{@main}")
  end
end

begin
  app = CkptCallsApp.new
  app.start
rescue => e
  Log.error("ckpt_calls: #{e.class}: #{e.message}")
end
