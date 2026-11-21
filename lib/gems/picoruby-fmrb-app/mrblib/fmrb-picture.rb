# FmrbGfx's picture store (doc/pictures P3): an app hands a picture file's
# path (or a name for pixels of its own) and the store keeps the sprite
# images on the display side. The same file is spliced into the Spinel
# programs (tools/spinel/gen_app_combined.rb).
#
#   @gfx.draw_picture("/home/stamps/a.png", 10, 20, scale: 2)
#   w, h = @gfx.picture_size("/home/stamps/a.png")
#   @gfx.put_picture("mine", 16, 16, pixels)
#   @gfx.release_pictures
#
# A picture is a sprite image with the transparent colour 0x01 (what the
# display sides read a PNG's transparency as, doc/pictures P1). Each FmrbGfx
# keeps up to its picture_limit (8 unless set) and lets the least recently
# drawn go first. When the machine has no room for another sprite image (64
# on sim and Retro, sharing a 256 KB pool; 160 on P4), the store lets its
# own oldest go and tries again; with none of its own left it draws nothing
# and answers false (picture_error says why). The images are the canvas's,
# so they go with it when the app ends (or the extra canvas is deleted):
# nothing to clean up.
#
# Nothing is kept until the first picture: an app that draws none carries
# only these methods. The state is parallel arrays (the Spinel compiler
# wants one type per array): @_pk the keys, oldest first, and @_pn six
# Integers per key: sprite image id, width, height, the file's bytes and
# mtime (-1 for put_picture's pixels: no file to look at), the time it was
# last looked at. A file is looked at again (picture_info: its head only)
# at most once a second; another size or time reads it again.
#
# Limits: 65,536 pixels. A file over 65,536 bytes is not handed to the
# display side (the Retro one reads no larger PNG; the sim and P4 take more,
# but a picture that shows on one machine shows on every one): it is read
# here instead (BMP332.load, the file and its pixels in the VM pool for a
# moment) and its pixels are put. A picture of more pixels is refused
# (:too_big), not shrunk: draw it at scale: -2 .. -32 once it is in.
class FmrbGfx
  PICTURE_LIMIT = 8
  PICTURE_MAX = 65536

  # Draw the picture at path (or put_picture's name) at (x, y), reading it
  # onto the display side the first time. scale: 1..32 draws each pixel
  # scale x scale, -2..-32 draws 1/|scale| of the size. checker: true draws
  # the checkerboard under it (where it is transparent). clip: [x, y, w, h]
  # cuts it (and the checkerboard) to that rect. false when it cannot be
  # drawn (picture_error says why).
  def draw_picture(path, x, y, scale: 1, checker: false, clip: nil)
    if scale > 32 || scale < -32
      raise ArgumentError, "draw_picture: scale is 1..32 or -2..-32"
    end
    scale = 1 if scale == 0 || scale == -1
    i = _pic_get(path)
    return false if i < 0
    b = i * 6
    n = @_pn
    id = n[b]
    w = n[b + 1]
    h = n[b + 2]
    cx = 0
    cy = 0
    cw = 0
    ch = 0
    if clip
      cx = clip[0]
      cy = clip[1]
      cw = clip[2]
      ch = clip[3]
      return true if cw <= 0 || ch <= 0
    end
    if checker
      if scale > 0
        dw = w * scale
        dh = h * scale
      else
        dw = (w - scale - 1) / -scale
        dh = (h - scale - 1) / -scale
      end
      draw_checker(x, y, dw, dh, clip: clip)
    end
    if scale == 1 && cw == 0
      draw_tile_raw(id, 0, 0, w, h, x, y)
    else
      draw_tile_scaled_raw(id, 0, 0, w, h, x, y, scale, cx, cy, cw, ch)
    end
    true
  end

  # [width, height] of the picture at path (or put_picture's name), from the
  # store or from the file's head (nothing is read onto the display side);
  # nil when it is not a picture.
  def picture_size(path)
    i = _pic_find(path)
    if i >= 0
      b = i * 6
      return [@_pn[b + 1], @_pn[b + 2]]
    end
    info = picture_info(path)
    return nil if info.nil?
    [info[:width].to_i, info[:height].to_i]
  end

  # Put RGB332 pixels (width * height bytes, top row first; 0x01 is
  # transparent) into the store under name, for draw_picture(name, ...).
  # Putting the same name again writes the pixels again. false when there is
  # no room (picture_error).
  def put_picture(name, width, height, pixels)
    if width <= 0 || height <= 0 || pixels.bytesize < width * height
      raise ArgumentError, "put_picture: pixels must be width * height bytes"
    end
    if width * height > PICTURE_MAX
      @_pe = :too_big
      return false
    end
    i = _pic_find(name)
    if i >= 0
      b = i * 6
      if @_pn[b + 1] == width && @_pn[b + 2] == height && @_pn[b + 3] < 0
        write_sprite_image(@_pn[b], 0, 0, width, height, pixels)
        @_pe = nil
        return true
      end
      _pic_drop(i)
    end
    id = _pic_make(width, height)
    return false if id <= 0
    write_sprite_image(id, 0, 0, width, height, pixels)
    _pic_add(name, id, width, height, -1, -1)
    true
  end

  # Let one picture go (a path or put_picture's name).
  def release_picture(path)
    i = _pic_find(path)
    _pic_drop(i) if i >= 0
    self
  end

  # Let every picture of this FmrbGfx go, and the checkerboard.
  def release_pictures
    if @_pk
      while @_pk.size > 0
        _pic_drop(@_pk.size - 1)
      end
    end
    if @_pc
      _pic_delete(@_pc[0])
      @_pc = nil
    end
    @_pk = nil
    @_pn = nil
    self
  end

  # How many pictures this FmrbGfx keeps (8 unless set). Fewer now lets the
  # oldest go.
  def picture_limit=(n)
    n = 1 if n < 1
    @_pl = n
    if @_pk
      while @_pk.size > n
        _pic_drop(0)
      end
    end
    n
  end

  # Why the last picture call failed: :unreadable, :too_big, :no_room or
  # :not_sent; nil after one that did not.
  def picture_error
    @_pe
  end

  # The checkerboard that stands for transparency (cells of cell px, white
  # and light grey, as the image viewer's) over (x, y, w, h), cut to clip:
  # [x, y, w, h] when given. One strip made on the display side the first
  # time (and again when a wider one is needed) is stamped row by row.
  def draw_checker(x, y, w, h, cell: 4, clip: nil)
    return self if w <= 0 || h <= 0
    cell = 1 if cell < 1
    c = @_pc
    if c.nil? || c[1] != cell || c[2] < w
      sw = w
      sw = c[2] if c && c[1] == cell && c[2] > sw
      sw = (sw + cell * 2 - 1) / (cell * 2) * (cell * 2)
      _pic_delete(c[0]) if c
      @_pc = nil
      id = 0
      begin
        id = create_sprite_image(sw, cell * 2, 0, false)
      rescue RuntimeError
        id = 0
      end
      return self if id <= 0
      set_sprite_image_target(id)
      fill_rect(0, 0, sw, cell * 2, 0xFF)
      cx = 0
      while cx < sw
        fill_rect(cx, 0, cell, cell, 0xB6)
        fill_rect(cx + cell, cell, cell, cell, 0xB6)
        cx += cell * 2
      end
      set_sprite_image_target(0)
      c = [id, cell, sw]
      @_pc = c
    end
    id = c[0]
    rows = cell * 2
    yy = 0
    while yy < h
      rh = h - yy < rows ? h - yy : rows
      if clip
        draw_tile_scaled_raw(id, 0, 0, w, rh, x, y + yy, 1, clip[0], clip[1], clip[2], clip[3])
      else
        draw_tile_raw(id, 0, 0, w, rh, x, y + yy)
      end
      yy += rows
    end
    self
  end

  # In an app started from a checkpoint image (FmrbApp#_checkpoint_restored):
  # the sprite image ids were the writing program's.
  def _picture_forget
    @_pk = nil
    @_pn = nil
    @_pc = nil
    nil
  end

  # The index of key in the store, or -1.
  def _pic_find(key)
    keys = @_pk
    return -1 if keys.nil?
    i = 0
    n = keys.size
    while i < n
      return i if keys[i] == key
      i += 1
    end
    -1
  end

  # The index of the picture at path, read onto the display side when it is
  # not there yet or its file changed, and made the most recently used; -1
  # when it cannot be (@_pe says why).
  def _pic_get(path)
    i = _pic_find(path)
    n = @_pn
    if i >= 0
      b = i * 6
      if n[b + 3] >= 0
        now = Machine.board_millis
        if now - n[b + 5] >= 1000
          n[b + 5] = now
          info = picture_info(path)
          if info.nil?
            _pic_drop(i)
            @_pe = :unreadable
            return -1
          end
          bytes = info[:bytes].to_i
          mtime = info[:mtime].to_i
          if bytes != n[b + 3] || mtime != n[b + 4]
            if info[:width].to_i == n[b + 1] && info[:height].to_i == n[b + 2] && bytes <= PICTURE_MAX
              # the same size: read again into the same image
              unless sync_file(path)
                _pic_drop(i)
                @_pe = :not_sent
                return -1
              end
              load_sprite_image(n[b], path)
              n[b + 3] = bytes
              n[b + 4] = mtime
            else
              _pic_drop(i)
              i = -1
            end
          end
        end
      end
    end
    if i >= 0
      # the most recently used goes last
      last = @_pk.size - 1
      if i < last
        k = @_pk[i]
        b = i * 6
        v0 = n[b]
        v1 = n[b + 1]
        v2 = n[b + 2]
        v3 = n[b + 3]
        v4 = n[b + 4]
        v5 = n[b + 5]
        j = i
        while j < last
          @_pk[j] = @_pk[j + 1]
          m = j * 6
          q = 0
          while q < 6
            n[m + q] = n[m + 6 + q]
            q += 1
          end
          j += 1
        end
        @_pk[last] = k
        m = last * 6
        n[m] = v0
        n[m + 1] = v1
        n[m + 2] = v2
        n[m + 3] = v3
        n[m + 4] = v4
        n[m + 5] = v5
        i = last
      end
      @_pe = nil
      return i
    end
    info = picture_info(path)
    if info.nil?
      @_pe = :unreadable
      return -1
    end
    w = info[:width].to_i
    h = info[:height].to_i
    bytes = info[:bytes].to_i
    if w * h > PICTURE_MAX
      @_pe = :too_big
      return -1
    end
    return _pic_read(path, w, h, bytes, info[:mtime].to_i) if bytes > PICTURE_MAX
    id = _pic_make(w, h)
    return -1 if id <= 0
    unless sync_file(path)
      _pic_delete(id)
      @_pe = :not_sent
      return -1
    end
    load_sprite_image(id, path)
    _pic_add(path, id, w, h, bytes, info[:mtime].to_i)
  end

  # A picture whose file is too large for the display side: read here and
  # its pixels put (write_sprite_image); its index, or -1.
  def _pic_read(path, w, h, bytes, mtime)
    pix = nil
    begin
      pix = BMP332.load(path)[:pixels]
    rescue => e
      pix = nil
    end
    if pix.nil? || pix.bytesize < w * h
      @_pe = :unreadable
      return -1
    end
    id = _pic_make(w, h)
    return -1 if id <= 0
    write_sprite_image(id, 0, 0, w, h, pix)
    _pic_add(path, id, w, h, bytes, mtime)
  end

  # A sprite image of w x h for a picture, the store's oldest let go while
  # it is at its limit or the machine has no room; 0 when none can be made
  # (@_pe = :no_room).
  def _pic_make(w, h)
    limit = @_pl || PICTURE_LIMIT
    if @_pk
      while @_pk.size >= limit
        _pic_drop(0)
      end
    end
    while true
      id = 0
      begin
        id = create_sprite_image(w, h, 1, true)
      rescue RuntimeError
        id = 0
      end
      return id if id > 0
      if @_pk.nil? || @_pk.size == 0
        @_pe = :no_room
        return 0
      end
      _pic_drop(0)
    end
  end

  # Adds the newest entry; its index.
  def _pic_add(key, id, w, h, bytes, mtime)
    if @_pk.nil?
      @_pk = []
      @_pn = []
    end
    @_pk << key
    n = @_pn
    n << id
    n << w
    n << h
    n << bytes
    n << mtime
    n << Machine.board_millis
    @_pe = nil
    @_pk.size - 1
  end

  # Lets entry i go (its sprite image too).
  def _pic_drop(i)
    n = @_pn
    _pic_delete(n[i * 6])
    @_pk.delete_at(i)
    b = i * 6
    last = n.size - 6
    while b < last
      n[b] = n[b + 6]
      b += 1
    end
    q = 0
    while q < 6
      n.pop
      q += 1
    end
    nil
  end

  def _pic_delete(id)
    begin
      delete_sprite_image(id)
    rescue RuntimeError
      # the FmrbGfx went first: the image goes with the canvas
    end
    nil
  end
end
