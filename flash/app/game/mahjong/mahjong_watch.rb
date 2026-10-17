# Mahjong table: the 640x360 spectators' screen (MJ5, doc/ruby_asterism/
# report/mj5.md). Part of MahjongApp (mahjong.app.rb requires this file
# first, as mahjong_xl.rb; an app file is read whole and may be at most
# 64 KB).
#
# A spectator sees the four hands (the view's "sh" and "sd"; a host without
# them gives the backs), each the same size and turned to its player along
# the screen's edge: the hand, then the melds. The middle is the 1280x720
# table at half its size: a board with the round and the dora, the four
# rivers around it in the small tiles, and above it a line of the four
# seats (wind, name, points, riichi; the seat to act framed). The side
# rows that do not fit the screen's height go on in a second column.
# Everything is drawn at each drawing (the 640x360 table's way).

class MahjongApp < FmrbApp
  # The parts of this screen, [x, y, w, h] (the overflow checker holds the
  # drawing to them).
  WATCH = {
    w_row0: [40, 326, 560, 32], w_row2: [40, 2, 560, 32], w_row3: [2, 2, 32, 356], w_row3b: [36, 38, 32, 284],
    w_row1: [606, 2, 32, 356], w_row1b: [572, 38, 32, 284],
    w_strip: [72, 38, 496, 30], w_board: [236, 166, 168, 64],
    w_river0: [236, 230, 332, 96], w_river2: [160, 70, 244, 96], w_river3: [100, 126, 136, 198],
    w_river1: [404, 70, 136, 200], w_leave: [72, 72, 84, 20], w_note: [72, 104, 160, 14]
  }
  W_BX = 236
  W_BY = 166
  W_BW = 168
  W_BH = 64
  # The room of a river's last row (rl 0-3).
  W_LAST_ROW = [332, 200, 244, 198]

  def watching?
    @big && !@xl && @seat.nil? && mode == :table
  end

  # A small tile turned rot quarter turns at (x, y).
  def turned_tile(rot, c, x, y)
    w = rot % 2 == 1 ? 32 : 24
    h = rot % 2 == 1 ? 24 : 32
    img = @turned ? @turned[rot] : nil
    return draw_text_tile(c, x, y, w, h) unless img
    @gfx.draw_tile(img.id, (c % 10) * w, (c / 10) * h, w, h, dst_x: x, dst_y: y)
  end

  def draw_watch
    load_turned_sheets
    v = @view
    s = 0
    while s < 4
      rl = rel(s)
      draw_watch_river(rl, xl_river_list(s))
      draw_spec_hand(rl, s)
      s += 1
    end
    draw_watch_board
    draw_score_strip
    # Leaving, and what the screen has to say (the network, a reply).
    fnt(12, 1)
    button(B_LEAVE, [], @x0 + WATCH[:w_leave][0], @y0 + WATCH[:w_leave][1], t(:mj_leave))
    note = net_line || @note
    text(@x0 + WATCH[:w_note][0], @y0 + WATCH[:w_note][1], fit(note.to_s, WATCH[:w_note][2]), C_TURN) unless note.to_s.empty?
  end

  # A river in the small tiles, six to a row from the board's edge, the last
  # row closing up past its room; the riichi tile sideways.
  def draw_watch_river(rl, list)
    side = rl == 1 || rl == 3
    rows = side ? 4 : 3
    base = xl_rot(rl)
    turned = (base + 3) % 4
    r = 0
    while r < rows
      from = r * 6
      to = r == rows - 1 ? list.size : from + 6
      to = list.size if to > list.size
      break if from >= to
      lens = []
      i = from
      while i < to
        lens << (list[i][1] ? 32 : 24)
        i += 1
      end
      offs = close_up(lens, r == rows - 1 ? W_LAST_ROW[rl] : 999)
      i = from
      while i < to
        e = list[i]
        len = e[1] ? 32 : 24
        off = e[1] ? 4 : 0
        a = offs[i - from]
        if rl == 0
          x = W_BX + a
          y = W_BY + W_BH + r * 32 + off
        elsif rl == 2
          x = W_BX + W_BW - a - len
          y = W_BY - (r + 1) * 32 + off
        elsif rl == 3
          x = W_BX - 8 - (r + 1) * 32 + off
          y = W_BY + W_BH / 2 - 72 + a
        else
          x = W_BX + W_BW + 8 + r * 32 + off
          y = W_BY + W_BH / 2 + 72 - a - len
        end
        turned_tile(e[1] ? turned : base, e[0], @x0 + x, @y0 + y)
        if e[2] == 1
          wide = side != e[1] # a side seat's upright tile and a sideways one of the others lie wide
          @gfx.draw_rect(@x0 + x, @y0 + y, wide ? 32 : 24, wide ? 24 : 32, C_LAST)
        end
        i += 1
      end
      r += 1
    end
  end

  # A seat's row along the edge: [code, sideways, offset] of its hand (the
  # drawn tile apart) and its melds (the first call at the far end).
  def watch_row(s)
    out = []
    a = 0
    sp = spec_hand(s)
    if sp
      tl = sp[0]
      tl += [sp[1]] if sp[1] >= 0
      i = 0
      while i < tl.size
        a += 8 if sp[1] >= 0 && i == tl.size - 1
        out << [tl[i], false, a]
        a += 24
        i += 1
      end
    else
      hc = @view["hc"].is_a?(Array) ? @view["hc"][s].to_i : 13
      i = 0
      while i < hc
        a += 8 if i == hc - 1 && hc % 3 == 2
        out << [37, false, a]
        a += 24
        i += 1
      end
    end
    a += 12
    ms = xl_melds(s)
    k = ms.size - 1
    while k >= 0
      kind = ms[k][0]
      tl = ms[k][2]
      si = xl_sideways_index(kind, ms[k][1], tl.size)
      j = 0
      while j < tl.size
        c = tl[j]
        c = 37 if kind == 3 && (j == 0 || j == tl.size - 1)
        sw = j == si || (kind == 5 && j == 3)
        out << [c, sw, a]
        a += sw ? 32 : 24
        j += 1
      end
      a += 4
      k -= 1
    end
    out
  end

  # Draws a seat's row: one line along the top or the bottom edge, a column
  # along a side edge (and a second column inside it for what does not fit).
  def draw_spec_hand(rl, s)
    list = watch_row(s)
    return if list.empty?
    base = xl_rot(rl)
    turned = (base + 3) % 4
    side = rl == 1 || rl == 3
    cols = [list]
    if side
      room = WATCH[:w_row1][3]
      i = 0
      i += 1 while i < list.size && list[i][2] + (list[i][1] ? 32 : 24) <= room
      if i < list.size
        cut = list[i][2]
        rest = []
        list[i..].each { |e| rest << [e[0], e[1], e[2] - cut] }
        cols = [list[0, i], rest]
      end
    end
    ci = 0
    while ci < cols.size
      col = cols[ci]
      pr = WATCH[ci == 0 ? [:w_row0, :w_row1, :w_row2, :w_row3][rl] : (rl == 1 ? :w_row1b : :w_row3b)]
      len_room = side ? pr[3] : pr[2]
      lens = []
      col.each { |e| lens << (e[1] ? 32 : 24) }
      # The offsets as laid out, closed up past the room.
      offs = []
      last = col[col.size - 1]
      total = last[2] + (last[1] ? 32 : 24)
      col.each do |e|
        offs << (total <= len_room ? e[2] : e[2] * (len_room - lens[lens.size - 1]) / (total - lens[lens.size - 1]))
      end
      j = 0
      while j < col.size
        e = col[j]
        a = offs[j]
        len = e[1] ? 32 : 24
        t = e[1] ? 24 : 32
        if rl == 0
          x = pr[0] + a
          y = pr[1] + 32 - t
        elsif rl == 2
          x = pr[0] + pr[2] - a - len
          y = pr[1]
        elsif rl == 3
          x = pr[0]
          y = pr[1] + a
        else
          x = pr[0] + 32 - t
          y = pr[1] + pr[3] - a - len
        end
        turned_tile(e[1] ? turned : base, e[0], @x0 + x, @y0 + y)
        j += 1
      end
      ci += 1
    end
  end

  # The board: the round, the honba, the sticks, what is left, the dora.
  def draw_watch_board
    v = @view
    bx = @x0 + W_BX
    by = @y0 + W_BY
    @gfx.fill_rect(bx, by, W_BW, W_BH, 0x24)
    @gfx.draw_rect(bx, by, W_BW, W_BH, C_DIM)
    r = v["r"]
    return unless r.is_a?(Array)
    fnt(12, 1)
    s = "#{WIND[r[0].to_i % 4]}#{r[1]}#{t(:mj_kyoku)} #{r[2]}#{t(:mj_honba)} #{t(:mj_sticks)}#{r[3]}"
    s = "#{s} #{t(:mj_left)}#{r[5]}" if r.size > 5
    s = fit(s, W_BW - 8)
    text_spaced(bx + (W_BW - tw(s)) / 2, by + 6, s)
    d = bytes_of(v["dora"])
    w = tw(t(:mj_dora)) + 4 + d.size * 24
    dx = bx + (W_BW - w) / 2
    text(dx, by + 38, t(:mj_dora), C_DIM)
    dx += tw(t(:mj_dora)) + 4
    i = 0
    while i < d.size
      small_tile(d[i], dx + i * 24, by + 26)
      i += 1
    end
  end

  # "手番: 南 name" (nil: nobody's turn now).
  def turn_text
    r = @view ? @view["r"] : nil
    return nil unless r.is_a?(Array) && r.size > 7 && @view["st"] == "play"
    s = r[6].to_i
    "手番: #{wind_of(s)} #{seat_name(s)}"
  end

  # The four seats on a line above the board (the 640x360 spectators):
  # wind and name, then the points and riichi; the seat to act framed.
  def draw_score_strip
    v = @view
    nm = v["nm"].is_a?(Array) ? v["nm"] : []
    sc = v["sc"].is_a?(Array) ? v["sc"] : []
    ri = v["ri"].to_i
    r = v["r"]
    cur = r.is_a?(Array) && r.size > 6 && v["st"] == "play" ? r[6].to_i : -1
    fnt(12, 1)
    pr = WATCH[:w_strip]
    cw = pr[2] / 4
    s = 0
    while s < 4
      x = @x0 + pr[0] + s * cw
      y = @y0 + pr[1]
      turn = s == cur
      @gfx.fill_rect(x, y, cw - 4, pr[3], turn ? C_SEL : C_BOX)
      @gfx.draw_rect(x, y, cw - 4, pr[3], C_TURN) if turn
      col = away?(s) ? C_AWAY : (turn ? C_TURN : C_TEXT)
      head = "#{wind_of(s)} "
      text(x + 4, y + 2, "#{head}#{fit(nm[s].to_s, cw - 12 - tw(head))}", col)
      tail = sc[s].to_s
      tail = "#{tail} #{t(:mj_riichi)}" if (ri >> s) & 1 == 1
      text(x + 4, y + 16, tail, col)
      s += 1
    end
  end
end
