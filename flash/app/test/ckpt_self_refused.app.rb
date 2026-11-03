# The same request as ckpt_self (FmrbApp#request_checkpoint, doc/checkpoint
# F1b) from an app that does not declare movable: the image is refused, the
# app is told why (on_checkpoint_refused) and goes on counting.

class CkptSelfRefusedApp < FmrbApp
  def on_create
    @main = 0
    Log.info("ckpt_self_refused: start")
  end

  def on_update
    @main += 1
    Log.info("ckpt_self_refused: main=#{@main}")
    if @main == 2
      why = request_checkpoint(true)
      Log.info("ckpt_self_refused: asked for its own image (#{why ? why : 'ok'})")
    end
    250
  end

  def on_checkpoint_refused(reason)
    Log.info("ckpt_self_refused: refused: #{reason}")
  end
end

begin
  app = CkptSelfRefusedApp.new
  app.start
rescue => e
  Log.error("ckpt_self_refused: #{e.class}: #{e.message}")
end
