# Pure-Ruby MessagePack subset for the fmruby VM-message wire format.
#
# Scope (matches doc/spinel_aot/phase0.md T0-5): nil / false / true /
# integers (fixint, int/uint 8..64) / float64 / String (fixstr, str8/16/32) /
# binary (bin8/16/32) / Array (fixarray, array16/32) / Map (fixmap, map16/32).
# Map keys are Strings only (the fmruby VM convention). Big-endian, per spec.
#
# Dual-build constraint: no Array#pack (absent in picoruby) -- everything is
# built and parsed with String#getbyte / #setbyte / #<<. Works byte-identically
# on CRuby and Spinel; syntactically within the picoruby subset.
#
# This is designed to eventually replace the C MessagePack binding used by the
# kernel VM. It is not wired into the build yet (Phase 2).

module MessagePack
  # ---- pack -------------------------------------------------------------
  #
  # The wire bytes are accumulated into an Array of Integers (0..255) and
  # converted to a String only at the very end (bytes_to_str). This sidesteps
  # both CRuby's UTF-8/ASCII-8BIT concatenation errors and Spinel's missing
  # Integer#chr -- the only string ops used are `"\x00" * n` + setbyte, both
  # proven byte-identical on CRuby and Spinel.

  def self.pack(obj)
    out = []
    pack_value(obj, out)
    bytes_to_str(out)
  end

  def self.bytes_to_str(bytes)
    n = bytes.size
    s = "\x00" * n
    i = 0
    while i < n
      s.setbyte(i, bytes[i])
      i += 1
    end
    s
  end

  # push every byte of a String into the byte accumulator
  def self.push_str_bytes(out, s)
    i = 0
    n = s.bytesize
    while i < n
      out << s.getbyte(i)
      i += 1
    end
  end

  def self.pack_value(v, out)
    if v.nil?
      out << 0xc0
    elsif v == true
      out << 0xc3
    elsif v == false
      out << 0xc2
    elsif v.is_a?(Integer)
      pack_int(v, out)
    elsif v.is_a?(Float)
      pack_float64(v, out)
    elsif v.is_a?(String)
      pack_string(v, out)
    elsif v.is_a?(Array)
      pack_array(v, out)
    elsif v.is_a?(Hash)
      pack_map(v, out)
    else
      raise "MessagePackPure: unsupported type for #{v.inspect}"
    end
  end

  # Append the low `n` bytes of `val` to `out`, big-endian.
  def self.append_be(out, val, n)
    i = n - 1
    while i >= 0
      out << ((val >> (i * 8)) & 0xFF)
      i -= 1
    end
  end

  def self.pack_int(v, out)
    if v >= 0
      if v < 128
        out << v                                       # positive fixint
      elsif v < 256
        out << 0xcc; out << v                          # uint8
      elsif v < 65536
        out << 0xcd; append_be(out, v, 2)             # uint16
      # Use (v >> 16) >> 16 instead of a `< 4294967296` literal: on a 32-bit
      # mrb_int (ESP32) that literal overflows and misroutes even small values.
      # This double shift is 0 for anything that fits 32 bits (always so on a
      # 32-bit build) and non-zero only for genuine >2^32 values on 64-bit.
      elsif ((v >> 16) >> 16) == 0
        out << 0xce; append_be(out, v, 4)             # uint32
      else
        out << 0xcf; append_be(out, v, 8)             # uint64 (64-bit mrb_int only)
      end
    else
      if v >= -32
        out << (0x100 + v)                             # negative fixint (0xe0..0xff)
      elsif v >= -128
        out << 0xd0; out << ((0x100 + v) & 0xFF)       # int8
      elsif v >= -32768
        out << 0xd1; append_be(out, v & 0xFFFF, 2)     # int16
      elsif v >= -2147483648
        out << 0xd2; append_be(out, v & 0xFFFFFFFF, 4) # int32
      else
        out << 0xd3; append_be(out, v & 0xFFFFFFFFFFFFFFFF, 8) # int64
      end
    end
  end

  def self.pack_string(s, out)
    n = s.bytesize
    if n < 32
      out << (0xa0 | n)                                # fixstr
    elsif n < 256
      out << 0xd9; out << n                            # str8
    elsif n < 65536
      out << 0xda; append_be(out, n, 2)               # str16
    else
      out << 0xdb; append_be(out, n, 4)               # str32
    end
    push_str_bytes(out, s)
  end

  def self.pack_array(a, out)
    n = a.size
    if n < 16
      out << (0x90 | n)                                # fixarray
    elsif n < 65536
      out << 0xdc; append_be(out, n, 2)               # array16
    else
      out << 0xdd; append_be(out, n, 4)               # array32
    end
    a.each { |e| pack_value(e, out) }
  end

  def self.pack_map(h, out)
    n = h.size
    if n < 16
      out << (0x80 | n)                                # fixmap
    elsif n < 65536
      out << 0xde; append_be(out, n, 2)               # map16
    else
      out << 0xdf; append_be(out, n, 4)               # map32
    end
    h.each do |k, val|
      # keys are strings only
      ks = k.is_a?(String) ? k : k.to_s
      pack_string(ks, out)
      pack_value(val, out)
    end
  end

  # float64 (0xcb), every double: signed zeros, subnormals, infinities and
  # NaN (written as the quiet NaN 7ff8000000000000) included. No Integer on
  # the way is wider than 16 bits: the 52-bit mantissa stays a Float (an
  # exact whole number below 2^52) and is cut into 16-bit pieces by dividing
  # by powers of two, so the bytes are the same on a 64-bit and a 32-bit
  # Integer (ESP32, wasm32, where Spinel's sp_int is 32-bit). Spinel's Float
  # is a C double on every machine, so the kernel holds a float64 exactly.
  def self.pack_float64(v, out)
    f = v.to_f
    out << 0xcb
    if f != f                                 # NaN
      out << 0x7f; out << 0xf8
      append_be(out, 0, 6)
      return
    end
    sign = 0
    if f < 0.0 || (f == 0.0 && 1.0 / f < 0.0) # -0.0: 1.0 / -0.0 is -Infinity
      sign = 1
      f = -f
    end
    bexp = 0
    m = 0.0                                   # the mantissa field, 0 .. 2^52 - 1
    if f > 1.7976931348623157e308             # Infinity
      bexp = 2047
    elsif f >= 2.2250738585072014e-308        # normal: find e with 1.0 <= f / 2**e < 2.0
      e = 0
      while f >= 4294967296.0                 # 2^32 at a time, then 2 at a time
        f = f / 4294967296.0
        e += 32
      end
      while f < 2.3283064365386963e-10        # 2^-32
        f = f * 4294967296.0
        e -= 32
      end
      while f >= 2.0
        f = f / 2.0
        e += 1
      end
      while f < 1.0
        f = f * 2.0
        e -= 1
      end
      bexp = e + 1023
      m = (f - 1.0) * 4503599627370496.0      # * 2^52, exact
    elsif f > 0.0                             # subnormal: f / 2^-1074, in two exact steps
      m = f * 4.49423283715579e+307 * 4503599627370496.0 # * 2^1022 * 2^52
    end
    p0 = (m / 281474976710656.0).floor        # bits 48..51
    m -= p0 * 281474976710656.0
    p1 = (m / 4294967296.0).floor             # bits 32..47
    m -= p1 * 4294967296.0
    p2 = (m / 65536.0).floor                  # bits 16..31
    p3 = (m - p2 * 65536.0).floor             # bits 0..15
    out << ((sign << 7) | (bexp >> 4))
    out << (((bexp & 0xF) << 4) | p0)
    append_be(out, p1, 2)
    append_be(out, p2, 2)
    append_be(out, p3, 2)
  end

  # ---- unpack -----------------------------------------------------------

  def self.unpack(bin)
    pos = [0]
    unpack_value(bin, pos)
  end

  # The bytes of this machine's Integer: 8 on CRuby and a 64-bit Spinel
  # build, 4 on a 32-bit one (ESP32, wasm32), where Spinel's sp_int is
  # intptr_t. Spinel folds Integer#size to sizeof(sp_int).
  def self.int_bytes
    0.size
  end

  # Reads an n-byte big-endian integer, two's complement when signed (kind
  # names the msgpack type for the error). Every value the machine's Integer
  # holds decodes, and no step on the way is larger than the result: a
  # negative value is built from its complement (-1 - v), so int32 down to
  # -2^31 + 1 reads on a 32-bit machine without overflowing `<<`. A value
  # the Integer cannot hold raises with its type and bytes instead of
  # overflowing part-way. Its most negative value (-2^31 on 32-bit, -2^63 on
  # 64-bit) is one of these: in Spinel it is the Integer's nil marker
  # (SP_INT_NIL), which no arithmetic accepts.
  def self.read_int(bin, pos, n, signed, kind)
    p0 = pos[0]
    pos[0] = p0 + n
    neg = signed && bin.getbyte(p0) >= 0x80
    ib = int_bytes
    skip = 0
    if n >= ib
      skip = n - ib
      ext = neg ? 0xFF : 0
      fits = true
      i = 0
      while i < skip
        fits = false if bin.getbyte(p0 + i) != ext
        i += 1
      end
      top = bin.getbyte(p0 + skip)
      fits = false if (top >= 0x80) != neg
      if fits && neg && top == 0x80
        low = 0
        i = skip + 1
        while i < n
          low = low | bin.getbyte(p0 + i)
          i += 1
        end
        fits = false if low == 0
      end
      raise int_too_wide(bin, p0, n, kind, ib) unless fits
    end
    v = 0
    i = skip
    while i < n
      b = bin.getbyte(p0 + i)
      b = b ^ 0xFF if neg
      v = (v << 8) | b
      i += 1
    end
    neg ? -1 - v : v
  end

  # The reason an integer is refused: its type, its bytes and the width it
  # did not fit (the value itself cannot be shown -- it is not an Integer here).
  def self.int_too_wide(bin, p0, n, kind, ib)
    digits = "0123456789abcdef"
    hex = ""
    i = 0
    while i < n
      b = bin.getbyte(p0 + i)
      # interpolation, not <<: a String literal is frozen in Spinel
      hex = "#{hex}#{digits[b >> 4]}#{digits[b & 0xF]}"
      i += 1
    end
    "MessagePack: #{kind} 0x#{hex} does not fit this machine's #{ib * 8}-bit Integer"
  end

  # read n bytes as an unsigned big-endian integer (lengths and float bits)
  def self.read_be(bin, pos, n)
    v = 0
    i = 0
    while i < n
      v = (v << 8) | bin.getbyte(pos[0])
      pos[0] += 1
      i += 1
    end
    v
  end

  # Byte-exact substring: CRuby's bin[i, n] slices by CHARACTER (wrong for
  # binary data with high bytes) and Spinel raises on it here; build from
  # getbyte instead so the result is byte-identical on both.
  def self.read_bytes(bin, pos, n)
    tmp = []
    i = 0
    while i < n
      tmp << bin.getbyte(pos[0])
      pos[0] += 1
      i += 1
    end
    bytes_to_str(tmp)
  end

  def self.unpack_value(bin, pos)
    c = bin.getbyte(pos[0])
    pos[0] += 1

    if c < 0x80
      return c                              # positive fixint
    elsif c >= 0xe0
      return c - 0x100                      # negative fixint
    elsif c >= 0x80 && c <= 0x8f
      return unpack_map(bin, pos, c & 0x0F) # fixmap
    elsif c >= 0x90 && c <= 0x9f
      return unpack_array(bin, pos, c & 0x0F) # fixarray
    elsif c >= 0xa0 && c <= 0xbf
      return read_bytes(bin, pos, c & 0x1F) # fixstr
    end

    case c
    when 0xc0 then nil
    when 0xc2 then false
    when 0xc3 then true
    when 0xcc then read_int(bin, pos, 1, false, "uint8")
    when 0xcd then read_int(bin, pos, 2, false, "uint16")
    when 0xce then read_int(bin, pos, 4, false, "uint32")
    when 0xcf then read_int(bin, pos, 8, false, "uint64")
    when 0xd0 then read_int(bin, pos, 1, true, "int8")
    when 0xd1 then read_int(bin, pos, 2, true, "int16")
    when 0xd2 then read_int(bin, pos, 4, true, "int32")
    when 0xd3 then read_int(bin, pos, 8, true, "int64")
    when 0xca then unpack_float32(bin, pos)                # float32 (decode only)
    when 0xcb then unpack_float64(bin, pos)                # float64
    when 0xc4 then read_bytes(bin, pos, read_be(bin, pos, 1)) # bin8
    when 0xc5 then read_bytes(bin, pos, read_be(bin, pos, 2)) # bin16
    when 0xc6 then read_bytes(bin, pos, read_be(bin, pos, 4)) # bin32
    when 0xd9 then read_bytes(bin, pos, read_be(bin, pos, 1)) # str8
    when 0xda then read_bytes(bin, pos, read_be(bin, pos, 2)) # str16
    when 0xdb then read_bytes(bin, pos, read_be(bin, pos, 4)) # str32
    when 0xdc then unpack_array(bin, pos, read_be(bin, pos, 2)) # array16
    when 0xdd then unpack_array(bin, pos, read_be(bin, pos, 4)) # array32
    when 0xde then unpack_map(bin, pos, read_be(bin, pos, 2))   # map16
    when 0xdf then unpack_map(bin, pos, read_be(bin, pos, 4))   # map32
    else
      raise "MessagePackPure: unknown prefix 0x#{c.to_s(16)}"
    end
  end

  def self.unpack_array(bin, pos, n)
    a = []
    i = 0
    while i < n
      a << unpack_value(bin, pos)
      i += 1
    end
    a
  end

  def self.unpack_map(bin, pos, n)
    h = {}
    i = 0
    while i < n
      k = unpack_value(bin, pos)
      v = unpack_value(bin, pos)
      h[k] = v
      i += 1
    end
    h
  end

  # float64 (0xcb) and float32 (0xca), every value: signed zeros,
  # subnormals, infinities and NaN included. As in pack_float64 the mantissa
  # is put together as a Float from 16-bit pieces, so no Integer on the way
  # is wider than 16 bits (a 32-bit sp_int overflowed `<<` here before). A
  # float32 converts to a double exactly.
  def self.unpack_float64(bin, pos)
    p0 = pos[0]
    pos[0] = p0 + 8
    b0 = bin.getbyte(p0)
    b1 = bin.getbyte(p0 + 1)
    m = (b1 & 0xF) * 65536.0 + (bin.getbyte(p0 + 2) * 256 + bin.getbyte(p0 + 3))
    m = m * 65536.0 + (bin.getbyte(p0 + 4) * 256 + bin.getbyte(p0 + 5))
    m = m * 65536.0 + (bin.getbyte(p0 + 6) * 256 + bin.getbyte(p0 + 7))
    bexp = ((b0 & 0x7F) << 4) | (b1 >> 4)
    float_from_parts(b0 >> 7, bexp, m, 2047, 52, 4503599627370496.0)
  end

  def self.unpack_float32(bin, pos)
    p0 = pos[0]
    pos[0] = p0 + 4
    b0 = bin.getbyte(p0)
    b1 = bin.getbyte(p0 + 1)
    m = (b1 & 0x7F) * 65536.0 + (bin.getbyte(p0 + 2) * 256 + bin.getbyte(p0 + 3))
    bexp = ((b0 & 0x7F) << 1) | (b1 >> 7)
    float_from_parts(b0 >> 7, bexp, m, 255, 23, 8388608.0)
  end

  # The value of an IEEE 754 binary float from its sign bit, its biased
  # exponent field (all ones = Infinity or NaN), its mantissa field as a
  # Float (m) and the mantissa's width (mbits, scale = 2^mbits).
  def self.float_from_parts(sign, bexp, m, emax, mbits, scale)
    bias = emax >> 1
    if bexp == emax
      return 0.0 / 0.0 if m != 0.0            # NaN
      val = 1.0 / 0.0                         # Infinity
    elsif bexp == 0                           # zero and subnormals: m * 2^(1 - bias - mbits)
      val = (m / scale) * (2.0 ** (1 - bias))
    else
      val = (1.0 + m / scale) * (2.0 ** (bexp - bias))
    end
    sign == 1 ? -val : val
  end
end
