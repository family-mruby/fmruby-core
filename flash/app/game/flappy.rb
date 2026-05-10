# Flappy Sprite - Flappy Bird style demo with BMP sprites
# Space/Up to flap, auto-scrolling pipes

class SpriteTestApp < FmrbApp
  GRAVITY = 1
  FLAP_POWER = -4
  PIPE_SPEED = 2
  PIPE_GAP = 48  # 3 tiles gap
  SPRITE_SIZE = 16

  def on_create
    @ox = @user_area_x0
    @oy = @user_area_y0
    @w = @user_area_width
    @h = @user_area_height
    @bird_x = 30
    @bird_y = @h / 2
    @bird_vy = 0
    @score = 0
    @game_over = false
    @ready = false
    @audio = FmrbAudio.new(self)
    @crash_off_in = nil

    # Transfer sprite BMP files to graphics-audio
    src_dir = "/usr/share/sprites"
    cache_dir = "/cache/app/sprite_test"
    files = ["bird_up.bmp", "bird_down.bmp", "pipe.bmp"]
    files.each_with_index do |name, i|
      @gfx.fill_rect(@ox, @oy, @w, @h, FmrbGfx::BLACK)
      @gfx.draw_text(@ox + 4, @oy + 4, "Loading sprites...", FmrbGfx::WHITE)
      @gfx.draw_text(@ox + 4, @oy + 18, "#{name} (#{i+1}/#{files.size})", FmrbGfx::CYAN)
      draw_window_frame
      @gfx.present
      cache_path = "#{cache_dir}/#{name}"
      status = @gfx.file_status(cache_path)
      unless status[:exists]
        @gfx.transfer_file("#{src_dir}/#{name}", dest: cache_path)
      end
    end
    Log.info("FlappySprite: files transferred")

    # Bird frames from BMP
    @bird_frames = []
    ["bird_up.bmp", "bird_down.bmp"].each do |name|
      img = SpriteImage.new(@gfx, width: SPRITE_SIZE, height: SPRITE_SIZE,
                            transparent_color: 0, use_transparent: true)
      img.load_bmp("#{cache_dir}/#{name}")
      @bird_frames << img
    end

    @bird = SpriteInstance.new(@gfx, @bird_frames,
                              x: @ox + @bird_x, y: @oy + @bird_y, z: 2)

    # Pipe image from BMP
    @pipe_img = SpriteImage.new(@gfx, width: SPRITE_SIZE, height: SPRITE_SIZE)
    @pipe_img.load_bmp("#{cache_dir}/pipe.bmp")

    # Pipe pairs
    @pipes = []
    i = 0
    while i < 3
      spawn_pipe(@w + i * 70)
      i += 1
    end

    @frame = 0
    Log.info("FlappySprite: setup complete")
    draw_screen
  end

  def on_update
    tick_audio
    return 50 if @game_over
    return 50 unless @ready

    @bird_vy += GRAVITY
    @bird_y += @bird_vy

    if @bird_y < 0
      @bird_y = 0
      @bird_vy = 0
    end
    if @bird_y > @h - SPRITE_SIZE
      @bird_y = @h - SPRITE_SIZE
      trigger_game_over
    end

    @bird.move(@ox + @bird_x, @oy + @bird_y)

    @frame += 1
    @bird.frame = (@frame / 3) % 2

    pi = 0
    pn = @pipes.length
    while pi < pn
      pipe = @pipes[pi]
      pipe[:x] -= PIPE_SPEED
      segs = pipe[:segments]
      seg_ys = pipe[:seg_ys]
      si = 0
      sn = segs.length
      while si < sn
        segs[si].move(@ox + pipe[:x], @oy + seg_ys[si])
        si += 1
      end

      if !pipe[:scored] && pipe[:x] + SPRITE_SIZE < @bird_x
        @score += 1
        pipe[:scored] = true
      end

      if pipe[:x] < @bird_x + SPRITE_SIZE && pipe[:x] + SPRITE_SIZE > @bird_x
        gap_top = pipe[:gap_y]
        gap_bottom = pipe[:gap_y] + PIPE_GAP
        if @bird_y < gap_top || @bird_y + SPRITE_SIZE > gap_bottom
          trigger_game_over
        end
      end
      pi += 1
    end

    pi = 0
    while pi < pn
      pipe = @pipes[pi]
      recycle_pipe(pipe) if pipe[:x] + SPRITE_SIZE < 0
      pi += 1
    end

    draw_screen
    50
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0
      if keycode == 82 || character == 32
        if @game_over
          restart
        else
          @ready = true
          @bird_vy = FLAP_POWER
          play_flap
        end
      end
    end
  end

  def on_destroy
    if @audio
      @audio.note_off(0)
      @audio.note_off(3)
    end
    @bird.destroy if @bird
    @bird_frames.each { |f| f.destroy } if @bird_frames
    @pipes.each { |pipe| pipe[:segments].each { |s| s.destroy } } if @pipes
    @pipe_img.destroy if @pipe_img
  end

  private

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

  def random_gap_y
    # Snap to tile grid: gap starts at tile boundary
    max_tile = (@h - PIPE_GAP) / SPRITE_SIZE - 1
    tile = 1 + rand(max_tile)
    tile * SPRITE_SIZE
  end

  def spawn_pipe(x)
    gap_y = random_gap_y
    segments = []
    seg_ys = []

    y = gap_y - SPRITE_SIZE
    while y >= 0
      inst = SpriteInstance.new(@gfx, @pipe_img, x: @ox + x, y: @oy + y, z: 1)
      segments << inst
      seg_ys << y
      y -= SPRITE_SIZE
    end

    y = gap_y + PIPE_GAP
    while y < @h
      inst = SpriteInstance.new(@gfx, @pipe_img, x: @ox + x, y: @oy + y, z: 1)
      segments << inst
      seg_ys << y
      y += SPRITE_SIZE
    end

    @pipes << { x: x, gap_y: gap_y, segments: segments, seg_ys: seg_ys, scored: false }
  end

  def recycle_pipe(pipe)
    max_x = @pipes.map { |p| p[:x] }.max
    pipe[:x] = max_x + 70
    pipe[:gap_y] = random_gap_y
    pipe[:scored] = false

    idx = 0
    y = pipe[:gap_y] - SPRITE_SIZE
    while y >= 0 && idx < pipe[:segments].size
      pipe[:seg_ys][idx] = y
      pipe[:segments][idx].move(@ox + pipe[:x], @oy + y)
      pipe[:segments][idx].visible = true
      idx += 1
      y -= SPRITE_SIZE
    end
    y = pipe[:gap_y] + PIPE_GAP
    while y < @h && idx < pipe[:segments].size
      pipe[:seg_ys][idx] = y
      pipe[:segments][idx].move(@ox + pipe[:x], @oy + y)
      pipe[:segments][idx].visible = true
      idx += 1
      y += SPRITE_SIZE
    end
    while idx < pipe[:segments].size
      pipe[:segments][idx].visible = false
      idx += 1
    end
  end

  def restart
    @bird_y = @h / 2
    @bird_vy = 0
    @score = 0
    @game_over = false
    @pipes.each_with_index do |pipe, i|
      pipe[:x] = @w + i * 70
      pipe[:gap_y] = random_gap_y
      pipe[:scored] = false
      idx = 0
      y = pipe[:gap_y] - SPRITE_SIZE
      while y >= 0 && idx < pipe[:segments].size
        pipe[:seg_ys][idx] = y
        pipe[:segments][idx].move(@ox + pipe[:x], @oy + y)
        pipe[:segments][idx].visible = true
        idx += 1
        y -= SPRITE_SIZE
      end
      y = pipe[:gap_y] + PIPE_GAP
      while y < @h && idx < pipe[:segments].size
        pipe[:seg_ys][idx] = y
        pipe[:segments][idx].move(@ox + pipe[:x], @oy + y)
        pipe[:segments][idx].visible = true
        idx += 1
        y += SPRITE_SIZE
      end
      while idx < pipe[:segments].size
        pipe[:segments][idx].visible = false
        idx += 1
      end
    end
  end

  def draw_screen
    @gfx.fill_rect(@ox, @oy, @w, @h, FmrbGfx::CYAN)
    @gfx.draw_text(@ox + 2, @oy + 2, @score.to_s, FmrbGfx::BLACK)

    if !@ready
      @gfx.draw_text(@ox + @w/2 - 30, @oy + @h/2, "Press Up", FmrbGfx::BLACK)
    elsif @game_over
      @gfx.draw_text(@ox + @w/2 - 30, @oy + @h/2, "GAME OVER", FmrbGfx::RED)
      @gfx.draw_text(@ox + @w/2 - 30, @oy + @h/2 + 12, "Up to retry", FmrbGfx::BLACK)
    end

    draw_window_frame
    @gfx.present
  end
end

begin
  app = SpriteTestApp.new
  app.start
rescue => e
  Log.error("SpriteTest: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
