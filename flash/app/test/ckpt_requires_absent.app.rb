# A movable counter that declares in its .app.toml a method no build has
# (requires = ["fmrb_no_such_method"]): every destination refuses it before
# it is stopped, and it goes on where it is (doc/checkpoint F5, the
# destination is asked first; doc/checkpoint/movable_apps.md 5.3).

class CkptRequiresAbsentApp < FmrbApp
  def on_create
    @main = 0
    Log.info("ckpt_requires_absent: start")
  end

  def on_update
    @main += 1
    Log.info("ckpt_requires_absent: main=#{@main}")
    250
  end

  def on_restore
    Log.info("ckpt_requires_absent: on_restore main=#{@main}")
  end
end

begin
  app = CkptRequiresAbsentApp.new
  app.start
rescue => e
  Log.error("ckpt_requires_absent: #{e.class}: #{e.message}")
end
