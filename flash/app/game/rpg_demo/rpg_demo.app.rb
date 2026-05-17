# Family mruby RPG Demo
#
# Layout:
#   - 320x240 fullscreen
#   - Left 176x176 (11x11 tiles) = map area (BG baked via TileMap + draw_tile)
#   - Right 144x240             = status panel (player position, last event)
#
# Player is a SpriteInstance (separate BMP). Map BG is stamped once into the
# canvas at on_create; no per-frame redraw of the BG.

class RpgDemoApp < FmrbApp
  TILE       = 16
  MAP_ORIGIN_X = 16      # offset map by one tile so it isn't flush with the
  MAP_ORIGIN_Y = 16      # window edge; gives a uniform border on top/left.
  MAP_AREA_W   = 176     # 11 tiles wide
  MAP_AREA_H   = 176     # 11 tiles tall
  STATUS_X   = MAP_ORIGIN_X + MAP_AREA_W   # 192
  STATUS_W   = 320 - STATUS_X              # 128

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

    # Bake the BG.
    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    cols = MAP_AREA_W / TILE
    rows = MAP_AREA_H / TILE
    @map.render(@sheet, origin_x: MAP_ORIGIN_X, origin_y: MAP_ORIGIN_Y,
                max_cols: cols, max_rows: rows)

    # Player (single SpriteInstance).
    @px = 0
    @py = 0
    @player = SpriteInstance.new(@gfx, @player_img,
                                 x: MAP_ORIGIN_X + @px * TILE,
                                 y: MAP_ORIGIN_Y + @py * TILE, z: 1)

    @last_event = nil
    draw_status
    @gfx.present
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :key_down || ev[:type] == :gamepad_down
    dx, dy = direction_for(ev)
    return if dx.nil?
    try_move(dx, dy)
  end

  def on_update
    100   # idle; input is event-driven
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

  def try_move(dx, dy)
    nx = @px + dx
    ny = @py + dy
    mc = MAP_AREA_W / TILE
    mr = MAP_AREA_H / TILE
    bounds_x = @map.width  < mc ? @map.width  : mc
    bounds_y = @map.height < mr ? @map.height : mr
    return if nx < 0 || ny < 0 || nx >= bounds_x || ny >= bounds_y
    @px = nx
    @py = ny
    @player.move(MAP_ORIGIN_X + @px * TILE, MAP_ORIGIN_Y + @py * TILE)
    @last_event = @map.event_at(@px, @py)
    if @last_event
      Log.info("event: id=#{@last_event["id"]} name=#{(@last_event["data"] || {})["name"]}")
    end
    draw_status
    @gfx.present
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
    @gfx.draw_text(STATUS_X + 16, y, "(#{@px}, #{@py})", FmrbGfx::CYAN)

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
