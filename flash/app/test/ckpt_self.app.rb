# A windowless app that asks for its own checkpoint image (doc/checkpoint,
# F1b: FmrbApp#request_checkpoint). Like ckpt_counter, two Tasks and the main
# loop count and log every step; at the fourth step of the main loop the app
# asks, from on_update, to be written and started again. The checkpoint check
# (tools/regress/checkpoint.rb) reads from the log that every counter goes on
# from the step after the last one written, and that the app asked only once
# (the restored app has @asked set).

class CkptSelfApp < FmrbApp
  ASK_AT = 4

  def on_create
    @t0 = Machine.board_millis
    @main = 0
    @asked = false
    Log.info("ckpt_self: start")
    t0 = @t0
    Task.new(name: "count_a") do
      a = 0
      while true
        a += 1
        Log.info("ckpt_self: a=#{a} t=#{Machine.board_millis - t0}")
        sleep_ms(200)
      end
    end
    Task.new(name: "count_b") do
      b = 0
      while true
        b += 1
        Log.info("ckpt_self: b=#{b} t=#{Machine.board_millis - t0}")
        sleep_ms(300)
      end
    end
  end

  def on_update
    @main += 1
    Log.info("ckpt_self: main=#{@main}")
    if @main == ASK_AT && !@asked
      @asked = true
      why = request_checkpoint(true)
      Log.info("ckpt_self: asked for its own image (#{why ? why : 'ok'})")
    end
    250
  end

  def on_checkpoint
    Log.info("ckpt_self: on_checkpoint main=#{@main} t=#{Machine.board_millis - @t0}")
  end

  def on_restore
    Log.info("ckpt_self: on_restore main=#{@main} t=#{Machine.board_millis - @t0}")
  end

  def on_checkpoint_refused(reason)
    Log.info("ckpt_self: refused: #{reason}")
  end
end

begin
  app = CkptSelfApp.new
  app.start
rescue => e
  Log.error("ckpt_self: #{e.class}: #{e.message}")
end
