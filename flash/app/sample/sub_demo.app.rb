# PubSub Demo - Subscriber
# Subscribes to "demo" topic and displays received messages

class SubDemoApp < FmrbApp
  TOPIC = "demo"

  def on_create
    @count = 0
    @last_msg = ""
    subscribe(TOPIC)
    draw_screen
  end

  def draw_screen
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "Waiting...", FmrbGfx::GRAY) if @count == 0
    if @count > 0
      @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                     "Received: #{@count}", FmrbGfx::GREEN)
      @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 20,
                     @last_msg, FmrbGfx::YELLOW)
    end
    draw_window_frame
    @gfx.present
  end

  def on_control(msg)
    if msg["cmd"] == "topic_data" && msg["topic"] == TOPIC
      @count += 1
      data = msg["data"]
      if data
        @last_msg = "n=#{data["n"]} #{data["msg"]}"
      end
      draw_screen
    end
  end

  def on_event(ev)
    super(ev)
  end

  def on_update
    100
  end

  def on_destroy
    unsubscribe(TOPIC)
  end
end

app = SubDemoApp.new
app.start
