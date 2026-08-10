#---fmrb
# default_window_mode = "fullscreen"
# fullscreen_switchable = true
#---
# A fullscreen app with a game loop. Fullscreen suspends the other apps, so
# the whole machine is yours while this runs. fullscreen_switchable lets
# Ctrl+Tab park it and come back; Ctrl+Q quits.
class MyApp < FmrbApp
  FRAME_MS = 33          # about 30 frames a second
  SPEED = 3

  def on_create
    @x = @user_area_x0 + 20
    @y = @user_area_y0 + 20
    @dx = SPEED
    @dy = SPEED
    draw_screen
  end

  def step
    @x += @dx
    @y += @dy
    right = @user_area_x0 + @user_area_width - 16
    bottom = @user_area_y0 + @user_area_height - 16
    @dx = -@dx if @x < @user_area_x0 || @x > right
    @dy = -@dy if @y < @user_area_y0 || @y > bottom
  end

  def draw_screen
    clear_user_area
    @gfx.fill_rect(@x, @y, 16, 16, FmrbGfx::YELLOW)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "Ctrl+Q to quit", FmrbGfx::WHITE)
    @gfx.present
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :key_down
      # scancode is a HID Usage ID and means the same thing on device and in
      # the simulator; keycode does not.
      case ev[:scancode]
      when 79 then @dx = SPEED     # right arrow
      when 80 then @dx = -SPEED    # left arrow
      end
    end
  end

  # Ctrl+Tab parked this app and the user came back to it.
  def on_resume
    draw_screen
  end

  def on_update
    step
    draw_screen
    FRAME_MS
  end
end

# An app file that stops here loads and does nothing: the class has to be
# instantiated and started.
begin
  MyApp.new.start
rescue => e
  Log.error("MyApp: #{e.class}: #{e.message}")
end
