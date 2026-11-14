# Readers for the packed records EditorCore returns.
#
# Compound return values cross as fixed-layout binstr rather than Arrays or
# Hashes, so the same API can be bound to Spinel FFI later (:int / :str /
# :binstr only) without changing either side.
module EditorCore
  def self.rec_u32(rec, off)
    (rec.getbyte(off) || 0) |
      ((rec.getbyte(off + 1) || 0) << 8) |
      ((rec.getbyte(off + 2) || 0) << 16) |
      ((rec.getbyte(off + 3) || 0) << 24)
  end

  # insert_multiline / paste_at -> [new_y, new_x]
  def self.pos_y(rec)
    rec_u32(rec, 0)
  end

  def self.pos_x(rec)
    rec_u32(rec, 4)
  end

  # find -> [found, y, x]
  def self.found?(rec)
    (rec.getbyte(0) || 0) != 0
  end

  def self.find_y(rec)
    rec_u32(rec, 1)
  end

  def self.find_x(rec)
    rec_u32(rec, 5)
  end
end
