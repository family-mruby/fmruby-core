# /tmp hand-off, writer side (doc/multivm_app/instruction_m1.md T3).
# Writes a payload too large for a message (the cap is 176 bytes) into /tmp and
# publishes only the path. Start tmp_reader first, then click Write here.
class TmpWriterApp < FmrbApp
  TOPIC = "tmpfile"
  PATH = "/tmp/handoff.txt"
  BTN_X = 20
  BTN_Y = 40
  BTN_W = 100
  BTN_H = 24

  def on_create
    @count = 0
    @bytes = 0
    draw_screen
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "sent #{@count} (#{@bytes}B)", FmrbGfx::WHITE)
    @gfx.fill_rect(BTN_X, BTN_Y, BTN_W, BTN_H, FmrbGfx::BLUE)
    @gfx.draw_rect(BTN_X, BTN_Y, BTN_W, BTN_H, FmrbGfx::WHITE)
    @gfx.draw_text(BTN_X + 22, BTN_Y + 8, "Write", FmrbGfx::WHITE, FmrbGfx::BLUE)
    draw_window_frame
    @gfx.present
  end

  def write_and_notify
    @count += 1
    body = "payload ##{@count} from the writer\n" + ("data line\n" * 40)
    File.open(PATH, "w") { |f| f.write(body) }
    @bytes = File.size(PATH)
    publish(TOPIC, { "path" => PATH, "n" => @count })
    Log.info("TMPW: wrote #{@bytes} bytes to #{PATH}, published path")
  rescue => e
    Log.error("TMPW: #{e.class}: #{e.message}")
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_up && ev[:button] == 1
      if ev[:x] >= BTN_X && ev[:x] < BTN_X + BTN_W &&
         ev[:y] >= BTN_Y && ev[:y] < BTN_Y + BTN_H
        write_and_notify
        draw_screen
      end
    end
  end

  def on_update
    100
  end
end

begin
  TmpWriterApp.new.start
rescue => e
  Log.error("TMPW: exception #{e.class}: #{e.message}")
end
