#---fmrb
# default_window_mode = "window"
# default_window_width = 200
# default_window_height = 140
# resizable = 1
#---
class MyApp < FmrbApp
  def on_create
    @clicks = 0
    draw_screen
  end

  def draw_screen
    clear_user_area   # the theme page colour; theme_fg is its ink
    @gfx.draw_text(4, 4, "clicks: #{@clicks}", theme_fg)
    @gfx.draw_text(12, 26, "Hello", FmrbGfx::WHITE, FmrbGfx::BLUE)
    @gfx.present
  end

  # Mouse and keyboard arrive here. The window frame (drag, close box,
  # resize) is handled by the base class before this is called.
  def on_event(ev)
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
