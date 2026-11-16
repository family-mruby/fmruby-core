# frozen_string_literal: true
#
# The Japanese font image (doc/flash_layout F4): the tool that writes it
# (tools/font_ja/font_ja_image.rb) and the firmware's reader
# (main/drivers/display_local/fonts/font_ja/font_ja_image.c, built by
# rake font_ja:test into the runner given as the first argument) must agree.
#
#   ruby test/font_ja/run.rb RUNNER

require "tmpdir"
require_relative "../../tools/font_ja/font_ja_image"

RUNNER = ARGV.fetch(0)
$fails = 0
$checks = 0

def check(label, got, want)
  $checks += 1
  return if got == want
  $fails += 1
  puts "FAIL #{label}: got #{got.inspect}, want #{want.inspect}"
end

def run(dir, img, avail, *names)
  path = File.join(dir, "img.bin")
  File.binwrite(path, img)
  out = IO.popen([RUNNER, path, avail.to_s, *names], &:read)
  raise "runner failed" unless $?.success?
  out.lines.map(&:split)
end

def font_line(name, bytes)
  ["font", name, bytes.bytesize.to_s, bytes.getbyte(0).to_s, bytes.getbyte(-1).to_s,
   (bytes.sum(32) & 0xFFFFFFFF).to_s]
end

# --- the C literal reader ----------------------------------------------------

src = <<~'C'
  PROGMEM const uint8_t lgfx_other[3] = "zzz";
  PROGMEM const uint8_t lgfx_font_a[12] =
    "\3\1\0012\60"
    "A\"\\\77\n"
    "x";
  PROGMEM const uint8_t lgfx_font_b[4] = "\377\377\377\377";
C
a = FontJaImage.array_bytes(src, "lgfx_font_a")
# "\001" then "2": an octal escape takes at most three digits.
check("literal bytes", a.bytes, [3, 1, 1, 0x32, 0x30, 0x41, 0x22, 0x5C, 0x3F, 0x0A, 0x78, 0])
check("declared size kept", FontJaImage.array_bytes(src, "lgfx_font_b").bytes, [255] * 4)
begin
  FontJaImage.array_bytes(src.sub("[4]", "[3]"), "lgfx_font_b")
  check("longer than declared raises", :no_error, :error)
rescue RuntimeError
  check("longer than declared raises", :error, :error)
end

# --- images ------------------------------------------------------------------

Dir.mktmpdir do |dir|
  f16 = (0...5001).map { |i| (i * 7) & 0xFF }.pack("C*")
  f12 = (0...3001).map { |i| (i * 13 + 5) & 0xFF }.pack("C*")
  f12b = "\x01\x02\x03".b
  fonts = [["efont_ja_16", f16], ["efont_ja_12", f12], ["efont_ja_12_b", f12b]]
  img = FontJaImage.build(fonts)
  info = FontJaImage.parse(img)

  check("offsets are multiples of 4", info[:fonts].map { |_, o, _| o % 4 }, [0, 0, 0])
  out = run(dir, img, 0, "efont_ja_16", "efont_ja_12", "efont_ja_12_b", "efont_ja_24")
  check("opens", out[0][0..1], ["open", "0"])
  check("info", out[1], ["info", "1", "3", img.bytesize.to_s, format("%08x", info[:crc])])
  check("16", out[2], font_line("efont_ja_16", f16))
  check("12", out[3], font_line("efont_ja_12", f12))
  check("12 bold", out[4], font_line("efont_ja_12_b", f12b))
  check("unknown name", out[5], %w[font efont_ja_24 missing])
  check("a prefix is not a name", run(dir, img, 0, "efont_ja_1")[2], %w[font efont_ja_1 missing])

  # A partition larger than the image (the usual case): the rest is erased flash.
  check("in a larger partition", run(dir, img + ("\xFF".b * 4096), 0, "efont_ja_12")[2],
        font_line("efont_ja_12", f12))

  # What the board meets when the partition is not what this firmware expects.
  check("erased partition", run(dir, "\xFF".b * 4096, 0)[0][0..1], ["open", "-7"])
  newer = img.dup
  newer[8, 2] = [2].pack("v")
  check("another version", run(dir, newer, 0)[0][0..1], ["open", "-4"])
  check("image past the partition", run(dir, img, img.bytesize - 1)[0][0..1], ["open", "-6"])
  broken = img.dup
  broken[24 + 24, 4] = [img.bytesize].pack("V") # the first font starts at the end
  check("entry past the image", run(dir, broken, 0)[0][0..1], ["open", "-6"])
  noterm = img.dup
  noterm[24, 24] = "x" * 24
  check("name without its NUL", run(dir, noterm, 0)[0][0..1], ["open", "-6"])
  begin
    FontJaImage.parse(img.dup.tap { |b| b[-1] = (b.getbyte(-1) ^ 1).chr })
    check("the tool sees a CRC mismatch", :no_error, :error)
  rescue RuntimeError
    check("the tool sees a CRC mismatch", :error, :error)
  end

  # The image the P4 build writes, when one has been made (rake font_ja:image).
  real = File.join(__dir__, "..", "..", "tmp", "font_ja", "font_ja.bin")
  if File.exist?(real)
    rimg = File.binread(real)
    rinfo = FontJaImage.parse(rimg)
    names = FontJaImage::FONTS.map(&:first)
    out = run(dir, rimg, 0, *names)
    check("real image opens", out[0][0..1], ["open", "0"])
    names.each_with_index do |n, i|
      check("real #{n}", out[2 + i], font_line(n, FontJaImage.font(rimg, n)))
    end
    check("real image's fonts", rinfo[:fonts].map(&:first), names)
  end
end

puts "font_ja: #{$checks - $fails}/#{$checks} checks passed"
exit($fails.zero? ? 0 : 1)
