# Tests for MessagePackPure. Runs identically on CRuby and Spinel.
#   ruby test_msgpack.rb
#   ./spinel -E test_msgpack.rb
#
# Deterministic; no wall clock / randomness. Floats are checked by an
# integer-scaled comparison (never a raw Float ==) per phase0.md.

require_relative "msgpack_pure"

# On a 32-bit mrb_int build (ESP32 / -m32) integers are 32-bit, so msgpack
# types that need >32-bit values -- uint32 with the high bit set, uint64,
# int32/int64, and float64 (its 52-bit mantissa is assembled as an integer) --
# cannot be represented. The fmruby VM messages use none of these (small ints,
# strings, bin, arrays, string-keyed maps only), so the kernel-relevant subset
# is dual-build safe; the 64-bit-only cases are skipped on a 32-bit build.
# 65536*65536 overflows a 32-bit mrb_int (raise mode) but is fine on 64-bit.
IS_64BIT = begin
  (65536 * 65536) > 0
rescue => _
  false
end

$fail = 0
$pass = 0
$skip = 0

def hexs(s)
  out = ""
  i = 0
  while i < s.bytesize
    b = s.getbyte(i)
    hi = b >> 4
    lo = b & 0xF
    out << "0123456789abcdef"[hi]
    out << "0123456789abcdef"[lo]
    i += 1
  end
  out
end

def assert_eq(label, got, want)
  if got == want
    $pass += 1
  else
    $fail += 1
    puts "FAIL #{label}: got #{got.inspect} want #{want.inspect}"
  end
end

# ---- fixed wire vectors (hand-computed / spec examples) -----------------
# pack(value) must equal the exact byte string.

def assert_pack(label, value, hex)
  got = hexs(MessagePackPure.pack(value))
  assert_eq("pack #{label}", got, hex)
end

assert_pack("nil",        nil,     "c0")
assert_pack("true",       true,    "c3")
assert_pack("false",      false,   "c2")
assert_pack("fixint 0",   0,       "00")
assert_pack("fixint 127", 127,     "7f")
assert_pack("uint8 128",  128,     "cc80")
assert_pack("uint8 255",  255,     "ccff")
assert_pack("uint16 256", 256,     "cd0100")
assert_pack("uint16 max", 65535,   "cdffff")
assert_pack("uint32",     65536,   "ce00010000")
if IS_64BIT
  assert_pack("uint32 big", 4294967295, "ceffffffff")
  assert_pack("uint64",     4294967296, "cf0000000100000000")
else
  $skip += 2
end
assert_pack("neg fixint -1",  -1,  "ff")
assert_pack("neg fixint -32", -32, "e0")
assert_pack("int8 -33",   -33,     "d0df")
assert_pack("int8 -128",  -128,    "d080")
assert_pack("int16 -129", -129,    "d1ff7f")
assert_pack("int16 -32768", -32768, "d18000")
assert_pack("int32",      -32769,  "d2ffff7fff")
assert_pack("fixstr empty", "",    "a0")
assert_pack("fixstr a",   "a",     "a161")
assert_pack("fixstr abc", "abc",   "a3616263")
# str8: 32-byte string
s32 = "x" * 32
assert_pack("str8 len32", s32, "d920" + ("78" * 32))
assert_pack("fixarray []", [],    "90")
assert_pack("fixarray [1,2,3]", [1, 2, 3], "93010203")
assert_pack("fixmap {}",  {},     "80")
assert_pack("fixmap {a:1}", { "a" => 1 }, "81a16101")

# a NUL-containing binary string (the HID case) round-trips through str
assert_pack("str with NUL", "\x04\x01\x00\x00", "a404010000")

# ---- roundtrip (pack -> unpack == original) -----------------------------

def assert_roundtrip(label, value)
  packed = MessagePackPure.pack(value)
  got = MessagePackPure.unpack(packed)
  assert_eq("roundtrip #{label}", got, value)
end

assert_roundtrip("nil", nil)
assert_roundtrip("true", true)
assert_roundtrip("false", false)
assert_roundtrip("0", 0)
assert_roundtrip("1", 1)
assert_roundtrip("127", 127)
assert_roundtrip("128", 128)
assert_roundtrip("255", 255)
assert_roundtrip("256", 256)
assert_roundtrip("65535", 65535)
assert_roundtrip("65536", 65536)
assert_roundtrip("-1", -1)
assert_roundtrip("-32", -32)
assert_roundtrip("-33", -33)
assert_roundtrip("-128", -128)
assert_roundtrip("-129", -129)
assert_roundtrip("-32768", -32768)
if IS_64BIT
  # >32-bit / int32 two's-complement decode: 64-bit mrb_int only
  assert_roundtrip("4294967295", 4294967295)
  assert_roundtrip("4294967296", 4294967296)
  assert_roundtrip("-32769", -32769)
else
  $skip += 3
end
assert_roundtrip("empty str", "")
assert_roundtrip("abc", "abc")
assert_roundtrip("long str", "y" * 300)
assert_roundtrip("nul str", "\x00\x01\x00\x02")
assert_roundtrip("array", [1, "a", nil, true, [2, 3]])
assert_roundtrip("map", { "cmd" => "spawn", "pid" => 7, "arg" => nil })
assert_roundtrip("nested", { "cmd" => "fwd", "data" => { "x" => 10, "y" => -5 }, "list" => [1, 2, 3] })
# the real kernel resize-preview message shape
assert_roundtrip("resize_preview",
                 { "cmd" => "resize_preview_update", "x" => 20, "y" => 20, "w" => 200, "h" => 150 })

# ---- float64: integer-scaled comparison ---------------------------------

def assert_float(label, value)
  packed = MessagePackPure.pack(value)
  got = MessagePackPure.unpack(packed)
  # compare scaled-to-int to avoid raw Float equality across engines
  gi = (got * 1000000.0).round
  wi = (value * 1000000.0).round
  assert_eq("float #{label}", gi, wi)
end

# float64 assembles a 52-bit mantissa as an integer, so it needs a 64-bit
# mrb_int. The fmruby VM messages carry no floats; skip on a 32-bit build.
if IS_64BIT
  assert_float("0.0", 0.0)
  assert_float("1.0", 1.0)
  assert_float("1.5", 1.5)
  assert_float("-2.25", -2.25)
  assert_float("3.14159", 3.14159)
  assert_float("100.0", 100.0)
  assert_float("0.5", 0.5)
  assert_float("-0.125", -0.125)
else
  $skip += 8
end

puts "msgpack tests: #{$pass} pass, #{$fail} fail, #{$skip} skip (64-bit-only), 64bit=#{IS_64BIT}"
