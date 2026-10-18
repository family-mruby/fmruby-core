# Mahjong table: what only the 640x360 table draws (MJ8, doc/ruby_asterism/
# report/mj8.md): its buttons by the clock, the marks' redraw test, the
# melds with the called tile sideways, the own seat's label by the hand.
# Part of MahjongApp, read when the screen is 640x360 (m640_load in
# mahjong_panels.rb).

class MahjongApp < FmrbApp
  # ------------------------------------------------------------ the buttons

  CTRL_RIGHT = 556 # the buttons' right edge (the clock is right of it)

  def draw_buttons640(labels, y, msg)
    calls = false
    labels.each { |l| calls = true if l[2].is_a?(Array) }
    calls = false unless answering_call?
    gap = 4
    total = controls_width(labels, gap)
    if total > CTRL_RIGHT - 4
      # Too wide: the calls' buttons leave out their keys, closer together.
      labels.each { |l| l[2][2] = nil if l[2].is_a?(Array) }
      gap = 2
      total = controls_width(labels, gap)
    end
    x = @x0 + CTRL_RIGHT - total
    left = x
    labels.each do |l|
      tall = calls || l[2].is_a?(Array)
      h = tall ? CALL_BTN_H : u(20)
      by = calls ? hand_y - h : (tall ? y + u(20) - h : y)
      x += button(l[0], l[1], x, by, l[2], h) + gap
    end
    ty = calls ? hand_y - CALL_BTN_H + (CALL_BTN_H - 12) / 2 : y + 4
    room = left - @x0 - 12
    text(@x0 + 4, ty, fit(msg, room), C_TURN) unless msg.strip.empty? || room < 40
  end

  # ------------------------------------------------------------ the same tiles

  # The 640x360 table: choosing hand tile b after a changes the marks (the
  # whole table is drawn again, not only the two hand tiles).
  def marks_differ?(a, b)
    ca = a >= 0 ? hand_at(a) : nil
    cb = b >= 0 ? hand_at(b) : nil
    ka = ca ? tile_kind(ca) : nil
    kb = cb ? tile_kind(cb) : nil
    return false if ka == kb
    (ka && on_table?(ka)) || (kb && on_table?(kb)) ? true : false
  end

  # Whether a tile of kind k is in a river, a meld or the dora indicators.
  def on_table?(k)
    v = @view
    return false unless v
    list = bytes_of(v["dora"])
    kw = v["kw"].is_a?(Array) ? v["kw"] : []
    fu = v["fu"].is_a?(Array) ? v["fu"] : []
    s = 0
    while s < 4
      list += bytes_of(kw[s])
      b = bytes_of(fu[s])
      j = 0
      while j + 2 < b.size # [kind, from, n, n tiles] per meld
        list += b[j + 3, b[j + 2]]
        j += 3 + b[j + 2]
      end
      s += 1
    end
    i = 0
    while i < list.size
      c = list[i]
      return true if c & 0x80 == 0 && tile_kind(c & 0x3F) == k
      i += 1
    end
    false
  end

  # ------------------------------------------------------------ the melds
  #
  # In the small tiles, as at the 1280x720 table: the tile called lies
  # sideways on the side of the seat it came from (the left seat's at the
  # left end, the across seat's second, the right seat's at the right end;
  # xl_sideways_index), an added kan's tile on the sideways one. The own
  # melds at the bottom right, the first call rightmost, the added tile on
  # top of the sideways one; the others' (the across seat's on a line under
  # its name, a side seat's two to a row) in a row 32 high, so an added
  # tile lies 8 px over the sideways one (the row has no room for a whole
  # tile more). A row longer than its room closes up.

  def draw_melds640
    s = 0
    while s < 4
      draw_melds640_seat(rel(s), s)
      s += 1
    end
  end

  def draw_melds640_seat(rl, s)
    ms = xl_melds(s)
    return if ms.empty?
    pr = LAYOUT640[[:melds0, :melds1, :melds2, :melds3][rl]]
    side = rl == 1 || rl == 3
    rows = []
    row = nil
    a = 0
    k = 0
    while k < ms.size
      if row.nil? || (side && k % 2 == 0)
        row = []
        rows << row
        a = 0
      end
      m = ms[rl == 0 ? ms.size - 1 - k : k]
      kind = m[0]
      tl = m[2]
      n = kind == 5 ? 3 : tl.size
      si = xl_sideways_index(kind, m[1], tl.size)
      j = 0
      while j < n
        c = tl[j]
        c = 37 if kind == 3 && (j == 0 || j == n - 1)
        row << [c, j == si, false, a]
        row << [tl[3], true, true, a] if j == si && kind == 5
        a += j == si ? 32 : 24
        j += 1
      end
      a += 4
      k += 1
    end
    r = 0
    while r < rows.size
      list = rows[r]
      last = list[list.size - 1]
      llen = last[1] ? 32 : 24
      total = last[3] + llen
      room = pr[2]
      x0 = @x0 + (side ? pr[0] : pr[0] + pr[2] - (total < room ? total : room))
      y0 = @y0 + (side ? pr[1] + r * 32 : pr[1] + pr[3] - 32)
      i = 0
      while i < list.size
        e = list[i]
        x = x0 + (total <= room ? e[3] : e[3] * (room - llen) / (total - llen))
        if !e[1]
          small_tile(e[0], x, y0)
          same_mark(e[0], x, y0, 24, 32)
        else
          ty = y0 + 8
          ty -= rl == 0 ? 24 : 8 if e[2]
          side_tile(e[0], x, ty)
          same_mark(e[0], x, ty, 32, 24)
        end
        i += 1
      end
      r += 1
    end
  end

  # A small tile lying sideways (the river sheet turned a quarter left).
  def side_tile(c, x, y)
    load_turned_sheets(true)
    img = @turned ? @turned[3] : nil
    return draw_text_tile(c, x, y, 32, 24) unless img
    @gfx.draw_tile(img.id, (c % 10) * 32, (c / 10) * 24, 32, 24, dst_x: x, dst_y: y)
  end

  # ------------------------------------------------------------ the own seat

  # The own seat's wind, name and points (MJ8): by the hand, white on a dark
  # box, framed in yellow on the own turn.
  def own_label(x, y, label, away, turn)
    w = tw(label) + 8
    @gfx.fill_rect(x, y, w, 16, C_BOX)
    @gfx.draw_rect(x, y, w, 16, C_TURN) if turn
    text(x + 4, y + 2, label, away ? C_AWAY : C_TEXT)
  end
end
