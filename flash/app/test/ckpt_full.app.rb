# A movable fullscreen app (doc/checkpoint F4): its screen is 320 px wide on
# Retro and 426 px on Modern, so a move between the two is a resize. The base
# lays the user area out again and calls on_resize before on_restore. It
# draws a frame round its user area and a square in each corner of it, so the
# checkpoint check (tools/regress/checkpoint.rb) finds the corners where the
# new size puts them. A Task counts in the log.

class CkptFullApp < FmrbApp
  def on_create
    @resizes = 0
    t0 = Machine.board_millis
    Task.new(name: "count_full") do
      k = 0
      while true
        k += 1
        Log.info("ckpt_full: task=#{k} t=#{Machine.board_millis - t0}")
        sleep_ms(300)
      end
    end
    draw_all
    Log.info("ckpt_full: drawn #{size_text}")
  end

  def size_text
    "window=#{@window_width}x#{@window_height} user=#{@user_area_x0},#{@user_area_y0}," \
      "#{@user_area_width}x#{@user_area_height}"
  end

  def draw_all
    clear_user_area
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    @gfx.draw_rect(x0, y0, w, h, FmrbGfx::BLUE)
    @gfx.fill_rect(x0 + 1, y0 + 1, 4, 4, FmrbGfx::RED)
    @gfx.fill_rect(x0 + w - 5, y0 + 1, 4, 4, FmrbGfx::RED)
    @gfx.fill_rect(x0 + 1, y0 + h - 5, 4, 4, FmrbGfx::RED)
    @gfx.fill_rect(x0 + w - 5, y0 + h - 5, 4, 4, FmrbGfx::RED)
    @gfx.draw_text(x0 + 8, y0 + 8, "#{w}x#{h} r#{@resizes}", theme_fg)
    @gfx.present
  end

  def on_resize(w, h)
    @resizes += 1
    Log.info("ckpt_full: on_resize #{w}x#{h}")
  end

  def on_update
    250
  end

  def on_checkpoint
    Log.info("ckpt_full: on_checkpoint #{size_text}")
  end

  def on_restore
    draw_all
    Log.info("ckpt_full: on_restore #{size_text} at #{@pos_x},#{@pos_y} resizes=#{@resizes}")
  end
end

begin
  app = CkptFullApp.new
  app.start
rescue => e
  Log.error("ckpt_full: #{e.class}: #{e.message}")
end
