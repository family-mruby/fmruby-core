# Flappy - Flappy Bird style demo with BMP sprites
# Space/Up to flap, auto-scrolling pipes
#
# Layout: the user area is split into a score band, a sky and a ground strip.
# Sprites are composited above everything the canvas drew (the window frame
# included), so the sky rect is installed as the sprite clip: pipes and bird
# are cut at its edges instead of spilling onto the score, the ground or the
# frame. The ground and the frame are drawn once - the canvas draw buffer
# keeps them, since sprites land on the render buffer, not on it.

class FlappyApp < FmrbApp
  GRAVITY      = 1
  FLAP_POWER   = -4
  PIPE_SPEED   = 2
  TILE         = 16
  GAP_TILES    = 3
  PIPE_GAP     = GAP_TILES * TILE
  PIPE_SPACING = 70
  PIPE_COUNT   = 3
  BIRD_X       = 30
  SCORE_H      = 10

  # RGB332 (RRRGGGBB). Cyan at the top washing out towards the horizon, so
  # the scenery reads as depth. Cyan rather than blue because the pipe and the
  # bird are blue (0x03 with a 0x17 highlight): a blue sky would swallow them.
  SKY_BANDS    = 4
  SKY_COLORS   = [0x1F, 0x5F, 0x9F, 0xDF]
  MOUNT_FAR    = 0x6A       # hazy purple-blue, distant range
  MOUNT_NEAR   = 0x0C       # dark green hills, carrying into the grass below
  SNOW_COLOR   = 0xFF
  CLOUD_COLOR  = 0xFF
  BAND_COLOR   = 0x00       # score band background
  DIRT_COLOR   = 0xA8       # under the grass row
  PANEL_COLOR  = 0x00       # message backdrop
  PANEL_BORDER = 0x92
  TEXT_SHADOW  = 0x49

  def on_create
    @bird_vy = 0
    @score = 0
    @game_over = false
    @ready = false
    @audio = FmrbAudio.new(self)
    @crash_off_in = nil
    @pipes = []

    setup_layout
    load_sprites
    @bird = SpriteInstance.new(@gfx, @bird_frames,
                               x: @ox + BIRD_X, y: @field_y, z: 2)
    build_pipes
    reset_game
    draw_ready_panel
    draw_score
    @gfx.present

    Log.info("Flappy: setup complete")
  end

  def on_update
    tick_audio
    if @game_over
      # Drawn once on the transition, then the frame is left as composited:
      # nothing moves any more, so there is nothing to present.
      unless @over_drawn
        draw_game_over
        @over_drawn = true
        @gfx.present
      end
      return 50
    end
    return 50 unless @ready

    @bird_vy += GRAVITY
    @bird_y += @bird_vy

    if @bird_y < 0
      @bird_y = 0
      @bird_vy = 0
    end
    if @bird_y > @sky_h - TILE
      @bird_y = @sky_h - TILE
      trigger_game_over
    end

    @bird.move(@ox + BIRD_X, @field_y + @bird_y)
    @frame += 1
    @bird.frame = (@frame / 3) % 2

    pi = 0
    pn = @pipes.length
    while pi < pn
      pipe = @pipes[pi]
      pipe[:x] -= PIPE_SPEED
      move_pipe(pipe)

      if !pipe[:scored] && pipe[:x] + TILE < BIRD_X
        @score += 1
        pipe[:scored] = true
      end

      if pipe[:x] < BIRD_X + TILE && pipe[:x] + TILE > BIRD_X
        gap_top = pipe[:gap_y]
        gap_bottom = pipe[:gap_y] + PIPE_GAP
        if @bird_y < gap_top || @bird_y + TILE > gap_bottom
          trigger_game_over
        end
      end
      pi += 1
    end

    pi = 0
    while pi < pn
      pipe = @pipes[pi]
      recycle_pipe(pipe) if pipe[:x] + TILE < 0
      pi += 1
    end

    draw_score
    @gfx.present
    50
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0
      if keycode == FmrbConst::KEY_UP || character == 32
        # The same key starts the run, flaps, and retries after a crash: the
        # retry press counts as the first flap so play resumes immediately.
        reset_game if @game_over
        # Wipe the title panel on the first flap and bring back the sprites it
        # hid. The background is only redrawn on transitions like this one.
        unless @ready
          draw_background
          show_sprites
        end
        @ready = true
        @bird_vy = FLAP_POWER
        play_flap
      end
    end
  end

  # The window can be resized while playing: everything derives from the user
  # area, so recompute the layout and rebuild the pipes for the new tile count.
  def on_resize(width, height)
    setup_layout
    build_pipes
    reset_game
    draw_ready_panel
    draw_score
    @gfx.present
  end

  def on_destroy
    if @audio
      @audio.note_off(0)
      @audio.note_off(3)
    end
    @bird.destroy if @bird
    @bird_frames.each { |f| f.destroy } if @bird_frames
    destroy_pipes
    @pipe_img.destroy if @pipe_img
    @grass_img.destroy if @grass_img
  end

  private

  # ---- layout ----

  # Score band on top, then a whole number of tile rows of sky, then the
  # ground. The ground takes the remainder so the sky is always tile-aligned
  # and a pipe never ends up half-drawn at the bottom edge.
  def setup_layout
    @ox = @user_area_x0
    @oy = @user_area_y0
    @w  = @user_area_width
    @h  = @user_area_height

    @field_y = @oy + SCORE_H
    rows = (@h - SCORE_H) / TILE
    rows = 2 if rows < 2
    @sky_rows = rows - 1                    # bottom row belongs to the ground
    @sky_h    = @sky_rows * TILE
    @ground_y = @field_y + @sky_h
    @ground_h = @h - SCORE_H - @sky_h
    @segs_per_pipe = @sky_rows - GAP_TILES
    @segs_per_pipe = 1 if @segs_per_pipe < 1

    # Keep the sprites inside the sky. Without this they would cover the score
    # band, the ground and the window frame the base class drew.
    @gfx.set_sprite_clip(@ox, @field_y, @w, @sky_h)
  end

  # ---- sprites ----

  def load_sprites
    src_dir = "/usr/share/sprites"
    cache_dir = "/cache/app/flappy"
    files = ["bird_up.bmp", "bird_down.bmp", "pipe.bmp", "grass.bmp"]
    files.each_with_index do |name, i|
      @gfx.fill_rect(@ox, @oy, @w, @h, FmrbGfx::BLACK)
      @gfx.draw_text(@ox + 4, @oy + 4, "Loading sprites...", FmrbGfx::WHITE)
      @gfx.draw_text(@ox + 4, @oy + 18, "#{name} (#{i + 1}/#{files.size})",
                     FmrbGfx::CYAN)
      draw_window_frame
      @gfx.present
      @gfx.sync_file("#{src_dir}/#{name}", dest: "#{cache_dir}/#{name}")
    end
    Log.info("Flappy: files transferred")

    @bird_frames = []
    ["bird_up.bmp", "bird_down.bmp"].each do |name|
      img = SpriteImage.new(@gfx, width: TILE, height: TILE,
                            transparent_color: 0, use_transparent: true)
      img.load_bmp("#{cache_dir}/#{name}")
      @bird_frames << img
    end

    @pipe_img = SpriteImage.new(@gfx, width: TILE, height: TILE)
    @pipe_img.load_bmp("#{cache_dir}/pipe.bmp")

    @grass_img = SpriteImage.new(@gfx, width: TILE, height: TILE)
    @grass_img.load_bmp("#{cache_dir}/grass.bmp")
  end

  # ---- pipes ----

  # Every pipe has the same number of segments (the column is full height
  # minus the fixed gap), so the instances are created once and only moved
  # afterwards - no visibility toggling, no allocation while playing.
  def build_pipes
    destroy_pipes
    @pipes = []
    i = 0
    while i < PIPE_COUNT
      segments = []
      seg_ys = []
      s = 0
      while s < @segs_per_pipe
        segments << SpriteInstance.new(@gfx, @pipe_img,
                                       x: @ox + @w, y: @field_y, z: 1)
        seg_ys << 0
        s += 1
      end
      @pipes << { x: @w, gap_y: TILE, segments: segments,
                  seg_ys: seg_ys, scored: false }
      i += 1
    end
  end

  def destroy_pipes
    return unless @pipes
    @pipes.each { |pipe| pipe[:segments].each { |s| s.destroy } }
    @pipes = []
  end

  # Gap top, snapped to the tile grid, leaving at least one tile of pipe
  # above the gap and one below it.
  def random_gap_y
    max_tile = @sky_rows - GAP_TILES - 1
    max_tile = 1 if max_tile < 1
    (1 + rand(max_tile)) * TILE
  end

  # Recompute the segment offsets for the current gap and move them into place.
  def place_pipe(pipe)
    segs = pipe[:segments]
    seg_ys = pipe[:seg_ys]
    n = segs.length
    idx = 0

    y = 0
    while y + TILE <= pipe[:gap_y] && idx < n
      seg_ys[idx] = y
      idx += 1
      y += TILE
    end
    y = pipe[:gap_y] + PIPE_GAP
    while y + TILE <= @sky_h && idx < n
      seg_ys[idx] = y
      idx += 1
      y += TILE
    end
    # Park any leftover segment on the last row: the counts match for every
    # gap the generator picks, so this only guards odd window sizes.
    while idx < n
      seg_ys[idx] = @sky_h - TILE
      idx += 1
    end

    move_pipe(pipe)
  end

  def move_pipe(pipe)
    segs = pipe[:segments]
    seg_ys = pipe[:seg_ys]
    x = @ox + pipe[:x]
    si = 0
    sn = segs.length
    while si < sn
      segs[si].move(x, @field_y + seg_ys[si])
      si += 1
    end
  end

  def recycle_pipe(pipe)
    max_x = @pipes[0][:x]
    @pipes.each { |p| max_x = p[:x] if p[:x] > max_x }
    pipe[:x] = max_x + PIPE_SPACING
    pipe[:gap_y] = random_gap_y
    pipe[:scored] = false
    place_pipe(pipe)
  end

  # ---- game state ----

  def reset_game
    @bird_y = (@sky_h - TILE) / 2
    @bird_vy = 0
    @score = 0
    @game_over = false
    @over_drawn = false
    @ready = false
    @frame = 0
    @bird.move(@ox + BIRD_X, @field_y + @bird_y)
    @bird.frame = 0

    @pipes.each_with_index do |pipe, i|
      pipe[:x] = @w + i * PIPE_SPACING
      pipe[:gap_y] = random_gap_y
      pipe[:scored] = false
      place_pipe(pipe)
    end

    draw_background
  end

  # ---- drawing ----

  # Sky, mountains, clouds, ground and window frame. Drawn on a layout change
  # and on the state transitions that have to wipe a panel - never per frame.
  # Sprites are composited onto the render buffer, so they never damage what
  # the canvas holds, and the scenery survives untouched underneath them.
  def draw_background
    draw_sky
    draw_mountains
    draw_clouds
    draw_ground
    draw_window_frame
  end

  # Four horizontal bands, deepest at the top. A per-scanline gradient would
  # cost one RPC per line; at this size the banding reads as a stylised sky.
  def draw_sky
    band = @sky_h / SKY_BANDS
    i = 0
    while i < SKY_BANDS
      y = @field_y + i * band
      h = (i == SKY_BANDS - 1) ? (@field_y + @sky_h - y) : band
      @gfx.fill_rect(@ox, y, @w, h, SKY_COLORS[i])
      i += 1
    end
  end

  # Two ranges sitting on the horizon: a hazy far one with snow caps, then a
  # nearer darker one overlapping it. Peaks are spaced off the playfield width
  # so they follow a window resize.
  def draw_mountains
    base = @ground_y
    far_h = @sky_h / 3
    far_h = 20 if far_h < 20
    near_h = far_h * 2 / 3

    i = 0
    while i < 3
      cx = @ox + @w * (2 * i + 1) / 6
      half = far_h * 3 / 4
      @gfx.fill_triangle(cx, base - far_h, cx - half, base, cx + half, base,
                         MOUNT_FAR)
      # Snow cap: the top fifth of the same triangle.
      cap = far_h / 5
      cap_half = half / 5
      @gfx.fill_triangle(cx, base - far_h, cx - cap_half, base - far_h + cap,
                         cx + cap_half, base - far_h + cap, SNOW_COLOR)
      i += 1
    end

    i = 0
    while i < 2
      cx = @ox + @w * (2 * i + 1) / 4
      half = near_h
      @gfx.fill_triangle(cx, base - near_h, cx - half, base, cx + half, base,
                         MOUNT_NEAR)
      i += 1
    end
  end

  # Three lumps per cloud, in the upper half where the pipes leave room.
  def draw_clouds
    draw_cloud(@ox + @w / 5, @field_y + @sky_h / 5, 9)
    draw_cloud(@ox + @w * 7 / 10, @field_y + @sky_h / 3, 7)
    draw_cloud(@ox + @w * 9 / 10, @field_y + @sky_h / 8, 6)
  end

  def draw_cloud(cx, cy, r)
    @gfx.fill_circle(cx, cy, r, CLOUD_COLOR)
    @gfx.fill_circle(cx - r, cy + r / 3, r * 2 / 3, CLOUD_COLOR)
    @gfx.fill_circle(cx + r, cy + r / 3, r * 3 / 4, CLOUD_COLOR)
    @gfx.fill_rect(cx - r, cy, r * 2, r, CLOUD_COLOR)
  end

  def draw_ground
    return if @ground_h <= 0
    @gfx.fill_rect(@ox, @ground_y, @w, @ground_h, DIRT_COLOR)
    # Partial tiles at the right edge and a short ground strip are cut by the
    # source rect, not by the canvas: a full tile would reach past the user
    # area and land on the window border.
    h = (@ground_h < TILE) ? @ground_h : TILE
    x = 0
    while x < @w
      w = @w - x
      w = TILE if w > TILE
      @gfx.draw_tile(@grass_img.id, 0, 0, w, h,
                     dst_x: @ox + x, dst_y: @ground_y)
      x += TILE
    end
  end

  def draw_score
    @gfx.fill_rect(@ox, @oy, @w, SCORE_H, BAND_COLOR)
    @gfx.draw_text(@ox + 2, @oy + 1, "SCORE #{@score}", FmrbGfx::WHITE)
  end

  def draw_ready_panel
    panel(28) do |py|
      draw_center_text("FLAPPY", py + 2, FmrbGfx::WHITE, 2)
      draw_center_text("Press Up to start", py + 20, FmrbGfx::WHITE, 1)
    end
  end

  def draw_game_over
    panel(30) do |py|
      draw_center_text("GAME OVER", py + 2, FmrbGfx::RED, 2)
      draw_center_text("Up to retry", py + 21, FmrbGfx::WHITE, 1)
    end
  end

  # Message backdrop. The scenery behind it is busy, so the text needs solid
  # ground - and the sprites have to go, because they are composited above
  # everything the canvas draws and would otherwise sit on top of the panel.
  # show_sprites brings them back when play resumes.
  def panel(h)
    hide_sprites
    py = @field_y + (@sky_h - h) / 2
    @gfx.fill_round_rect(@ox + 6, py - 4, @w - 12, h + 8, 4, PANEL_BORDER)
    @gfx.fill_round_rect(@ox + 7, py - 3, @w - 14, h + 6, 4, PANEL_COLOR)
    yield py
  end

  def hide_sprites
    set_sprites_visible(false)
  end

  def show_sprites
    set_sprites_visible(true)
  end

  def set_sprites_visible(on)
    @bird.visible = on if @bird
    return unless @pipes
    @pipes.each do |pipe|
      pipe[:segments].each { |s| s.visible = on }
    end
  end

  # Centred, and bold by overdrawing one pixel to the right - the font has no
  # bold face - over a dark shadow that lifts it off the panel.
  def draw_center_text(text, y, color, size = 1)
    @gfx.set_text_size(size) unless size == 1
    x = @ox + (@w - @gfx.text_width(text)) / 2
    if size > 1
      @gfx.draw_text(x + 1, y + 1, text, TEXT_SHADOW)
      @gfx.draw_text(x, y, text, color)
      @gfx.draw_text(x + 1, y, text, color)
    else
      @gfx.draw_text(x, y, text, color)
    end
    @gfx.set_text_size(1) unless size == 1
  end

  # ---- audio ----

  # Up-sweep blip on pulse-1. Sweep auto-silences the channel so we
  # only emit one note_on message per flap (no scheduled note_off),
  # keeping input responsiveness. period=0 (60Hz update), shift=2
  # (25% per step) yields ~160ms snappy chirp before muting.
  # sweep byte: enable | (period<<4) | negate(0x08=up) | shift
  FLAP_SWEEP = 0x80 | (0 << 4) | 0x08 | 2

  def play_flap
    return unless @audio
    @audio.note_on(0, 880, 8, 1, FLAP_SWEEP)
  end

  # Noise burst + low pulse for the crash. Game-over so input-latency
  # no longer matters; keep scheduled note_off for a clean tail.
  def trigger_game_over
    return if @game_over
    @game_over = true
    return unless @audio
    @audio.note_on(0, 196, 12, 0, 0)  # G3 thud
    @audio.note_on(3, 0, 14, 0, 0)    # noise rumble
    @crash_off_in = 8                 # ~400ms at 50ms ticks
  end

  def tick_audio
    return unless @audio
    if @crash_off_in
      @crash_off_in -= 1
      if @crash_off_in <= 0
        @audio.note_off(0)
        @audio.note_off(3)
        @crash_off_in = nil
      end
    end
  end
end

begin
  app = FlappyApp.new
  app.start
rescue => e
  Log.error("Flappy: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
