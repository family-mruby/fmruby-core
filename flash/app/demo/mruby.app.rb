# Bouncing Balls Application
# Multiple bouncing balls demo app.
#
# USE_BALL_BLOCK toggle:
#   true  = cached GfxBlock (1 EXEC_PROG / update; clear + 8 circles in bytecode)
#   false = legacy direct commands (erase + draw = 16 fill_circle / update)
#
# Bottom nav bar lets you tune the target FPS with < / > (1 fps step, 1..10 fps).

class BouncingBallApp < FmrbApp
  BALL_COUNT = 8           # fixed at 8 (GfxBlock uses exactly 16 regs = BALL_COUNT * 2)
  USE_BALL_BLOCK = true

  # FPS control
  MIN_FPS = 1
  MAX_FPS = 60
  INITIAL_FPS = 5

  # Nav bar layout
  NAV_H = 12
  CHAR_W = 6
  CHAR_H = 8
  ARROW_HIT_W = 16
  COLOR_NAV_BG   = 0xDB   # light gray (RGB332)
  COLOR_NAV_TEXT = 0x00   # black

  def initialize
    super()
    @counter = 0
    @bounce_count = 0
    @balls = []
    @ball_block = nil
    @fps = INITIAL_FPS
    @use_block = USE_BALL_BLOCK   # runtime toggle; tracks current mode

    @colors = [
      FmrbGfx::RED,
      FmrbGfx::GREEN,
      FmrbGfx::BLUE,
      FmrbGfx::YELLOW,
      FmrbGfx::CYAN,
      FmrbGfx::MAGENTA
    ]
  end

  def on_create()
    Log.info("on_create called")
    Log.info("user_area: x0=#{@user_area_x0}, y0=#{@user_area_y0}, width=#{@user_area_width}, height=#{@user_area_height}")
    Log.info("window: width=#{@window_width}, height=#{@window_height}")

    initialize_balls
    _build_ball_block if @use_block
    draw_full_screen
  end

  def on_destroy
    @ball_block&.destroy
    Log.info("Destroyed")
  end

  def initialize_balls()
    @balls = []
    bx = _ball_area_x0
    by = _ball_area_y0
    bw = _ball_area_w
    bh = _ball_area_h

    i = 0
    while i < BALL_COUNT
      radius = 6 + (i % 3) * 2   # 6, 8, or 10

      x = bx + radius + (RNG.random_int % (bw - radius * 2))
      y = by + radius + (RNG.random_int % (bh - radius * 2))

      vx = (RNG.random_int % 7) - 3
      vx = 2 if vx == 0
      vy = (RNG.random_int % 7) - 3
      vy = 2 if vy == 0

      color = @colors[RNG.random_int % @colors.length]

      @balls << {
        x: x, y: y, vx: vx, vy: vy,
        radius: radius, color: color
      }
      i += 1
    end

    Log.info("Initialized #{@balls.length} balls")
  end

  def draw_full_screen()
    if @ball_block
      @ball_block.draw(**_ball_kwargs)
    else
      @gfx.fill_rect(_ball_area_x0, _ball_area_y0,
                      _ball_area_w, _ball_area_h, FmrbGfx::WHITE)
      draw_balls
    end
    draw_window_frame
    draw_nav
    @gfx.present
  end

  def on_update()
    if @ball_block
      update_ball_positions
      @ball_block.draw(**_ball_kwargs)
    else
      erase_balls
      update_ball_positions
      draw_balls
    end

    @gfx.present
    @counter += 1
    1000 / @fps
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    x = ev[:x]
    y = ev[:y]
    ny = _nav_y
    return unless y >= ny && y < ny + NAV_H

    # Nav bar is split: left 60% = FPS control, right 40% = mode toggle
    fps_right_x = @user_area_x0 + (@user_area_width * 3) / 5

    if x >= @user_area_x0 && x < @user_area_x0 + ARROW_HIT_W
      _change_fps(-1)
    elsif x >= fps_right_x - ARROW_HIT_W && x < fps_right_x
      _change_fps(1)
    elsif x >= fps_right_x && x < @user_area_x0 + @user_area_width
      _toggle_mode
    end
  end

  def on_resize(new_width, new_height)
    Log.info("Resize event: #{new_width}x#{new_height}")

    # Clamp balls to new ball-area boundaries
    bi = 0
    bn = @balls.length
    while bi < bn
      ball = @balls[bi]
      left_boundary = _ball_area_x0 + ball[:radius]
      right_boundary = _ball_area_x0 + _ball_area_w - ball[:radius]
      top_boundary = _ball_area_y0 + ball[:radius]
      bottom_boundary = _ball_area_y0 + _ball_area_h - ball[:radius]

      ball[:x] = left_boundary if ball[:x] < left_boundary
      ball[:x] = right_boundary if ball[:x] > right_boundary
      ball[:y] = top_boundary if ball[:y] < top_boundary
      ball[:y] = bottom_boundary if ball[:y] > bottom_boundary
      bi += 1
    end

    # Ball block captured old bounds; rebuild for the new size.
    if @use_block
      @ball_block&.destroy
      @ball_block = nil
      _build_ball_block
    end

    draw_full_screen
  end

  private

  # --- Layout helpers ---------------------------------------------------------

  def _ball_area_x0; @user_area_x0; end
  def _ball_area_y0; @user_area_y0; end
  def _ball_area_w;  @user_area_width; end
  def _ball_area_h;  @user_area_height - NAV_H; end
  def _nav_y;        @user_area_y0 + @user_area_height - NAV_H; end

  # --- Nav bar ---------------------------------------------------------------

  def draw_nav
    ny = _nav_y
    @gfx.fill_rect(@user_area_x0, ny, @user_area_width, NAV_H, COLOR_NAV_BG)
    text_y = ny + (NAV_H - CHAR_H) / 2

    # Left 60%: FPS controls  "< N fps >"
    left_w = (@user_area_width * 3) / 5
    @gfx.draw_text(@user_area_x0 + 2, text_y, "<", COLOR_NAV_TEXT, COLOR_NAV_BG)
    @gfx.draw_text(@user_area_x0 + left_w - CHAR_W - 2, text_y,
                    ">", COLOR_NAV_TEXT, COLOR_NAV_BG)
    label = "#{@fps} fps"
    label_x = @user_area_x0 + (left_w - label.length * CHAR_W) / 2
    @gfx.draw_text(label_x, text_y, label, COLOR_NAV_TEXT, COLOR_NAV_BG)

    # Right 40%: mode toggle  "[BLK]" or "[LGC]"
    right_x = @user_area_x0 + left_w
    right_w = @user_area_width - left_w
    mode_text = @use_block ? "[BLK]" : "[LGC]"
    mode_x = right_x + (right_w - mode_text.length * CHAR_W) / 2
    @gfx.draw_text(mode_x, text_y, mode_text, COLOR_NAV_TEXT, COLOR_NAV_BG)
  end

  def _change_fps(delta)
    new_fps = @fps + delta
    new_fps = MIN_FPS if new_fps < MIN_FPS
    new_fps = MAX_FPS if new_fps > MAX_FPS
    return if new_fps == @fps
    @fps = new_fps
    draw_nav
    @gfx.present
  end

  def _toggle_mode
    if @use_block
      @ball_block&.destroy
      @ball_block = nil
      @use_block = false
    else
      _build_ball_block
      @use_block = true
    end
    # Redraw: both paths fully repaint the ball area so the buffer is consistent.
    draw_full_screen
  end

  # --- Legacy (per-primitive) drawing -----------------------------------------

  def draw_balls()
    bi = 0
    bn = @balls.length
    while bi < bn
      ball = @balls[bi]
      @gfx.fill_circle(ball[:x], ball[:y], ball[:radius], ball[:color])
      bi += 1
    end
  end

  def erase_balls()
    bi = 0
    bn = @balls.length
    while bi < bn
      ball = @balls[bi]
      @gfx.fill_circle(ball[:x], ball[:y], ball[:radius], FmrbGfx::WHITE)
      bi += 1
    end
  end

  def update_ball_positions
    bi = 0
    bn = @balls.length
    while bi < bn
      ball = @balls[bi]
      ball[:x] += ball[:vx]
      ball[:y] += ball[:vy]

      left_boundary = _ball_area_x0 + ball[:radius]
      right_boundary = _ball_area_x0 + _ball_area_w - ball[:radius]
      top_boundary = _ball_area_y0 + ball[:radius]
      bottom_boundary = _ball_area_y0 + _ball_area_h - ball[:radius]

      if ball[:x] <= left_boundary || ball[:x] >= right_boundary
        ball[:vx] = -ball[:vx]
        ball[:x] += ball[:vx]
        @bounce_count += 1
      end
      if ball[:y] <= top_boundary || ball[:y] >= bottom_boundary
        ball[:vy] = -ball[:vy]
        ball[:y] += ball[:vy]
        @bounce_count += 1
      end
      bi += 1
    end
  end

  # --- GfxBlock drawing -------------------------------------------------------

  # Build the ball-drawing GfxBlock. The block fills only the ball area (not
  # the nav bar), so the nav bar stays intact and only needs a single redraw
  # when FPS changes.
  def _build_ball_block
    bx = _ball_area_x0
    by = _ball_area_y0
    bw = _ball_area_w
    bh = _ball_area_h
    @ball_block = GfxBlock.new(@gfx, **_ball_kwargs) do |r,
      x1:, y1:, x2:, y2:, x3:, y3:, x4:, y4:,
      x5:, y5:, x6:, y6:, x7:, y7:, x8:, y8:|
      r.fill_rect bx, by, bw, bh, FmrbGfx::WHITE
      r.fill_circle x1, y1, @balls[0][:radius], @balls[0][:color]
      r.fill_circle x2, y2, @balls[1][:radius], @balls[1][:color]
      r.fill_circle x3, y3, @balls[2][:radius], @balls[2][:color]
      r.fill_circle x4, y4, @balls[3][:radius], @balls[3][:color]
      r.fill_circle x5, y5, @balls[4][:radius], @balls[4][:color]
      r.fill_circle x6, y6, @balls[5][:radius], @balls[5][:color]
      r.fill_circle x7, y7, @balls[6][:radius], @balls[6][:color]
      r.fill_circle x8, y8, @balls[7][:radius], @balls[7][:color]
    end
  end

  def _ball_kwargs
    {
      x1: @balls[0][:x], y1: @balls[0][:y],
      x2: @balls[1][:x], y2: @balls[1][:y],
      x3: @balls[2][:x], y3: @balls[2][:y],
      x4: @balls[3][:x], y4: @balls[3][:y],
      x5: @balls[4][:x], y5: @balls[4][:y],
      x6: @balls[5][:x], y6: @balls[5][:y],
      x7: @balls[6][:x], y7: @balls[6][:y],
      x8: @balls[7][:x], y8: @balls[7][:y],
    }
  end
end

# Create and start the app
Log.info("Creating BouncingBallApp")
begin
  app = BouncingBallApp.new
  Log.info("App created successfully")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
end
Log.info("Script ended")
