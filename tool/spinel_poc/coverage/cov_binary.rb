# Coverage: binary assembly/parse via String#setbyte/getbyte/<<.
# Mirrors input_router.rb / audio_handler.rb byte handling, including
# embedded-NUL payloads.

# build a 6-byte HID-style packet
def pack_hid(subtype, button, x, y)
  s = "\x00" * 6
  s.setbyte(0, subtype)
  s.setbyte(1, button)
  s.setbyte(2, x & 0xFF)
  s.setbyte(3, (x >> 8) & 0xFF)
  s.setbyte(4, y & 0xFF)
  s.setbyte(5, (y >> 8) & 0xFF)
  s
end

def unpack_hid(s)
  subtype = s.getbyte(0)
  button = s.getbyte(1)
  x = s.getbyte(2) | (s.getbyte(3) << 8)
  y = s.getbyte(4) | (s.getbyte(5) << 8)
  [subtype, button, x, y]
end

[[4, 1, 100, 80], [3, 0, 300, 200], [5, 1, 0, 0], [3, 0, 65535, 12345]].each do |st, bt, x, y|
  pkt = pack_hid(st, bt, x, y)
  got = unpack_hid(pkt)
  puts "in=(#{st},#{bt},#{x},#{y}) size=#{pkt.bytesize} out=#{got.inspect}"
end

# build via << (append bytes as 1-char strings)
buf = ""
[0x02, 0x00, 0xFF, 0x00, 0x41].each do |b|
  one = "\x00"
  one.setbyte(0, b)
  buf << one
end
puts "buf bytesize=#{buf.bytesize}"
i = 0
hexs = ""
while i < buf.bytesize
  v = buf.getbyte(i)
  hexs << "0123456789abcdef"[v >> 4]
  hexs << "0123456789abcdef"[v & 0xF]
  i += 1
end
puts "buf hex=#{hexs}"
