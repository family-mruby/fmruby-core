# Raycaster - Wolfenstein 3D style FPS demo
# Controls:
#   Keyboard: Left/Right = Turn, Up/Down = Move, Space = Shoot
#   Gamepad: Left stick/D-pad = Move+Turn

class RaycasterApp < FmrbApp
  # Fixed-point scale (multiply by 256)
  FP_SHIFT = 8
  FP_ONE = 256
  FP_HALF = 128

  # Viewport
  VP_W = 150
  VP_H = 150
  STRIP_W = 6
  NUM_RAYS = 25  # VP_W / STRIP_W

  # Player
  MOVE_SPEED = 40   # fixed-point units per step
  ROT_SPEED = 8     # degrees per step

  # Map cell size in fixed-point
  CELL_SIZE = 256   # = FP_ONE

  # Colors (RGB332)
  C_BLACK   = 0x00
  C_WHITE   = 0xFF
  C_CEIL    = 0x24  # dark gray-blue
  C_FLOOR   = 0x49  # dark gray-brown
  C_HUD_BG  = 0x00
  C_HUD_TXT = 0xFF
  C_RED     = 0xE0
  C_GREEN   = 0x1C
  C_BLUE    = 0x03
  C_YELLOW  = 0xFC
  C_ENEMY   = 0xE3  # magenta
  C_ENEMY_D = 0x61  # dark magenta
  C_CROSS   = 0xFF  # crosshair

  # Wall colors by map value (1-4)
  WALL_COLORS = [
    nil,
    [0xE0, 0x80],  # 1: red wall, dark red
    [0x1C, 0x08],  # 2: green wall, dark green
    [0x17, 0x03],  # 3: cyan-blue wall, dark blue
    [0xFC, 0x90],  # 4: yellow wall, dark yellow
  ]

  # 12x12 map (0=empty, 1-4=wall types)
  MAP_W = 12
  MAP_H = 12
  WORLD_MAP = [
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 2, 0, 0, 0, 0, 3, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 0, 0, 0, 2, 0, 0, 0, 0, 4, 1,
    1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1,
    1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 1,
    1, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  ]

  # FOV = 60 degrees
  FOV = 60
  HALF_FOV = 30

  def initialize
    super()
    @input = {}

    # Build sin/cos lookup table (fixed-point, 360 entries)
    # sin_table[deg] = (sin(deg) * 256).to_i
    @sin_tbl = build_sin_table
    @cos_tbl = build_cos_table

    # Player position in fixed-point (start at cell 1.5, 1.5)
    @px = FP_ONE + FP_HALF  # 1.5 cells
    @py = FP_ONE + FP_HALF
    @pa = 0  # angle in degrees (0=right, 90=down)
    @frame_count = 0
    @needs_draw = true
    @shoot_pressed = false
    @shoot_flash = 0
    @hit_flash = 0
    @score = 0

    # Enemies: array of {x:, y:, alive:}  (fixed-point positions)
    @enemies = [
      { x: 6 * CELL_SIZE + FP_HALF, y: 3 * CELL_SIZE + FP_HALF, alive: true },
      { x: 4 * CELL_SIZE + FP_HALF, y: 7 * CELL_SIZE + FP_HALF, alive: true },
      { x: 9 * CELL_SIZE + FP_HALF, y: 8 * CELL_SIZE + FP_HALF, alive: true },
      { x: 6 * CELL_SIZE + FP_HALF, y: 9 * CELL_SIZE + FP_HALF, alive: true },
      { x: 10 * CELL_SIZE + FP_HALF, y: 2 * CELL_SIZE + FP_HALF, alive: true },
    ]
  end

  def on_create
    Log.info("Raycaster on_create")
    @depth_buf = cast_all_rays
    draw_frame
  end

  # ---- Trig LUT ----

  def build_sin_table
    tbl = []
    360.times do |deg|
      rad = deg * Math::PI / 180.0
      tbl << (Math.sin(rad) * FP_ONE).to_i
    end
    tbl
  end

  def build_cos_table
    tbl = []
    360.times do |deg|
      rad = deg * Math::PI / 180.0
      tbl << (Math.cos(rad) * FP_ONE).to_i
    end
    tbl
  end

  # ---- Helpers ----

  def fp_sin(deg)
    @sin_tbl[deg % 360]
  end

  def fp_cos(deg)
    @cos_tbl[deg % 360]
  end

  def map_at(mx, my)
    return 1 if mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H
    WORLD_MAP[my * MAP_W + mx]
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
      when 2  then @shoot_pressed = true  # Circle = Shoot
      when 9 then stop  # Start = Quit
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
      when 80 then @input[:left] = true    # Left arrow
      when 79 then @input[:right] = true   # Right arrow
      when 82 then @input[:up] = true      # Up arrow
      when 81 then @input[:down] = true    # Down arrow
      end
      if ev[:character] == 32  # Space = Shoot
        @shoot_pressed = true
      elsif ev[:character] == 113  # q = Quit
        stop
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
    @frame_count += 1
    moved = update_player
    shot = false
    if @shoot_pressed
      @shoot_pressed = false
      @shoot_flash = 3
      shot = true
      @needs_draw = true
    end
    if @shoot_flash > 0
      @shoot_flash -= 1
      @needs_draw = true
    end
    if @hit_flash > 0
      @hit_flash -= 1
      @needs_draw = true
    end
    if moved || @needs_draw
      @depth_buf = cast_all_rays
      if shot
        try_shoot
      end
      draw_frame
      @needs_draw = false
    end
    100
  end

  def update_player
    moved = false

    # Rotation
    if @input[:left]
      @pa = (@pa - ROT_SPEED) % 360
      moved = true
    end
    if @input[:right]
      @pa = (@pa + ROT_SPEED) % 360
      moved = true
    end

    # Movement (forward/backward along facing direction)
    if @input[:up]
      dx = fp_cos(@pa) * MOVE_SPEED / FP_ONE
      dy = fp_sin(@pa) * MOVE_SPEED / FP_ONE
      try_move(@px + dx, @py + dy)
      moved = true
    end
    if @input[:down]
      dx = fp_cos(@pa) * MOVE_SPEED / FP_ONE
      dy = fp_sin(@pa) * MOVE_SPEED / FP_ONE
      try_move(@px - dx, @py - dy)
      moved = true
    end
    moved
  end

  def try_move(nx, ny)
    # Check collision with wall (add margin of 20 FP units)
    margin = 20
    mx = nx / CELL_SIZE
    my = ny / CELL_SIZE
    # Check target cell
    if map_at(mx, my) == 0
      # Also check corners with margin
      mx1 = (nx - margin) / CELL_SIZE
      mx2 = (nx + margin) / CELL_SIZE
      my1 = (ny - margin) / CELL_SIZE
      my2 = (ny + margin) / CELL_SIZE
      if map_at(mx1, my1) == 0 && map_at(mx2, my1) == 0 &&
         map_at(mx1, my2) == 0 && map_at(mx2, my2) == 0
        @px = nx
        @py = ny
      end
    end
  end

  # ---- Raycasting ----

  def cast_ray(angle)
    # DDA raycasting algorithm (integer-based)
    angle = angle % 360
    sin_a = fp_sin(angle)
    cos_a = fp_cos(angle)

    # Avoid division by zero
    cos_a = 1 if cos_a == 0
    sin_a = 1 if sin_a == 0

    # Current map cell
    map_x = @px / CELL_SIZE
    map_y = @py / CELL_SIZE

    # Ray direction signs
    step_x = cos_a > 0 ? 1 : -1
    step_y = sin_a > 0 ? 1 : -1

    # Distance to next cell boundary (fixed-point)
    if cos_a > 0
      dx_to_edge = (map_x + 1) * CELL_SIZE - @px
    else
      dx_to_edge = @px - map_x * CELL_SIZE
      dx_to_edge = 1 if dx_to_edge == 0
    end

    if sin_a > 0
      dy_to_edge = (map_y + 1) * CELL_SIZE - @py
    else
      dy_to_edge = @py - map_y * CELL_SIZE
      dy_to_edge = 1 if dy_to_edge == 0
    end

    # Step through grid
    cos_abs = cos_a.abs
    sin_abs = sin_a.abs

    # t_x = distance along ray to next vertical grid line
    # t_y = distance along ray to next horizontal grid line
    # We track these as scaled values to avoid division

    # t_max_x = dx_to_edge / cos_abs (in FP)
    # t_max_y = dy_to_edge / sin_abs (in FP)
    # Compare: t_max_x vs t_max_y
    # Instead of division, cross-multiply:
    # dx_to_edge / cos_abs < dy_to_edge / sin_abs
    # dx_to_edge * sin_abs < dy_to_edge * cos_abs

    t_x_num = dx_to_edge
    t_x_den = cos_abs
    t_y_num = dy_to_edge
    t_y_den = sin_abs

    side = 0  # 0=vertical wall hit, 1=horizontal wall hit
    hit = 0
    depth = 0

    24.times do
      # Compare t_x vs t_y using cross multiplication
      if t_x_num * t_y_den < t_y_num * t_x_den
        # Step in X
        map_x += step_x
        depth = t_x_num * FP_ONE / t_x_den
        t_x_num += CELL_SIZE
        side = 0
      else
        # Step in Y
        map_y += step_y
        depth = t_y_num * FP_ONE / t_y_den
        t_y_num += CELL_SIZE
        side = 1
      end

      hit = map_at(map_x, map_y)
      break if hit > 0
    end

    # Fix fisheye: multiply by cos of angle offset from player angle
    angle_diff = (angle - @pa) % 360
    angle_diff -= 360 if angle_diff > 180
    cos_diff = fp_cos(angle_diff)
    cos_diff = 1 if cos_diff == 0
    depth = depth * cos_diff / FP_ONE

    # Ensure positive
    depth = depth.abs
    depth = 1 if depth == 0

    { dist: depth, wall: hit, side: side }
  end

  # ---- Ray buffer ----

  def cast_all_rays
    buf = []
    NUM_RAYS.times do |i|
      ray_angle = @pa - HALF_FOV + (i * FOV / NUM_RAYS)
      buf << cast_ray(ray_angle)
    end
    buf
  end

  # ---- Enemies ----

  def try_shoot
    # Check if center ray (crosshair) hits an enemy
    # Find closest alive enemy near the center of view
    center_angle = @pa
    best_enemy = nil
    best_dist = 999999

    @enemies.each do |e|
      next unless e[:alive]
      dx = e[:x] - @px
      dy = e[:y] - @py
      dist = isqrt(dx * dx + dy * dy)
      next if dist < 10  # too close

      # Angle to enemy
      enemy_angle = atan2_deg(dy, dx)
      angle_diff = (enemy_angle - center_angle) % 360
      angle_diff -= 360 if angle_diff > 180

      # Enemy must be within ~5 degrees of crosshair
      if angle_diff.abs < 5 && dist < best_dist
        # Check wall occlusion: is wall closer than enemy?
        center_ray = @depth_buf[NUM_RAYS / 2]
        if center_ray && dist < center_ray[:dist]
          # Enemy is NOT occluded
        elsif center_ray && dist >= center_ray[:dist]
          next  # Wall is closer, skip
        end
        best_dist = dist
        best_enemy = e
      end
    end

    if best_enemy
      best_enemy[:alive] = false
      @score += 100
      @hit_flash = 5
    end
  end

  # Integer square root
  def isqrt(n)
    return 0 if n <= 0
    x = n
    y = (x + 1) / 2
    while y < x
      x = y
      y = (x + n / x) / 2
    end
    x
  end

  # atan2 returning degrees (0-359), using LUT
  def atan2_deg(dy, dx)
    # Use Math.atan2 for accuracy (only called for enemies, not per-pixel)
    rad = Math.atan2(dy, dx)
    deg = (rad * 180.0 / Math::PI).to_i
    deg += 360 if deg < 0
    deg % 360
  end

  def draw_enemies(vp_x, vp_y)
    @enemies.each do |e|
      next unless e[:alive]

      dx = e[:x] - @px
      dy = e[:y] - @py
      dist = isqrt(dx * dx + dy * dy)
      next if dist < 10

      # Angle to enemy relative to player
      enemy_angle = atan2_deg(dy, dx)
      angle_diff = (enemy_angle - @pa) % 360
      angle_diff -= 360 if angle_diff > 180

      # Skip if outside FOV
      next if angle_diff.abs > HALF_FOV + 5

      # Fisheye correction
      cos_diff = fp_cos(angle_diff)
      cos_diff = 1 if cos_diff == 0
      perp_dist = dist * cos_diff / FP_ONE
      perp_dist = 1 if perp_dist <= 0

      # Screen X position
      screen_x = VP_W / 2 + angle_diff * VP_W / FOV
      # Enemy height on screen
      enemy_h = VP_H * CELL_SIZE * 3 / (perp_dist * 4)
      enemy_h = VP_H if enemy_h > VP_H
      enemy_w = enemy_h / 2
      enemy_w = 4 if enemy_w < 4

      # Screen coordinates
      ex = vp_x + screen_x - enemy_w / 2
      ey = vp_y + (VP_H - enemy_h) / 2

      # Skip if completely outside viewport
      next if ex + enemy_w <= vp_x || ex >= vp_x + VP_W
      next if ey + enemy_h <= vp_y || ey >= vp_y + VP_H

      # Check wall occlusion using depth buffer
      strip_idx = screen_x / STRIP_W
      if strip_idx >= 0 && strip_idx < NUM_RAYS
        wall_dist = @depth_buf[strip_idx][:dist]
        next if perp_dist >= wall_dist
      end

      # Clamp draw rect to viewport
      draw_x = ex < vp_x ? vp_x : ex
      draw_y = ey < vp_y ? vp_y : ey
      draw_r = ex + enemy_w > vp_x + VP_W ? vp_x + VP_W : ex + enemy_w
      draw_b = ey + enemy_h > vp_y + VP_H ? vp_y + VP_H : ey + enemy_h
      draw_w = draw_r - draw_x
      draw_h = draw_b - draw_y
      next if draw_w <= 0 || draw_h <= 0

      @gfx.fill_rect(draw_x, draw_y, draw_w, draw_h, C_ENEMY)
      # Eyes
      eye_y = ey + enemy_h / 3
      eye_w = enemy_w / 4
      eye_w = 2 if eye_w < 2
      if eye_y >= vp_y && eye_y + eye_w <= vp_y + VP_H
        lx = ex + enemy_w / 3 - 1
        rx = ex + enemy_w * 2 / 3 - 1
        @gfx.fill_rect(lx, eye_y, eye_w, eye_w, C_BLACK) if lx >= vp_x && lx + eye_w <= vp_x + VP_W
        @gfx.fill_rect(rx, eye_y, eye_w, eye_w, C_BLACK) if rx >= vp_x && rx + eye_w <= vp_x + VP_W
      end
    end
  end

  # ---- Drawing ----

  def draw_frame
    ox = @user_area_x0
    oy = @user_area_y0

    # Viewport position (centered horizontally, top-aligned)
    vp_x = ox + (@user_area_width - VP_W) / 2
    vp_y = oy + 22

    # Draw ceiling and floor for entire viewport
    half_h = VP_H / 2
    @gfx.fill_rect(vp_x, vp_y, VP_W, half_h, C_CEIL)
    @gfx.fill_rect(vp_x, vp_y + half_h, VP_W, half_h, C_FLOOR)

    # Draw wall strips from ray buffer
    NUM_RAYS.times do |i|
      result = @depth_buf[i]
      dist = result[:dist]
      wall = result[:wall]
      side = result[:side]

      wall_h = VP_H * CELL_SIZE / dist
      wall_h = VP_H if wall_h > VP_H
      wall_top = vp_y + (VP_H - wall_h) / 2

      colors = WALL_COLORS[wall]
      if colors
        color = side == 1 ? colors[1] : colors[0]
      else
        color = C_WHITE
      end

      strip_x = vp_x + i * STRIP_W
      @gfx.fill_rect(strip_x, wall_top, STRIP_W, wall_h, color)
    end

    # Draw enemies (after walls, using depth buffer for occlusion)
    draw_enemies(vp_x, vp_y)

    # Crosshair + shoot beam
    cx = vp_x + VP_W / 2
    cy = vp_y + VP_H / 2
    if @shoot_flash > 0
      # Beam effect: vertical line from bottom to center
      @gfx.fill_rect(cx - 3, cy, 7, vp_y + VP_H - cy, C_YELLOW)
      @gfx.fill_rect(cx - 6, cy, 13, 1, C_YELLOW)
      @gfx.fill_rect(cx, cy - 6, 1, 13, C_YELLOW)
    else
      @gfx.fill_rect(cx - 4, cy, 9, 1, C_CROSS)
      @gfx.fill_rect(cx, cy - 4, 1, 9, C_CROSS)
    end

    # HIT! indicator
    if @hit_flash > 0
      @gfx.draw_text(cx - 10, vp_y + 10, "HIT!", C_YELLOW, C_RED)
    end

    # HUD area (below viewport)
    hud_y = vp_y + VP_H + 4
    @gfx.fill_rect(ox, hud_y, @user_area_width, 20, C_HUD_BG)
    alive_count = 0
    @enemies.each { |e| alive_count += 1 if e[:alive] }
    @gfx.draw_text(ox + 4, hud_y + 2, "SCORE:#{@score} ENEMY:#{alive_count}", C_HUD_TXT, C_HUD_BG)

    # Mini-map (right side of viewport, every 4th frame to save draw calls)
    if (@frame_count % 4) == 0
      draw_minimap(vp_x + VP_W + 8, vp_y + 4)
    end

    draw_window_frame
    @gfx.present
  end

  def draw_minimap(mx0, my0)
    cell_px = 3  # pixels per cell
    map_w_px = MAP_W * cell_px
    map_h_px = MAP_H * cell_px

    # Background (empty space)
    @gfx.fill_rect(mx0, my0, map_w_px, map_h_px, C_BLACK)

    # Draw wall cells
    MAP_H.times do |cy|
      MAP_W.times do |cx|
        wall = WORLD_MAP[cy * MAP_W + cx]
        if wall > 0
          colors = WALL_COLORS[wall]
          c = colors ? colors[0] : C_WHITE
          @gfx.fill_rect(mx0 + cx * cell_px, my0 + cy * cell_px, cell_px, cell_px, c)
        end
      end
    end

    # Enemy dots (magenta)
    @enemies.each do |e|
      next unless e[:alive]
      edx = e[:x] * cell_px / CELL_SIZE
      edy = e[:y] * cell_px / CELL_SIZE
      @gfx.fill_rect(mx0 + edx, my0 + edy, 2, 2, C_ENEMY)
    end

    # Player dot (white)
    pdx = @px * cell_px / CELL_SIZE
    pdy = @py * cell_px / CELL_SIZE
    @gfx.fill_rect(mx0 + pdx - 1, my0 + pdy - 1, 3, 3, C_WHITE)
  end

  def on_destroy
    Log.info("Raycaster destroyed")
  end
end

Log.info("RaycasterApp.new")
begin
  app = RaycasterApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
