# Family mruby RPG Demo
#
# Layout:
#   - 320x240 fullscreen
#   - Left 176x176 viewport (= 11x11 tiles) onto a 64x64 tile world
#   - Right 144x240 status panel (player position, last event)
#
# The player stays near the viewport center; moving slides the map under the
# player at 4 px/frame for 4 frames per tile (16 px). When the player
# approaches a world edge the camera clamps and the player walks the rest of
# the way to the corner. Each slide frame re-stamps only the tiles that
# intersect the viewport (sub-tile clipping in TileSheet#stamp keeps the
# status panel area untouched).

class RpgDemoApp < FmrbApp
  TILE              = 16
  MAP_ORIGIN_X      = 16      # viewport top-left on the canvas
  MAP_ORIGIN_Y      = 16
  VIEWPORT_W        = 176     # 11 tiles wide
  VIEWPORT_H        = 176     # 11 tiles tall
  STATUS_X          = MAP_ORIGIN_X + VIEWPORT_W   # 192
  STATUS_W          = 320 - STATUS_X              # 128

  SLIDE_STEPS       = 4
  SLIDE_PX_PER_STEP = TILE / SLIDE_STEPS          # 4
  FRAME_MS          = 16
  IDLE_MS           = 100

  APP_DIR    = "/app/game/rpg_demo"
  MAP_PATH   = "#{APP_DIR}/world.map.json"
  SHEET_SRC  = "#{APP_DIR}/world.bmp"
  PLAYER_SRC = "#{APP_DIR}/player.bmp"
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
    @player_img = SpriteImage.new(@gfx, width: TILE, height: TILE,
                                  transparent_color: 0, use_transparent: true)
    @player_img.load_bmp(cache_path_for(PLAYER_SRC))

    @map_w = @map.width  * TILE
    @map_h = @map.height * TILE

    # Player starts at the center of the world (matches the embedded spawn
    # island in generate_world.rb).
    @player_tx = @map.width  / 2
    @player_ty = @map.height / 2
    @player_px = @player_tx * TILE
    @player_py = @player_ty * TILE

    @view_x = clamp(@player_px + TILE / 2 - VIEWPORT_W / 2, 0, @map_w - VIEWPORT_W)
    @view_y = clamp(@player_py + TILE / 2 - VIEWPORT_H / 2, 0, @map_h - VIEWPORT_H)

    @slide_step = nil
    @slide_dx = @slide_dy = 0
    @slide_target_tx = @player_tx
    @slide_target_ty = @player_ty
    @last_event = @map.event_at(@player_tx, @player_ty)

    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @player = SpriteInstance.new(@gfx, @player_img,
                                 x: MAP_ORIGIN_X + @player_px - @view_x,
                                 y: MAP_ORIGIN_Y + @player_py - @view_y, z: 1)
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
      IDLE_MS
    end
  end

  private

  def transfer_assets
    pairs = [[SHEET_SRC, cache_path_for(SHEET_SRC)],
             [PLAYER_SRC, cache_path_for(PLAYER_SRC)]]
    pairs.each do |src, dst|
      status = @gfx.file_status(dst)
      @gfx.transfer_file(src, dest: dst) unless status[:exists]
    end
  end

  def cache_path_for(src)
    "#{CACHE_DIR}/#{src.split("/").last}"
  end

  def try_slide(dx, dy)
    return unless @slide_step.nil?
    nx = @player_tx + dx
    ny = @player_ty + dy
    return if nx < 0 || ny < 0 || nx >= @map.width || ny >= @map.height
    @slide_dx = dx
    @slide_dy = dy
    @slide_target_tx = nx
    @slide_target_ty = ny
    @slide_step = 0
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
