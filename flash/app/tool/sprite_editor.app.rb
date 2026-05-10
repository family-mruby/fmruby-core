# Sprite Editor - 16x16 RGB332 tile editor
# Load a tile sheet BMP, select a tile, edit pixels, save back

class SpriteEditorApp < FmrbApp
  TILE = 16
  ZOOM = 7
  PAL_COLS = 8
  PAL_CELL = 10

  PALETTE = [
    0x00, 0xE0, 0x1C, 0x03, 0xFC, 0x1F, 0xE3, 0xFF,
    0x6D, 0xF0, 0x88, 0x14, 0x5F, 0xF4, 0x02, 0x48,
  ]

  def on_create
    @ox = @user_area_x0
    @oy = @user_area_y0
    @w = @user_area_width
    @h = @user_area_height
    @mode = :waiting
    @need_redraw = true
    @current_color = 0xE0
    @sel_tile_x = 0
    @sel_tile_y = 0
    @dirty = false

    # Request file selection via system file selector
    request_file_select("open")
  end

  def on_update
    if @need_redraw
      @need_redraw = false
      case @mode
      when :waiting
        draw_waiting
      when :editor
        draw_editor
      end
    end
    200
  end

  def on_control(msg)
    if msg["cmd"] == "file_selected" && msg["path"]
      load_sheet(msg["path"])
    end
  end

  def on_event(ev)
    super(ev)
    return unless @mode == :editor

    if ev[:type] == :mouse_up
      handle_editor_click(ev[:x], ev[:y])
    end
    if ev[:type] == :key_down
      character = ev[:character] || 0
      if character == 115 || character == 83  # s/S
        save_sheet
      end
    end
  end

  def on_destroy
  end

  private

  # --- Loading ---

  def draw_waiting
    @gfx.fill_rect(@ox, @oy, @w, @h, FmrbGfx::BLACK)
    @gfx.draw_text(@ox + 4, @oy + @h / 2 - 6, "Select a BMP file...", FmrbGfx::WHITE)
    draw_window_frame
    @gfx.present
  end

  def load_sheet(path)
    @sheet_path = path

    @gfx.fill_rect(@ox, @oy, @w, @h, FmrbGfx::BLACK)
    @gfx.draw_text(@ox + 4, @oy + 4, "Loading...", FmrbGfx::WHITE)
    draw_window_frame
    @gfx.present

    # Transfer to graphics-audio cache
    name = File.basename(path)
    cache_path = "/cache/app/sprite_editor/#{name}"
    status = @gfx.file_status(cache_path)
    unless status[:exists]
      @gfx.transfer_file(path, dest: cache_path)
    end

    # Load BMP
    bmp = BMP332.load(path)
    @sheet_w = bmp[:width]
    @sheet_h = bmp[:height]
    @sheet_pixels = bmp[:pixels].bytes
    @cols = @sheet_w / TILE
    @rows = @sheet_h / TILE
    @cols = 1 if @cols < 1
    @rows = 1 if @rows < 1
    @sel_tile_x = 0
    @sel_tile_y = 0
    @dirty = false

    # Layout
    sheet_draw_w = @cols * TILE
    @sheet_x = @ox + 2
    @sheet_y = @oy + 2
    @editor_x = @ox + sheet_draw_w + 6
    @editor_y = @oy + 2
    @pal_x = @editor_x + ZOOM * TILE + 4
    @pal_y = @oy + 2
    @status_y = @oy + @h - 12

    @mode = :editor
    @need_redraw = true
    Log.info("SpriteEd: loaded #{path} #{@sheet_w}x#{@sheet_h} (#{@cols}x#{@rows} tiles)")
  end

  # --- Editor ---

  def handle_editor_click(mx, my)
    # Sheet area
    sheet_draw_w = @cols * TILE
    sheet_draw_h = @rows * TILE
    if mx >= @sheet_x && mx < @sheet_x + sheet_draw_w &&
       my >= @sheet_y && my < @sheet_y + sheet_draw_h
      @sel_tile_x = (mx - @sheet_x) / TILE
      @sel_tile_y = (my - @sheet_y) / TILE
      @need_redraw = true
      return
    end

    # Editor area
    ed_w = ZOOM * TILE
    ed_h = ZOOM * TILE
    if mx >= @editor_x && mx < @editor_x + ed_w &&
       my >= @editor_y && my < @editor_y + ed_h
      px = (mx - @editor_x) / ZOOM
      py = (my - @editor_y) / ZOOM
      set_tile_pixel(px, py, @current_color)
      @dirty = true
      @need_redraw = true
      return
    end

    # Palette area
    pal_rows = (PALETTE.size + PAL_COLS - 1) / PAL_COLS
    pal_w = PAL_COLS * PAL_CELL
    pal_h = pal_rows * PAL_CELL
    if mx >= @pal_x && mx < @pal_x + pal_w &&
       my >= @pal_y && my < @pal_y + pal_h
      pc = (mx - @pal_x) / PAL_CELL
      pr = (my - @pal_y) / PAL_CELL
      idx = pr * PAL_COLS + pc
      if idx < PALETTE.size
        @current_color = PALETTE[idx]
        @need_redraw = true
      end
      return
    end
  end

  def get_tile_pixel(px, py)
    sx = @sel_tile_x * TILE + px
    sy = @sel_tile_y * TILE + py
    @sheet_pixels[sy * @sheet_w + sx]
  end

  def set_tile_pixel(px, py, color)
    sx = @sel_tile_x * TILE + px
    sy = @sel_tile_y * TILE + py
    @sheet_pixels[sy * @sheet_w + sx] = color
  end

  def save_sheet
    pixels_str = @sheet_pixels.map { |b| b.chr }.join
    BMP332.save(@sheet_path, @sheet_w, @sheet_h, pixels_str)
    @dirty = false
    Log.info("SpriteEd: saved #{@sheet_path}")
    @need_redraw = true
  end

  def draw_editor
    @gfx.fill_rect(@ox, @oy, @w, @h, 0x49)

    # Sheet view
    ty = 0
    while ty < @rows
      tx = 0
      while tx < @cols
        py = 0
        while py < TILE
          px = 0
          while px < TILE
            sx = tx * TILE + px
            sy = ty * TILE + py
            color = @sheet_pixels[sy * @sheet_w + sx]
            @gfx.set_pixel(@sheet_x + sx, @sheet_y + sy, color)
            px += 1
          end
          py += 1
        end
        tx += 1
      end
      ty += 1
    end
    @gfx.draw_rect(@sheet_x + @sel_tile_x * TILE - 1,
                    @sheet_y + @sel_tile_y * TILE - 1,
                    TILE + 2, TILE + 2, FmrbGfx::WHITE)

    # Zoom editor
    py = 0
    while py < TILE
      px = 0
      while px < TILE
        color = get_tile_pixel(px, py)
        @gfx.fill_rect(@editor_x + px * ZOOM, @editor_y + py * ZOOM,
                        ZOOM, ZOOM, color)
        px += 1
      end
      py += 1
    end
    (0..TILE).each do |i|
      @gfx.draw_line(@editor_x + i * ZOOM, @editor_y,
                      @editor_x + i * ZOOM, @editor_y + TILE * ZOOM, 0x49)
      @gfx.draw_line(@editor_x, @editor_y + i * ZOOM,
                      @editor_x + TILE * ZOOM, @editor_y + i * ZOOM, 0x49)
    end

    # Palette
    PALETTE.each_with_index do |color, idx|
      pc = idx % PAL_COLS
      pr = idx / PAL_COLS
      x = @pal_x + pc * PAL_CELL
      y = @pal_y + pr * PAL_CELL
      @gfx.fill_rect(x, y, PAL_CELL - 1, PAL_CELL - 1, color)
      if color == @current_color
        @gfx.draw_rect(x - 1, y - 1, PAL_CELL + 1, PAL_CELL + 1, FmrbGfx::WHITE)
      end
    end
    @gfx.fill_rect(@pal_x, @pal_y + 30, 20, 12, @current_color)
    @gfx.draw_rect(@pal_x, @pal_y + 30, 20, 12, FmrbGfx::WHITE)

    # Status
    tile_idx = @sel_tile_y * @cols + @sel_tile_x
    status = "T:#{tile_idx} C:#{sprintf('%02X', @current_color)}"
    status << " *" if @dirty
    status << " [S]save"
    @gfx.draw_text(@ox + 2, @status_y, status, FmrbGfx::WHITE)

    draw_window_frame
    @gfx.present
  end
end

begin
  app = SpriteEditorApp.new
  app.start
rescue => e
  Log.error("SpriteEd: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
