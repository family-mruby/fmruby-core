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

  PLAYER_W = 16
  PLAYER_H = 16
  BULLET_W = 2
  BULLET_H = 4
  ENEMY_W  = 16
  ENEMY_H  = 12
  PLAYER_SPEED = 3
  BULLET_SPEED = 5
  ENEMY_SPEED_BASE = 1

  MAX_BULLETS = 16
  MAX_ENEMIES = 24

  def initialize
    super()
    @input = {}
    @shoot_pressed = false
  end

  def on_create
    Log.info("Shooter on_create")
    setup_sprites
    reset_game
  end

  def setup_sprites
    # Player triangle on a transparent background
    @player_img = SpriteImage.new(@gfx, width: PLAYER_W, height: PLAYER_H,
                                  transparent_color: 0, use_transparent: true)
    @player_img.draw do |g|
      g.fill_triangle(PLAYER_W / 2, 0,
                      0, PLAYER_H - 1,
                      PLAYER_W - 1, PLAYER_H - 1, PLAYER_COLOR)
    end

    @bullet_img = SpriteImage.new(@gfx, width: BULLET_W, height: BULLET_H)
    @bullet_img.draw do |g|
      g.fill_rect(0, 0, BULLET_W, BULLET_H, BULLET_COLOR)
    end

    @enemy_img = SpriteImage.new(@gfx, width: ENEMY_W, height: ENEMY_H)
    @enemy_img.draw do |g|
      g.fill_rect(0, 0, ENEMY_W, ENEMY_H, ENEMY_COLOR)
    end

    @player_sprite = SpriteInstance.new(@gfx, @player_img,
                                        x: @user_area_x0, y: @user_area_y0, z: 2)
    @player_sprite.visible = false

    @bullet_pool = []
    MAX_BULLETS.times do
      inst = SpriteInstance.new(@gfx, @bullet_img, x: 0, y: 0, z: 1)
      inst.visible = false
      @bullet_pool << inst
    end

    @enemy_pool = []
    MAX_ENEMIES.times do
      inst = SpriteInstance.new(@gfx, @enemy_img, x: 0, y: 0, z: 1)
      inst.visible = false
      @enemy_pool << inst
    end
  end

  def reset_game
    @player_x = @user_area_width / 2
    @player_y = @user_area_height - 20

    # Recycle any active sprites back into their pools
    if @bullets
      @bullets.each { |b| b[:sprite].visible = false }
    end
    if @enemies
      @enemies.each { |e| e[:sprite].visible = false }
    end
    @bullets = []
    @enemies = []
    @free_bullets = @bullet_pool.dup
    @free_enemies = @enemy_pool.dup

    @score = 0
    @game_over = false
    @spawn_timer = 0
    @input = {}
    @shoot_pressed = false

    draw_background
    draw_score

    @player_sprite.visible = true
    move_player_sprite

    @gfx.present
  end

  # ---- Input ----

  def on_event(ev)
    super(ev)
    case ev[:type]
    when :gamepad_down
      case ev[:button]
      when FmrbConst::GP_UP    then @input[:up]    = true
      when FmrbConst::GP_DOWN  then @input[:down]  = true
      when FmrbConst::GP_LEFT  then @input[:left]  = true
      when FmrbConst::GP_RIGHT then @input[:right] = true
      when FmrbConst::GP_CIRCLE then @shoot_pressed = true
      when FmrbConst::GP_CROSS
        reset_game if @game_over
      when FmrbConst::GP_START then stop
      end
    when :gamepad_up
      case ev[:button]
      when FmrbConst::GP_UP    then @input[:up]    = false
      when FmrbConst::GP_DOWN  then @input[:down]  = false
      when FmrbConst::GP_LEFT  then @input[:left]  = false
      when FmrbConst::GP_RIGHT then @input[:right] = false
      end
    when :gamepad_axis
      if ev[:axis] == FmrbConst::GP_AXIS_LX
        v = ev[:value]
        @input[:left] = v < -30
        @input[:right] = v > 30
      elsif ev[:axis] == FmrbConst::GP_AXIS_LY
        v = ev[:value]
        @input[:up] = v < -30
        @input[:down] = v > 30
      end
    when :key_down
      case ev[:keycode]
      when FmrbConst::KEY_LEFT  then @input[:left]  = true
      when FmrbConst::KEY_RIGHT then @input[:right] = true
      when FmrbConst::KEY_UP    then @input[:up]    = true
      when FmrbConst::KEY_DOWN  then @input[:down]  = true
      end
      if ev[:character] == 32  # Space
        @shoot_pressed = true
      end
      if @game_over && (ev[:character] == 10 || ev[:character] == 13)
        reset_game
      end
    when :key_up
      case ev[:keycode]
      when FmrbConst::KEY_LEFT  then @input[:left]  = false
      when FmrbConst::KEY_RIGHT then @input[:right] = false
      when FmrbConst::KEY_UP    then @input[:up]    = false
      when FmrbConst::KEY_DOWN  then @input[:down]  = false
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
    return 100 if @game_over

    draw_score
    @gfx.present
    100
  end

  def update_player
    @player_x -= PLAYER_SPEED if @input[:left]
    @player_x += PLAYER_SPEED if @input[:right]
    @player_y -= PLAYER_SPEED if @input[:up]
    @player_y += PLAYER_SPEED if @input[:down]

    half_w = PLAYER_W / 2
    @player_x = half_w if @player_x < half_w
    @player_x = @user_area_width - half_w if @player_x > @user_area_width - half_w
    @player_y = 0 if @player_y < 0
    @player_y = @user_area_height - PLAYER_H if @player_y > @user_area_height - PLAYER_H

    move_player_sprite

    if @shoot_pressed
      @shoot_pressed = false
      spawn_bullet(@player_x, @player_y - BULLET_H)
    end
  end

  def move_player_sprite
    @player_sprite.move(@user_area_x0 + @player_x - PLAYER_W / 2,
                        @user_area_y0 + @player_y)
  end

  def spawn_bullet(x, y)
    return if @free_bullets.empty?
    sprite = @free_bullets.pop
    sprite.visible = true
    sprite.move(@user_area_x0 + x - BULLET_W / 2, @user_area_y0 + y)
    @bullets << { x: x, y: y, sprite: sprite }
  end

  def update_bullets
    alive = []
    bi = 0
    bn = @bullets.length
    while bi < bn
      b = @bullets[bi]
      b[:y] -= BULLET_SPEED
      if b[:y] < -BULLET_H
        b[:sprite].visible = false
        @free_bullets.push(b[:sprite])
      else
        b[:sprite].move(@user_area_x0 + b[:x] - BULLET_W / 2,
                        @user_area_y0 + b[:y])
        alive << b
      end
      bi += 1
    end
    @bullets = alive
  end

  def update_enemies
    speed = ENEMY_SPEED_BASE + @score / 500
    alive = []
    ei = 0
    en = @enemies.length
    while ei < en
      e = @enemies[ei]
      e[:y] += speed
      if e[:y] > @user_area_height + ENEMY_H
        e[:sprite].visible = false
        @free_enemies.push(e[:sprite])
      else
        e[:sprite].move(@user_area_x0 + e[:x] - ENEMY_W / 2,
                        @user_area_y0 + e[:y])
        alive << e
      end
      ei += 1
    end
    @enemies = alive
  end

  def spawn_enemies
    @spawn_timer += 1
    interval = 20 - @score / 300
    interval = 8 if interval < 8
    if @spawn_timer >= interval && !@free_enemies.empty?
      @spawn_timer = 0
      ex = (RNG.random_int % (@user_area_width - ENEMY_W * 2)) + ENEMY_W
      sprite = @free_enemies.pop
      sprite.visible = true
      sprite.move(@user_area_x0 + ex - ENEMY_W / 2, @user_area_y0 + (-ENEMY_H))
      @enemies << { x: ex, y: -ENEMY_H, sprite: sprite }
    end
  end

  def check_collisions
    # Bullet vs Enemy
    bi = @bullets.length - 1
    while bi >= 0
      b = @bullets[bi]
      hit_idx = -1
      ei = @enemies.length - 1
      while ei >= 0
        e = @enemies[ei]
        if rect_hit?(b[:x] - BULLET_W / 2, b[:y], BULLET_W, BULLET_H,
                     e[:x] - ENEMY_W / 2, e[:y], ENEMY_W, ENEMY_H)
          hit_idx = ei
          break
        end
        ei -= 1
      end
      if hit_idx >= 0
        e = @enemies[hit_idx]
        e[:sprite].visible = false
        @free_enemies.push(e[:sprite])
        @enemies.delete_at(hit_idx)

        b[:sprite].visible = false
        @free_bullets.push(b[:sprite])
        @bullets.delete_at(bi)
        @score += 100
      end
      bi -= 1
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
        trigger_game_over
        return
      end
      ei += 1
    end
  end

  def trigger_game_over
    @game_over = true
    # Hide every sprite so the GAME OVER overlay is not occluded
    @player_sprite.visible = false
    @bullets.each { |b| b[:sprite].visible = false }
    @enemies.each { |e| e[:sprite].visible = false }
    draw_score
    draw_game_over
    @gfx.present
  end

  def rect_hit?(x1, y1, w1, h1, x2, y2, w2, h2)
    !(x1 + w1 <= x2 || x2 + w2 <= x1 || y1 + h1 <= y2 || y2 + h2 <= y1)
  end

  # ---- Drawing ----

  def draw_background
    ox = @user_area_x0
    oy = @user_area_y0
    @gfx.fill_rect(ox, oy, @user_area_width, @user_area_height, BG_COLOR)
    draw_window_frame
  end

  # Fixed-width score keeps prior digits from leaking through when the
  # background is no longer cleared every frame.
  def draw_score
    @gfx.draw_text(@user_area_x0 + 2, @user_area_y0 + 2,
                   sprintf("SCORE:%06d", @score), TEXT_COLOR, BG_COLOR)
  end

  def draw_game_over
    ox = @user_area_x0
    oy = @user_area_y0
    cx = ox + @user_area_width / 2
    cy = oy + @user_area_height / 2
    @gfx.fill_rect(cx - 40, cy - 16, 80, 34, GAMEOVER_BG)
    @gfx.draw_text(cx - 27, cy - 12, "GAME OVER", TEXT_COLOR, GAMEOVER_BG)
    @gfx.draw_text(cx - 38, cy + 4, "X:Retry ST:Quit", TEXT_COLOR, GAMEOVER_BG)
  end

  def on_destroy
    @bullet_pool.each { |s| s.destroy } if @bullet_pool
    @enemy_pool.each { |s| s.destroy } if @enemy_pool
    @player_sprite.destroy if @player_sprite
    @player_img.destroy if @player_img
    @bullet_img.destroy if @bullet_img
    @enemy_img.destroy if @enemy_img
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
