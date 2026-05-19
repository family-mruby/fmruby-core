# TileSheet / TileMap — runtime support for fmrb_map JSON and its sister BMP.
#
# Architecture (see plan):
#   - The tilesheet BMP must already live on the WROVER LittleFS (use
#     @gfx.transfer_file to push it from core flash first).
#   - TileSheet wraps the loaded SpriteImage and exposes one stamp(index)
#     helper that issues a draw_tile RPC (sub-region copy onto the current
#     canvas, transparent_color honored). No SpriteInstance is allocated.
#   - TileMap parses fmrb_map JSON (web-editor format) and walks each layer
#     calling stamp once per cell, so the BG is baked into the canvas in a
#     single pass at app start.

# Wraps one BMP-backed SpriteImage that holds a grid of tiles.
class TileSheet
  TILE = 16
  attr_reader :image, :cols, :rows, :tile_size

  # @param gfx       [FmrbGfx]
  # @param path      [String]  WROVER-side BMP path (already transferred)
  # @param cols      [Integer] tile columns in the sheet
  # @param rows      [Integer] tile rows. If nil, derived from file size via
  #                            @gfx.file_status (assumes 8bpp BMP with the
  #                            standard 1078-byte header+palette).
  # @param tile_size [Integer] pixel size of one tile (default 16)
  def initialize(gfx, path, cols:, rows: nil, tile_size: TILE)
    @gfx = gfx
    @tile_size = tile_size
    @cols = cols
    width = @cols * tile_size

    if rows.nil?
      status = @gfx.file_status(path)
      unless status && status[:exists]
        raise "TileSheet: file not found on graphics side: #{path}"
      end
      # 8-bit indexed BMP layout: 14 + 40 + 256*4 = 1078 bytes of header+palette,
      # then width*height pixel bytes (width is a multiple of 16 in our use
      # case so no row padding to worry about).
      pixel_bytes = status[:size] - 1078
      @rows = pixel_bytes / (width * tile_size)
    else
      @rows = rows
    end
    height = @rows * tile_size

    @image = SpriteImage.new(gfx,
                             width: width, height: height,
                             transparent_color: 0, use_transparent: true)
    @image.load_bmp(path)
  end

  # Stamp tile #index onto the current canvas at (dst_x, dst_y). index is
  # row-major: row * cols + col. nil / negative indices are no-ops (used to
  # represent empty map cells).
  #
  # Optional clip_{x,y,w,h} restricts output to the given canvas rect: the
  # source/destination rects are intersected with it so a tile straddling the
  # rect edge is partially blitted instead of bleeding past it. Used by
  # TileMap#render_view for sub-tile scrolling.
  def stamp(index, dst_x:, dst_y:, clip_x: nil, clip_y: nil, clip_w: nil, clip_h: nil)
    return if index.nil? || index < 0
    sx = (index % @cols) * @tile_size
    sy = (index / @cols) * @tile_size
    w  = @tile_size
    h  = @tile_size
    if clip_x
      rx0 = dst_x > clip_x ? dst_x : clip_x
      ry0 = dst_y > clip_y ? dst_y : clip_y
      rx1_a = dst_x + w
      rx1_b = clip_x + clip_w
      rx1   = rx1_a < rx1_b ? rx1_a : rx1_b
      ry1_a = dst_y + h
      ry1_b = clip_y + clip_h
      ry1   = ry1_a < ry1_b ? ry1_a : ry1_b
      return if rx0 >= rx1 || ry0 >= ry1
      sx += rx0 - dst_x
      sy += ry0 - dst_y
      w   = rx1 - rx0
      h   = ry1 - ry0
      dst_x = rx0
      dst_y = ry0
    end
    @gfx.draw_tile(@image.id, sx, sy, w, h, dst_x: dst_x, dst_y: dst_y)
  end

  def destroy
    @image.destroy if @image
    @image = nil
  end
end

# Wraps an fmrb_map JSON document.
class TileMap
  FORMAT  = "fmrb_map"
  VERSION = 1

  attr_reader :width, :height, :tile_size, :tilesheet_path,
              :tilesheet_cols, :layers, :events

  # @param json_path [String] core-side JSON path readable by File.open
  def initialize(json_path)
    text = File.open(json_path, "r") { |f| f.read }
    # NOTE: bare `JSON` is looked up as TileMap::JSON first under picoruby's
    # constant resolution (same trap as feedback_picoruby_mixin_const_lookup).
    obj = ::JSON.parse(text)
    raise "TileMap: unexpected format: #{obj["format"]}" if obj["format"] != FORMAT
    raise "TileMap: unsupported version: #{obj["version"]}" if obj["version"] != VERSION

    @width          = obj["width"].to_i
    @height         = obj["height"].to_i
    @tile_size      = obj["tile_size"].to_i
    @tile_size      = 16 if @tile_size <= 0
    @tilesheet_path = obj["tilesheet"]
    @tilesheet_cols = obj["tilesheet_cols"].to_i
    @layers         = obj["layers"] || []
    @events         = obj["events"] || []
  end

  # Render all layers onto the current canvas via repeated draw_tile stamps.
  # (origin_x, origin_y) = where the top-left tile maps to on the canvas.
  # max_cols / max_rows clip the rendered region (used for fixed-size viewports
  # that are smaller than the underlying map).
  def render(sheet, origin_x:, origin_y:, max_cols: nil, max_rows: nil)
    cols = max_cols ? (@width  < max_cols ? @width  : max_cols) : @width
    rows = max_rows ? (@height < max_rows ? @height : max_rows) : @height

    li = 0
    while li < @layers.size
      data = @layers[li]["data"]
      if data.is_a?(Array)
        y = 0
        while y < rows
          row = data[y]
          if row.is_a?(Array)
            x = 0
            while x < cols
              sheet.stamp(row[x],
                          dst_x: origin_x + x * @tile_size,
                          dst_y: origin_y + y * @tile_size)
              x += 1
            end
          end
          y += 1
        end
      end
      li += 1
    end
  end

  # Render only the tiles that intersect the viewport rect, with sub-tile
  # offset support. (origin_x, origin_y) is where the viewport's top-left
  # corner lands on the canvas; (view_x, view_y) is the top-left of the
  # viewport expressed in full-map pixel coordinates.
  def render_view(sheet, origin_x:, origin_y:, view_x:, view_y:, view_w:, view_h:)
    ts = @tile_size
    col0 = view_x / ts
    col0 = 0 if col0 < 0
    col1 = (view_x + view_w - 1) / ts
    col1 = @width - 1 if col1 > @width - 1
    row0 = view_y / ts
    row0 = 0 if row0 < 0
    row1 = (view_y + view_h - 1) / ts
    row1 = @height - 1 if row1 > @height - 1
    return if col0 > col1 || row0 > row1

    li = 0
    while li < @layers.size
      data = @layers[li]["data"]
      if data.is_a?(Array)
        y = row0
        while y <= row1
          row = data[y]
          if row.is_a?(Array)
            x = col0
            while x <= col1
              sheet.stamp(row[x],
                          dst_x: origin_x + x * ts - view_x,
                          dst_y: origin_y + y * ts - view_y,
                          clip_x: origin_x, clip_y: origin_y,
                          clip_w: view_w,   clip_h: view_h)
              x += 1
            end
          end
          y += 1
        end
      end
      li += 1
    end
  end

  # Find an event placed at tile (x, y), or nil. Linear scan; map events are
  # typically a handful per map.
  def event_at(x, y)
    i = 0
    while i < @events.size
      ev = @events[i]
      return ev if ev["x"] == x && ev["y"] == y
      i += 1
    end
    nil
  end
end
