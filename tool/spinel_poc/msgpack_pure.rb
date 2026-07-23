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

module MessagePackPure
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

  # float64 (0xcb) assembled byte-by-byte to stay within a 63-bit int and
  # avoid Array#pack. Handles zero and normal doubles (inf/nan/subnormal are
  # out of scope -- the VM messages carry none).
  def self.pack_float64(v, out)
    out << 0xcb
    if v == 0.0
      append_be(out, 0, 8)
      return
    end
    sign = 0
    av = v
    if v < 0.0
      sign = 1
      av = -v
    end
    # normalize: find e with 1.0 <= av / 2**e < 2.0
    e = 0
    while av >= 2.0
      av = av / 2.0
      e += 1
    end
    while av < 1.0
      av = av * 2.0
      e -= 1
    end
    frac = av - 1.0
    mant = (frac * 4503599627370496.0).round   # * 2**52
    if mant >= 4503599627370496              # rounding overflow -> bump exponent
      mant = 0
      e += 1
    end
    bexp = e + 1023
    b0 = (sign << 7) | ((bexp >> 4) & 0x7F)
    b1 = ((bexp & 0xF) << 4) | ((mant >> 48) & 0xF)
    out << b0
    out << b1
    append_be(out, mant & 0xFFFFFFFFFFFF, 6)
  end

  # ---- unpack -----------------------------------------------------------

  def self.unpack(bin)
    pos = [0]
    unpack_value(bin, pos)
  end

  # read n bytes as an unsigned big-endian integer
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
    when 0xcc then read_be(bin, pos, 1)                    # uint8
    when 0xcd then read_be(bin, pos, 2)                    # uint16
    when 0xce then read_be(bin, pos, 4)                    # uint32
    when 0xcf then read_be(bin, pos, 8)                    # uint64
    when 0xd0 then to_signed(read_be(bin, pos, 1), 8)      # int8
    when 0xd1 then to_signed(read_be(bin, pos, 2), 16)     # int16
    when 0xd2 then to_signed(read_be(bin, pos, 4), 32)     # int32
    when 0xd3 then to_signed(read_be(bin, pos, 8), 64)     # int64
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

  def self.to_signed(v, bits)
    half = 1 << (bits - 1)
    v >= half ? v - (1 << bits) : v
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

  def self.unpack_float64(bin, pos)
    b0 = bin.getbyte(pos[0]); b1 = bin.getbyte(pos[0] + 1)
    pos[0] += 2
    mant = read_be(bin, pos, 6)
    sign = (b0 >> 7) & 1
    bexp = ((b0 & 0x7F) << 4) | ((b1 >> 4) & 0xF)
    mant = ((b1 & 0xF) << 48) | mant
    return (sign == 1 ? -0.0 : 0.0) if bexp == 0 && mant == 0
    val = (1.0 + mant.to_f / 4503599627370496.0) * (2.0 ** (bexp - 1023))
    sign == 1 ? -val : val
  end

  def self.unpack_float32(bin, pos)
    bits = read_be(bin, pos, 4)
    sign = (bits >> 31) & 1
    bexp = (bits >> 23) & 0xFF
    mant = bits & 0x7FFFFF
    return (sign == 1 ? -0.0 : 0.0) if bexp == 0 && mant == 0
    val = (1.0 + mant.to_f / 8388608.0) * (2.0 ** (bexp - 127))
    sign == 1 ? -val : val
  end
end
