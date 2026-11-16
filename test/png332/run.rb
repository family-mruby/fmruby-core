# The sprite editor's PNG reader and writer (png332.c) against the Ruby one
# (tools/rgb332_png.rb) over every PNG in flash/: the C reader must give the
# same RGB332 pixels, and what the C writer writes must read back the same in
# Ruby and carry the same header and palette as Rgb332Png.encode.
#
#   ruby test/png332/run.rb <png332_test binary> <build dir>

require "fileutils"
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
puts "png332: #{expected.size} C-written PNGs read back in Ruby with the same pixels, header and palette"
