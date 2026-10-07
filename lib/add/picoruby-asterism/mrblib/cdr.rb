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
# always little endian. Only the primitive types are here; message layouts
# are built from them (Asterism::ROS::StdMsgs).
#
# Pure Ruby without Array#pack (PicoRuby has none): bytes are set one by one.
module Asterism
  module CDR
    # Malformed or truncated CDR.
    class DecodeError < ::StandardError; end

    HEADER_LE = "\x00\x01\x00\x00"
    HEADER_BE = "\x00\x00\x00\x00"

    # n bytes of v (two's complement), little endian.
    def self.le_bytes(v, n)
      s = "\x00" * n
      i = 0
      while i < n
        s.setbyte(i, (v >> (8 * i)) & 0xff)
        i += 1
      end
      s
    end

    class Writer
      def initialize
        @buf = "" + HEADER_LE
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

      alias int16 uint16
      alias int32 uint32
      alias int64 uint64

      # A string: uint32 length including the terminating NUL, the bytes, NUL.
      def string(s)
        s = s.to_s
        uint32(s.bytesize + 1)
        @buf << s
        @buf << "\x00"
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

      def string
        len = uint32
        raise DecodeError, "string without its NUL" if len == 0
        need(len)
        s = @buf.byteslice(@pos, len - 1)
        @pos += len
        s
      end

      private

      def need(n)
        raise DecodeError, "truncated CDR (#{n} bytes at #{@pos} of #{@buf.bytesize})" if @pos + n > @buf.bytesize
      end

      # Byte i (0 = least significant) of the n-byte value at @pos.
      def byte_at(i, n)
        @buf.getbyte(@le ? @pos + i : @pos + n - 1 - i)
      end

      def unsigned(n)
        align(n)
        need(n)
        v = 0
        i = 0
        while i < n
          v |= byte_at(i, n) << (8 * i)
          i += 1
        end
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
