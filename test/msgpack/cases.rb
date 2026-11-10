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
