# FmrbHitMask: a pixel-by-pixel hit shape for a frameless window
# (doc/frameless_window/ W3b). A grid of cells, each one pixel or a square of
# `scale` pixels; a set cell takes a press. Bits are kept row by row,
# (width + 7) / 8 bytes per row, the leftmost cell in the most significant
# bit -- the layout the kernel reads (fmrb_app.h, FMRB_HIT_*).
#
# Given to FmrbApp#set_hit_mask or FmrbApp#add_hit_sprite. The same class is
# in the Spinel base (fmrb_app_base_spinel.rb).
class FmrbHitMask
  KIND_RECT = 1
  KIND_MASK = 2
  HIDDEN = 0x80
  ENTRY_HDR = 12
  SHAPE_MAX = 2048

  attr_reader :width, :height, :scale, :bits

  # width, height: cells. bits: height rows of (width + 7) / 8 bytes.
  def initialize(width, height, bits, scale = 1)
    @width = width
    @height = height
    @scale = scale < 1 ? 1 : scale
    @bits = bits
  end

  # A mask from dot-art rows (one ASCII character per cell): every character
  # not in `transparent` takes a press. Each cell is scale x scale pixels.
  def self.from_art(rows, transparent: ".", scale: 1)
    h = rows.size
    w = 0
    y = 0
    while y < h
      n = rows[y].bytesize
      w = n if n > w
      y += 1
    end
    stride = (w + 7) / 8
    bits = "\x00" * (stride * h)
    tn = transparent.bytesize
    y = 0
    while y < h
      row = rows[y]
      n = row.bytesize
      x = 0
      while x < n
        c = row.getbyte(x)
        see = false
        k = 0
        while k < tn
          if transparent.getbyte(k) == c
            see = true
            break
          end
          k += 1
        end
        unless see
          i = y * stride + (x >> 3)
          bits.setbyte(i, bits.getbyte(i) | (0x80 >> (x & 7)))
        end
        x += 1
      end
      y += 1
    end
    FmrbHitMask.new(w, h, bits, scale)
  end

  # Whether the cell at (cx, cy) takes a press.
  def set?(cx, cy)
    return false if cx < 0 || cy < 0 || cx >= @width || cy >= @height
    b = @bits.getbyte(cy * ((@width + 7) / 8) + (cx >> 3))
    (b & (0x80 >> (cx & 7))) != 0
  end

  # One entry header of the kernel's layout, written at off.
  def self._put_entry(buf, off, kind, scale, x, y, w, h, id)
    buf.setbyte(off, kind)
    buf.setbyte(off + 1, scale)
    buf.setbyte(off + 2, x & 0xFF)
    buf.setbyte(off + 3, (x >> 8) & 0xFF)
    buf.setbyte(off + 4, y & 0xFF)
    buf.setbyte(off + 5, (y >> 8) & 0xFF)
    buf.setbyte(off + 6, w & 0xFF)
    buf.setbyte(off + 7, (w >> 8) & 0xFF)
    buf.setbyte(off + 8, h & 0xFF)
    buf.setbyte(off + 9, (h >> 8) & 0xFF)
    buf.setbyte(off + 10, id & 0xFF)
    buf.setbyte(off + 11, (id >> 8) & 0xFF)
    off + ENTRY_HDR
  end

  # A mask entry (header and rows) at off; returns the offset after it.
  def _put(buf, off, x, y, id, hidden)
    kind = hidden ? (KIND_MASK | HIDDEN) : KIND_MASK
    off = FmrbHitMask._put_entry(buf, off, kind, @scale, x, y, @width, @height, id)
    n = @bits.bytesize
    i = 0
    while i < n
      buf.setbyte(off + i, @bits.getbyte(i))
      i += 1
    end
    off + n
  end
end
