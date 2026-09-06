# Reproduces the input jam of doc/midi/report/p7_6.md 6.2: an app too busy
# to drain its message queue while mouse moves stream in. Each update sleeps
# most of a second, so at 30Hz the moves overrun a 32-deep queue in about a
# second -- the scenario the kernel's move latch (input_router.rb) exists for.
#
# What to read in the log:
#   busy_input: moves=N last=(x,y)
# After a flood, `last` must equal the final injected position even though
# most moves were dropped, and the kernel log must stay free of
# "msg_send TIMEOUT".

class BusyInputApp < FmrbApp
  def on_create
    @moves = 0
    @downs = 0
    @last_x = -1
    @last_y = -1
    draw
  end

  def draw
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, FmrbConst::THEME_WINDOW_BG)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "busy: moves=#{@moves}", FmrbConst::THEME_TEXT,
                   FmrbConst::THEME_WINDOW_BG)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 16,
                   "last=(#{@last_x},#{@last_y}) downs=#{@downs}",
                   FmrbConst::THEME_TEXT, FmrbConst::THEME_WINDOW_BG)
    @gfx.present
  end

  def on_update
    # Deliberately busy: hold the task so the inbox backs up. Machine.delay_ms
    # keeps the FreeRTOS task occupied without spinning the scheduler.
    Machine.delay_ms(700)
    Log.info("busy_input: moves=#{@moves} last=(#{@last_x},#{@last_y}) downs=#{@downs}")
    draw
    50
  end

  def on_event(ev)
    case ev[:type]
    when :mouse_move
      @moves += 1
      @last_x = ev[:x]
      @last_y = ev[:y]
    when :mouse_down
      @downs += 1
    end
  end
end

begin
  app = BusyInputApp.new
  app.start
rescue => e
  Log.error("busy_input: #{e.class}: #{e.message}")
end
