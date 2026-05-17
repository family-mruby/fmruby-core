# draw_tile smoke test
#
# Loads /usr/share/sprites/tilesheet.bmp (64x32 = 8 tiles, 4 columns x 2 rows)
# and stamps a few sub-regions onto the canvas via the new draw_tile RPC.
#
# Expected: each tile of the sheet appears at the matching position in a 4x2
# grid inside the window's user area, with transparent (RGB332 0x00) pixels
# showing the magenta-ish background through.

class DrawTileTestApp < FmrbApp
  TILE = 16
  SHEET_SRC  = "/usr/share/sprites/tilesheet.bmp"
  CACHE_DIR  = "/cache/app/draw_tile_test"
  SHEET_DST  = "#{CACHE_DIR}/sheet.bmp"
  SHEET_W    = 64
  SHEET_H    = 32
  SHEET_COLS = SHEET_W / TILE   # 4
  SHEET_ROWS = SHEET_H / TILE   # 2

  def on_create
    @ox = @user_area_x0
    @oy = @user_area_y0

    # Cyan background (distinct from the ruby-red title bar) so transparent
    # pixels in each tile clearly punch through.
    @gfx.fill_rect(@ox, @oy, @user_area_width, @user_area_height, FmrbGfx::CYAN)

    status = @gfx.file_status(SHEET_DST)
    @gfx.transfer_file(SHEET_SRC, dest: SHEET_DST) unless status[:exists]

    @sheet = SpriteImage.new(@gfx,
                             width: SHEET_W, height: SHEET_H,
                             transparent_color: 0, use_transparent: true)
    @sheet.load_bmp(SHEET_DST)

    # Stamp every tile of the sheet onto the canvas in its native layout.
    SHEET_ROWS.times do |r|
      SHEET_COLS.times do |c|
        @gfx.draw_tile(@sheet.id,
                       c * TILE, r * TILE,    # src
                       TILE, TILE,            # w, h
                       dst_x: @ox + c * TILE,
                       dst_y: @oy + r * TILE)
      end
    end

    # And re-stamp tile (2, 1) at a second position to confirm sub-region
    # selection (different tile, different dest).
    @gfx.draw_tile(@sheet.id, 2 * TILE, 1 * TILE, TILE, TILE,
                   dst_x: @ox + 4 * TILE + 8, dst_y: @oy + 0)

    draw_window_frame
    @gfx.present
  end

  def on_update
    500
  end
end

begin
  app = DrawTileTestApp.new
  Log.info("DrawTileTestApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.message}")
end

