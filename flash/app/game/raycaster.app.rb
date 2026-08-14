# Raycaster - Wolfenstein 3D style FPS demo
# Controls:
#   Keyboard: Left/Right = Turn, Up/Down = Move, Space = Shoot, B = engine
#   Gamepad: Left stick/D-pad = Move+Turn
#
# The ray loop is not in this file. It lives in the picoruby-fmrb-raycast gem,
# which runs the same Ruby on two engines: the mruby VM interpreting it, and
# Spinel running it as compiled native code. B swaps between them mid-game and
# the HUD shows how long the last frame's rays took, so the difference appears
# under a picture that does not change (doc/raycast_spinel/plan.md).
#
# A raycaster suits that comparison better than the FFT did. Every value in the
# cast is a fixed-point integer, so there is no double arithmetic being emulated
# in software underneath -- what the two numbers differ by is the engine.

class RaycasterApp < FmrbApp
  # Fixed-point scale (multiply by 256)
  FP_SHIFT = 8
  FP_ONE = 256
  FP_HALF = 128

  # Viewport
  VP_W = 160
  VP_H = 150
  STRIP_W = 4
  # VP_W / STRIP_W, and it must equal RaycastCore::NUM_RAYS -- the gem decides
  # how many rays come back, this decides how they are drawn. Changing one
  # without the other leaves strips undrawn or reads past the buffer.
  NUM_RAYS = 40

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

  # 32x32 map (0=empty, 1-4=wall types). Small rooms off two long corridors,
  # a hall at each end. Built and checked by a generator rather than by hand:
  # every open cell is reachable from the player's start, and the long runs are
  # deliberate -- a ray down the 30-cell corridor reaches MAX_STEPS, which is
  # what makes the cast heavy enough to see the engine in the frame rate.
  MAP_W = 32
  MAP_H = 32
  WORLD_MAP = [
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1,
    1, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4, 4, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4, 4, 0, 0, 1,
    1, 2, 2, 2, 2, 2, 2, 2, 0, 0, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4, 4, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 4, 4, 0, 0, 0, 0, 4, 4, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 3, 3, 3, 3, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 4, 4, 4, 4, 4, 4, 4, 0, 0, 4, 4, 4, 4, 4, 0, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 3, 3, 0, 0, 0, 0, 3, 3, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 3, 3, 0, 0, 0, 0, 3, 3, 0, 0, 0, 1,
    1, 4, 4, 4, 4, 4, 4, 4, 0, 0, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 4, 4, 4, 4, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 3, 3, 0, 0, 0, 0, 3, 3, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 0, 0, 3, 3, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  ]

  # FOV = 60 degrees
  FOV = 60
  HALF_FOV = 30

  # The engines the B key rotates through. :ruby first so the demo opens on the
  # interpreter and the improvement is what you press for.
  BACKENDS = [:ruby, :spinel]

  # How long on_update asks to sleep between frames. The old 100ms was three
  # quarters of a frame on any machine, which hid everything else: the ray cast
  # was 7% of a frame even on mruby, so making it 49x faster moved nothing you
  # could see. At 33ms the frame is the work rather than the wait, and the
  # engine shows up as a frame rate.
  #
  # Only on the boards with the headroom for it. FmrbConst::BOARD is the
  # hardware identity an app gets ("tab5" / "naryav4" / "narya_v3" / "linux");
  # add a name here to opt another machine in. Note the Linux simulator reports
  # "linux" whatever it is simulating, so it keeps the slower frame.
  FAST_BOARDS = ["tab5"]
  FRAME_MS_FAST = 33
  FRAME_MS_SLOW = 100

  def initialize
    super()
    @input = {}

    # The ray loop and its trig tables belong to the gem now.
    @caster = nil
    @backend_idx = 0
    @last_us = 0
    @draw_us = 0
    @draw_count = 0
    @last_log_us = 0
    @update_us = 0
    @gap_us = 0
    @last_exit_us = 0
    @acc_update = 0
    @acc_gap = 0
    @tick_count = 0

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

    # Enemies: array of {x:, y:, alive:}  (fixed-point positions). One in each
    # hall, one in each corridor -- all cells checked open against the map
    # above, since a wall is a fine place to stand and a poor place to be shot.
    @enemies = [
      { x: 24 * CELL_SIZE + FP_HALF, y: 5 * CELL_SIZE + FP_HALF, alive: true },
      { x: 25 * CELL_SIZE + FP_HALF, y: 11 * CELL_SIZE + FP_HALF, alive: true },
      { x: 20 * CELL_SIZE + FP_HALF, y: 15 * CELL_SIZE + FP_HALF, alive: true },
      { x: 8 * CELL_SIZE + FP_HALF, y: 20 * CELL_SIZE + FP_HALF, alive: true },
      { x: 24 * CELL_SIZE + FP_HALF, y: 26 * CELL_SIZE + FP_HALF, alive: true },
    ]
  end

  def on_create
    Log.info("Raycaster on_create")
    @frame_ms = FAST_BOARDS.include?(FmrbConst::BOARD) ? FRAME_MS_FAST : FRAME_MS_SLOW
    Log.info("Raycaster board=#{FmrbConst::BOARD} frame=#{@frame_ms}ms")
    open_caster
    cast_rays
    draw_frame
  end

  # ---- Ray engine ----

  def backend
    BACKENDS[@backend_idx]
  end

  # Build the caster for the current backend and hand it the world. Falls
  # through to the next engine if this build has not got the one asked for,
  # rather than failing to start.
  def open_caster
    @caster.close if @caster
    @caster = nil
    tried = 0
    while tried < BACKENDS.size
      if Fmrb::Raycast.available?(backend)
        @caster = Fmrb::Raycast.new(backend: backend)
        @caster.set_map(WORLD_MAP, MAP_W, MAP_H)
        Log.info("Raycaster engine: #{backend}")
        return
      end
      @backend_idx = (@backend_idx + 1) % BACKENDS.size
      tried += 1
    end
    raise RuntimeError, "no raycast engine in this build"
  end

  def cast_rays
    # The rays stay packed inside the caster; read them with dist/wall/side.
    # Unpacking them into Hashes cost 40 objects a frame for nothing the
    # drawing code needed.
    @last_us = @caster.cast(@px, @py, @pa)
  end

  # ---- Helpers ----

  def fp_sin(deg)
    @caster.fp_sin(deg)
  end

  def fp_cos(deg)
    @caster.fp_cos(deg)
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
      when FmrbConst::GP_UP    then @input[:up]    = true
      when FmrbConst::GP_DOWN  then @input[:down]  = true
      when FmrbConst::GP_LEFT  then @input[:left]  = true
      when FmrbConst::GP_RIGHT then @input[:right] = true
      when FmrbConst::GP_CIRCLE then @shoot_pressed = true
      when FmrbConst::GP_START  then stop
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
      if ev[:character] == 32  # Space = Shoot
        @shoot_pressed = true
      elsif ev[:character] == 113  # q = Quit
        stop
      elsif ev[:character] == 98 || ev[:character] == 66  # b / B = next engine
        @backend_idx = (@backend_idx + 1) % BACKENDS.size
        open_caster
        @needs_draw = true
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
    # Split the frame into "what this method does" and "what happens between
    # calls" (Task.pass + _spin + _run_timers in FmrbApp#main_loop). The cast
    # and the draw together did not account for the measured frame time, and
    # guessing which side the rest was on is what this settles.
    t_enter = Fmrb::Raycast.micros
    if @last_exit_us > 0
      @gap_us = t_enter - @last_exit_us
      @acc_gap += @gap_us
    end
    @tick_count += 1

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
      cast_rays
      if shot
        try_shoot
      end
      t0 = Fmrb::Raycast.micros
      draw_frame
      @draw_us = Fmrb::Raycast.micros - t0
      @needs_draw = false
      # What a frame is actually made of, and what it adds up to. Counted in
      # drawn frames, not update ticks: the update runs whether or not anything
      # is redrawn, so keying the log off @frame_count almost never coincided
      # with a frame that did the work being reported.
      @draw_count += 1
      if (@draw_count % 32) == 0
        now = Fmrb::Raycast.micros
        span = now - @last_log_us
        @last_log_us = now
        # Every figure from this same frame. Reading @update_us here when it
        # was assigned on the way out printed the previous frame's total
        # against this frame's cast and draw, and the three stopped adding up.
        # Accumulated over the same 32 frames the span covers, so the parts
        # have to add up to the whole. Single-frame samples did not, and with
        # one sample each there was no telling whether the shortfall was a
        # frame that skipped the draw, time spent outside this task, or a
        # measurement that simply missed a stretch.
        Log.info("Raycaster frame: engine=#{backend} cast=#{@last_us}us " \
                 "draw=#{@draw_us}us rest=#{now - t_enter - @last_us - @draw_us}us " \
                 "gap=#{@gap_us}us avg_frame=#{span / 32}us | acc: " \
                 "update=#{@acc_update / 32}us gap=#{@acc_gap / 32}us " \
                 "ticks=#{@tick_count} draws=32")
        @acc_update = 0
        @acc_gap = 0
        @tick_count = 0
      end
    end
    @acc_update += Fmrb::Raycast.micros - t_enter
    @last_exit_us = Fmrb::Raycast.micros
    @frame_ms
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

  # ---- Enemies ----

  def try_shoot
    # Check if center ray (crosshair) hits an enemy
    # Find closest alive enemy near the center of view
    center_angle = @pa
    best_enemy = nil
    best_dist = 999999

    ei = -1
    en = @enemies.length
    while (ei += 1) < en
      e = @enemies[ei]
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
        wall_dist = @caster.dist(NUM_RAYS / 2)
        next if wall_dist > 0 && dist >= wall_dist   # wall is closer
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
    ei = -1
    en = @enemies.length
    while (ei += 1) < en
      e = @enemies[ei]
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
        wall_dist = @caster.dist(strip_idx)
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
    i = 0
    while i < NUM_RAYS
      dist = @caster.dist(i)
      wall = @caster.wall(i)
      side = @caster.side(i)

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
      i += 1
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
    ei = 0
    en = @enemies.length
    while ei < en
      alive_count += 1 if @enemies[ei][:alive]
      ei += 1
    end
    @gfx.draw_text(ox + 4, hud_y + 2, "SCORE:#{@score} ENEMY:#{alive_count}", C_HUD_TXT, C_HUD_BG)
    # The engine and what the last cast cost. This line is the demo: press B
    # and only these two fields change.
    @gfx.draw_text(ox + 4, hud_y + 11,
                   "[B]#{backend} cast:#{@last_us}us draw:#{@draw_us}us",
                   C_HUD_TXT, C_HUD_BG)

    # Mini-map (right side of viewport, every 4th frame to save draw calls)
    if (@frame_count % 4) == 0
      draw_minimap(vp_x + VP_W + 8, vp_y + 4)
    end

    draw_window_frame
    @gfx.present
  end

  # A window on the map around the player, not the whole thing. The map is 32x32
  # now: drawing all of it would be 1024 cells scanned and several hundred
  # fill_rects every time, which would grow the frame by more than the faster
  # ray engine takes out of it -- and at three pixels a cell it would not fit
  # beside the viewport on a Retro screen either. Cost here is fixed whatever
  # the map's size.
  MINIMAP_CELLS = 16
  MINIMAP_PX = 3

  def draw_minimap(mx0, my0)
    span = MINIMAP_CELLS
    cell_px = MINIMAP_PX

    # Centre on the player, then pull back inside the map so the window is
    # always full rather than half off the edge.
    cx0 = @px / CELL_SIZE - span / 2
    cy0 = @py / CELL_SIZE - span / 2
    cx0 = 0 if cx0 < 0
    cy0 = 0 if cy0 < 0
    cx0 = MAP_W - span if cx0 > MAP_W - span
    cy0 = MAP_H - span if cy0 > MAP_H - span

    @gfx.fill_rect(mx0, my0, span * cell_px, span * cell_px, C_BLACK)

    ry = 0
    while ry < span
      rx = 0
      while rx < span
        wall = WORLD_MAP[(cy0 + ry) * MAP_W + (cx0 + rx)]
        if wall > 0
          colors = WALL_COLORS[wall]
          c = colors ? colors[0] : C_WHITE
          @gfx.fill_rect(mx0 + rx * cell_px, my0 + ry * cell_px, cell_px, cell_px, c)
        end
        rx += 1
      end
      ry += 1
    end

    # Enemy dots (magenta), only the ones inside the window
    ei = -1
    en = @enemies.length
    while (ei += 1) < en
      e = @enemies[ei]
      next unless e[:alive]
      edx = e[:x] * cell_px / CELL_SIZE - cx0 * cell_px
      edy = e[:y] * cell_px / CELL_SIZE - cy0 * cell_px
      next if edx < 0 || edy < 0 || edx >= span * cell_px || edy >= span * cell_px
      @gfx.fill_rect(mx0 + edx, my0 + edy, 2, 2, C_ENEMY)
    end

    # Player dot (white)
    pdx = @px * cell_px / CELL_SIZE - cx0 * cell_px
    pdy = @py * cell_px / CELL_SIZE - cy0 * cell_px
    @gfx.fill_rect(mx0 + pdx - 1, my0 + pdy - 1, 3, 3, C_WHITE)
  end

  def on_destroy
    @caster.close if @caster
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
