# Family mruby RPG Demo
#
# Layout:
#   - 320x240 fullscreen
#   - Left 176x176 viewport onto a 64x64 tile world (Dragon-Quest-1 style)
#   - Right 144x240 status panel (position, last event)
#
# Terrain tiles (see generate_tiles.rb / generate_world.rb):
#   0 plain  1 forest  2 desert  3 sea  4 hill  5 mountain  6 cave  7 castle
# Sea and mountain are not walkable. Cave / castle are walkable and trigger
# named events. The walkable_tiles array is read from world.map.json.
#
# Player sheet is one BMP (128x16) with 8 frames laid out horizontally:
#   0 down_0  1 down_1  2 up_0  3 up_1  4 left_0  5 left_1  6 right_0  7 right_1
# The sheet is loaded into one SpriteImage, then split into eight 16x16
# SpriteImage frames so SpriteInstance#frame= can toggle the walking pose.

class RpgDemoApp < FmrbApp
  TILE              = 16
  MAP_ORIGIN_X      = 16
  MAP_ORIGIN_Y      = 16
  VIEWPORT_W        = 176
  VIEWPORT_H        = 176
  STATUS_X          = MAP_ORIGIN_X + VIEWPORT_W
  STATUS_W          = 320 - STATUS_X

  SLIDE_STEPS       = 4
  SLIDE_PX_PER_STEP = TILE / SLIDE_STEPS
  FRAME_MS          = 33
  # Idle tick: doubles as both the foot-tap cadence and the input-poll
  # cadence (key_down still queued by the host, but on_update only runs
  # after each _spin completes). Keep short so held keys feel responsive.
  IDLE_ANIM_MS      = 60

  # Frame indices into @player_frames. [stand, step] per direction.
  DIR_FRAMES = {
    :down  => [0, 1],
    :up    => [2, 3],
    :left  => [4, 5],
    :right => [6, 7],
  }

  PLAYER_FRAME_NAMES = [
    "player_down_0.bmp",  "player_down_1.bmp",
    "player_up_0.bmp",    "player_up_1.bmp",
    "player_left_0.bmp",  "player_left_1.bmp",
    "player_right_0.bmp", "player_right_1.bmp",
  ]

  APP_DIR    = "/app/game/rpg_demo"
  MAP_PATH   = "#{APP_DIR}/world.map.json"
  SHEET_SRC  = "#{APP_DIR}/world.bmp"
  BGM_SRC    = "#{APP_DIR}/bgm.fmsq"
  CACHE_DIR  = "/cache/app/rpg_demo"

  BGM_SLOT      = 0
  SE_TRI_CH     = 2          # APU triangle channel (FMRB_APU_CH_TRIANGLE)
  SE_BUMP_FREQ  = 90         # ~kick-drum range
  BUMP_SE_TICKS = 2          # short thump, auto-silenced after this many ticks

  def on_create
    splash("Loading map...")
    @map = TileMap.new(MAP_PATH)
    Log.info("Map loaded: #{@map.width}x#{@map.height}, sheet=#{@map.tilesheet_path}")

    splash("Transferring assets...")
    transfer_assets

    @map_w = @map.width  * TILE
    @map_h = @map.height * TILE

    # Modern (P4) has a PPA-composited display with torus canvas viewports:
    # keep a small ring-buffer canvas (viewport + 1 tile of margin), stamp
    # only the tiles that newly enter the visible range as the camera moves,
    # and scroll with set_viewport (a register update) instead of
    # re-stamping ~150 tiles per frame. Retro keeps the classic per-frame
    # tile redraw below.
    @hw_scroll = (FmrbConst::CHIP_MODEL == "ESP32-P4")

    if @hw_scroll
      buf_tiles = VIEWPORT_W / TILE + 1   # 12x12 tiles for the 176px viewport
      @map_gfx = create_canvas_gfx(width: buf_tiles * TILE,
                                   height: buf_tiles * TILE)
      @sheet = TileSheet.new(@map_gfx, cache_path_for(SHEET_SRC),
                             cols: @map.tilesheet_cols,
                             tile_size: @map.tile_size)
      @ring = TileRing.new(@map, @sheet, tiles_w: buf_tiles, tiles_h: buf_tiles)
    else
      @sheet = TileSheet.new(@gfx, cache_path_for(SHEET_SRC),
                             cols: @map.tilesheet_cols,
                             tile_size: @map.tile_size)
    end

    load_player_frames

    @player_tx = @map.spawn_x
    @player_ty = @map.spawn_y
    @player_px = @player_tx * TILE
    @player_py = @player_ty * TILE

    @view_x = clamp(@player_px + TILE / 2 - VIEWPORT_W / 2, 0, @map_w - VIEWPORT_W)
    @view_y = clamp(@player_py + TILE / 2 - VIEWPORT_H / 2, 0, @map_h - VIEWPORT_H)

    @slide_step = nil
    @slide_dx = @slide_dy = 0
    @slide_target_tx = @player_tx
    @slide_target_ty = @player_ty
    @dir = :down
    @anim_step = 0
    @idle_ticks = 0
    @held_dx = nil
    @held_dy = nil
    @bump_off_in = nil
    @last_event = @map.event_at(@player_tx, @player_ty)

    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    if @hw_scroll
      # Player rides the map canvas: coordinates are viewport-relative
      @player = SpriteInstance.new(@map_gfx, @player_frames,
                                   x: @player_px - @view_x,
                                   y: @player_py - @view_y, z: 1)
      @ring.ensure_view(@view_x, @view_y, VIEWPORT_W, VIEWPORT_H)
      @map_gfx.set_viewport(@view_x % @ring.buf_w, @view_y % @ring.buf_h,
                            VIEWPORT_W, VIEWPORT_H)
      @map_gfx.present(MAP_ORIGIN_X, MAP_ORIGIN_Y)
    else
      @player = SpriteInstance.new(@gfx, @player_frames,
                                   x: MAP_ORIGIN_X + @player_px - @view_x,
                                   y: MAP_ORIGIN_Y + @player_py - @view_y, z: 1)
    end
    apply_facing_frame
    draw_status
    update_camera_and_draw

    start_bgm
  end

  def on_event(ev)
    super(ev)
    case ev[:type]
    when :key_down, :gamepad_down
      dx, dy = direction_for(ev)
      return if dx.nil?
      @held_dx = dx
      @held_dy = dy
      try_slide(dx, dy)
    when :key_up, :gamepad_up
      dx, dy = direction_for(ev)
      return if dx.nil?
      if dx == @held_dx && dy == @held_dy
        @held_dx = nil
        @held_dy = nil
      end
    end
  end

  def on_update
    # Resume walking while a direction key is held: covers chains broken by
    # a wall bump (slide ends blocked, then the player turns free) or any
    # missed key event.
    try_slide(@held_dx, @held_dy) if @slide_step.nil? && @held_dx
    if @slide_step
      advance_slide
      FRAME_MS
    else
      advance_idle_anim
      IDLE_ANIM_MS
    end
  end

  private

  def transfer_assets
    # sync_file compares size and CRC32, so regenerated assets are picked up
    # and unchanged ones cost one round trip instead of a full transfer.
    # BGM_SRC is included so the audio task can load it via load_fmsq_file
    # (which bypasses the inline IPC payload cap).
    srcs = [SHEET_SRC, BGM_SRC] + PLAYER_FRAME_NAMES.map { |n| "#{APP_DIR}/#{n}" }
    srcs.each do |src|
      @gfx.sync_file(src, dest: cache_path_for(src))
    end
  end

  def start_bgm
    # picoruby has no `defined?`, so just try to instantiate; rescue covers
    # both "FmrbAudio class missing" (NameError) and audio-side failures.
    # We push bgm.fmsq via transfer_file (handled in transfer_assets) and
    # then ask the audio task to load it from its own LittleFS path with
    # load_fmsq_file, which avoids the inline IPC payload cap (~150 B).
    @audio = FmrbAudio.new(self)
    @audio.load_fmsq_file(BGM_SLOT, cache_path_for(BGM_SRC))
    @audio.play_slot(BGM_SLOT)
    Log.info("BGM started from #{cache_path_for(BGM_SRC)}")
  rescue => e
    Log.error("BGM load failed: #{e.message}")
    @audio = nil
  end

  def play_bump_se
    return unless @audio
    return if @bump_off_in   # already playing; let it finish
    # Low-frequency triangle wave produces a short "doon" thump rather
    # than a buzzy noise burst. The triangle channel ignores volume /
    # duty / sweep; only freq matters. Goes to SUB APU (note_on always
    # targets SUB), so it doesn't fight the BGM on MAIN.
    @audio.note_on(SE_TRI_CH, SE_BUMP_FREQ, 0, 0, 0)
    @bump_off_in = BUMP_SE_TICKS
  end

  def tick_se
    return unless @bump_off_in
    @bump_off_in -= 1
    if @bump_off_in <= 0
      @audio.note_off(SE_TRI_CH) if @audio
      @bump_off_in = nil
    end
  end

  def cache_path_for(src)
    "#{CACHE_DIR}/#{src.split("/").last}"
  end

  # Load each player frame from its own 16x16 BMP into a separate SpriteImage.
  # With hardware scroll the frames live on the map canvas (the player
  # instance rides that canvas and everything is freed with it).
  def load_player_frames
    target = @hw_scroll ? @map_gfx : @gfx
    @player_frames = []
    PLAYER_FRAME_NAMES.each do |name|
      frm = SpriteImage.new(target, width: TILE, height: TILE,
                            transparent_color: 0, use_transparent: true)
      frm.load_bmp(cache_path_for("#{APP_DIR}/#{name}"))
      @player_frames << frm
    end
  end

  def try_slide(dx, dy)
    return unless @slide_step.nil?
    # Always update facing so the player turns even when bumping a wall.
    @dir = dir_of(dx, dy)
    apply_facing_frame
    nx = @player_tx + dx
    ny = @player_ty + dy
    blocked = nx < 0 || ny < 0 || nx >= @map.width || ny >= @map.height ||
              !@map.walkable?(nx, ny)
    if blocked
      play_bump_se
      return
    end
    @slide_dx = dx
    @slide_dy = dy
    @slide_target_tx = nx
    @slide_target_ty = ny
    @slide_step = 0
    # Toggle walking step at the start of each tile so consecutive moves
    # alternate left / right foot.
    @anim_step = 1 - @anim_step
    apply_facing_frame
  end

  def advance_slide
    @slide_step += 1
    @player_px = @player_tx * TILE + @slide_dx * SLIDE_PX_PER_STEP * @slide_step
    @player_py = @player_ty * TILE + @slide_dy * SLIDE_PX_PER_STEP * @slide_step
    if @slide_step >= SLIDE_STEPS
      @player_tx = @slide_target_tx
      @player_ty = @slide_target_ty
      @player_px = @player_tx * TILE
      @player_py = @player_ty * TILE
      @slide_step = nil
      @slide_dx = @slide_dy = 0
      fire_event_at(@player_tx, @player_ty)
      draw_status
      # Continue moving if a direction key is still held.
      try_slide(@held_dx, @held_dy) if @held_dx
    end
    update_camera_and_draw
    tick_se
  end

  def update_camera_and_draw
    @view_x = clamp(@player_px + TILE / 2 - VIEWPORT_W / 2, 0, @map_w - VIEWPORT_W)
    @view_y = clamp(@player_py + TILE / 2 - VIEWPORT_H / 2, 0, @map_h - VIEWPORT_H)
    if @hw_scroll
      # PPA scroll: stamp only newly exposed tiles (usually none), then
      # move the composite source window over the torus canvas
      @ring.ensure_view(@view_x, @view_y, VIEWPORT_W, VIEWPORT_H)
      @map_gfx.set_viewport(@view_x % @ring.buf_w, @view_y % @ring.buf_h,
                            VIEWPORT_W, VIEWPORT_H)
      @player.move(@player_px - @view_x, @player_py - @view_y)
      @gfx.present
    else
      @gfx.fill_rect(MAP_ORIGIN_X, MAP_ORIGIN_Y, VIEWPORT_W, VIEWPORT_H, FmrbGfx::BLACK)
      @map.render_view(@sheet,
                       origin_x: MAP_ORIGIN_X, origin_y: MAP_ORIGIN_Y,
                       view_x: @view_x, view_y: @view_y,
                       view_w: VIEWPORT_W, view_h: VIEWPORT_H)
      @player.move(MAP_ORIGIN_X + @player_px - @view_x,
                   MAP_ORIGIN_Y + @player_py - @view_y)
      @gfx.present
    end
  end

  def fire_event_at(tx, ty)
    @last_event = @map.event_at(tx, ty)
    if @last_event
      Log.info("event: id=#{@last_event["id"]} name=#{(@last_event["data"] || {})["name"]}")
    end
  end

  def apply_facing_frame
    pair = DIR_FRAMES[@dir]
    @player.frame = pair[@anim_step] if pair
  end

  # Idle tick driver. Runs every IDLE_ANIM_MS (~60 ms) so held keys are
  # picked up quickly, but the visible foot-tap only updates every ~300 ms.
  def advance_idle_anim
    @idle_ticks += 1
    if @idle_ticks * IDLE_ANIM_MS >= 300
      @idle_ticks = 0
      @anim_step = 1 - @anim_step
      apply_facing_frame
      @gfx.present
    end
    tick_se
  end

  def dir_of(dx, dy)
    return :left  if dx < 0
    return :right if dx > 0
    return :up    if dy < 0
    return :down  if dy > 0
    @dir
  end

  def clamp(v, lo, hi)
    return lo if v < lo
    return hi if v > hi
    v
  end

  def direction_for(ev)
    type = ev[:type]
    if type == :key_down || type == :key_up
      case ev[:keycode]
      when FmrbConst::KEY_LEFT  then return [-1, 0]
      when FmrbConst::KEY_RIGHT then return [1, 0]
      when FmrbConst::KEY_UP    then return [0, -1]
      when FmrbConst::KEY_DOWN  then return [0, 1]
      end
    elsif type == :gamepad_down || type == :gamepad_up
      case ev[:button]
      when FmrbConst::GP_LEFT  then return [-1, 0]
      when FmrbConst::GP_RIGHT then return [1, 0]
      when FmrbConst::GP_UP    then return [0, -1]
      when FmrbConst::GP_DOWN  then return [0, 1]
      end
    end
    [nil, nil]
  end

  def draw_status
    @gfx.fill_rect(STATUS_X, 0, STATUS_W, @user_area_height, FmrbGfx::BLACK)
    @gfx.fill_rect(STATUS_X, 0, 1, @user_area_height, FmrbGfx::GRAY)

    y = 6
    @gfx.draw_text(STATUS_X + 8, y, "RPG Demo", FmrbGfx::WHITE)
    y += 16
    @gfx.draw_text(STATUS_X + 8, y, "Position", FmrbGfx::GRAY)
    y += 12
    @gfx.draw_text(STATUS_X + 16, y, "(#{@player_tx}, #{@player_ty})", FmrbGfx::CYAN)

    y += 24
    @gfx.draw_text(STATUS_X + 8, y, "Last event", FmrbGfx::GRAY)
    y += 12
    if @last_event
      @gfx.draw_text(STATUS_X + 16, y, "id=#{@last_event["id"]}", FmrbGfx::YELLOW)
      y += 12
      name = (@last_event["data"] || {})["name"] || "?"
      @gfx.draw_text(STATUS_X + 16, y, name.to_s, FmrbGfx::YELLOW)
    else
      @gfx.draw_text(STATUS_X + 16, y, "(none)", FmrbGfx::GRAY)
    end

    y = @user_area_height - 32
    @gfx.draw_text(STATUS_X + 8, y, "Arrow keys", FmrbGfx::GRAY)
    y += 12
    @gfx.draw_text(STATUS_X + 8, y, "or D-pad",   FmrbGfx::GRAY)
  end

  def splash(msg)
    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @gfx.draw_text(8, 8, msg, FmrbGfx::WHITE)
    @gfx.present
  end
end

begin
  app = RpgDemoApp.new
  Log.info("RpgDemoApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.message}")
end
