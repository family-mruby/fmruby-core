#---fmrb
# default_window_mode = "window"
# default_window_width = 200
# default_window_height = 140
#---
# A windowed app. Save this somewhere under /app or /home and run it with F5
# (or `run <path>` from the shell). The fenced comment above is the whole
# configuration -- no .app.toml sidecar needed.
class MyApp < FmrbApp
  def on_create
    @clicks = 0
    draw_screen
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "clicks: #{@clicks}", FmrbGfx::WHITE)
    @gfx.fill_rect(@user_area_x0 + 4, @user_area_y0 + 20, 60, 20, FmrbGfx::BLUE)
    @gfx.draw_text(@user_area_x0 + 12, @user_area_y0 + 26,
                   "click", FmrbGfx::WHITE, FmrbGfx::BLUE)
    # Redraw the frame every time: a resize repaints over it otherwise.
    draw_window_frame
    @gfx.present
  end

  # Mouse and keyboard arrive here. Call super so the window frame (drag,
  # close box, resize) keeps working.
  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_up && ev[:button] == 1
      @clicks += 1
      draw_screen
    elsif ev[:type] == :key_down
      Log.info("key scancode=#{ev[:scancode]}")
    end
  end

  # Called repeatedly. Return the milliseconds to wait before the next call.
  def on_update
    100
  end

  def on_destroy
    Log.info("MyApp: bye")
  end
end

# An app file that stops here loads and does nothing: the class has to be
# instantiated and started.
begin
  MyApp.new.start
rescue => e
  Log.error("MyApp: #{e.class}: #{e.message}")
end
