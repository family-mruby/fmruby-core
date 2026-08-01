# TileSheet / TileMap end-to-end smoke test.
#
# - Loads tile_map_test.map.json (5x4 map referencing tilesheet.bmp).
# - Transfers tilesheet.bmp to the WROVER cache, builds a TileSheet.
# - Renders the map onto the user area via map.render(sheet, ...).
# - Logs the event found at every cell so we can confirm event_at works.
#
# Expected: 5x4 grid of tiles from tilesheet.bmp, with cells (2,2), (0,3)
# and (4,3) showing the cyan background (those are -1 in the JSON).

class TileMapTestApp < FmrbApp
  MAP_JSON  = "/app/test/tile_map_test.map.json"
  CACHE_DIR = "/cache/app/tile_map_test"

  def on_create
    @ox = @user_area_x0
    @oy = @user_area_y0

    @gfx.fill_rect(@ox, @oy, @user_area_width, @user_area_height, FmrbGfx::CYAN)

    @map = TileMap.new(MAP_JSON)
    Log.info("TileMap loaded: #{@map.width}x#{@map.height}, sheet=#{@map.tilesheet_path}")

    sheet_dst = "#{CACHE_DIR}/sheet.bmp"
    @gfx.sync_file(@map.tilesheet_path, dest: sheet_dst)
    @sheet = TileSheet.new(@gfx, sheet_dst,
                           cols: @map.tilesheet_cols,
                           tile_size: @map.tile_size)
    Log.info("TileSheet: #{@sheet.cols}x#{@sheet.rows} tiles")

    @map.render(@sheet, origin_x: @ox, origin_y: @oy)

    # event_at sanity check.
    y = 0
    while y < @map.height
      x = 0
      while x < @map.width
        ev = @map.event_at(x, y)
        Log.info("event at (#{x},#{y}): id=#{ev["id"]} name=#{ev["data"]["name"]}") if ev
        x += 1
      end
      y += 1
    end

    draw_window_frame
    @gfx.present
  end

  def on_update
    500
  end
end

begin
  app = TileMapTestApp.new
  Log.info("TileMapTestApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.message}")
end
