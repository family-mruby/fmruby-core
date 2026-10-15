# Mahjong table: the conventional 1280x720 table of the browser build
# (MJ-L, doc/ruby_asterism/report/mj_layout.md). Part of MahjongApp
# (mahjong.app.rb requires this file first; the constants it names are the
# app's, looked up when the methods run).

class MahjongApp < FmrbApp
  # ------------------------------------------------------------ the 1280x720 table (MJ-L)
  #
  # Sizes (the sheets in large/): hand 84x112 (twice the 640x360 hand),
  # river and own melds 48x64 (twice the 640x360 river), turned by quarter
  # turns for the other seats, the opponents' melds 24x32 (the river sheet
  # itself and its turns), the backs of the opponents' hands 32x20 / 20x32.
  # The board is 288x160 in the middle (6 river tiles wide); every river
  # starts at the board's edge and runs from its player's left to right,
  # six to a row: three rows above and below (the third takes the rest),
  # four beside it. The layout is worked out in the report (mj_layout.md 4).
  #
  # Drawing: every tile, back and highlight is an item [x, y, w, h, image,
  # src_x, src_y, mark, tile]. A drawing builds the items of each part
  # (rivers, opponents' rows, own hand, own melds) and compares them with
  # what was drawn: an item that changed is cleared (its old rectangle) and
  # drawn again; an unchanged item that a clear touched is drawn again too.
  # The texts (the board, the status line, the buttons) have a signature
  # each and are redrawn when it changes. A changed screen (another mode, a
  # result shown or gone) draws everything.

  XL_BX = 496
  XL_BY = 236
  XL_BW = 288
  XL_BH = 160
  XL_HAND_X = 24
  XL_HAND_Y = 600
  XL_RIGHT = 1272
  XL_BTN_Y = 540
  XL_STATUS = [16, 530, 470, 56]   # the status lines (below the left player's row, above the hand)
  XL_WAITS = [56, 480, 430, 48]    # the riichi help (above the status lines, right of the left player's row)
  XL_LIFT = 10                     # the chosen hand tile is this much higher
  C_BOARD = 0x24
  C_BTN_WIN = 0xA0
  C_BTN_PASS = 0x6D

  def load_xl_sheets
    return if @xl_sheets || !@xl
    @xl_sheets = true
    @xl_hand = [load_sheet("tiles_hand2_a.bmp", 840, 224, XL_DIR), load_sheet("tiles_hand2_b.bmp", 840, 224, XL_DIR)]
    @xl_r2 = [load_sheet("tiles_river2_r0.bmp", 480, 256, XL_DIR), load_sheet("tiles_river2_r90.bmp", 640, 192, XL_DIR),
              load_sheet("tiles_river2_r180.bmp", 480, 256, XL_DIR), load_sheet("tiles_river2_r270.bmp", 640, 192, XL_DIR)]
    @xl_r1 = [@river_sheet, load_sheet("tiles_river_r90.bmp", 320, 96, XL_DIR),
              load_sheet("tiles_river_r180.bmp", 240, 128, XL_DIR), load_sheet("tiles_river_r270.bmp", 320, 96, XL_DIR)]
    @xl_side = load_sheet("tiles_side.bmp", 72, 32, XL_DIR)
  end

  def img_id(img)
    img ? img.id : -1
  end

  # Items. mark: 0 none, 1 the last discard (a frame), 2 not to be chosen
  # (a bar, while picking the riichi tile), 3 a discard that leaves the hand
  # tenpai (the help: a yellow triangle at the top), 4 one that keeps the
  # least shanten (the help at level 3: a white triangle).
  def xl_hand_item(c, x, y, mark)
    k = c < 20 ? c : c - 20
    [x, y, 84, 112, img_id(@xl_hand[c < 20 ? 0 : 1]), (k % 10) * 84, (k / 10) * 112, mark, c]
  end

  # A river-size tile (big: 48x64, else 24x32) turned rot quarter turns.
  def xl_tile_item(big, rot, c, x, y, mark)
    w = big ? 48 : 24
    h = big ? 64 : 32
    if rot % 2 == 1
      t = w
      w = h
      h = t
    end
    img = big ? @xl_r2[rot] : @xl_r1[rot]
    [x, y, w, h, img_id(img), (c % 10) * w, (c / 10) * h, mark, c]
  end

  # The back of an opponent's concealed tile, for the seat rl (1, 2, 3).
  def xl_side_item(rl, x, y)
    return [x, y, 32, 20, img_id(@xl_side), 0, 0, 0, 37] if rl == 2
    [x, y, 20, 32, img_id(@xl_side), rl == 3 ? 32 : 52, 0, 0, 37]
  end

  def xl_draw_item(it)
    if it[4] >= 0
      @gfx.draw_tile(it[4], it[5], it[6], it[2], it[3], dst_x: it[0], dst_y: it[1])
    else
      draw_text_tile(it[8], it[0], it[1], it[2], it[3])
    end
    if it[7] == 1
      @gfx.draw_rect(it[0], it[1], it[2], it[3], C_LAST)
      @gfx.draw_rect(it[0] + 1, it[1] + 1, it[2] - 2, it[3] - 2, C_LAST)
    elsif it[7] == 2
      @gfx.fill_rect(it[0] + 4, it[1] + it[3] - 14, it[2] - 8, 5, C_DIM)
    elsif it[7] >= 3
      tenpai_mark(it[0], it[1], it[2], 8, it[7] == 4 ? C_KEEP : C_TURN)
    end
    @xl_n_items += 1
    @xl_n_px += it[2] * it[3]
  end

  # The base turn of a seat's tiles (rl: 0 self, 1 right, 2 across, 3 left):
  # the top of the picture points away from its player.
  def xl_rot(rl)
    [0, 3, 2, 1][rl]
  end

  # A river: [[code, sideways, mark], ...] laid out for the seat rl.
  def xl_river_items(rl, list)
    out = []
    side = rl == 1 || rl == 3
    rows = side ? 4 : 3
    base = xl_rot(rl)
    turned = (base + 3) % 4
    row = 0
    a = 0
    i = 0
    while i < list.size
      if i > 0 && i % 6 == 0 && i / 6 < rows
        row += 1
        a = 0
      end
      e = list[i]
      sw = e[1]
      len = sw ? 64 : 48       # along the row
      off = sw ? 8 : 0         # a sideways tile is 48 deep in a 64 deep row
      rot = sw ? turned : base
      if rl == 0
        x = XL_BX + a
        y = XL_BY + XL_BH + row * 64 + off
      elsif rl == 2
        x = XL_BX + XL_BW - a - len
        y = XL_BY - (row + 1) * 64 + off
      elsif rl == 3
        x = XL_BX - (row + 1) * 64 + off
        y = XL_BY + XL_BH / 2 - 144 + a
      else
        x = XL_BX + XL_BW + row * 64 + off
        y = XL_BY + XL_BH / 2 + 144 - a - len
      end
      out << xl_tile_item(true, rot, e[0], x, y, e[2])
      a += len
      i += 1
    end
    out
  end

  # The river of a seat as [[code, sideways, mark], ...] (called tiles are
  # not in it; the tile after a called riichi tile stays upright).
  def xl_river_list(s)
    b = bytes_of(@view["kw"][s])
    ls = @view["ls"]
    out = []
    i = 0
    while i < b.size
      c = b[i]
      if c & 0x80 == 0
        last = i == b.size - 1 && ls.is_a?(Array) && ls[0] == s
        out << [c & 0x3F, c & 0x40 != 0, last ? 1 : 0]
      end
      i += 1
    end
    out
  end

  # The melds of a seat: [[kind, from, [tiles]], ...].
  def xl_melds(s)
    fu = @view["fu"]
    out = []
    return out unless fu.is_a?(Array)
    b = bytes_of(fu[s])
    i = 0
    while i + 2 < b.size
      n = b[i + 2]
      out << [b[i], b[i + 1], b[i + 3, n]]
      i += 3 + n
    end
    out
  end

  # Which of a meld's tiles lies sideways: the one taken from the player on
  # the left goes at the left, from across second, from the right at the end.
  def xl_sideways_index(kind, from, n)
    return -1 if kind == 3 || from == 0
    m = kind == 5 ? 3 : n
    return 0 if from == 3
    return 1 if from == 2
    m - 1
  end

  # The own melds, from the right edge leftwards (the first call rightmost).
  def xl_own_meld_items(seat)
    out = []
    ms = xl_melds(seat)
    cx = XL_RIGHT
    k = 0
    while k < ms.size
      kind = ms[k][0]
      tl = ms[k][2]
      n = kind == 5 ? 3 : tl.size
      si = xl_sideways_index(kind, ms[k][1], tl.size)
      w = 0
      j = 0
      while j < n
        w += j == si ? 64 : 48
        j += 1
      end
      x = cx - w
      j = 0
      while j < n
        c = tl[j]
        c = 37 if kind == 3 && (j == 0 || j == n - 1)
        if j == si
          out << xl_tile_item(true, 3, c, x, XL_HAND_Y + 112 - 48, 0)
          out << xl_tile_item(true, 3, tl[3], x, XL_HAND_Y + 112 - 96, 0) if kind == 5
          x += 64
        else
          out << xl_tile_item(true, 0, c, x, XL_HAND_Y + 112 - 64, 0)
          x += 48
        end
        j += 1
      end
      cx -= w + 8
      k += 1
    end
    @xl_meld_left = cx
    out
  end

  # Where an item goes on an opponent's row: a along the row from its start
  # (ox, oy), len along it, t across it, against the screen's edge.
  def xl_place(rl, ox, oy, a, len, t)
    if rl == 2
      [ox - a - len, oy, len, t]
    elsif rl == 3
      [ox, oy + a, t, len]
    else
      [ox - t, oy - a - len, t, len]
    end
  end

  XL_ROW_START = [nil, [1268, 532], [900, 10], [12, 56]]

  # An opponent's row: the backs of its hand (the drawn tile apart), then
  # its melds in small tiles, the first call at the far end.
  def xl_opp_items(s, rl)
    out = []
    o = XL_ROW_START[rl]
    hc = @view["hc"].is_a?(Array) ? @view["hc"][s].to_i : 13
    a = 0
    i = 0
    while i < hc
      a += 8 if i == hc - 1 && hc % 3 == 2
      p = xl_place(rl, o[0], o[1], a, 32, 20)
      out << xl_side_item(rl, p[0], p[1])
      a += 32
      i += 1
    end
    a += 14
    base = xl_rot(rl)
    turned = (base + 3) % 4
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
        p = sw ? xl_place(rl, o[0], o[1], a, 32, 24) : xl_place(rl, o[0], o[1], a, 24, 32)
        out << xl_tile_item(false, sw ? turned : base, c, p[0], p[1], 0)
        a += sw ? 32 : 24
        j += 1
      end
      a += 6
      k -= 1
    end
    out
  end

  def xl_hand_items
    out = []
    h = hand_codes
    n = h.size + (drawn >= 0 ? 1 : 0)
    pick = @pick
    i = 0
    while i < n
      c = hand_at(i)
      x = XL_HAND_X + i * 84
      x += 12 if i >= h.size
      y = XL_HAND_Y - (i == @sel ? XL_LIFT : 0)
      hm = help_mark(c)
      mark = hm == :tenpai ? 3 : (hm == :keep ? 4 : (pick && !can_discard?(c) ? 2 : 0))
      out << xl_hand_item(c, x, y, mark)
      i += 1
    end
    out
  end

  def xl_hand_index_at(x, y)
    return nil if y < XL_HAND_Y - XL_LIFT || y >= XL_HAND_Y + 112
    n = hand_codes.size + (drawn >= 0 ? 1 : 0)
    i = 0
    while i < n
      hx = XL_HAND_X + i * 84
      hx += 12 if i >= hand_codes.size
      return i if x >= hx && x < hx + 84
      i += 1
    end
    nil
  end

  def overlap?(a, b)
    a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3]
  end

  # Everything of the table that is made of items, by part.
  def xl_parts
    parts = {}
    s = 0
    while s < 4
      rl = rel(s)
      parts["r#{rl}"] = xl_river_items(rl, xl_river_list(s))
      parts["o#{rl}"] = xl_opp_items(s, rl) if rl != 0
      s += 1
    end
    parts["m"] = xl_own_meld_items(@seat || 0)
    if @seat
      parts["h"] = xl_hand_items
    else
      # Watching: the player of seat 0 is at the bottom, its hand face down.
      hc = @view["hc"].is_a?(Array) ? @view["hc"][0].to_i : 0
      hs = []
      i = 0
      while i < hc
        hs << xl_hand_item(37, XL_HAND_X + i * 84 + (i == hc - 1 && hc % 3 == 2 ? 12 : 0), XL_HAND_Y, 0)
        i += 1
      end
      parts["h"] = hs
    end
    parts
  end

  def xl_draw
    @xl_n_items = 0
    @xl_n_px = 0
    res = @view["res"] || @view["fin"]
    screen = res ? "result #{@view['sv']} #{@view['st']}" : "table"
    full = @xl_items.nil? || @xl_screen != screen
    # A result stays as drawn (its box covers the table) until it goes.
    return if res && !full
    @xl_screen = screen
    if full
      clear_user_area(C_FELT)
      @xl_n_px += @w * @h
      @xl_items = {}
      @xl_sig = {}
      @xl_btn_rects = []
    end
    parts = xl_parts
    clears = []
    draws = []
    parts.each do |key, items|
      old = @xl_items[key] || []
      n = items.size > old.size ? items.size : old.size
      i = 0
      while i < n
        a = old[i]
        b = items[i]
        if a != b
          clears << a if a
          draws << b if b
        end
        i += 1
      end
    end
    # The texts: the board, the status lines, the buttons.
    board = xl_board_sig
    status = xl_status_sig
    btns = xl_button_sig
    help = xl_waits_sig
    redraw_board = full || board != @xl_sig[:board]
    redraw_status = full || status != @xl_sig[:status]
    redraw_btns = full || btns != @xl_sig[:btns]
    redraw_help = full || help != @xl_sig[:help]
    if redraw_btns
      i = 0
      while i < @xl_btn_rects.size
        clears << @xl_btn_rects[i]
        i += 1
      end
    end
    clears << XL_STATUS if redraw_status && !full
    clears << XL_WAITS if redraw_help && !full && @xl_sig[:help]
    i = 0
    while i < clears.size
      c = clears[i]
      @gfx.fill_rect(c[0], c[1], c[2], c[3], C_FELT)
      @xl_n_px += c[2] * c[3]
      i += 1
    end
    # Unchanged items a clear touched are drawn again.
    unless full || clears.empty?
      parts.each do |key, items|
        old = @xl_items[key] || []
        i = 0
        while i < items.size
          it = items[i]
          if old[i] == it
            j = 0
            while j < clears.size
              if overlap?(it, clears[j])
                draws << it
                break
              end
              j += 1
            end
          end
          i += 1
        end
      end
      j = 0
      while j < clears.size && !redraw_help
        redraw_help = true if help && overlap?(XL_WAITS, clears[j])
        j += 1
      end
      j = 0
      while j < clears.size && !redraw_btns
        k = 0
        while k < @xl_btn_rects.size
          redraw_btns = true if overlap?(@xl_btn_rects[k], clears[j])
          k += 1
        end
        j += 1
      end
    end
    i = 0
    while i < draws.size
      xl_draw_item(draws[i])
      i += 1
    end
    @xl_items = parts
    if redraw_board
      xl_draw_board
      @xl_sig[:board] = board
    end
    if redraw_status
      xl_draw_status
      @xl_sig[:status] = status
    end
    if redraw_help
      xl_draw_waits(help) if help && !res
      @xl_sig[:help] = res ? nil : help
    end
    if res
      xl_draw_result if full
      @xl_btn_rects = []
      @xl_sig[:btns] = btns
      return
    end
    if redraw_btns
      xl_draw_buttons
      @xl_sig[:btns] = btns
    end
  end

  def xl_board_sig
    v = @view
    [v["r"], v["sc"], v["nm"], v["ri"], v["aw"], v["dora"], v["st"], @seat, v["sk"]]
  end

  def xl_status_sig
    [status_line.to_s, @pick, @deadline ? time_left_text : nil, @lat]
  end

  # The help: nil (nothing shown), [:hint] (the marks are on, no tenpai
  # discard chosen), [:keep] (level 3, no tile with its acceptance chosen),
  # the chosen tile and its waits, or [:uk, tile, shanten, acceptance].
  def xl_waits_sig
    return nil if @seat.nil?
    unless @waits.empty?
      w = sel_waits
      return [:hint] unless w
      return [hand_at(@sel), w]
    end
    return nil if @uk.empty?
    u = sel_uk
    return [:keep, @uk_min] unless u
    [:uk, hand_at(@sel), u[0], u[1]]
  end

  # The riichi help over XL_WAITS: the waits of the chosen discard (each
  # with what is left of it unseen) and their sum, or a line that says what
  # the marks mean.
  def xl_draw_waits(sig)
    x = @x0 + XL_WAITS[0]
    y = @y0 + XL_WAITS[1]
    fnt(16, 1)
    if sig[0] == :hint || sig[0] == :keep
      keep = sig[0] == :keep
      tenpai_mark(x + 4, y + 12, 24, 8, keep ? C_KEEP : C_TURN)
      text(x + 32, y + 16, keep ? "#{t(:mj_keep)} (#{sig[1]}#{t(:mj_shanten)})" : t(:mj_help), C_DIM)
      return
    end
    @gfx.fill_round_rect(x, y, XL_WAITS[2], XL_WAITS[3], 6, C_BOX)
    @gfx.draw_round_rect(x, y, XL_WAITS[2], XL_WAITS[3], 6, C_TURN)
    @xl_n_px += XL_WAITS[2] * XL_WAITS[3]
    if sig[0] == :uk
      if sig[2] == 0
        text(x + 10, y + 16, t(:mj_no_wait), C_RED)
        return
      end
      xl_tiles_counts(x, y, "#{sig[2]}#{t(:mj_shanten)}", sig[2] == @uk_min ? C_TURN : C_DIM, sig[3],
                      "#{sig[3].size}#{t(:mj_kinds)} ")
      return
    end
    xl_tiles_counts(x, y, t(:mj_waits), C_TURN, sig[1], "")
  end

  # Tiles with what is left of each, then the sum, from x + lx in the help
  # box; the counts go when they do not fit, then the tiles close up, and
  # past what fits at 24 px the sum goes up and the tiles' names go below it.
  def xl_tiles_counts(x, y, head, hcol, w, pre)
    lx = tw(head) + 22
    sum = 0
    i = 0
    while i < w.size
      sum += w[i][1]
      i += 1
    end
    tail = "#{pre}#{t(:mj_total)}#{sum}#{t(:mj_mai)}"
    room = XL_WAITS[2] - lx - tw(tail) - 16
    counts = w.size * 42 <= room
    step = counts ? 42 : (w.size * 28 <= room ? 28 : 24)
    if w.size * step > room
      # Too many for the tiles: the sum on the first line, the names below.
      text_spaced(x + 10, y + 6, head, hcol)
      text_spaced(x + lx, y + 6, tail, sum == 0 ? C_RED : C_TURN)
      fnt(12, 1)
      cx = x + 10
      i = 0
      while i < w.size && cx + 18 <= x + XL_WAITS[2] - 6
        text(cx, y + 30, tile_text(w[i][0]), C_TEXT)
        cx += 18
        i += 1
      end
      return
    end
    n = w.size
    text_spaced(x + 10, y + 16, head, hcol)
    cx = x + lx
    i = 0
    while i < n
      xl_draw_item(xl_tile_item(false, 0, w[i][0], cx, y + 8, 0))
      text(cx + 26, y + 16, w[i][1].to_s, w[i][1] == 0 ? C_RED : C_TEXT) if counts
      cx += step
      i += 1
    end
    text_spaced(cx + 6, y + 16, tail, sum == 0 ? C_RED : C_TURN)
  end

  def xl_button_sig
    [@view["a"], @seat.nil?, @assist]
  end

  # The centre board: the round, the honba and the sticks, what is left of
  # the wall, the dora indicators, and each seat's wind, name and points on
  # the side that faces it.
  def xl_draw_board
    v = @view
    bx = @x0 + XL_BX
    by = @y0 + XL_BY
    @gfx.fill_rect(bx, by, XL_BW, XL_BH, C_BOARD)
    @gfx.draw_rect(bx, by, XL_BW, XL_BH, C_DIM)
    @xl_n_px += XL_BW * XL_BH
    r = v["r"]
    return unless r.is_a?(Array)
    fnt(16, 2)
    head = "#{WIND[r[0].to_i % 4]}#{r[1]}#{t(:mj_kyoku)}"
    text(bx + (XL_BW - tw(head)) / 2, by + 26, head)
    fnt(16, 1)
    line = "#{r[2]}#{t(:mj_honba)}  #{t(:mj_sticks)}#{r[3]}"
    line = "#{line}  #{t(:mj_left)}#{r[5]}" if r.size > 5
    text(bx + (XL_BW - tw(line)) / 2, by + 60, line, C_DIM)
    d = bytes_of(v["dora"])
    dl = tw(t(:mj_dora)) + 6 + d.size * 24
    dx = bx + (XL_BW - dl) / 2
    text(dx, by + 86, t(:mj_dora), C_DIM)
    dx += tw(t(:mj_dora)) + 6
    i = 0
    while i < d.size
      xl_draw_item(xl_tile_item(false, 0, d[i], dx + i * 24, by + 78, 0))
      i += 1
    end
    nm = v["nm"].is_a?(Array) ? v["nm"] : []
    sc = v["sc"].is_a?(Array) ? v["sc"] : []
    ri = v["ri"].to_i
    cur = r.size > 6 ? r[6].to_i : -1
    s = 0
    while s < 4
      rl = rel(s)
      col = away?(s) ? C_AWAY : (s == cur && v["st"] == "play" ? C_TURN : C_TEXT)
      name = s == @seat ? t(:mj_you) : nm[s].to_s
      head = "#{wind_of(s)} "
      pts = sc[s].to_s
      stick = (ri >> s) & 1 == 1
      if rl == 0 || rl == 2
        name = fit(name, 120)
        label = "#{head}#{name}  #{pts}"
        lx = bx + (XL_BW - tw(label)) / 2
        ly = rl == 0 ? by + XL_BH - 22 : by + 6
        text(lx, ly, label, col)
        @gfx.fill_rect(lx, ly + 17, tw(label), 2, C_TURN) if s == cur && v["st"] == "play"
        xl_stick(bx + XL_BW / 2 - 24, rl == 0 ? ly - 8 : ly + 19) if stick
      else
        name = fit(name, 72)
        lx = rl == 3 ? bx + 6 : bx + XL_BW - 6 - (tw("#{head}#{name}") > tw(pts) ? tw("#{head}#{name}") : tw(pts))
        text(lx, by + 112, "#{head}#{name}", col)
        @gfx.fill_rect(lx, by + 147, tw(pts), 2, C_TURN) if s == cur && v["st"] == "play"
        text(lx, by + 130, pts, col)
        xl_stick(lx, by + 149) if stick
      end
      s += 1
    end
  end

  # A riichi stick (48x6, white with a red dot).
  def xl_stick(x, y)
    @gfx.fill_rect(x, y, 48, 6, C_TEXT)
    @gfx.fill_rect(x + 21, y + 1, 6, 4, C_RED)
  end

  # The status lines: the time left, what the screen waits for, the reply time.
  def xl_draw_status
    x = @x0 + XL_STATUS[0]
    y = @y0 + XL_STATUS[1]
    fnt(16, 1)
    msg = status_line.to_s
    msg = "#{t(:mj_pick)} #{msg}" if @pick
    text(x + 4, y + 4, msg, C_TURN) unless msg.strip.empty?
    line = ""
    line = "#{t(:mj_left)} #{time_left_text}" if @deadline
    line = "#{line}  #{t(:mj_resp)} #{@lat}#{t(:mj_ms)}" if @lat
    text(x + 4, y + 28, line, C_TURN) unless line.empty?
    @xl_n_px += XL_STATUS[2] * XL_STATUS[3]
  end

  # The buttons, right-aligned above the right end of the hand.
  def xl_draw_buttons
    @buttons = []
    @xl_btn_rects = []
    list = @view["a"].is_a?(Array) ? @view["a"] : []
    labels = []
    i = 0
    while i < list.size
      o = list[i]
      tl = bytes_of(o[1])
      l = button_label(o[0], tl)
      labels << [o[0], tl, l] if l
      i += 1
    end
    labels << [B_LEAVE, [], t(:mj_leave)] if @seat.nil?
    fnt(12, 2)
    # The help level, at the top left (nothing of the table reaches it).
    if @seat
      l = assist_label
      xl_button(B_ASSIST, [], 12, 8, tw(l) + 24, l)
    end
    x = XL_RIGHT
    i = labels.size - 1
    while i >= 0
      w = tw(labels[i][2]) + 24
      x -= w
      xl_button(labels[i][0], labels[i][1], x, XL_BTN_Y, w, labels[i][2])
      x -= 8
      i -= 1
    end
  end

  # Win buttons red, passing grey, the rest the usual colour (as in the sample).
  def xl_button(type, tiles, x, y, w, label)
    @buttons << [type, x, y, w, 40, tiles]
    @xl_btn_rects << [x, y, w, 40]
    col = type == A_RON || type == A_TSUMO ? C_BTN_WIN : (type == A_SKIP || type == B_LEAVE || type == B_ASSIST ? C_BTN_PASS : C_BTN)
    @gfx.fill_round_rect(x, y, w, 40, 8, col)
    text(x + 12, y + 8, label)
    @xl_n_px += w * 40
  end

  # A round's result (or the end of the game) in a box over the middle. One
  # block per winner; with three winners the tiles are the small ones.
  def xl_draw_result
    res = @view["res"]
    fin = @view["fin"]
    wins = res.is_a?(Hash) && res["wn"].is_a?(Array) ? res["wn"] : []
    big = wins.size <= 2
    over = fin.is_a?(Array) && @view["st"] == "over"
    fnt(12, 2)
    hgt = 24
    wins.each { |w| hgt += xl_win_height(w, big) }
    hgt += 30 if res.is_a?(Hash) # the round
    hgt += 34 if res.is_a?(Hash) && wins.empty?
    hgt += 130 if res.is_a?(Hash) && !over
    hgt += 40 + fin.size * 30 + 34 if over
    hgt += 56
    bw = 800
    bx = @x0 + (@w - bw) / 2
    by = @y0 + (@h - hgt) / 2
    by = @y0 + 4 if by < 4
    @gfx.fill_rect(bx, by, bw, hgt, C_BOX)
    @gfx.draw_rect(bx, by, bw, hgt, C_TURN)
    @xl_n_px += bw * hgt
    x = bx + 16
    y = by + 12
    if res.is_a?(Hash)
      k = res["k"].to_s
      text(x, y, round_head(res), C_DIM)
      y += 30
      if k == "tsumo_agari" || k == "ron_agari"
        i = 0
        while i < wins.size
          y = xl_draw_win(wins[i], k, res["f"], x, y, big)
          i += 1
        end
      else
        text_spaced(x, y, fit(draw_head(res), bw - 32), C_TURN)
        y += 34
      end
      unless over
        # Every seat: wind, name (and tenpai at a draw), before -> after, the change.
        i = 0
        while i < 4
          l = score_line(i, res)
          cy = y + i * 30
          text(x, cy, l[0])
          text(x + 400, cy, l[1], C_DIM)
          text(x + 640, cy, l[2], delta_color(i, res))
          i += 1
        end
        y += 130
      end
    end
    if over
      text(x, y, over_head, C_TURN)
      y += 34
      i = 0
      while i < fin.size
        f = fin[i]
        text(x, y, fin_line(f))
        y += 30
        i += 1
      end
      text(x, y, t(:mj_back_lobby), C_DIM)
    end
    # The buttons (next) at the bottom right of the box.
    @buttons = []
    @xl_btn_rects = []
    list = @view["a"].is_a?(Array) ? @view["a"] : []
    bxr = bx + bw - 12
    i = list.size - 1
    while i >= 0
      l = button_label(list[i][0], bytes_of(list[i][1]))
      if l
        w = tw(l) + 24
        bxr -= w
        xl_button(list[i][0], bytes_of(list[i][1]), bxr, by + hgt - 50, w, l)
        bxr -= 8
      end
      i -= 1
    end
  end

  def xl_win_height(w, big)
    ys = w["y"].is_a?(Array) ? w["y"].size : 0
    34 + (big ? 64 : 32) + 10 + ((ys + 2) / 3) * 28 + 36
  end

  def xl_draw_win(w, k, from, x, y, big)
    text(x, y, win_head(w, k, from), C_TURN)
    y += 34
    hc = bytes_of(w["h"])
    tw_ = big ? 48 : 24
    i = 0
    while i < hc.size
      xl_draw_item(xl_tile_item(big, 0, hc[i], x + i * tw_, y, 0))
      i += 1
    end
    xl_draw_item(xl_tile_item(big, 0, w["ag"].to_i, x + hc.size * tw_ + 12, y, 1))
    # The melds after the winning tile, smaller when they would not fit.
    ms = meld_lists(w["w"].to_i)
    n = 0
    ms.each { |m| n += m.size }
    cx = x + (hc.size + 1) * tw_ + 12
    big_m = big && cx + n * 48 + ms.size * 12 <= x + 768
    mw = big_m ? 48 : 24
    my = big_m || !big ? y : y + 32
    ms.each do |m|
      cx += 12
      m.each do |c|
        xl_draw_item(xl_tile_item(big_m, 0, c, cx, my, 0))
        cx += mw
      end
    end
    y += (big ? 64 : 32) + 10
    ys = w["y"].is_a?(Array) ? w["y"] : []
    i = 0
    while i < ys.size
      text(x + (i % 3) * 256, y + (i / 3) * 28, yaku_label(w, i))
      i += 1
    end
    y += ((ys.size + 2) / 3) * 28
    text_spaced(x, y, w["pt"].to_s, C_TURN)
    pa = pao_text(w)
    text_spaced(x + tw(w["pt"].to_s) + 8, y, pa, C_AWAY) unless pa.empty?
    y + 36
  end
end
