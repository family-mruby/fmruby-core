# The same as ckpt_calls.app.rb, with the method its .app.toml declares as
# required (requires = ["linux_settings"]): a build that has no method of
# that name refuses the image, and the app goes on where it was
# (doc/checkpoint F4, doc/checkpoint/movable_apps.md).

class CkptRequiresApp < FmrbApp
  def on_create
    @main = 0
    @open_settings = false
    Log.info("ckpt_requires: start")
  end

  def on_update
    @main += 1
    have = FmrbApp.respond_to?(:linux_settings)
    FmrbApp.linux_settings if @open_settings && have
    Log.info("ckpt_requires: main=#{@main} pack=#{[65].pack('C')} linux_settings=#{have}")
    250
  end

  def on_restore
    Log.info("ckpt_requires: on_restore main=#{@main}")
  end
end

begin
  app = CkptRequiresApp.new
  app.start
rescue => e
  Log.error("ckpt_requires: #{e.class}: #{e.message}")
end
