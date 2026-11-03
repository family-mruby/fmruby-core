# A windowless app that can be moved (doc/checkpoint, F1): two Tasks and the
# main loop count, each at its own pace, and log every step. The checkpoint
# check (tools/regress/checkpoint.rb) writes it to an image while it counts,
# starts the image again, and reads from the log that every counter goes on
# from the step after the last one written, and that Machine.board_millis
# goes on from where it was (the "t=" values) even when the image waited.

class CkptCounterApp < FmrbApp
  def on_create
    @t0 = Machine.board_millis
    @main = 0
    Log.info("ckpt_counter: start")
    # A Task's block runs with the top-level self, not the app: what it needs
    # from here is in locals.
    t0 = @t0
    Task.new(name: "count_a") do
      a = 0
      while true
        a += 1
        Log.info("ckpt_counter: a=#{a} t=#{Machine.board_millis - t0}")
        sleep_ms(200)
      end
    end
    Task.new(name: "count_b") do
      b = 0
      while true
        b += 1
        Log.info("ckpt_counter: b=#{b} t=#{Machine.board_millis - t0}")
        sleep_ms(300)
      end
    end
  end

  def on_update
    @main += 1
    Log.info("ckpt_counter: main=#{@main}")
    250
  end

  def on_checkpoint
    Log.info("ckpt_counter: on_checkpoint main=#{@main} t=#{Machine.board_millis - @t0}")
  end

  def on_restore
    Log.info("ckpt_counter: on_restore main=#{@main} t=#{Machine.board_millis - @t0}")
  end
end

begin
  app = CkptCounterApp.new
  app.start
rescue => e
  Log.error("ckpt_counter: #{e.class}: #{e.message}")
end
