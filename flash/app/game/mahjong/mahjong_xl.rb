# Mahjong table: the conventional 1280x720 table of the browser build
# (MJ-L, doc/ruby_asterism/report/mj_layout.md; the worst cases fitted in
# MJ5, mj5.md). Part of MahjongApp (mahjong.app.rb requires this file
# first; the constants it names are the app's, looked up when the methods
# run).

class MahjongApp < FmrbApp
  # ------------------------------------------------------------ the 1280x720 table (MJ-L)
  #
  # Sizes (the sheets in large/): hand 84x112 (twice the 640x360 hand),
  # river and own melds 48x64 (twice the 640x360 river), turned by quarter
  # turns for the other seats, the opponents' melds 24x32 (the river sheet
  # itself and its turns), the backs of the opponents' hands 32x20 / 20x32.
  # The board is 288x160 in the middle (6 river tiles wide); every river
  # starts at the board's edge (the side ones 16 px off it, room for a
  # sideways tile in the row next to them) and runs from its player's left
  # to right, six to a row: three rows above and below, four beside it. The
  # last row takes the rest; past the room of its panel (xl_panel) its
  # tiles close up. A spectator sees the four hands face up, all in the
  # small tiles, each row turned to its player (no big hand at the bottom).
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
  XL_HAND_X = 48
  XL_HAND_Y = 608                  # the hand's bottom on the screen's (room above it for the marks)
  XL_RIGHT = 1272
  XL_BTN_RIGHT = 1228                # the buttons' right edge (the right player's row is past it)
  XL_BTN_ROWS = [548, 504, 460, 416] # the buttons' rows, the first at the bottom
  XL_STATUS = [52, 530, 434, 56]   # the status lines (right of the left player's row, above the hand)
  XL_WAITS = [56, 480, 430, 48]    # the riichi help (above the status lines)
  XL_LIFT = 10                     # the chosen hand tile is this much higher
  XL_SIDE = 16                     # the side rivers stand this far off the board
  C_BOARD = 0x24
  C_BTN_WIN = 0xA0
  C_BTN_PASS = 0x6D

  # The parts of the screen (the overflow checker, tools/mahjong_overflow,
  # holds every drawing to them): [x, y, w, h].
  XL_PANELS = {
    board: [496, 236, 288, 160], status: XL_STATUS, waits: XL_WAITS, assist: [12, 8, 248, 40],
    buttons: [496, 416, 732, 172],
    r0: [496, 396, 732, 192], r1: [800, 12, 256, 448], r2: [260, 44, 524, 192], r3: [224, 172, 256, 304],
    o0: [380, 600, 848, 32], o1: [1236, 12, 32, 584], o2: [260, 10, 640, 32], o3: [12, 52, 32, 660],
    h: [48, 588, 1188, 132], m: [400, 608, 872, 112]
  }

  def xl_panel(key)
    XL_PANELS[key]
  end

  def load_xl_sheets
    return if @xl_sheets || !@xl
    @xl_sheets = true
    @xl_hand = [load_sheet("tiles_hand2_a.bmp", 840, 224, XL_DIR), load_sheet("tiles_hand2_b.bmp", 840, 224, XL_DIR)]
    @xl_r2 = [load_sheet("tiles_river2_r0.bmp", 480, 256, XL_DIR), load_sheet("tiles_river2_r90.bmp", 640, 192, XL_DIR),
              load_sheet("tiles_river2_r180.bmp", 480, 256, XL_DIR), load_sheet("tiles_river2_r270.bmp", 640, 192, XL_DIR)]
    load_turned_sheets
    @xl_r1 = @turned
    @xl_side = load_sheet("tiles_side.bmp", 72, 32, XL_DIR)
  end

  # The small tiles turned for the four seats ([r0, r90, r180, r270]): the
  # 1280x720 table's opponents and the 640x360 spectators (mahjong_watch.rb).
  def load_turned_sheets
    return if @turned
    @turned = [@river_sheet, load_sheet("tiles_river_r90.bmp", 320, 96, XL_DIR),
               load_sheet("tiles_river_r180.bmp", 240, 128, XL_DIR), load_sheet("tiles_river_r270.bmp", 320, 96, XL_DIR)]
  end

  def img_id(img)
    img ? img.id : -1
  end

  # Items. mark: 0 none, 1 the last discard (a frame), 2 not to be chosen
  # (a bar, while picking the riichi tile), 3 a discard that leaves the hand
  # tenpai (the help: a yellow triangle above the tile), 4 one that keeps the
  # least shanten (the help at level 3: a cyan triangle). A hand item has a
  # tenth element, true when it is the chosen tile (a blue frame).
  def xl_hand_item(c, x, y, mark, chosen = false)
    k = c < 20 ? c : c - 20
    [x, y, 84, 112, img_id(@xl_hand[c < 20 ? 0 : 1]), (k % 10) * 84, (k / 10) * 112, mark, c, chosen]
  end

  # The rectangle an item covers on the screen: its tile, and the help mark
  # above it.
  def xl_extent(it)
    return it if it[7] < 3
    r = mark_rise
    [it[0], it[1] - r, it[2], it[3] + r]
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
    chosen_frame(it[0], it[1], it[2], it[3]) if it[9]
    if it[7] == 1
      @gfx.draw_rect(it[0], it[1], it[2], it[3], C_LAST)
      @gfx.draw_rect(it[0] + 1, it[1] + 1, it[2] - 2, it[3] - 2, C_LAST)
    elsif it[7] == 2
      @gfx.fill_rect(it[0] + 4, it[1] + it[3] - 14, it[2] - 8, 5, C_DIM)
    elsif it[7] >= 3
      assist_mark(it[0], it[1], it[2], it[7] == 4 ? C_KEEP : C_TURN)
    end
    @xl_n_items += 1
    @xl_n_px += it[2] * it[3]
  end

  # The base turn of a seat's tiles (rl: 0 self, 1 right, 2 across, 3 left):
  # the top of the picture points away from its player.
  def xl_rot(rl)
    [0, 3, 2, 1][rl]
  end

  # Where each tile of a row goes along it: lens are the tiles' lengths,
  # room the length there is; past the room the tiles close up (each still
  # shows its start, the last one whole). Returns the offsets.
  def close_up(lens, room)
    total = 0
    lens.each { |l| total += l }
    out = []
    a = 0
    if total <= room || lens.size < 2
      lens.each do |l|
        out << a
        a += l
      end
      return out
    end
    last = lens[lens.size - 1]
    i = 0
    while i < lens.size
      out << a * (room - last) / (total - last)
      a += lens[i]
      i += 1
    end
    out
  end

  # The room of a river's last row (xl_panel).
  XL_LAST_ROW = [732, 448, 524, 304]

  # A river: [[code, sideways, mark], ...] laid out for the seat rl. The
  # bottom river's row ends go to @xl_r0_ends and the rows of the right one
  # to @xl_r1_rows (the buttons keep clear of them).
  def xl_river_items(rl, list)
    out = []
    side = rl == 1 || rl == 3
    rows = side ? 4 : 3
    base = xl_rot(rl)
    turned = (base + 3) % 4
    ends = [0, 0, 0, 0]
    r = 0
    while r < rows
      from = r * 6
      to = r == rows - 1 ? list.size : from + 6
      to = list.size if to > list.size
      break if from >= to
      lens = []
      i = from
      while i < to
        lens << (list[i][1] ? 64 : 48)
        i += 1
      end
      offs = close_up(lens, r == rows - 1 ? XL_LAST_ROW[rl] : 999)
      i = from
      while i < to
        e = list[i]
        sw = e[1]
        len = sw ? 64 : 48       # along the row
        off = sw ? 8 : 0         # a sideways tile is 48 deep in a 64 deep row
        a = offs[i - from]
        if rl == 0
          x = XL_BX + a
          y = XL_BY + XL_BH + r * 64 + off
        elsif rl == 2
          x = XL_BX + XL_BW - a - len
          y = XL_BY - (r + 1) * 64 + off
        elsif rl == 3
          x = XL_BX - XL_SIDE - (r + 1) * 64 + off
          y = XL_BY + XL_BH / 2 - 144 + a
        else
          x = XL_BX + XL_BW + XL_SIDE + r * 64 + off
          y = XL_BY + XL_BH / 2 + 144 - a - len
        end
        out << xl_tile_item(true, sw ? turned : base, e[0], x, y, e[2])
        ends[r] = XL_BX + a + len if rl == 0
        i += 1
      end
      r += 1
    end
    @xl_r0_ends = ends if rl == 0
    @xl_r1_rows = r if rl == 1
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
    out
  end

  # Where an item goes on a row: a along the row from its start (ox, oy),
  # len along it, t across it, against the screen's edge.
  def xl_place(rl, ox, oy, a, len, t)
    if rl == 2
      [ox - a - len, oy, len, t]
    elsif rl == 3
      [ox, oy + a, t, len]
    elsif rl == 0
      [ox + a, oy + 32 - t, len, t]
    else
      [ox - t, oy - a - len, t, len]
    end
  end

  XL_ROW_START = [[380, 600], [1268, 596], [900, 10], [12, 52]]

  # A spectator's hand of seat s: [codes, drawn] ("sh", "sd"), or nil (a
  # host without them, or a seated player's screen).
  def spec_hand(s)
    return nil if @seat
    sh = @view["sh"]
    return nil unless sh.is_a?(Array) && sh[s]
    sd = @view["sd"]
    d = sd.is_a?(Array) ? sd[s].to_i : -1
    [bytes_of(sh[s]), d]
  end

  # A row of seat s at rl: the hand (backs, or the faces for a spectator;
  # the drawn tile apart), then its melds in small tiles, the first call at
  # the far end.
  def xl_opp_items(s, rl)
    out = []
    o = XL_ROW_START[rl]
    base = xl_rot(rl)
    turned = (base + 3) % 4
    a = 0
    sp = spec_hand(s)
    if sp
      tl = sp[0]
      tl += [sp[1]] if sp[1] >= 0
      i = 0
      while i < tl.size
        a += 8 if sp[1] >= 0 && i == tl.size - 1
        p = xl_place(rl, o[0], o[1], a, 24, 32)
        out << xl_tile_item(false, base, tl[i], p[0], p[1], 0)
        a += 24
        i += 1
      end
    else
      hc = @view["hc"].is_a?(Array) ? @view["hc"][s].to_i : 13
      i = 0
      while i < hc
        a += 8 if i == hc - 1 && hc % 3 == 2
        p = xl_place(rl, o[0], o[1], a, 32, 20)
        out << xl_side_item(rl, p[0], p[1])
        a += 32
        i += 1
      end
    end
    a += 14
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
      out << xl_hand_item(c, x, y, mark, i == @sel)
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
    if @seat
      parts["m"] = xl_own_meld_items(@seat)
      parts["h"] = xl_hand_items
    elsif spec_hand(0)
      # Watching: seat 0's row at the bottom, as the others'.
      parts["o0"] = xl_opp_items(0, 0)
    else
      # Watching a host without the hands: seat 0's hand face down.
      parts["m"] = xl_own_meld_items(0)
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
    # A win's result waits while its shout shows (MJ6).
    res = @shout ? nil : (@view["res"] || @view["fin"])
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
      @xl_shouts = nil
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
          clears << xl_extent(a) if a
          draws << b if b
        end
        i += 1
      end
    end
    # A part the new screen does not have (the own hand and melds when a
    # seated table turns into watching, the bottom row the other way) goes
    # as a whole.
    @xl_items.each do |key, old|
      next if parts[key]
      i = 0
      while i < old.size
        clears << xl_extent(old[i])
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
    # A shout that went (or changed) leaves what was under it to redraw.
    shout = @shout ? [@shout[0], @shout[1]] : nil
    shout_new = full || shout != @xl_sig[:shout]
    if shout_new && @xl_shouts
      @xl_shouts.each do |r|
        clears << r
        redraw_board = true if overlap?(r, [@x0 + XL_BX, @y0 + XL_BY, XL_BW, XL_BH])
        redraw_status = true if overlap?(r, XL_STATUS)
      end
      @xl_shouts = nil
    end
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
              if overlap?(xl_extent(it), clears[j])
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
      @buttons = []
      @xl_btn_rects = []
      xl_draw_assist
      xl_draw_buttons
      @xl_sig[:btns] = btns
    elsif clock_parts != @xl_sig[:clock]
      xl_draw_clock
    end
    # The shout over everything, again whenever something was drawn.
    if @shout && (shout_new || !draws.empty? || redraw_board || redraw_btns || redraw_status)
      @xl_shouts = draw_shout
    end
    @xl_sig[:shout] = shout
  end

  def xl_board_sig
    v = @view
    [v["r"], v["sc"], v["nm"], v["ri"], v["aw"], v["dora"], v["st"], @seat, v["sk"]]
  end

  def xl_status_sig
    [status_line.to_s, @pick]
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
    [@view["a"], @seat.nil?, @assist, xl_btn_lefts, clock_parts ? true : false]
  end

  # The centre board: the round, the honba and the sticks, what is left of
  # the wall, the dora indicators, and each seat's wind, name and points on
  # the side that faces it. Rows (from the top): the seat across, the round
  # with the side seats' names and points beside it, the dora, the honba
  # line, the own seat.
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
    text(bx + (XL_BW - tw(head)) / 2, by + 24, head)
    fnt(16, 1)
    line = "#{r[2]}#{t(:mj_honba)}  #{t(:mj_sticks)}#{r[3]}"
    line = "#{line}  #{t(:mj_left)}#{r[5]}" if r.size > 5
    text(bx + (XL_BW - tw(line)) / 2, by + 110, line, C_DIM)
    d = bytes_of(v["dora"])
    dl = tw(t(:mj_dora)) + 6 + d.size * 24
    dx = bx + (XL_BW - dl) / 2
    text(dx, by + 80, t(:mj_dora), C_DIM)
    dx += tw(t(:mj_dora)) + 6
    i = 0
    while i < d.size
      xl_draw_item(xl_tile_item(false, 0, d[i], dx + i * 24, by + 72, 0))
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
      turn = s == cur && v["st"] == "play"
      if rl == 0 || rl == 2
        label = "#{head}#{fit(name, 100)}  #{pts}"
        lw = tw(label) + (stick ? 54 : 0)
        lx = bx + (XL_BW - lw) / 2
        ly = rl == 0 ? by + XL_BH - 24 : by + 4
        text(lx, ly, label, col)
        @gfx.fill_rect(lx, ly + 17, tw(label), 2, C_TURN) if turn
        xl_stick(lx + tw(label) + 6, ly + 5) if stick
      else
        label = "#{head}#{fit(name, 84 - tw(head))}"
        lw = tw(label) > tw(pts) ? tw(label) : tw(pts)
        lx = rl == 3 ? bx + 6 : bx + XL_BW - 6 - lw
        text(lx, by + 24, label, col)
        text(lx, by + 42, pts, col)
        @gfx.fill_rect(lx, by + 59, tw(pts), 2, C_TURN) if turn
        xl_stick(lx, by + 63) if stick
      end
      s += 1
    end
  end

  # A riichi stick (48x6, white with a red dot).
  def xl_stick(x, y)
    @gfx.fill_rect(x, y, 48, 6, C_TEXT)
    @gfx.fill_rect(x + 21, y + 1, 6, 4, C_RED)
  end

  # The status line: what the screen waits for (the time left is the
  # clock's, MJ6).
  def xl_draw_status
    x = @x0 + XL_STATUS[0]
    y = @y0 + XL_STATUS[1]
    fnt(16, 1)
    msg = status_line.to_s
    msg = "#{t(:mj_pick)} #{msg}" if @pick
    text(x + 4, y + 4, fit(msg, XL_STATUS[2] - 8), C_TURN) unless msg.strip.empty?
    @xl_n_px += XL_STATUS[2] * XL_STATUS[3]
  end

  # How far left each row of buttons may reach (XL_BTN_ROWS): clear of the
  # rows of the bottom river beside it, and the top row of the right river.
  def xl_btn_lefts
    e = @xl_r0_ends || [0, 0, 0, 0]
    r1 = @xl_r1_rows || 0
    out = [e[2], e[1] > e[2] ? e[1] : e[2], e[1], e[0]]
    out[3] = XL_BX + XL_BW + XL_SIDE + r1 * 64 if r1 > 0 && XL_BX + XL_BW + XL_SIDE + r1 * 64 > out[3]
    i = 0
    while i < out.size
      out[i] = out[i] + 8 < XL_BX ? XL_BX : out[i] + 8
      i += 1
    end
    out
  end

  # The help level, at the top left (nothing of the table reaches it).
  def xl_draw_assist
    return unless @seat
    fnt(12, 2)
    l = assist_label
    xl_button(B_ASSIST, [], 12, 8, tw(l) + 24, l)
  end

  # The buttons, right-aligned above the right end of the hand, the last at
  # the bottom right; a row that is full goes on in the row above (each row
  # clear of the bottom river beside it), and when four rows at the large
  # font are not enough, the font goes down.
  def xl_draw_buttons
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
    # The clock (MJ6) is the last, at the bottom right, by the drawn tile.
    labels << [B_CLOCK, [], nil] if clock_parts
    @xl_clock = nil
    lefts = xl_btn_lefts
    place = nil
    [[12, 2, 24], [16, 1, 16], [12, 1, 12]].each do |px, size, pad|
      next if place
      fnt(px, size)
      place = xl_pack_buttons(labels, lefts, pad)
    end
    place ||= xl_pack_buttons(labels, [XL_BX] * 4, 12)
    i = 0
    while i < place.size
      p = place[i]
      if labels[i][0] == B_CLOCK
        @xl_clock = [p[0], p[1], p[2], 40]
        @xl_btn_rects << @xl_clock
      else
        xl_button(labels[i][0], labels[i][1], p[0], p[1], p[2], labels[i][2])
      end
      i += 1
    end
    xl_draw_clock
  end

  XL_CLOCK_W = 120
  B_CLOCK = -1 # the clock's place among the buttons (not a button)

  # The clock in its place among the buttons (@xl_clock).
  def xl_draw_clock
    c = clock_parts
    r = @xl_clock
    @xl_sig[:clock] = c
    return unless c && r
    @gfx.fill_rect(r[0], r[1], r[2], r[3], C_FELT)
    clock_box(c, r[0], r[1], r[2], r[3], 16, 2)
    @xl_n_px += r[2] * r[3]
  end

  # [x, y, w] of each label in its row (nil: they do not fit).
  def xl_pack_buttons(labels, lefts, pad)
    out = []
    row = 0
    x = XL_BTN_RIGHT
    i = labels.size - 1
    while i >= 0
      w = labels[i][2] ? tw(labels[i][2]) + pad : XL_CLOCK_W
      while x - w < lefts[row]
        row += 1
        x = XL_BTN_RIGHT
        return nil if row >= XL_BTN_ROWS.size
      end
      x -= w
      out.unshift([x, XL_BTN_ROWS[row], w])
      x -= 8
      i -= 1
    end
    out
  end

  # Win buttons red, passing grey, the rest the usual colour (as in the sample).
  def xl_button(type, tiles, x, y, w, label)
    @buttons << [type, x, y, w, 40, tiles]
    @xl_btn_rects << [x, y, w, 40]
    col = type == A_RON || type == A_TSUMO ? C_BTN_WIN : (type == A_SKIP || type == B_LEAVE || type == B_ASSIST ? C_BTN_PASS : C_BTN)
    @gfx.fill_round_rect(x, y, w, 40, 8, col)
    text(x + (w - tw(label)) / 2, y + (40 - @fw) / 2, label)
    @xl_n_px += w * 40
  end

  # A round's result (or the end of the game) in a box over the middle. One
  # block per winner; with three winners the tiles are the small ones. When
  # the blocks would not fit the screen, the yaku go in four columns of the
  # smaller font (xl_compact).
  def xl_draw_result
    res = @view["res"]
    fin = @view["fin"]
    wins = res.is_a?(Hash) && res["wn"].is_a?(Array) ? res["wn"] : []
    big = wins.size <= 2
    over = fin.is_a?(Array) && @view["st"] == "over"
    fnt(12, 2)
    @xl_compact = false
    hgt = xl_result_height(res, wins, fin, big, over)
    if hgt > @h - 8
      @xl_compact = true
      hgt = xl_result_height(res, wins, fin, big, over)
    end
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
        lh = @xl_compact ? 26 : 30
        i = 0
        while i < 4
          l = score_line(i, res)
          cy = y + i * lh
          text(x, cy, l[0])
          text(x + 400, cy, l[1], C_DIM)
          text(x + 640, cy, l[2], delta_color(i, res))
          i += 1
        end
        y += lh * 4 + 10
      end
    end
    if over
      text(x, y, over_head, C_TURN)
      y += 34
      i = 0
      while i < fin.size
        f = fin[i]
        text(x, y, fit(fin_line(f), bw - 32))
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

  def xl_result_height(res, wins, fin, big, over)
    hgt = 24
    wins.each { |w| hgt += xl_win_height(w, big) }
    hgt += 30 if res.is_a?(Hash) # the round
    hgt += 34 if res.is_a?(Hash) && wins.empty?
    hgt += (@xl_compact ? 26 : 30) * 4 + 10 if res.is_a?(Hash) && !over
    hgt += 40 + fin.size * 30 + 34 if over
    hgt + 56
  end

  # The yaku: [columns, line height] (compact: four columns, 16 px font).
  def xl_yaku_grid
    @xl_compact ? [4, 20] : [3, 28]
  end

  def xl_win_height(w, big)
    ys = w["y"].is_a?(Array) ? w["y"].size : 0
    cols, lh = xl_yaku_grid
    c = @xl_compact
    (c ? 28 : 34) + (big ? 64 : 32) + (c ? 6 : 10) + ((ys + cols - 1) / cols) * lh + xl_pt_lines(w) * (c ? 26 : 30) + (c ? 2 : 6)
  end

  # The points line, and pao on a line of its own when both do not fit.
  def xl_pt_lines(w)
    pa = pao_text(w)
    return 1 if pa.empty?
    tw(w["pt"].to_s) + 8 + tw(pa) <= 768 ? 1 : 2
  end

  def xl_draw_win(w, k, from, x, y, big)
    text(x, y, fit(win_head(w, k, from), 768), C_TURN)
    y += @xl_compact ? 28 : 34
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
    y += (big ? 64 : 32) + (@xl_compact ? 6 : 10)
    ys = w["y"].is_a?(Array) ? w["y"] : []
    cols, lh = xl_yaku_grid
    cw = 768 / cols
    fnt(16, 1) if @xl_compact
    i = 0
    while i < ys.size
      text_spaced(x + (i % cols) * cw, y + (i / cols) * lh, fit(yaku_label(w, i), cw - 8))
      i += 1
    end
    fnt(12, 2)
    y += ((ys.size + cols - 1) / cols) * lh
    pt = w["pt"].to_s
    text_spaced(x, y, fit(pt, 768), C_TURN)
    pa = pao_text(w)
    unless pa.empty?
      if xl_pt_lines(w) == 1
        text_spaced(x + tw(pt) + 8, y, pa, C_AWAY)
      else
        y += @xl_compact ? 26 : 30
        text_spaced(x, y, fit(pa.strip, 768), C_AWAY)
      end
    end
    y + (@xl_compact ? 28 : 36)
  end
end
