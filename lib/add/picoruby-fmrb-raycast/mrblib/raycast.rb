# Fmrb::Raycast -- one raycaster, two engines, chosen at run time.
#
#   rc = Fmrb::Raycast.new(backend: :spinel)
#   rc.set_map(WORLD_MAP, 12, 12)
#   us, rays = rc.cast(px, py, pa)   # rays[i] => { dist:, wall:, side: }
#   rc.close
#
# Both backends run mrblib/raycast_core.rb: mruby interprets it, Spinel runs it
# as native code. The app can flip between them mid-game and watch the
# microsecond count change under the same picture, which is the whole point
# (doc/raycast_spinel/plan.md).
#
# What crosses the boundary is integers and bytes, never a Float: the player's
# position and angle go in, and a packed depth buffer comes back -- dist as
# int32 little-endian, then wall and side, six bytes a ray.
module Fmrb
  class Raycast
    BACKENDS = [:ruby, :spinel]

    attr_reader :backend

    def initialize(backend: :ruby)
      unless BACKENDS.include?(backend)
        raise ArgumentError, "unknown raycast backend: #{backend}"
      end
      @backend = backend
      @map = nil
      @mw = 0
      @mh = 0
      @core = nil

      # A fixed-point trig table on this side of the boundary, for the caller's
      # own geometry -- moving the player, placing sprites. The casting core has
      # its own copy and cannot share it: on :spinel it lives in another
      # runtime's heap, and reaching across per call would cost more than the
      # table. Building it here instead means the app's movement behaves
      # identically whichever engine is casting, so flipping the backend
      # changes only the thing being compared.
      @sin_tbl = []
      @cos_tbl = []
      pi = 3.141592653589793
      deg = 0
      while deg < 360
        rad = deg * pi / 180.0
        @sin_tbl << (Math.sin(rad) * ::RaycastCore::FP_ONE).to_i
        @cos_tbl << (Math.cos(rad) * ::RaycastCore::FP_ONE).to_i
        deg += 1
      end

      Fmrb::Raycast.open if backend == :spinel
    end

    # Fixed-point sine/cosine of a whole number of degrees, any sign.
    def fp_sin(deg)
      @sin_tbl[deg % 360]
    end

    def fp_cos(deg)
      @cos_tbl[deg % 360]
    end

    def self.available?(backend)
      case backend
      when :ruby then true
      when :spinel then ::RaycastNative.available?
      else false
      end
    end

    # The clock both backends are timed with, so the two numbers on screen
    # mean the same thing.
    def self.micros
      ::RaycastNative.micros
    end

    # Hand the gem the world. `cells` may be the app's Integer array or an
    # already-packed byte String; either way the gem keeps bytes, one per cell.
    #
    # On :spinel this uploads to the receiver and bumps a generation counter --
    # the Spinel entry keeps its core between calls, so it needs to be told
    # when the map it built against is no longer the current one. On :ruby
    # there is no boundary, so the core is simply rebuilt.
    def set_map(cells, w, h)
      bytes = Fmrb::Raycast.pack_map(cells, w, h)
      @map = bytes
      @mw = w
      @mh = h
      if @backend == :spinel
        rc = ::RaycastNative.set_map(bytes, w, h)
        raise RuntimeError, "the raycast map was rejected (#{rc})" if rc < 0
      else
        @core = ::RaycastCore.new(bytes, w, h, 0)
      end
      nil
    end

    # One frame of rays. Returns [microseconds, array of {dist:, wall:, side:}].
    # The microseconds cover the cast alone in both cases: inside the entry for
    # :spinel, around the core call for :ruby.
    # One frame of rays. Returns the microseconds the cast took; the rays
    # themselves stay in the packed buffer and are read with #dist / #wall /
    # #side.
    #
    # It used to return an Array of Hashes, which read better and cost 40
    # objects a frame -- measured at 7ms of unpacking plus the collector's
    # share of a 15ms-per-frame wobble that only showed up once the frame was
    # accounted for end to end. Reading the buffer in place does the same six
    # getbytes per ray with nothing left behind.
    def cast(px, py, pa)
      raise RuntimeError, "set_map has not been called" if @map.nil?
      if @backend == :spinel
        us, buf = ::RaycastNative.cast(px, py, pa)
      else
        t0 = ::RaycastNative.micros
        buf = @core.cast_packed(px, py, pa)
        us = ::RaycastNative.micros - t0
      end
      @buf = buf
      us
    end

    # How many rays the last cast returned. 0 before the first one.
    def rays
      return 0 if @buf.nil?
      @buf.bytesize / ::RaycastCore::RAY_BYTES
    end

    # Distance to the wall ray `i` hit, in the app's fixed-point units.
    # int32 little-endian: it is the denominator of the wall height and runs to
    # six figures down a long corridor.
    def dist(i)
      return 0 if @buf.nil?
      o = i * ::RaycastCore::RAY_BYTES
      @buf.getbyte(o) | (@buf.getbyte(o + 1) << 8) |
        (@buf.getbyte(o + 2) << 16) | (@buf.getbyte(o + 3) << 24)
    end

    # The map value the ray hit (0 = ran out of steps without hitting one).
    def wall(i)
      return 0 if @buf.nil?
      @buf.getbyte(i * ::RaycastCore::RAY_BYTES + 4)
    end

    # Which face was hit: 0 vertical, 1 horizontal. The app shades them apart.
    def side(i)
      return 0 if @buf.nil?
      @buf.getbyte(i * ::RaycastCore::RAY_BYTES + 5)
    end

    def close
      Fmrb::Raycast.close if @backend == :spinel
      @core = nil
      nil
    end

    # Cells to one byte each. Values are 0..4 here, so a byte is plenty and the
    # boundary stays as narrow as it can be.
    def self.pack_map(cells, w, h)
      return cells if cells.is_a?(String)
      n = w * h
      s = "\x00" * n
      i = 0
      while i < n
        v = cells[i]
        v = 0 if v.nil?
        s.setbyte(i, v & 0xFF)
        i += 1
      end
      s
    end

    # The packed depth buffer as an Array of Hashes, for a caller that would
    # rather have objects than accessors. NOT what the drawing path uses: it
    # allocates one Hash per ray, which on a per-frame path is the largest
    # source of garbage in the app (see #cast).
    def self.unpack(buf)
      stride = ::RaycastCore::RAY_BYTES
      out = []
      n = buf.bytesize / stride
      i = 0
      while i < n
        o = i * stride
        d = buf.getbyte(o) | (buf.getbyte(o + 1) << 8) |
            (buf.getbyte(o + 2) << 16) | (buf.getbyte(o + 3) << 24)
        out << { dist: d, wall: buf.getbyte(o + 4), side: buf.getbyte(o + 5) }
        i += 1
      end
      out
    end

    # One Spinel instance per task is enough and creating it costs a memory
    # pool, so it is opened on demand and reference counted rather than tied to
    # one Raycast object (same shape as the FFT and SpinelHello gems).
    #
    # Constraint: the :spinel backend is a single instance owned by one task.
    # Use it from one task only.
    def self.open
      @refs ||= 0
      if @refs == 0
        raise RuntimeError, "the Spinel raycast backend is not in this build" unless ::RaycastNative.available?
        rc = ::RaycastNative.begin_instance
        raise RuntimeError, "could not start the Spinel raycast instance (#{rc})" if rc < 0
      end
      @refs += 1
    end

    def self.close
      @refs ||= 0
      return if @refs == 0
      @refs -= 1
      ::RaycastNative.end_instance if @refs == 0
    end
  end
end
