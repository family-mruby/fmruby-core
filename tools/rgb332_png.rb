# Read and write the sprite pictures: palette PNGs whose colours are RGB332.
#
# A sprite picture is a grid of RGB332 bytes (the value is the colour; there is
# no palette on the device). The display sides read it from a palette PNG: the
# decoder gives RGB888 and the sprite (RGB332, or an 8-bit scratch sprite on
# the P4 side) keeps the top 3/3/2 bits, so a palette entry made by expanding
# an RGB332 value (palette_rgb below) comes back as exactly that value. Only
# the colours a picture uses go in its palette; small pictures get the fewest
# bits per pixel (they mostly end up under 512 B, inline in littlefs), large
# ones 8 bits (no bit unpacking on decode; a few % bigger).
#
# The reader takes these PNGs, the 8-bit BMPs the sprites used to be (and that
# the sprite editor still saves), and plain RGB/RGBA PNGs (folded to RGB332 the
# same way the device does), so the generators can read either.
#
# Standard library only (zlib), so the generators run in a bare checkout.
#
#   require_relative "rgb332_png"    # from tools/
#   Rgb332Png.write(path, w, h, px)  # px: w*h RGB332 values, top-down
#   w, h, px = Rgb332Png.read(path)  # .png or .bmp

require "zlib"

module Rgb332Png
  SIGNATURE = "\x89PNG\r\n\x1a\n".b
  # Pictures with at least this many pixels are written 8 bits per pixel.
  BIG = 16 * 1024

  module_function

  # RGB332 -> [r, g, b], the expansion every tool here has used for the BMP
  # palettes (gen_icon_bmp.rb): the top bits stay the RGB332 bits.
  def palette_rgb(v)
    [((v >> 5) & 7) * 255 / 7, ((v >> 2) & 7) * 255 / 7, (v & 3) * 255 / 3]
  end

  # RGB888 -> RGB332, as LovyanGFX converts (keep the top bits).
  def to_rgb332(r, g, b)
    (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)
  end

  def encode(w, h, px)
    raise ArgumentError, "#{w}x#{h} needs #{w * h} pixels, got #{px.size}" if px.size != w * h
    colours = px.uniq.sort
    index = {}
    colours.each_with_index { |c, i| index[c] = i }
    depth = if w * h >= BIG || colours.size > 16 then 8
            elsif colours.size > 4 then 4
            elsif colours.size > 2 then 2
            else 1
            end
    per_byte = 8 / depth
    raw = +"".b
    h.times do |y|
      raw << "\x00".b # filter: none (the best for indexed pictures)
      row = px[y * w, w].map { |c| index[c] }
      if depth == 8
        raw << row.pack("C*")
      else
        row.each_slice(per_byte) do |s|
          b = 0
          per_byte.times { |i| b = (b << depth) | (s[i] || 0) }
          raw << b.chr
        end
      end
    end
    plte = colours.map { |c| palette_rgb(c).pack("C3") }.join
    SIGNATURE +
      chunk("IHDR", [w, h, depth, 3, 0, 0, 0].pack("NNCCCCC")) +
      chunk("PLTE", plte) +
      chunk("IDAT", Zlib::Deflate.deflate(raw, Zlib::BEST_COMPRESSION)) +
      chunk("IEND", "")
  end

  def write(path, w, h, px)
    data = encode(w, h, px)
    File.binwrite(path, data)
    data.bytesize
  end

  def chunk(type, data)
    [data.bytesize].pack("N") + type.b + data + [Zlib.crc32(type.b + data)].pack("N")
  end

  # [w, h, px] from a .png or .bmp (told apart by the first bytes).
  def read(path)
    data = File.binread(path)
    return decode_png(data, path) if data.start_with?(SIGNATURE)
    return decode_bmp8(data, path) if data.start_with?("BM")
    raise "#{path}: neither PNG nor BMP"
  end

  # An 8-bit BMP: the pixel bytes are the colours (the palette is not read,
  # as on the device).
  def decode_bmp8(b, path = "bmp")
    off = b[10, 4].unpack1("V")
    w = b[18, 4].unpack1("l<")
    h = b[22, 4].unpack1("l<")
    bpp = b[28, 2].unpack1("v")
    raise "#{path}: #{bpp} bits per pixel (8 expected)" unless bpp == 8
    top_down = h < 0
    h = h.abs
    stride = (w + 3) & ~3
    px = Array.new(w * h)
    h.times do |r|
      y = top_down ? r : h - 1 - r
      line = b[off + r * stride, w].bytes
      w.times { |x| px[y * w + x] = line[x] }
    end
    [w, h, px]
  end

  def decode_png(data, path = "png")
    pos = 8
    w = h = depth = ctype = nil
    plte = nil
    idat = +"".b
    while pos < data.bytesize
      len = data[pos, 4].unpack1("N")
      type = data[pos + 4, 4]
      body = data[pos + 8, len]
      case type
      when "IHDR"
        w, h, depth, ctype, _c, _f, interlace = body.unpack("NNCCCCC")
        raise "#{path}: interlaced PNG" if interlace != 0
      when "PLTE" then plte = body.bytes.each_slice(3).to_a
      when "IDAT" then idat << body
      when "IEND" then break
      end
      pos += 12 + len
    end
    channels = { 0 => 1, 2 => 3, 3 => 1, 4 => 2, 6 => 4 }.fetch(ctype) { raise "#{path}: colour type #{ctype}" }
    raise "#{path}: #{depth}-bit #{ctype == 3 ? 'palette' : 'colour'} not handled" if ctype != 3 && depth != 8
    bpp_bits = depth * channels
    stride = (w * bpp_bits + 7) / 8
    step = [(bpp_bits + 7) / 8, 1].max
    raw = Zlib::Inflate.inflate(idat).bytes
    prev = Array.new(stride, 0)
    px = Array.new(w * h)
    h.times do |y|
      f = raw[y * (stride + 1)]
      line = raw[y * (stride + 1) + 1, stride]
      stride.times do |i|
        a = i >= step ? line[i - step] : 0
        b = prev[i]
        c = i >= step ? prev[i - step] : 0
        line[i] = (line[i] + case f
                             when 0 then 0
                             when 1 then a
                             when 2 then b
                             when 3 then (a + b) / 2
                             when 4
                               p = a + b - c
                               pa = (p - a).abs
                               pb = (p - b).abs
                               pc = (p - c).abs
                               pa <= pb && pa <= pc ? a : (pb <= pc ? b : c)
                             else raise "#{path}: filter #{f}"
                             end) & 0xFF
      end
      prev = line
      w.times do |x|
        px[y * w + x] =
          if ctype == 3
            v = depth == 8 ? line[x] : (line[x * depth / 8] >> (8 - depth - (x * depth % 8))) & ((1 << depth) - 1)
            to_rgb332(*plte[v])
          elsif ctype == 0 || ctype == 4
            g = line[x * channels]
            to_rgb332(g, g, g)
          else
            to_rgb332(line[x * channels], line[x * channels + 1], line[x * channels + 2])
          end
      end
    end
    [w, h, px]
  end
end
