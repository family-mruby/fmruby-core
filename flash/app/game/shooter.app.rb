# Shooter - Simple shooting game (Gamepad + Keyboard)
# Controls:
#   Gamepad: D-pad/Left stick = Move, Circle = Shoot, Cross = Restart
#   Keyboard: Arrow keys = Move, Space = Shoot, Enter = Restart

class ShooterApp < FmrbApp
  # Colors (RGB332)
  BG_COLOR     = 0x00
  PLAYER_COLOR = 0x1C  # green
  BULLET_COLOR = 0xFC  # yellow
  ENEMY_COLOR  = 0xE0  # red
  TEXT_COLOR   = 0xFF  # white
  GAMEOVER_BG  = 0xE0  # red

  PLAYER_W = 8*2
  PLAYER_H = 8*2
  BULLET_W = 2
  BULLET_H = 4
  ENEMY_W  = 8*2
  ENEMY_H  = 6*2
  PLAYER_SPEED = 3
  BULLET_SPEED = 5
  ENEMY_SPEED_BASE = 1

  def initialize
    super()
    @input = {}
    @shoot_pressed = false
  end

  def on_create
    Log.info("Shooter on_create")
    reset_game
  end

  def reset_game
    @player_x = @user_area_width / 2
    @player_y = @user_area_height - 20
    @bullets = []
    @enemies = []
    @score = 0
    @game_over = false
    @spawn_timer = 0
    @input = {}
    @shoot_pressed = false
    draw_all
  end

  # ---- Input ----

  def on_event(ev)
    super(ev)
    case ev[:type]
    when :gamepad_down
      case ev[:button]
      when 12 then @input[:up] = true
      when 13 then @input[:down] = true
      when 14 then @input[:left] = true
      when 15 then @input[:right] = true
      when 2  then @shoot_pressed = true   # Circle
      when 1                               # Cross
        reset_game if @game_over
      when 9 then stop                     # Start = Quit
      end
    when :gamepad_up
      case ev[:button]
      when 12 then @input[:up] = false
      when 13 then @input[:down] = false
      when 14 then @input[:left] = false
      when 15 then @input[:right] = false
      end
    when :gamepad_axis
      if ev[:axis] == 0  # Left stick X
        v = ev[:value]
        @input[:left] = v < -30
        @input[:right] = v > 30
      elsif ev[:axis] == 1  # Left stick Y
        v = ev[:value]
        @input[:up] = v < -30
        @input[:down] = v > 30
      end
    when :key_down
      case ev[:keycode]
      when 80 then @input[:left] = true
      when 79 then @input[:right] = true
      when 82 then @input[:up] = true
      when 81 then @input[:down] = true
      end
      if ev[:character] == 32  # Space
        @shoot_pressed = true
      end
      if @game_over && (ev[:character] == 10 || ev[:character] == 13)
        reset_game
      end
    when :key_up
      case ev[:keycode]
      when 80 then @input[:left] = false
      when 79 then @input[:right] = false
      when 82 then @input[:up] = false
      when 81 then @input[:down] = false
      end
    end
  end

  # ---- Update ----

  def on_update
    return 100 if @game_over

    update_player
    update_bullets
    update_enemies
    spawn_enemies
    check_collisions

    draw_all

    100
  end

  def update_player
    @player_x -= PLAYER_SPEED if @input[:left]
    @player_x += PLAYER_SPEED if @input[:right]
    @player_y -= PLAYER_SPEED if @input[:up]
    @player_y += PLAYER_SPEED if @input[:down]

    # Clamp
    half_w = PLAYER_W / 2
    @player_x = half_w if @player_x < half_w
    @player_x = @user_area_width - half_w if @player_x > @user_area_width - half_w
    @player_y = 0 if @player_y < 0
    @player_y = @user_area_height - PLAYER_H if @player_y > @user_area_height - PLAYER_H

    if @shoot_pressed
      @shoot_pressed = false
      @bullets << { x: @player_x, y: @player_y - BULLET_H }
    end
  end

  def update_bullets
    bi = 0
    bn = @bullets.length
    while bi < bn
      @bullets[bi][:y] -= BULLET_SPEED
      bi += 1
    end
    @bullets.reject! { |b| b[:y] < -BULLET_H }
  end

  def update_enemies
    speed = ENEMY_SPEED_BASE + @score / 500
    ei = 0
    en = @enemies.length
    while ei < en
      @enemies[ei][:y] += speed
      ei += 1
    end
    @enemies.reject! { |e| e[:y] > @user_area_height + ENEMY_H }
  end

  def spawn_enemies
    @spawn_timer += 1
    interval = 20 - @score / 300
    interval = 8 if interval < 8
    if @spawn_timer >= interval
      @spawn_timer = 0
      ex = (RNG.random_int % (@user_area_width - ENEMY_W * 2)) + ENEMY_W
      @enemies << { x: ex, y: -ENEMY_H }
    end
  end

  def check_collisions
    # Bullet vs Enemy
    @bullets.reject! do |b|
      hit = false
      @enemies.reject! do |e|
        if !hit && rect_hit?(b[:x] - BULLET_W / 2, b[:y], BULLET_W, BULLET_H,
                             e[:x] - ENEMY_W / 2, e[:y], ENEMY_W, ENEMY_H)
          @score += 100
          hit = true
          true
        else
          false
        end
      end
      hit
    end

    # Enemy vs Player
    px = @player_x - PLAYER_W / 2
    py = @player_y
    ei = 0
    en = @enemies.length
    while ei < en
      e = @enemies[ei]
      if rect_hit?(px, py, PLAYER_W, PLAYER_H,
                   e[:x] - ENEMY_W / 2, e[:y], ENEMY_W, ENEMY_H)
        @game_over = true
        draw_all
        draw_game_over
        return
      end
      ei += 1
    end
  end

  def rect_hit?(x1, y1, w1, h1, x2, y2, w2, h2)
    !(x1 + w1 <= x2 || x2 + w2 <= x1 || y1 + h1 <= y2 || y2 + h2 <= y1)
  end

  # ---- Drawing ----

  def draw_all
    ox = @user_area_x0
    oy = @user_area_y0

    # Background
    @gfx.fill_rect(ox, oy, @user_area_width, @user_area_height, BG_COLOR)

    # Player (triangle shape)
    px = ox + @player_x
    py = oy + @player_y
    @gfx.fill_triangle(px, py - 2, px - PLAYER_W / 2, py + PLAYER_H,
                        px + PLAYER_W / 2, py + PLAYER_H, PLAYER_COLOR)

    # Bullets
    bi = 0
    bn = @bullets.length
    while bi < bn
      b = @bullets[bi]
      @gfx.fill_rect(ox + b[:x] - BULLET_W / 2, oy + b[:y], BULLET_W, BULLET_H, BULLET_COLOR)
      bi += 1
    end

    # Enemies
    ei = 0
    en = @enemies.length
    while ei < en
      e = @enemies[ei]
      ex = ox + e[:x]
      ey = oy + e[:y]
      @gfx.fill_rect(ex - ENEMY_W / 2, ey, ENEMY_W, ENEMY_H, ENEMY_COLOR)
      ei += 1
    end

    # Score
    @gfx.draw_text(ox + 2, oy + 2, "SCORE:#{@score}", TEXT_COLOR, BG_COLOR)

    draw_window_frame
    @gfx.present
  end

  def draw_game_over
    ox = @user_area_x0
    oy = @user_area_y0
    cx = ox + @user_area_width / 2
    cy = oy + @user_area_height / 2
    @gfx.fill_rect(cx - 40, cy - 16, 80, 34, GAMEOVER_BG)
    @gfx.draw_text(cx - 27, cy - 12, "GAME OVER", TEXT_COLOR, GAMEOVER_BG)
    @gfx.draw_text(cx - 38, cy + 4, "X:Retry ST:Quit", TEXT_COLOR, GAMEOVER_BG)
    @gfx.present
  end

  def on_destroy
    Log.info("Shooter destroyed")
  end
end

Log.info("ShooterApp.new")
begin
  app = ShooterApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
