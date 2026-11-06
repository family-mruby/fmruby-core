# A movable app with a window (doc/checkpoint F3): it draws only when
# something happens (a click leaves a dot, a key adds a letter), in a
# Japanese font, with a GfxBlock and a sprite of its own, and a Task counts in
# the log. Moved, it draws the same window again from its state in on_restore
# (nothing drawn is in the image) and makes its sprite again, is subscribed
# to its topic again by the base and goes on taking keys and clicks. The
# checkpoint check (tools/regress/checkpoint.rb) compares the window before
# and after the move and reads the counts and the events from the log.

class CkptWindowApp < FmrbApp
  CHOSEN_BG = 0x92

  def on_create
    @dots = []
    @text = ""
    # a topic the base subscribes to again in the program it moves to
    subscribe("ckpt_window")
    @gfx.set_font(:ja, 12)
    t0 = Machine.board_millis
    Task.new(name: "count_w") do
      k = 0
      while true
        k += 1
        Log.info("ckpt_window: task=#{k} t=#{Machine.board_millis - t0}")
        sleep_ms(300)
      end
    end
    # the marker under the last dot: a program of its own on the display side
    @mark = GfxBlock.new(@gfx, x: @user_area_x0 + 2, y: @user_area_y0 + 2) do |r, x:, y:|
      r.draw_rect x, y, 7, 7, FmrbGfx::RED
    end
    @ui = FmrbUI.new(self)
    @ui.label(:theme_bg, 4, 24, 30, 10, "ui")
    @ui_chosen = FmrbUI.new(self, bg: CHOSEN_BG)
    @ui_chosen.label(:chosen_bg, 40, 24, 30, 10, "bg")
    make_sprite
    draw_all
    Log.info("ckpt_window: drawn #{ui_colours} #{font_note}")
  end

  # A sprite (an image and an instance of it on the display side): the base
  # forgets them in a restored app, which makes them again in on_restore.
  def make_sprite
    @sprite_img = SpriteImage.new(@gfx, width: 6, height: 6)
    @sprite_img.draw { |g| g.fill_rect(0, 0, 6, 6, FmrbGfx::MAGENTA) }
    @sprite = SpriteInstance.new(@gfx, @sprite_img, x: @user_area_x0 + @user_area_width - 10,
                                 y: @user_area_y0 + @user_area_height - 10)
  end

  def draw_all
    clear_user_area
    x0 = @user_area_x0
    y0 = @user_area_y0
    @gfx.fill_rect(x0 + 2, y0 + 2, @user_area_width - 4, 6, FmrbGfx::BLUE)
    @gfx.draw_text(x0 + 4, y0 + 12, "Window #{@text}", theme_fg)
    i = 0
    while i < @dots.size
      d = @dots[i]
      @gfx.fill_circle(d[0], d[1], 2, FmrbGfx::GREEN)
      i += 1
    end
    last = @dots[-1]
    if last
      @mark.draw(x: last[0] - 3, y: last[1] - 3)
    else
      @mark.draw(x: x0 + 2, y: y0 + 2)
    end
    @ui.flush
    @ui_chosen.flush
    @gfx.present
  end

  # The labels' ground: the default one, the chosen one, and the theme's
  # window background of this machine (decimal RGB332).
  def ui_colours
    "ui_bg=#{@ui.widgets[0].bg} chosen_bg=#{@ui_chosen.widgets[0].bg} window_bg=#{FmrbConst::THEME_WINDOW_BG}"
  end

  # The font this machine picked for the [:ja, 12] asked for in on_create:
  # misaki 8 on a build without the efontJA fonts, 12 again on one with them
  # (the base picks it again from what was asked, report/font_pct.md).
  def font_note
    f = @gfx.current_font
    "font=#{f[0]},#{f[1]}"
  end

  def on_event(ev)
    case ev[:type]
    when :key_down
      c = ev[:character].to_i
      if c >= 0x20 && c < 0x7F
        @text << c.chr
        Log.info("ckpt_window: key #{c.chr} text=#{@text}")
        draw_all
      end
    when :mouse_up
      if ev[:button] == 1 && ev[:y] >= @user_area_y0 + 24
        @dots << [ev[:x], ev[:y]]
        Log.info("ckpt_window: click #{ev[:x]},#{ev[:y]} dots=#{@dots.size}")
        draw_all
      end
    end
  end

  def on_update
    250
  end

  def on_checkpoint
    Log.info("ckpt_window: on_checkpoint dots=#{@dots.size} text=#{@text}")
  end

  def on_restore
    make_sprite
    draw_all
    Log.info("ckpt_window: on_restore dots=#{@dots.size} text=#{@text} #{ui_colours} #{font_note}")
  end
end

begin
  app = CkptWindowApp.new
  app.start
rescue => e
  Log.error("ckpt_window: #{e.class}: #{e.message}")
end
