# Listener for the worker template (doc/multivm_app/instruction_m1.md T2).
# Subscribes to the topic the worker skeleton publishes on, reads the result
# file it names out of /tmp, and shows what arrived. Start this first, then run
# a worker.
class WorkerWatchApp < FmrbApp
  TOPIC = "worker"

  def on_create
    @count = 0
    @line = "waiting"
    subscribe(TOPIC)
    draw_screen
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "results: #{@count}", FmrbGfx::GREEN)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 18, @line, FmrbGfx::YELLOW)
    draw_window_frame
    @gfx.present
  end

  def on_control(msg)
    return unless msg["cmd"] == "topic_data" && msg["topic"] == TOPIC
    data = msg["data"]
    return unless data
    path = data["path"]
    begin
      body = File.open(path, "r") { |f| f.read }
      @count += 1
      @line = body.split("\n")[0].to_s
      Log.info("WATCH: #{path} -> #{@line}")
    rescue => e
      @line = "read failed"
      Log.error("WATCH: #{e.class}: #{e.message}")
    end
    draw_screen
  end

  def on_update
    100
  end

  def on_destroy
    unsubscribe(TOPIC)
  end
end

begin
  WorkerWatchApp.new.start
rescue => e
  Log.error("WATCH: exception #{e.class}: #{e.message}")
end
