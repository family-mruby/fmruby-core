# PubSub Demo - Publisher
# Click the button to publish a message to "demo" topic

class PubDemoApp < FmrbApp
  TOPIC = "demo"
  BTN_X = 30
  BTN_Y = 30
  BTN_W = 100
  BTN_H = 24

  def on_create
    @count = 0
    draw_screen
  end

  def draw_screen
    clear_user_area
    # Info
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "Sent: #{@count}", FmrbGfx::WHITE)
    # Button
    @gfx.fill_rect(BTN_X, BTN_Y, BTN_W, BTN_H, FmrbGfx::BLUE)
    @gfx.draw_rect(BTN_X, BTN_Y, BTN_W, BTN_H, FmrbGfx::WHITE)
    label = "Publish"
    lx = BTN_X + (BTN_W - label.length * 6) / 2
    ly = BTN_Y + (BTN_H - 8) / 2
    @gfx.draw_text(lx, ly, label, FmrbGfx::WHITE, FmrbGfx::BLUE)

    draw_window_frame
    @gfx.present
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_up && ev[:button] == 1
      if ev[:x] >= BTN_X && ev[:x] < BTN_X + BTN_W &&
         ev[:y] >= BTN_Y && ev[:y] < BTN_Y + BTN_H
        @count += 1
        publish(TOPIC, {"msg" => "hello", "n" => @count})
        draw_screen
      end
    end
  end

  def on_update
    100
  end
end

app = PubDemoApp.new
app.start
