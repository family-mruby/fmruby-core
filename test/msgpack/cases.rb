# The kernel's msgpack reader (main/prebuild_scripts/spinel/msgpack_pure.rb)
# at every integer width and its boundaries. Prints one line per case; run.rb
# runs it on CRuby (with the Integer width set to 8 and to 4 bytes) and, when
# the Spinel compiler is there, compiled by Spinel for 64-bit and 32-bit
# (-m32) machines, and compares the lines. Written in the subset Spinel
# compiles: no blocks over the cases, no Array#pack, no Regexp.

MP_HEX = "0123456789abcdef"

def mp_nibble(c)
  i = 0
  while i < 16
    return i if MP_HEX[i] == c
    i += 1
  end
  0
end

def mp_bin(hex)
  n = hex.length / 2
  s = "\x00" * n
  i = 0
  while i < n
    s.setbyte(i, mp_nibble(hex[i * 2]) * 16 + mp_nibble(hex[i * 2 + 1]))
    i += 1
  end
  s
end

def mp_hex(s)
  out = ""
  i = 0
  while i < s.bytesize
    b = s.getbyte(i)
    out = "#{out}#{MP_HEX[b >> 4]}#{MP_HEX[b & 0xF]}"
    i += 1
  end
  out
end

def mp_show(v)
  if v.is_a?(Integer)
    v.to_s
  elsif v.is_a?(Hash)
    "cmd=#{v["cmd"]} v=#{v["v"]}"
  else
    "other"
  end
end

# decode one message (hex) and print what came out, or why it did not
def mp_case(hex)
  line = "#{hex} => "
  # through a mixed Array, so Spinel types the bytes as a boxed value as in
  # the kernel (msg[:data]) and compiles the reader the way the kernel has it
  holder = [0, mp_bin(hex)]
  begin
    line = line + mp_show(MessagePack.unpack(holder[1]))
  rescue => e
    line = line + "refused: #{e.message}"
  end
  puts line
end

# pack v, print the bytes, and decode them back
def mp_round(v)
  line = "pack #{v} => "
  begin
    bin = MessagePack.pack(v)
    back = MessagePack.unpack(bin)
    line = line + "#{mp_hex(bin)} => #{back}"
    line = line + " MISMATCH" unless back == v
  rescue => e
    line = line + "refused: #{e.message}"
  end
  puts line
end

puts "int_bytes #{MessagePack.int_bytes}"

# fixints
mp_case("00")
mp_case("7f")
mp_case("e0")
mp_case("ff")
# uint8 / uint16
mp_case("cc00")
mp_case("ccff")
mp_case("cd0000")
mp_case("cdffff")
# uint32: 2^31 and up do not fit a 32-bit Integer
mp_case("ce00000000")
mp_case("ce7fffffff")
mp_case("ce80000000")
mp_case("ceffffffff")
# uint64: the values that fit read on both
mp_case("cf0000000000000000")
mp_case("cf000000007fffffff")
mp_case("cf0000000080000000")
mp_case("cf7fffffffffffffff")
mp_case("cf8000000000000000")
mp_case("cfffffffffffffffff")
# int8 / int16
mp_case("d000")
mp_case("d07f")
mp_case("d080")
mp_case("d0ff")
mp_case("d10000")
mp_case("d17fff")
mp_case("d18000")
mp_case("d1ff7f")
mp_case("d1ffff")
# int32: -32769 and below overflowed `<< 8` on a 32-bit machine before
mp_case("d200000000")
mp_case("d27fffffff")
mp_case("d2ffff7fff")
mp_case("d2ffff8000")
mp_case("d2fffeffff")
mp_case("d2ffffffff")
mp_case("d280000001")
mp_case("d280000000")
# int64: the values that fit read on both
mp_case("d30000000000000000")
mp_case("d3000000007fffffff")
mp_case("d30000000080000000")
mp_case("d3ffffffff80000001")
mp_case("d3ffffffff80000000")
mp_case("d3ffffffff7fffffff")
mp_case("d3ffffffffffff7fff")
mp_case("d3ffffffffffffffff")
mp_case("d37fffffffffffffff")
mp_case("d38000000000000001")
mp_case("d38000000000000000")
mp_case("d380000000ffffffff")
# a request as the kernel gets it: {"cmd" => "x", "v" => <int>}
mp_case("82a3636d64a178a176d2ffff63c0")
mp_case("82a3636d64a178a176ce80000000")
mp_case("82a3636d64a178a176cf0000019d1f2e3d4c")

# pack and back (the kernel packs what it forwards)
mp_round(0)
mp_round(127)
mp_round(128)
mp_round(255)
mp_round(256)
mp_round(65535)
mp_round(65536)
mp_round(2147483647)
mp_round(-1)
mp_round(-32)
mp_round(-33)
mp_round(-128)
mp_round(-129)
mp_round(-32768)
mp_round(-32769)
mp_round(-40000)
mp_round(-2147483647)

# ---- floats ----------------------------------------------------------------
# A float is shown by its bits, never by Float#to_s: the kernel re-packs what
# it reads, so "hex => the float64 the kernel packs back" is the whole story,
# and run.rb works the bytes out with CRuby's Array#pack. A 32-bit Integer
# overflowed `<<` reading float32/float64 and packed 1.5 as 2.0 before.

def mp_same_float(a, b)
  return b != b if a != a                  # NaN
  return false if a != b
  return true if a != 0.0
  (1.0 / a < 0.0) == (1.0 / b < 0.0)        # the sign of a zero
end

# decode one float (hex), print the float64 the kernel would pack it back as,
# and whether it equals want (a Float the program spells out)
def mp_fcase(hex, want)
  line = "#{hex} => "
  holder = [0, mp_bin(hex)]
  begin
    back = MessagePack.unpack(holder[1])
    line = line + mp_hex(MessagePack.pack(back))
    line = line + (mp_same_float(back, want) ? " eq" : " ne")
  rescue => e
    line = line + "refused: #{e.message}"
  end
  puts line
end

# decode a message and print what the kernel packs back
def mp_repack(hex)
  line = "#{hex} => "
  holder = [0, mp_bin(hex)]
  begin
    line = line + mp_hex(MessagePack.pack(MessagePack.unpack(holder[1])))
  rescue => e
    line = line + "refused: #{e.message}"
  end
  puts line
end

# pack a Float, print the bytes, decode them back and compare
def mp_fround(label, v)
  line = "fpack #{label} => "
  begin
    holder = [0, v]
    bin = MessagePack.pack(holder[1])
    back = MessagePack.unpack(bin)
    line = line + mp_hex(bin)
    line = line + (mp_same_float(back, v) ? " back" : " MISMATCH")
  rescue => e
    line = line + "refused: #{e.message}"
  end
  puts line
end

mp_inf = 1.0 / 0.0
mp_nan = 0.0 / 0.0

# float64 (0xcb): what mruby apps send (msgpack-c packs a double as 0xcb)
mp_fcase("cb0000000000000000", 0.0)
mp_fcase("cb8000000000000000", -0.0)
mp_fcase("cb3ff8000000000000", 1.5)
mp_fcase("cb3fe6666666666666", 0.7)
mp_fcase("cbc004000000000000", -2.5)
mp_fcase("cbbff8000000000000", -1.5)
mp_fcase("cb400921f9f01b866e", 3.14159)
mp_fcase("cb3ff0000000000000", 1.0)
mp_fcase("cb3fefffffffffffff", 0.9999999999999999)
mp_fcase("cb7fefffffffffffff", 1.7976931348623157e308)
mp_fcase("cbffefffffffffffff", -1.7976931348623157e308)
mp_fcase("cb0010000000000000", 2.2250738585072014e-308)
mp_fcase("cb000fffffffffffff", 2.225073858507201e-308)
mp_fcase("cb0000000000000001", 5.0e-324)
mp_fcase("cb8000000000000001", -5.0e-324)
mp_fcase("cb7ff0000000000000", mp_inf)
mp_fcase("cbfff0000000000000", -mp_inf)
mp_fcase("cb7ff8000000000000", mp_nan)
mp_fcase("cbfff8000000000001", mp_nan)
mp_fcase("cb41dfffffffc00000", 2147483647.0)
mp_fcase("cb4415af1d78b58c40", 1.0e20)
# float32 (0xca): read only; the kernel packs it back as float64
mp_fcase("ca00000000", 0.0)
mp_fcase("ca80000000", -0.0)
mp_fcase("ca3fc00000", 1.5)
mp_fcase("ca3f333333", 0.699999988079071)
mp_fcase("cac0200000", -2.5)
mp_fcase("cabfc00000", -1.5)
mp_fcase("ca40490fd0", 3.141590118408203)
mp_fcase("ca7f7fffff", 3.4028234663852886e38)
mp_fcase("caff7fffff", -3.4028234663852886e38)
mp_fcase("ca00800000", 1.1754943508222875e-38)
mp_fcase("ca000116c2", 9.99994610111476e-41)
mp_fcase("ca00000001", 1.401298464324817e-45)
mp_fcase("ca80000001", -1.401298464324817e-45)
mp_fcase("ca7f800000", mp_inf)
mp_fcase("caff800000", -mp_inf)
mp_fcase("ca7fc00000", mp_nan)
# a request as the kernel gets it: {"cmd" => "x", "v" => 0.7}
mp_repack("82a3636d64a178a176cb3fe6666666666666")
mp_repack("82a3636d64a178a176ca3fc00000")

# pack and back (the kernel packs what it forwards)
mp_fround("0.0", 0.0)
mp_fround("-0.0", -0.0)
mp_fround("1.5", 1.5)
mp_fround("0.7", 0.7)
mp_fround("-2.5", -2.5)
mp_fround("3.14159", 3.14159)
mp_fround("0.1", 0.1)
mp_fround("-123.456", -123.456)
mp_fround("1.0e20", 1.0e20)
mp_fround("6.02e23", 6.02e23)
mp_fround("1.0e-40", 1.0e-40)
mp_fround("1.7976931348623157e308", 1.7976931348623157e308)
mp_fround("2.2250738585072014e-308", 2.2250738585072014e-308)
mp_fround("2.225073858507201e-308", 2.225073858507201e-308)
mp_fround("5.0e-324", 5.0e-324)
mp_fround("-5.0e-324", -5.0e-324)
mp_fround("Infinity", mp_inf)
mp_fround("-Infinity", -mp_inf)
mp_fround("NaN", mp_nan)
