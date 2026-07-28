# Read and write the BASIC character sheets (128x128 indexed BMP).
#
# The sheet holds 16 x 16 cells of 8x8 pixels; the character code is
# row * 16 + column. This is the format the firmware loads at app start
# (components/basic/assets/basic_assets.c), so the artwork can be edited in a
# graphics editor. Palette index 0 is off (background / transparent), 1-3 are on;
# the extra indices exist so four-colour artwork can be stored before the
# renderer grows 2bpp colour attribute support.
#
# Only the standard library is used on purpose: the generators must run in a
# bare checkout. tool/basic/basic_sheet_convert.py does PNG <-> BMP with Pillow.
module BasicSheet
  CELL = 8
  COLS = 16
  DIM = CELL * COLS  # 128

  # Index 0 is the background. 1 is the ink the renderer draws today; 2 and 3 are
  # reserved for the colour attributes and are given visible colours so artwork
  # using them is not invisible in an editor.
  PALETTE = [
    [0x00, 0x00, 0x00],  # 0: off
    [0xFF, 0xFF, 0xFF],  # 1: on
    [0xE0, 0x40, 0x40],  # 2: reserved (colour attribute 2)
    [0x40, 0xC0, 0xE0],  # 3: reserved (colour attribute 3)
  ].freeze

  module_function

  # [256][8] row bitmaps -> [128][128] palette indices.
  def glyphs_to_pixels(glyphs)
    px = Array.new(DIM) { Array.new(DIM, 0) }
    256.times do |code|
      ox = (code % COLS) * CELL
      oy = (code / COLS) * CELL
      CELL.times do |row|
        bits = glyphs[code][row]
        CELL.times do |col|
          px[oy + row][ox + col] = 1 if bits & (0x80 >> col) != 0
        end
      end
    end
    px
  end

  # [128][128] palette indices -> [256][8] row bitmaps (index != 0 = on).
  def pixels_to_glyphs(px)
    glyphs = Array.new(256) { Array.new(CELL, 0) }
    DIM.times do |y|
      DIM.times do |x|
        next if px[y][x] == 0
        code = (y / CELL) * COLS + (x / CELL)
        glyphs[code][y % CELL] |= 0x80 >> (x % CELL)
      end
    end
    glyphs
  end

  # Write [128][128] palette indices as an 8bpp bottom-up indexed BMP.
  def write_bmp(px, path)
    palette = PALETTE.map { |r, g, b| [b, g, r, 0].pack("C4") }.join
    palette += "\0" * (4 * (256 - PALETTE.length))
    pixel_offset = 14 + 40 + palette.bytesize
    body = (DIM - 1).downto(0).map { |y| px[y].pack("C*") }.join  # bottom-up

    header = "BM".b + [pixel_offset + body.bytesize, 0, pixel_offset].pack("VVV")
    dib = [40, DIM, DIM].pack("VVV") +
          [1, 8].pack("vv") +
          [0, body.bytesize, 2835, 2835, PALETTE.length, 0].pack("V6")
    File.binwrite(path, header + dib + palette + body)
  end

  # Read an indexed BMP sheet back into [128][128] palette indices.
  #
  # Mirrors the firmware loader: 1, 4 and 8 bits per pixel, either row order.
  def read_bmp(path)
    data = File.binread(path)
    raise "#{path}: not a BMP" unless data[0, 2] == "BM"
    rd32 = ->(off) { data[off, 4].unpack1("V") }
    bits_offset = rd32.call(10)
    dib_size = rd32.call(14)
    width = data[18, 4].unpack1("l<")
    raw_height = data[22, 4].unpack1("l<")
    bpp = data[28, 2].unpack1("v")
    compression = rd32.call(30)
    top_down = raw_height < 0
    height = raw_height.abs
    raise "#{path}: need an uncompressed BITMAPINFOHEADER BMP" if dib_size < 40 || compression != 0
    raise "#{path}: sheet must be #{DIM}x#{DIM}, got #{width}x#{height}" if width != DIM || height != DIM
    raise "#{path}: need 1, 4 or 8 bits per pixel, got #{bpp}" unless [1, 4, 8].include?(bpp)

    stride = ((width * bpp + 31) / 32) * 4
    px = Array.new(DIM) { Array.new(DIM, 0) }
    DIM.times do |y|
      file_row = top_down ? y : (DIM - 1 - y)
      row = data[bits_offset + file_row * stride, stride].bytes
      DIM.times do |x|
        px[y][x] = case bpp
                   when 8 then row[x]
                   when 4 then x.odd? ? (row[x / 2] & 0x0F) : (row[x / 2] >> 4)
                   else (row[x / 8] >> (7 - (x % 8))) & 1
                   end
      end
    end
    px
  end
end
