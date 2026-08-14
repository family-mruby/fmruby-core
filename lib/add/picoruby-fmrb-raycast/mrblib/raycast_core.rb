# The raycaster's inner loop, in Ruby. One source, two engines.
#
# This exact file is what the :ruby and :spinel backends both run: mruby
# compiles it to bytecode, Spinel compiles it to C and runs it natively
# (spinel/raycast_entry.rb requires it). The only difference between the two
# numbers the app shows is the engine, which is the point of the demo
# (doc/raycast_spinel/plan.md).
#
# Why a raycaster is the right subject: every value here is a fixed-point
# integer. The FFT comparison had to reckon with double arithmetic being a
# software emulation on a single-precision FPU, which cost the Ruby engines
# more than the engines themselves did. Nothing here touches a float once the
# tables are built, so what the two numbers differ by IS the engine.
#
# How it is written matters as much as what it does (ruby_writing_constraints):
#   - while loops, not Integer#times: a block call per ray would be measured
#     instead of the ray
#   - the trig tables are built once in the constructor, and the map arrives
#     from outside, so nothing per-frame allocates
#   - the output is one byte String, reused between calls, packed by hand
#     (picoruby has no Array#pack)
#   - no parallel assignment, no bare top-level constants
#
# The map is NOT baked in. It arrives as a byte per cell, so the app owns its
# world and this stays a pure function of (map, player).
class RaycastCore
  FP_ONE    = 256   # fixed-point scale: 1.0 == one cell == 256
  CELL_SIZE = 256
  # Must equal the app's NUM_RAYS: this decides how many rays come back, the
  # app decides how wide each strip is drawn.
  NUM_RAYS  = 40
  FOV       = 60
  HALF_FOV  = 30
  MAX_STEPS = 24    # give up after this many grid cells

  # Per ray in the packed output: dist as int32 little-endian, then the wall
  # value and the side, one byte each. int32 because dist is the denominator of
  # VP_H * CELL_SIZE / dist and reaches six figures on a long corridor -- int16
  # would wrap and put a wall in the player's face.
  RAY_BYTES = 6

  def initialize(map_bytes, w, h, gen)
    @map = map_bytes
    @mw = w
    @mh = h
    @gen = gen

    # Degrees to fixed-point sine and cosine, once. A Math.sin in the ray loop
    # would make this a benchmark of Math.sin -- and would put a float on the
    # hot path, which is exactly what this demo is meant to avoid. The floats
    # here never leave the constructor.
    pi = 3.141592653589793
    @sin_tbl = []
    @cos_tbl = []
    deg = 0
    while deg < 360
      rad = deg * pi / 180.0
      # Bare Math, not ::Math. Spinel resolves the plain name (fft_core.rb
      # relies on it) but types the ::-qualified call as unknown and plants a
      # NoMethodError for the .to_i at that point -- source that compiles
      # without a word and dies on the first call.
      @sin_tbl << (Math.sin(rad) * FP_ONE).to_i
      @cos_tbl << (Math.cos(rad) * FP_ONE).to_i
      deg += 1
    end

    # Reused every frame rather than rebuilt: 25 rays of 6 bytes, and the
    # caller copies out of it before the next call.
    @out = "\x00" * (NUM_RAYS * RAY_BYTES)
  end

  # The generation the app gave this map. The Spinel entry keeps the core in a
  # global between calls and asks the object itself whether its map is still
  # the current one -- see spinel/raycast_entry.rb for why the key has to live
  # on the object rather than in a second global.
  def map_gen
    @gen
  end

  def map_w
    @mw
  end

  def map_h
    @mh
  end

  # Outside the map is solid: a ray that escapes the world stops at the edge
  # rather than running to MAX_STEPS.
  def map_at(mx, my)
    return 1 if mx < 0 || mx >= @mw || my < 0 || my >= @mh
    v = @map.getbyte(my * @mw + mx)
    return 1 if v.nil?
    v
  end

  # Degrees into 0..359. Written out rather than left to `%` because a negative
  # ray angle indexing the table would be a silent wrong answer, and this is
  # the one place the two engines could have disagreed.
  def norm_deg(d)
    a = d % 360
    a += 360 if a < 0
    a
  end

  # One ray, by DDA over the grid. Every comparison is a cross-multiplication
  # so the loop never divides: `dx/cos < dy/sin` becomes `dx*sin < dy*cos`.
  # Writes dist/wall/side into `out` at `off`.
  def cast_ray_into(out, off, px, py, pa, angle)
    a = norm_deg(angle)
    sin_a = @sin_tbl[a]
    cos_a = @cos_tbl[a]

    # A zero component would divide by zero below; one fixed-point unit is far
    # under a pixel at any distance the player can see.
    cos_a = 1 if cos_a == 0
    sin_a = 1 if sin_a == 0

    map_x = px / CELL_SIZE
    map_y = py / CELL_SIZE

    step_x = cos_a > 0 ? 1 : -1
    step_y = sin_a > 0 ? 1 : -1

    if cos_a > 0
      dx_to_edge = (map_x + 1) * CELL_SIZE - px
    else
      dx_to_edge = px - map_x * CELL_SIZE
      dx_to_edge = 1 if dx_to_edge == 0
    end

    if sin_a > 0
      dy_to_edge = (map_y + 1) * CELL_SIZE - py
    else
      dy_to_edge = py - map_y * CELL_SIZE
      dy_to_edge = 1 if dy_to_edge == 0
    end

    t_x_num = dx_to_edge
    t_x_den = cos_a < 0 ? -cos_a : cos_a
    t_y_num = dy_to_edge
    t_y_den = sin_a < 0 ? -sin_a : sin_a

    side = 0     # 0 = hit a vertical wall face, 1 = horizontal
    hit = 0
    depth = 0

    step_n = 0
    while step_n < MAX_STEPS
      if t_x_num * t_y_den < t_y_num * t_x_den
        map_x += step_x
        depth = t_x_num * FP_ONE / t_x_den
        t_x_num += CELL_SIZE
        side = 0
      else
        map_y += step_y
        depth = t_y_num * FP_ONE / t_y_den
        t_y_num += CELL_SIZE
        side = 1
      end

      hit = map_at(map_x, map_y)
      break if hit > 0
      step_n += 1
    end

    # Fisheye: the distance that matters for the wall's height on screen is the
    # one perpendicular to the view, not along the ray.
    diff = norm_deg(angle - pa)
    diff -= 360 if diff > 180
    cos_diff = @cos_tbl[norm_deg(diff)]
    cos_diff = 1 if cos_diff == 0
    depth = depth * cos_diff / FP_ONE

    depth = -depth if depth < 0
    depth = 1 if depth == 0

    out.setbyte(off, depth & 0xFF)
    out.setbyte(off + 1, (depth >> 8) & 0xFF)
    out.setbyte(off + 2, (depth >> 16) & 0xFF)
    out.setbyte(off + 3, (depth >> 24) & 0xFF)
    out.setbyte(off + 4, hit & 0xFF)
    out.setbyte(off + 5, side & 0xFF)
    nil
  end

  # A whole frame's worth of rays, packed. This is the call the demo times.
  def cast_packed(px, py, pa)
    out = @out
    i = 0
    while i < NUM_RAYS
      angle = pa - HALF_FOV + (i * FOV / NUM_RAYS)
      cast_ray_into(out, i * RAY_BYTES, px, py, pa, angle)
      i += 1
    end
    out
  end
end
