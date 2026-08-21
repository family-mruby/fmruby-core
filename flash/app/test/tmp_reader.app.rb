# /tmp hand-off, reader side (doc/multivm_app/instruction_m1.md T3).
# Subscribes to the topic the writer publishes on, reads the file it names out
# of /tmp, and shows what came across. The message carries a path; the payload
# itself never goes through the 176-byte message channel.
class TmpReaderApp < FmrbApp
  TOPIC = "tmpfile"

  def on_create
    @count = 0
    @info = "waiting"
    subscribe(TOPIC)
    draw_screen
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "got #{@count}", theme_fg)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 18, @info, theme_accent)
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
      first = body.split("\n")[0]
      @info = "#{body.bytesize}B"
      Log.info("TMPR: read #{body.bytesize} bytes from #{path}: #{first}")
    rescue => e
      @info = "read failed"
      Log.error("TMPR: #{e.class}: #{e.message}")
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
  TmpReaderApp.new.start
rescue => e
  Log.error("TMPR: exception #{e.class}: #{e.message}")
end
