# BMP332 module - the sprite pictures: RGB332 pixel bytes read from and written
# to 8-bit BMPs and palette PNGs.
# parse and encode_png are defined in C (ports/esp32/bmp332.c, png332.c).

module BMP332
  # Load and parse a BMP or PNG file (told apart by its first bytes).
  # Returns { width:, height:, pixels: String(RGB332 bytes, top-down) }
  def self.load(path)
    data = File.open(path, "r") { |f| f.read }
    parse(data)
  end

  # Save pixel data. A path ending in .png gets the palette PNG of
  # tools/rgb332_png.rb (only the colours used); any other an 8-bit BMP.
  # @param path [String] output file path
  # @param width [Integer] image width
  # @param height [Integer] image height
  # @param pixels [String] RGB332 pixel data (width*height bytes, top-down)
  def self.save(path, width, height, pixels)
    if png_path?(path)
      data = encode_png(width, height, pixels)
      File.open(path, "w") { |f| f.write(data) }
    else
      save_bmp(path, width, height, pixels)
    end
  end

  def self.png_path?(path)
    path.size >= 4 && path[-4, 4].downcase == ".png"
  end

  # Save pixel data as 8-bit indexed BMP with RGB332 palette
  def self.save_bmp(path, width, height, pixels)
    row_size = (width + 3) & ~3  # Pad to 4 bytes
    pixel_data_size = row_size * height
    palette_size = 1024  # 256 * 4
    header_size = 14
    dib_size = 40
    offset = header_size + dib_size + palette_size
    file_size = offset + pixel_data_size

    bmp = ""

    # BMP file header (14 bytes)
    bmp << "BM"
    bmp << _pack_u32(file_size)
    bmp << _pack_u16(0)       # reserved1
    bmp << _pack_u16(0)       # reserved2
    bmp << _pack_u32(offset)

    # DIB header (40 bytes)
    bmp << _pack_u32(dib_size)
    bmp << _pack_u32(width)
    bmp << _pack_u32(height)
    bmp << _pack_u16(1)       # planes
    bmp << _pack_u16(8)       # bpp
    bmp << _pack_u32(0)       # compression
    bmp << _pack_u32(pixel_data_size)
    bmp << _pack_u32(2835)    # h pixels/meter
    bmp << _pack_u32(2835)    # v pixels/meter
    bmp << _pack_u32(256)     # colors used
    bmp << _pack_u32(0)       # important colors

    # RGB332 palette (256 entries, BGRA)
    256.times do |i|
      r3 = (i >> 5) & 0x07
      g3 = (i >> 2) & 0x07
      b2 = i & 0x03
      r8 = (r3 * 255 / 7)
      g8 = (g3 * 255 / 7)
      b8 = (b2 * 255 / 3)
      bmp << b8.chr << g8.chr << r8.chr << "\x00"
    end

    # Pixel data (bottom-up row order)
    pad = "\x00" * (row_size - width)
    (height - 1).downto(0) do |y|
      row_start = y * width
      bmp << pixels[row_start, width]
      bmp << pad if pad.size > 0
    end

    File.open(path, "w") { |f| f.write(bmp) }
  end

  # Little-endian pack helpers
  def self._pack_u16(v)
    (v & 0xFF).chr << ((v >> 8) & 0xFF).chr
  end

  def self._pack_u32(v)
    (v & 0xFF).chr << ((v >> 8) & 0xFF).chr <<
    ((v >> 16) & 0xFF).chr << ((v >> 24) & 0xFF).chr
  end
end
