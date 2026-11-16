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
# bare checkout. tools/basic/basic_sheet_convert.py does PNG <-> BMP with Pillow.
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

  # [256][8] glyph rows -> [128][128] palette indices.
  #
  # Rows are either 1bpp (bit 7 = leftmost pixel, the text font) or 2bpp
  # (leftmost pixel is the top pair, table A); which one is decided by whether
  # any row needs more than 8 bits. 1bpp ink becomes index 3, the same
  # promotion the firmware does when it reads a 1bpp source.
  def glyphs_to_pixels(glyphs, two_bpp = nil)
    two_bpp = glyphs.flatten.any? { |v| v > 0xFF } if two_bpp.nil?
    px = Array.new(DIM) { Array.new(DIM, 0) }
    256.times do |code|
      ox = (code % COLS) * CELL
      oy = (code / COLS) * CELL
      CELL.times do |row|
        bits = glyphs[code][row]
        CELL.times do |col|
          index = if two_bpp
                    (bits >> (14 - 2 * col)) & 3
                  else
                    (bits & (0x80 >> col)) != 0 ? 3 : 0
                  end
          px[oy + row][ox + col] = index
        end
      end
    end
    px
  end

  # [128][128] palette indices -> [256][8] 2bpp glyph rows.
  def pixels_to_glyphs(px)
    glyphs = Array.new(256) { Array.new(CELL, 0) }
    DIM.times do |y|
      DIM.times do |x|
        index = px[y][x]
        next if index == 0
        index = 3 if index > 3
        code = (y / CELL) * COLS + (x / CELL)
        glyphs[code][y % CELL] |= index << (14 - 2 * (x % CELL))
      end
    end
    glyphs
  end

  # Write [128][128] palette indices as a bottom-up indexed BMP, in the fewest
  # bits that hold them (the firmware reads 1, 4 and 8):
  #   1 bpp when the sheet is off and index 3 only (the text font: a 1bpp
  #     sheet's ink is index 3, the firmware's rule), 2,102 B;
  #   4 bpp when every index fits in 4 bits (table A), 8,310 B;
  #   8 bpp otherwise, 17,462 B.
  def write_bmp(px, path)
    flat = px.flatten
    bpp = if flat.all? { |v| v == 0 || v == 3 } then 1
          elsif flat.max <= 15 then 4
          else 8
          end
    colours = bpp == 1 ? [PALETTE[0], PALETTE[3]] : PALETTE
    entries = 1 << bpp
    palette = colours.map { |r, g, b| [b, g, r, 0].pack("C4") }.join
    palette += "\0" * (4 * (entries - colours.length))
    pixel_offset = 14 + 40 + palette.bytesize
    stride = ((DIM * bpp + 31) / 32) * 4
    body = (DIM - 1).downto(0).map do |y|  # bottom-up
      row = px[y]
      bytes = case bpp
              when 8 then row
              when 4 then row.each_slice(2).map { |a, b| (a << 4) | b }
              else row.each_slice(8).map { |s| s.each_with_index.sum { |v, i| v == 0 ? 0 : 0x80 >> i } }
              end
      bytes.pack("C*").ljust(stride, "\0")
    end.join

    header = "BM".b + [pixel_offset + body.bytesize, 0, pixel_offset].pack("VVV")
    dib = [40, DIM, DIM].pack("VVV") +
          [1, bpp].pack("vv") +
          [0, body.bytesize, 2835, 2835, colours.length, 0].pack("V6")
    File.binwrite(path, header + dib + palette + body)
  end

  # Read an indexed BMP sheet back into [128][128] palette indices.
  #
  # Mirrors the firmware loader: 1, 4 and 8 bits per pixel, either row order;
  # a 1bpp sheet's ink is index 3.
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
                   else ((row[x / 8] >> (7 - (x % 8))) & 1) * 3
                   end
      end
    end
    px
  end
end
