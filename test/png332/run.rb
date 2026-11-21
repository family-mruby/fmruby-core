# The sprite editor's PNG reader and writer (png332.c) against the Ruby one
# (tools/rgb332_png.rb) over every PNG in flash/: the C reader must give the
# same RGB332 pixels, and what the C writer writes must read back the same in
# Ruby and carry the same header and palette as Rgb332Png.encode. A few
# made-up pictures with alpha and tRNS check that both readers turn their
# transparent pixels into the transparent colour (0x01).
#
#   ruby test/png332/run.rb <png332_test binary> <build dir>

require "fileutils"
require "zlib"
require_relative "../../tools/rgb332_png"

bin, build = ARGV
abort "usage: run.rb <bin> <build dir>" unless bin && build
refs = File.join(build, "ref")
outs = File.join(build, "out")
FileUtils.rm_rf([refs, outs])
FileUtils.mkdir_p([refs, outs])

pngs = `git ls-files "flash/*.png"`.split("\n").sort
abort "no PNG under flash/" if pngs.empty?
args = []
expected = []
pngs.each_with_index do |path, i|
  w, h, px = Rgb332Png.read(path)
  ref = File.join(refs, "#{i}.raw")
  File.binwrite(ref, px.pack("C*"))
  args << path << ref
  expected << [path, w, h, px]
end

# Transparency: one picture per way a PNG says it, each with the pixels both
# readers must give (written out here, not taken from either reader).
def chunk(type, data) = Rgb332Png.chunk(type, data)
def png(w, h, depth, ctype, rows, extra = {})
  raw = rows.map { |r| "\x00".b + r.pack("C*") }.join
  Rgb332Png::SIGNATURE + chunk("IHDR", [w, h, depth, ctype, 0, 0, 0].pack("NNCCCCC")) +
    (extra[:plte] ? chunk("PLTE", extra[:plte].flatten.pack("C*")) : "".b) +
    (extra[:trns] ? chunk("tRNS", extra[:trns].pack("C*")) : "".b) +
    chunk("IDAT", Zlib::Deflate.deflate(raw)) + chunk("IEND", "")
end
t = Rgb332Png::TRANSPARENT
alpha_cases = {
  # RGBA: alpha 0, 127 and 128 (the threshold), 255; red, green, blue, white.
  "rgba" => [png(4, 1, 8, 6, [[255, 0, 0, 0, 0, 255, 0, 127, 0, 0, 255, 128, 255, 255, 255, 255]]),
             4, 1, [t, t, 0x03, 0xFF]],
  "grey_alpha" => [png(3, 1, 8, 4, [[255, 0, 255, 200, 0, 127]]), 3, 1, [t, 0xFF, t]],
  # Palette with tRNS shorter than the palette: entry 2 is opaque.
  "palette_trns" => [png(3, 1, 8, 3, [[0, 1, 2]], plte: [[255, 0, 0], [0, 255, 0], [0, 0, 255]], trns: [0, 200]),
                     3, 1, [t, 0x1C, 0x03]],
  "grey_trns" => [png(3, 1, 8, 0, [[0, 128, 255]], trns: [0, 128]), 3, 1, [0x00, t, 0xFF]],
  "rgb_trns" => [png(2, 1, 8, 2, [[10, 20, 30, 255, 0, 0]], trns: [0, 10, 0, 20, 0, 30]), 2, 1, [t, 0xE0]],
}
alpha_cases.each do |name, (data, w, h, px)|
  path = File.join(refs, "#{name}.png")
  File.binwrite(path, data)
  w2, h2, px2 = Rgb332Png.read(path)
  abort "png332: #{name}: the Ruby reader gives #{px2.inspect}, not #{px.inspect}" if [w2, h2, px2] != [w, h, px]
  ref = File.join(refs, "#{name}.raw")
  File.binwrite(ref, px.pack("C*"))
  args << path << ref
  expected << [path, w, h, px]
end

system(bin, outs, *args) or abort "png332: the C test failed"

# The C writer's files, read by the Ruby reader.
fails = 0
expected.each_with_index do |(path, w, h, px), i|
  out = File.join(outs, "#{i}.png")
  w2, h2, px2 = Rgb332Png.read(out)
  if [w2, h2] != [w, h] || px2 != px
    puts "FAIL: #{path}: the C writer's PNG reads back different in Ruby"
    fails += 1
  end
  # Header (size, bit depth, colour type) and palette as the Ruby writer's.
  ruby = Rgb332Png.encode(w, h, px)
  c = File.binread(out)
  head = ->(d) { d[8, 25 + 12 + d[33, 4].unpack1("N")] }
  if head.call(c) != head.call(ruby)
    puts "FAIL: #{path}: IHDR/PLTE differ from tools/rgb332_png.rb"
    fails += 1
  end
end
%w[synth_noise synth_checker].each do |name|
  Rgb332Png.read(File.join(outs, "#{name}.png"))
end
abort "png332: #{fails} failures" if fails > 0
puts "png332: #{alpha_cases.size} pictures with alpha or tRNS read as transparent in C and Ruby"
puts "png332: #{expected.size} C-written PNGs read back in Ruby with the same pixels, header and palette"
