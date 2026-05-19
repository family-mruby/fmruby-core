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
  IDLE_ANIM_MS      = 300   # foot-tap cadence while standing still

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
  CACHE_DIR  = "/cache/app/rpg_demo"

  def on_create
    splash("Loading map...")
    @map = TileMap.new(MAP_PATH)
    Log.info("Map loaded: #{@map.width}x#{@map.height}, sheet=#{@map.tilesheet_path}")

    splash("Transferring assets...")
    transfer_assets

    @sheet = TileSheet.new(@gfx, cache_path_for(SHEET_SRC),
                           cols: @map.tilesheet_cols,
                           tile_size: @map.tile_size)

    load_player_frames

    @map_w = @map.width  * TILE
    @map_h = @map.height * TILE

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
    @last_event = @map.event_at(@player_tx, @player_ty)

    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @player = SpriteInstance.new(@gfx, @player_frames,
                                 x: MAP_ORIGIN_X + @player_px - @view_x,
                                 y: MAP_ORIGIN_Y + @player_py - @view_y, z: 1)
    apply_facing_frame
    draw_status
    update_camera_and_draw
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :key_down || ev[:type] == :gamepad_down
    dx, dy = direction_for(ev)
    return if dx.nil?
    try_slide(dx, dy)
  end

  def on_update
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
    # Transfer unconditionally: regenerating BMPs locally can change their
    # size, and skipping on @gfx.file_status[:exists] would leave the WROVER
    # cache stale.
    srcs = [SHEET_SRC] + PLAYER_FRAME_NAMES.map { |n| "#{APP_DIR}/#{n}" }
    srcs.each do |src|
      @gfx.transfer_file(src, dest: cache_path_for(src))
    end
  end

  def cache_path_for(src)
    "#{CACHE_DIR}/#{src.split("/").last}"
  end

  # Load each player frame from its own 16x16 BMP into a separate SpriteImage.
  def load_player_frames
    @player_frames = []
    PLAYER_FRAME_NAMES.each do |name|
      frm = SpriteImage.new(@gfx, width: TILE, height: TILE,
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
    return if nx < 0 || ny < 0 || nx >= @map.width || ny >= @map.height
    return unless @map.walkable?(nx, ny)
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
    end
    update_camera_and_draw
  end

  def update_camera_and_draw
    @view_x = clamp(@player_px + TILE / 2 - VIEWPORT_W / 2, 0, @map_w - VIEWPORT_W)
    @view_y = clamp(@player_py + TILE / 2 - VIEWPORT_H / 2, 0, @map_h - VIEWPORT_H)
    @gfx.fill_rect(MAP_ORIGIN_X, MAP_ORIGIN_Y, VIEWPORT_W, VIEWPORT_H, FmrbGfx::BLACK)
    @map.render_view(@sheet,
                     origin_x: MAP_ORIGIN_X, origin_y: MAP_ORIGIN_Y,
                     view_x: @view_x, view_y: @view_y,
                     view_w: VIEWPORT_W, view_h: VIEWPORT_H)
    @player.move(MAP_ORIGIN_X + @player_px - @view_x,
                 MAP_ORIGIN_Y + @player_py - @view_y)
    @gfx.present
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

  # Toggle the foot-tap pose while standing still. Only the sprite frame
  # changes, so we skip the BG redraw and just present.
  def advance_idle_anim
    @anim_step = 1 - @anim_step
    apply_facing_frame
    @gfx.present
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
    if ev[:type] == :key_down
      case ev[:keycode]
      when FmrbConst::KEY_LEFT  then return [-1, 0]
      when FmrbConst::KEY_RIGHT then return [1, 0]
      when FmrbConst::KEY_UP    then return [0, -1]
      when FmrbConst::KEY_DOWN  then return [0, 1]
      end
    elsif ev[:type] == :gamepad_down
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
