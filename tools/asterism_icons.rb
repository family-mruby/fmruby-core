# The default icons of Asterism's node profiles (doc/ruby_asterism, V2.5),
# drawn here with nothing but the Ruby stdlib (zlib):
#
#   ruby tools/asterism_icons.rb OUTDIR
#
# writes OUTDIR/{family_mruby,browser,cruby,console,demo}.png, 32x32 RGBA. Where
# each one is used:
#   family_mruby.png  flash/usr/share/asterism/icon.png (boards and the sim)
#   browser.png       flash/usr/share/asterism/icon_browser.png, staged as
#                     usr/share/asterism/icon.png in the browser bundle
#   cruby.png         the asterism gem's data/icon.png (CRuby's default)
#   console.png       asterism-console's app/assets/images/asterism_console.png
#   demo.png          flash/app/test/asterism_demo.png, the icon the test
#                     app asterism_demo switches to (an application's own)
# The files are copied by hand when they change; nothing reads this script at
# build time. Anyone may draw their own: a profile icon is any PNG of up to
# 8 KB and 256x256.
require "zlib"
require "fileutils"

module AsterismIcons
  SIZE = 32
  SS = 4 # samples per pixel along each axis

  RUBY = [0xcc, 0x34, 0x2d]
  RUBY_LIGHT = [0xe8, 0x6a, 0x5f]
  RUBY_DARK = [0x8f, 0x1e, 0x19]
  SLATE = [0x26, 0x32, 0x38]
  SLATE_LIGHT = [0x45, 0x5a, 0x64]
  PIN = [0xb0, 0xb7, 0xbf]
  FRAME = [0x5f, 0x63, 0x68]
  PAPER = [0xff, 0xff, 0xff]
  BAR = [0xdf, 0xe3, 0xe8]
  BLUE = [0x1f, 0x4e, 0x99]
  PURPLE = [0x6a, 0x1b, 0x9a]
  GREY = [0x8a, 0x8f, 0x98]
  GOLD = [0xf2, 0xb7, 0x05]
  GOLD_DARK = [0xb3, 0x86, 0x00]

  module_function

  # ---------------------------------------------------------------- shapes
  # Each shape is [colour, test(x, y)] in pixel units (floats).

  def rect(x0, y0, x1, y1, r = 0)
    lambda do |x, y|
      next false unless x >= x0 && x <= x1 && y >= y0 && y <= y1
      next true if r <= 0
      cx = x.clamp(x0 + r, x1 - r)
      cy = y.clamp(y0 + r, y1 - r)
      (x - cx)**2 + (y - cy)**2 <= r * r
    end
  end

  def circle(cx, cy, r)
    ->(x, y) { (x - cx)**2 + (y - cy)**2 <= r * r }
  end

  # A convex polygon (points in order, either direction).
  def poly(*pts)
    lambda do |x, y|
      sign = 0
      pts.each_with_index do |(ax, ay), i|
        bx, by = pts[(i + 1) % pts.size]
        c = (bx - ax) * (y - ay) - (by - ay) * (x - ax)
        next if c.zero?
        s = c.positive? ? 1 : -1
        return false if sign != 0 && s != sign
        sign = s
      end
      true
    end
  end

  def line(x0, y0, x1, y1, w)
    lambda do |x, y|
      dx = x1 - x0
      dy = y1 - y0
      t = (((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy).to_f).clamp(0.0, 1.0)
      px = x0 + t * dx
      py = y0 + t * dy
      (x - px)**2 + (y - py)**2 <= (w / 2.0)**2
    end
  end

  # A cut gem in the box (x0, y0)-(x1, y1): a flat top, a crown band and a
  # pointed bottom, with lighter and darker facets.
  def gem(x0, y0, x1, y1)
    w = x1 - x0
    h = y1 - y0
    top = y0
    band = y0 + h * 0.34
    tip = [x0 + w / 2.0, y1]
    left = [x0, band]
    right = [x1, band]
    tl = [x0 + w * 0.25, top]
    tr = [x1 - w * 0.25, top]
    [
      [RUBY, poly(tl, tr, right, tip, left)],
      [RUBY_LIGHT, poly(tl, [x0 + w / 2.0, top], [x0 + w / 2.0, band], left)],
      [RUBY_DARK, poly([x0 + w / 2.0, band], right, tip)],
      [RUBY_LIGHT, poly(left, [x0 + w / 2.0, band], tip)],
    ]
  end

  # ---------------------------------------------------------------- icons

  def cruby
    gem(3, 5, 29, 28)
  end

  def family_mruby
    pins = []
    [8, 13, 18, 23].each do |p|
      pins << [PIN, rect(p, 1.5, p + 2.2, 6)]
      pins << [PIN, rect(p, 26, p + 2.2, 30.5)]
      pins << [PIN, rect(1.5, p, 6, p + 2.2)]
      pins << [PIN, rect(26, p, 30.5, p + 2.2)]
    end
    pins + [[SLATE, rect(4.5, 4.5, 27.5, 27.5, 3)], [SLATE_LIGHT, rect(6.5, 6.5, 25.5, 25.5, 2)]] +
      gem(9.5, 10.5, 22.5, 22)
  end

  def browser
    [
      [FRAME, rect(1.5, 3.5, 30.5, 28.5, 3)],
      [BAR, rect(3, 5, 29, 9.5, 1.5)],
      [PAPER, rect(3, 10.5, 29, 27, 1.5)],
      [RUBY, circle(5.8, 7.25, 1.25)],
      [GREY, circle(9.4, 7.25, 1.25)],
      [GREY, circle(13.0, 7.25, 1.25)],
    ] + gem(9.5, 12.5, 22.5, 25.5)
  end

  def console
    hub = [16.0, 16.0]
    ends = [[5.5, 6.5], [26.5, 6.5], [5.5, 25.5], [26.5, 25.5]]
    lines = ends.map { |(x, y)| [GREY, line(hub[0], hub[1], x, y, 1.8)] }
    nodes = ends.each_with_index.map do |(x, y), i|
      [[RUBY, PURPLE, RUBY, RUBY][i], circle(x, y, 4.2)]
    end
    lines + nodes + [[BLUE, poly([16, 9.5], [22.5, 16], [16, 22.5], [9.5, 16])]]
  end

  def demo
    [[GOLD_DARK, circle(16, 16, 15)], [GOLD, circle(16, 16, 13)]] + gem(8.5, 9.5, 23.5, 23.5)
  end

  ICONS = %w[family_mruby browser cruby console demo].freeze

  # ---------------------------------------------------------------- raster

  # RGBA rows, each shape painted over the ones before (supersampled).
  def render(shapes)
    rows = []
    SIZE.times do |py|
      row = +""
      SIZE.times do |px|
        acc = [0.0, 0.0, 0.0, 0.0] # premultiplied r, g, b and alpha
        SS.times do |sy|
          SS.times do |sx|
            x = px + (sx + 0.5) / SS
            y = py + (sy + 0.5) / SS
            colour = nil
            shapes.each { |c, test| colour = c if test.call(x, y) }
            next unless colour
            acc[0] += colour[0]
            acc[1] += colour[1]
            acc[2] += colour[2]
            acc[3] += 1
          end
        end
        n = SS * SS
        a = acc[3] / n
        if a.zero?
          row << [0, 0, 0, 0].pack("C4")
        else
          rgb = acc[0, 3].map { |v| (v / acc[3]).round.clamp(0, 255) }
          row << (rgb + [(a * 255).round]).pack("C4")
        end
      end
      rows << row
    end
    rows
  end

  def chunk(type, body)
    [body.bytesize].pack("N") + type + body + [Zlib.crc32(type + body)].pack("N")
  end

  def png(rows)
    raw = rows.map { |r| "\x00".b + r }.join
    "\x89PNG\r\n\x1a\n".b +
      chunk("IHDR", [SIZE, SIZE, 8, 6, 0, 0, 0].pack("NNC5")) +
      chunk("IDAT", Zlib::Deflate.deflate(raw, Zlib::BEST_COMPRESSION)) +
      chunk("IEND", "")
  end
end

if $PROGRAM_NAME == __FILE__
  out = ARGV[0] or abort("usage: ruby #{$PROGRAM_NAME} OUTDIR")
  FileUtils.mkdir_p(out)
  AsterismIcons::ICONS.each do |name|
    bytes = AsterismIcons.png(AsterismIcons.render(AsterismIcons.send(name)))
    File.binwrite(File.join(out, "#{name}.png"), bytes)
    puts "#{name}.png: #{bytes.bytesize} bytes"
  end
end
