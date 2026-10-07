# Asterism::CDR: the CDR encoding that ROS 2 messages use on the wire
# (doc/ruby_asterism/design.md ch. 5, R1).
#
#   w = Asterism::CDR::Writer.new        # starts with the 4-byte header
#   w.string("hello")                    # uint32 length (with the NUL) + bytes + NUL
#   bytes = w.to_s                       # "\x00\x01\x00\x00\x06\x00\x00\x00hello\x00"
#   r = Asterism::CDR::Reader.new(bytes) # either byte order
#   r.string                             # => "hello"
#
# Plain CDR (XCDR1) as rmw_zenoh writes it: a 4-byte encapsulation header
# (00 01 00 00 = little endian, 00 00 00 00 = big endian), then each value
# aligned to its own size counted from the end of the header. Writes are
# always little endian. Primitives, strings, arrays and sequences are here;
# message layouts are built from them by the generated types
# (tools/asterism_msggen.rb, loaded with Asterism::ROS.require_type).
#
# Pure Ruby. Integers are set byte by byte; floats use Array#pack /
# String#unpack when the VM has them (mruby-pack, CRuby) and fall back to
# arithmetic (CDR.f64_bytes and friends) when it does not.
module Asterism
  module CDR
    # Malformed or truncated CDR.
    class DecodeError < ::StandardError; end

    HEADER_LE = "\x00\x01\x00\x00".freeze
    HEADER_BE = "\x00\x00\x00\x00".freeze

    # Array#pack / String#unpack are there (mruby-pack, CRuby).
    PACK = [].respond_to?(:pack) && "".respond_to?(:unpack)
    MATH_LDEXP = Object.const_defined?(:Math) && ::Math.respond_to?(:ldexp)
    MATH_FREXP = Object.const_defined?(:Math) && ::Math.respond_to?(:frexp)
    # Strings carry an encoding (CRuby): the buffers are binary, decoded
    # strings UTF-8. mruby has no encodings and skips this.
    ENCODINGS = "".respond_to?(:force_encoding)

    # A binary copy of s (CRuby); s itself where there are no encodings.
    def self.binary(s)
      ENCODINGS ? s.dup.force_encoding("BINARY") : s
    end

    def self.utf8(s)
      ENCODINGS ? s.force_encoding("UTF-8") : s
    end

    # n bytes of v (two's complement), little endian.
    def self.le_bytes(v, n)
      v = v.to_i
      s = ENCODINGS ? ::Asterism::CDR.binary("\x00" * n) : "\x00" * n
      i = 0
      while i < n
        s.setbyte(i, (v >> (8 * i)) & 0xff)
        i += 1
      end
      s
    end

    # A binary String from a String or an Array of byte values.
    def self.byte_string(v)
      return binary("") if v.nil?
      return binary(v) if v.is_a?(::String)
      s = binary("\x00" * v.size)
      i = 0
      while i < v.size
        s.setbyte(i, v[i].to_i & 0xff)
        i += 1
      end
      s
    end

    # x * 2**e, exact while the result is representable (e may be far
    # from 0: x is scaled in steps, never 2**e on its own).
    def self.ldexp(x, e)
      return ::Math.ldexp(x, e) if MATH_LDEXP
      x = x.to_f
      while e > 0
        step = e > 60 ? 60 : e
        x *= (1 << step)
        e -= step
      end
      while e < 0
        step = e < -60 ? 60 : -e
        x /= (1 << step)
        e += step
      end
      x
    end

    # [mantissa in [0.5, 1), exponent] of a positive finite Float.
    def self.frexp(a)
      return ::Math.frexp(a) if MATH_FREXP
      e = 0
      while a >= 1.0
        a /= 2.0
        e += 1
      end
      while a < 0.5
        a *= 2.0
        e -= 1
      end
      [a, e]
    end

    # x rounded to an Integer, ties to even (x >= 0).
    def self.round_even(x)
      i = x.floor
      d = x - i
      i += 1 if d > 0.5 || (d == 0.5 && i.odd?)
      i
    end

    # IEEE 754 bits of a Float as [sign, biased exponent, fraction]
    # for a format with mbits fraction bits and the given bias.
    def self.float_fields(v, mbits, ebits)
      bias = (1 << (ebits - 1)) - 1
      emax = (1 << ebits) - 1
      return [0, emax, 1 << (mbits - 1)] if v != v # NaN
      sign = v < 0 || (v == 0 && 1.0 / v < 0) ? 1 : 0
      a = v < 0 ? -v : v
      return [sign, 0, 0] if a == 0
      return [sign, emax, 0] if a == ::Float::INFINITY
      m, e = ::Asterism::CDR.frexp(a)
      biased = e - 1 + bias
      if biased >= 1
        frac = ::Asterism::CDR.round_even(::Asterism::CDR.ldexp(m * 2.0 - 1.0, mbits))
        if frac == (1 << mbits)
          frac = 0
          biased += 1
        end
        return [sign, emax, 0] if biased >= emax
        [sign, biased, frac]
      else
        # Subnormal: a / 2**(1 - bias - mbits).
        frac = ::Asterism::CDR.round_even(::Asterism::CDR.ldexp(a, bias - 1 + mbits))
        return [sign, 1, 0] if frac == (1 << mbits)
        [sign, 0, frac]
      end
    end

    # 8 bytes (little endian) of a float64, without Array#pack.
    def self.f64_bytes(v)
      sign, ex, frac = float_fields(v.to_f, 52, 11)
      hi = (sign << 31) | (ex << 20) | (frac >> 32)
      le_bytes(frac & 0xffffffff, 4) + le_bytes(hi, 4)
    end

    # 4 bytes (little endian) of a float32, without Array#pack.
    def self.f32_bytes(v)
      sign, ex, frac = float_fields(v.to_f, 23, 8)
      le_bytes((sign << 31) | (ex << 23) | frac, 4)
    end

    # A Float from its IEEE 754 fields.
    def self.float_from(sign, ex, frac, mbits, ebits)
      bias = (1 << (ebits - 1)) - 1
      emax = (1 << ebits) - 1
      v = if ex == emax
            frac == 0 ? ::Float::INFINITY : ::Float::NAN
          elsif ex == 0
            ldexp(frac, 1 - bias - mbits)
          else
            ldexp(frac + (1 << mbits), ex - bias - mbits)
          end
      sign == 1 ? -v : v
    end

    # Float from 8 little-endian bytes (lo: low 32 bits, hi: high 32 bits).
    def self.f64_from(lo, hi)
      float_from(hi >> 31, (hi >> 20) & 0x7ff, ((hi & 0xfffff) << 32) | lo, 52, 11)
    end

    def self.f32_from(bits)
      float_from(bits >> 31, (bits >> 23) & 0xff, bits & 0x7fffff, 23, 8)
    end

    class Writer
      def initialize
        # A new String each time (the constant must never be appended to).
        @buf = ENCODINGS ? ::Asterism::CDR.binary(HEADER_LE) : "" + HEADER_LE
      end

      # Pads with zeros to a multiple of n, counted from the end of the header.
      def align(n)
        rem = (@buf.bytesize - 4) % n
        @buf << ("\x00" * (n - rem)) if rem > 0
        self
      end

      def uint8(v)
        @buf << ::Asterism::CDR.le_bytes(v, 1)
        self
      end

      def bool(v)
        uint8(v ? 1 : 0)
      end

      def uint16(v)
        align(2)
        @buf << ::Asterism::CDR.le_bytes(v, 2)
        self
      end

      def uint32(v)
        align(4)
        @buf << ::Asterism::CDR.le_bytes(v, 4)
        self
      end

      def uint64(v)
        align(8)
        @buf << ::Asterism::CDR.le_bytes(v, 8)
        self
      end

      alias int8 uint8
      alias int16 uint16
      alias int32 uint32
      alias int64 uint64

      def float32(v)
        align(4)
        @buf << (PACK ? [v.to_f].pack("e") : ::Asterism::CDR.f32_bytes(v))
        self
      end

      def float64(v)
        align(8)
        @buf << (PACK ? [v.to_f].pack("E") : ::Asterism::CDR.f64_bytes(v))
        self
      end

      # A string: uint32 length including the terminating NUL, the bytes, NUL.
      # max: the bound of a bounded string (string<=N), in bytes.
      def string(s, max = nil)
        s = s.to_s
        if max && s.bytesize > max
          raise ArgumentError, "string of #{s.bytesize} bytes is longer than its bound #{max}"
        end
        uint32(s.bytesize + 1)
        @buf << (ENCODINGS ? ::Asterism::CDR.binary(s) : s)
        @buf << "\x00"
        self
      end

      def wstring(_s, _max = nil)
        raise NotImplementedError, "wstring is not supported"
      end

      # The element count of an array (fixed: its length, nothing written)
      # or a sequence (max: its bound or nil; the count is written).
      def count(n, fixed, max)
        if fixed
          raise ArgumentError, "array needs #{fixed} elements, got #{n}" if n != fixed
        else
          raise ArgumentError, "sequence of #{n} is longer than its bound #{max}" if max && n > max
          uint32(n)
        end
        self
      end

      # An array or sequence of a primitive kind (:float64, :string, ...).
      # string_max: the bound of each string.
      def array(kind, v, fixed = nil, max = nil, string_max = nil)
        a = v.nil? ? [] : v
        n = a.size
        count(n, fixed, max)
        i = 0
        if string_max
          while i < n
            string(a[i], string_max)
            i += 1
          end
        else
          while i < n
            __send__(kind, a[i])
            i += 1
          end
        end
        self
      end

      # An array or sequence of bytes (byte, char, uint8): a String or an
      # Array of Integers.
      def bytes(v, fixed = nil, max = nil)
        s = ::Asterism::CDR.byte_string(v)
        count(s.bytesize, fixed, max)
        @buf << s
        self
      end

      # An array or sequence of messages of type t (a generated type, with
      # from and write); the elements are t's or Hashes.
      def structs(t, v, fixed = nil, max = nil)
        a = v.nil? ? [] : v
        n = a.size
        count(n, fixed, max)
        i = 0
        while i < n
          t.write(self, t.from(a[i]))
          i += 1
        end
        self
      end

      def to_s
        @buf
      end
    end

    class Reader
      attr_reader :pos

      def initialize(bytes)
        @buf = bytes.to_s
        raise DecodeError, "shorter than the CDR header" if @buf.bytesize < 4
        kind = @buf.getbyte(1)
        raise DecodeError, "unknown CDR encapsulation #{kind}" if @buf.getbyte(0) != 0 || kind > 1
        @le = (kind == 1)
        @pos = 4
      end

      def little_endian?
        @le
      end

      def align(n)
        rem = (@pos - 4) % n
        @pos += n - rem if rem > 0
        self
      end

      def uint8
        need(1)
        v = @buf.getbyte(@pos)
        @pos += 1
        v
      end

      def int8
        v = uint8
        v >= 0x80 ? v - 0x100 : v
      end

      def bool
        uint8 != 0
      end

      def uint16
        unsigned(2)
      end

      def uint32
        unsigned(4)
      end

      # Integers are 64-bit signed here: a uint64 at or above 2**63 comes out
      # negative (the same bits).
      def uint64
        signed(8)
      end

      def int16
        signed(2)
      end

      def int32
        signed(4)
      end

      def int64
        signed(8)
      end

      def float32
        align(4)
        need(4)
        v = if PACK
              @buf.byteslice(@pos, 4).unpack(@le ? "e" : "g")[0]
            else
              ::Asterism::CDR.f32_from(raw(4))
            end
        @pos += 4
        v
      end

      def float64
        align(8)
        need(8)
        v = if PACK
              @buf.byteslice(@pos, 8).unpack(@le ? "E" : "G")[0]
            else
              lo = raw_at(@le ? 0 : 4)
              hi = raw_at(@le ? 4 : 0)
              ::Asterism::CDR.f64_from(lo, hi)
            end
        @pos += 8
        v
      end

      def string
        len = uint32
        raise DecodeError, "string without its NUL" if len == 0
        need(len)
        s = @buf.byteslice(@pos, len - 1)
        @pos += len
        ENCODINGS ? ::Asterism::CDR.utf8(s) : s
      end

      def wstring
        raise NotImplementedError, "wstring is not supported"
      end

      # The element count of an array (fixed) or a sequence (read). Each
      # element takes at least min bytes, so a corrupt count cannot make a
      # huge Array.
      def count(fixed, min = 1)
        return fixed if fixed
        n = uint32
        raise DecodeError, "sequence of #{n} does not fit in the data" if n * min > @buf.bytesize - @pos
        n
      end

      def array(kind, fixed = nil)
        n = count(fixed)
        out = []
        i = 0
        while i < n
          out << __send__(kind)
          i += 1
        end
        out
      end

      # An array or sequence of bytes, as a binary String.
      def bytes(fixed = nil)
        n = count(fixed)
        need(n)
        s = @buf.byteslice(@pos, n)
        @pos += n
        ENCODINGS ? s.force_encoding("BINARY") : s
      end

      def structs(t, fixed = nil)
        n = count(fixed, 0)
        out = []
        i = 0
        while i < n
          out << t.read(self)
          i += 1
        end
        out
      end

      private

      def need(n)
        raise DecodeError, "truncated CDR (#{n} bytes at #{@pos} of #{@buf.bytesize})" if @pos + n > @buf.bytesize
      end

      # Byte i (0 = least significant) of the n-byte value at @pos.
      def byte_at(i, n)
        @buf.getbyte(@le ? @pos + i : @pos + n - 1 - i)
      end

      # The n bytes at @pos (in the data's byte order) as an unsigned Integer.
      def raw(n)
        v = 0
        i = 0
        while i < n
          v |= byte_at(i, n) << (8 * i)
          i += 1
        end
        v
      end

      # 32 bits at @pos + off, in the data's byte order (for float64 halves).
      def raw_at(off)
        v = 0
        i = 0
        while i < 4
          b = @le ? @buf.getbyte(@pos + off + i) : @buf.getbyte(@pos + off + 3 - i)
          v |= b << (8 * i)
          i += 1
        end
        v
      end

      def unsigned(n)
        align(n)
        need(n)
        v = raw(n)
        @pos += n
        v
      end

      # Built from the signed top byte down, so 8 bytes never overflow.
      def signed(n)
        align(n)
        need(n)
        top = byte_at(n - 1, n)
        v = top >= 0x80 ? top - 0x100 : top
        i = n - 2
        while i >= 0
          v = (v << 8) | byte_at(i, n)
          i -= 1
        end
        @pos += n
        v
      end
    end
  end
end
