# Mahjong table: the recent games of a table and their records (MJ6,
# doc/ruby_asterism/report/mj6.md). Part of MahjongApp, but read only when
# the player opens them (rp_open in mahjong.app.rb requires this file), so
# a game never has it in its VM.
#
# The game host keeps the games each table played to the end (asterism-games
# README "Game records") and gives them out: records(from) the list, newest
# first, in pages; record(id) a game's head (who sat where, the final ranks,
# a line per round); record(id, round, part) a round: the deal, the dora
# indicators, every draw, discard and call, the result (a long round in
# parts). Tiles are look's names ("5m", "0p" a red five, "1z" east).
#
# The list shows a line per game (when, the game, the four by rank with
# their points; this player's name in yellow). A game opens on its first
# round: the table as a spectator sees it, the four hands face up, built
# from the record up to the move chosen and drawn by the table's own
# spectator drawing (the 640x360 one of mahjong_watch.rb, the 1280x720 one
# of mahjong_xl.rb) on a view made for it. One round is held at a time.
#
# Keys: the list: up / down choose, Enter opens, Esc back. A record: right
# (or Enter, a click on the table) the next move, left the one before, down
# the next round's start, up this round's start (at the start: the round
# before), Esc back to the list. A declaration shows its shout as in a game.
#
# The base methods it changes (draw_screen, run_key, run_click, run_button,
# run_move, status_line) are kept under rp_base_* and called when no
# record is open.

class MahjongApp < FmrbApp
  B_RP_ROW = 120
  B_RP_BACK = 121
  RP_KINDS = ["draw", "discard", "riichi", "chi", "pon", "open_kan", "closed_kan", "added_kan", "dora", "tsumo",
              "ron", "nine_kinds"]
  # The tiles in a hand's order (the red five after the four).
  RP_ORDER = [0, 1, 2, 3, 4, 9, 5, 6, 7, 8, 10, 11, 12, 13, 14, 19, 15, 16, 17, 18,
              20, 21, 22, 23, 24, 29, 25, 26, 27, 28, 30, 31, 32, 33, 34, 35, 36]

  alias rp_base_draw_screen draw_screen
  alias rp_base_run_key run_key
  alias rp_base_run_click run_click
  alias rp_base_run_button run_button
  alias rp_base_run_move run_move
  alias rp_base_status_line status_line
  alias rp_base_on_update on_update

  # ------------------------------------------------------------ open, close, the calls

  def rp_start(p)
    rp_strings
    @rp = p
    @rp_state = :list
    @rp_list = []
    @rp_sel = 0
    @rp_top = 0
    @rp_note = t(:mj_rp_loading)
    @rp_head = nil
    @rp_data = nil
    @rp_parts = nil
    @rp_v = 0
    @rp_f = nil
    @shout = nil
    @dirty = true
    rp_call(:page, 0)
    Log.info("mahjong: records of #{p}")
  end

  def rp_close
    if @rp_state == :view
      @rp_state = :list
      @rp_data = nil
      @rp_head = nil
      @shout = nil
    else
      @rp = nil
      @rp_list = nil
    end
    @rp_f = nil
    @xl_items = nil
    @dirty = true
  end

  # Asks the host: :page (records(from)), :head (record(id)), :round
  # (record(id, round, part)).
  def rp_call(kind, a, b = 0, c = 0)
    return if @rp_f
    px = ::Asterism[@rp, timeout: CALL_TIMEOUT]
    @rp_kind = kind
    @rp_f = if kind == :page then px.async.records(a)
            elsif kind == :head then px.async.record(a)
            else px.async.record(a, b, c)
            end
  rescue ::Asterism::Error => e
    @rp_f = nil
    @rp_note = e.message.to_s
    @dirty = true
  end

  def rp_poll
    # The game starts while this player sits at the table: back to it.
    rp_close while @rp && @token && @seat && @view && @view["st"] != "wait"
    f = @rp_f
    return unless f && f.done?
    @rp_f = nil
    begin
      r = f.value
      if r.is_a?(Hash) && r["error"]
        @rp_note = r["error"].to_s
      elsif @rp_kind == :page
        rp_take_page(r)
      elsif @rp_kind == :head
        rp_take_head(r)
      else
        rp_take_part(r)
      end
    rescue ::Asterism::Error => e
      @rp_note = "#{t(:mj_host_away)} (#{e.class})"
    end
    @dirty = true
  end

  def rp_take_page(r)
    list = r.is_a?(Hash) && r["records"].is_a?(Array) ? r["records"] : []
    @rp_list += list
    if r["next"].is_a?(Integer) && @rp_list.size < 40
      rp_call(:page, r["next"])
    else
      @rp_note = @rp_list.empty? ? t(:mj_rp_none) : ""
    end
  end

  def rp_take_head(r)
    @rp_head = r
    @rp_state = :view
    @xl_items = nil
    rp_load(0, false)
  end

  # Loads round i (its first move, or its end when at_end).
  def rp_load(i, at_end)
    rs = @rp_head["rounds"]
    return unless rs.is_a?(Array) && i >= 0 && i < rs.size
    @rp_at_end = at_end
    @rp_parts = []
    @rp_note = t(:mj_rp_loading)
    rp_call(:round, @rp_head["id"], i, 0)
  end

  def rp_take_part(r)
    @rp_parts << r
    n = r["parts"].to_i
    return rp_call(:round, @rp_head["id"], r["round"], @rp_parts.size) if @rp_parts.size < n
    rp_take_round(@rp_parts)
    @rp_parts = nil
  end

  # The round's parts as the screen keeps it: tile codes, the moves as
  # [seat, kind (RP_KINDS), tile or tiles, from].
  def rp_take_round(parts)
    r = parts[0]
    moves = []
    parts.each do |pt|
      ms = pt["moves"].is_a?(Array) ? pt["moves"] : []
      i = 0
      while i < ms.size
        moves << rp_move(ms[i])
        i += 1
      end
    end
    hands = []
    hs = r["hands"].is_a?(Array) ? r["hands"] : []
    4.times { |s| hands << rp_codes(hs[s]) }
    @rp_data = { "n" => r["round"].to_i, "wind" => ["east", "south", "west", "north"].index(r["wind"].to_s) || 0,
                 "number" => r["number"].to_i, "honba" => r["honba"].to_i, "sticks" => r["riichi_sticks"].to_i,
                 "dealer" => r["dealer"].to_i, "scores" => r["scores"], "dora" => rp_codes(r["dora_indicators"]),
                 "hands" => hands, "moves" => moves, "result" => r["result"], "end" => r["end_scores"] }
    @rp_note = ""
    @rp_step = @rp_at_end ? rp_last : 0
    @shout = nil
  end

  def rp_move(m)
    k = RP_KINDS.index(m[1].to_s) || 0
    a = m[2]
    a = a.is_a?(Array) ? rp_codes(a) : rp_code(a)
    out = [m[0].to_i, k, a]
    out << rp_codes(m[3]) << m[4].to_i if k >= 3 && k <= 5
    out << m[3].to_i if k == 10
    out << true if (k == 1 || k == 2) && m[3] == true
    out
  end

  def rp_code(name)
    s = name.to_s
    return -1 if s.bytesize < 2
    n = s.getbyte(0) - 48
    suit = "mpsz".index(s[1])
    return -1 unless suit
    return 29 + n if suit == 3
    suit * 10 + (n == 0 ? 9 : n - 1)
  end

  def rp_codes(list)
    out = []
    return out unless list.is_a?(Array)
    i = 0
    while i < list.size
      out << rp_code(list[i])
      i += 1
    end
    out
  end

  # ------------------------------------------------------------ steps

  # The last step of the round: every move, then its result, then (the
  # last round) the end of the game.
  def rp_last
    d = @rp_data
    n = d["moves"].size + (d["result"] ? 1 : 0)
    n += 1 if d["n"] == @rp_head["rounds"].size - 1 && @rp_head["final"]
    n
  end

  def rp_go(step)
    d = @rp_data
    return unless d
    if step < 0
      rp_load(d["n"] - 1, true) if d["n"] > 0
      return
    end
    if step > rp_last
      rp_load(d["n"] + 1, false) if d["n"] + 1 < @rp_head["rounds"].size
      return
    end
    @rp_step = step
    @shout = nil
    m = step > 0 && step <= d["moves"].size ? d["moves"][step - 1] : nil
    if m && ((m[1] >= 2 && m[1] <= 7) || m[1] >= 9)
      ty = [0, 0, A_RIICHI, A_CHI, A_PON, A_MINKAN, A_ANKAN, A_KAKAN, 0, A_TSUMO, A_RON, A_KYUUSHU][m[1]]
      @shout = [ty, [m[0]], Machine.board_millis + SHOUT_MS]
    end
    @dirty = true
  end

  def rp_key(k)
    sc = k[0]
    ch = k[1]
    if sc == FmrbConst::KEY_ESC || ch == 27
      rp_close
    elsif @rp_state == :list
      n = @rp_list.size
      return if n == 0
      if sc == FmrbConst::KEY_UP
        @rp_sel = (@rp_sel - 1) % n
      elsif sc == FmrbConst::KEY_DOWN
        @rp_sel = (@rp_sel + 1) % n
      elsif enter?(k)
        rp_call(:head, @rp_list[@rp_sel]["id"])
      end
      @dirty = true
    elsif @rp_data && @rp_f.nil?
      if sc == FmrbConst::KEY_RIGHT || enter?(k) || ch == 32
        rp_go(@rp_step + 1)
      elsif sc == FmrbConst::KEY_LEFT
        rp_go(@rp_step - 1)
      elsif sc == FmrbConst::KEY_DOWN
        rp_load(@rp_data["n"] + 1, false) if @rp_data["n"] + 1 < @rp_head["rounds"].size
      elsif sc == FmrbConst::KEY_UP
        @rp_step > 0 ? rp_go(0) : rp_load(@rp_data["n"] - 1, false)
      end
    end
  end

  # ------------------------------------------------------------ the view made from the record

  # The table at the step as a spectator's view (the host's short keys).
  def rp_build
    d = @rp_data
    counts = []
    s = 0
    while s < 4
      counts << rp_count(d["hands"][s])
      s += 1
    end
    rivers = [[], [], [], []]
    melds = [[], [], [], []]
    drawn = [-1, -1, -1, -1]
    dora = d["dora"].dup
    ri = 0
    sticks = d["sticks"]
    cur = d["dealer"]
    wall = 70
    ls = nil
    moves = d["moves"]
    n = @rp_step < moves.size ? @rp_step : moves.size
    k = 0
    while k < n
      m = moves[k]
      s = m[0]
      kind = m[1]
      if kind == 0
        rp_take_drawn(counts, drawn, s)
        counts[s][m[2]] += 1
        drawn[s] = m[2]
        cur = s
        wall -= 1
        ls = nil
      elsif kind == 1 || kind == 2
        rp_take_drawn(counts, drawn, s)
        counts[s][m[2]] -= 1
        rivers[s] << (m[2] | (kind == 2 ? 0x40 : 0))
        if kind == 2
          ri |= 1 << s
          sticks += 1
        end
        ls = [s, m[2]]
        cur = s
      elsif kind >= 3 && kind <= 5
        rp_take_drawn(counts, drawn, s)
        rp_uncount(counts[s], m[3])
        fr = m[4]
        rv = rivers[fr]
        rv[rv.size - 1] = rv[rv.size - 1] | 0x80 unless rv.empty?
        rel_from = (fr - s) % 4
        tl = m[3]
        # The called tile where it lies sideways (from the left first,
        # from across second, from the right last).
        at = rel_from == 3 ? 0 : (rel_from == 2 ? 1 : tl.size)
        tl = tl[0, at] + [m[2]] + tl[at, tl.size - at]
        melds[s] << [kind == 3 ? 1 : (kind == 4 ? 2 : 4), rel_from, tl]
        cur = s
        ls = nil
      elsif kind == 6
        rp_take_drawn(counts, drawn, s)
        rp_uncount(counts[s], m[2])
        melds[s] << [3, 0, m[2]]
        ls = nil
      elsif kind == 7
        rp_take_drawn(counts, drawn, s)
        counts[s][m[2]] -= 1
        b = m[2] == 9 || m[2] == 19 || m[2] == 29 ? m[2] - 5 : m[2]
        j = 0
        while j < melds[s].size
          me = melds[s][j]
          f = me[2][0]
          f = f - 5 if f == 9 || f == 19 || f == 29
          if me[0] == 2 && f == b
            me[0] = 5
            me[2] = me[2] + [m[2]]
          end
          j += 1
        end
        ls = nil
      elsif kind == 8
        dora << m[2]
      end
      k += 1
    end
    end_step = @rp_step > moves.size
    v = { "v" => (@rp_v += 1), "sv" => @rp_v, "t" => "", "st" => "play", "me" => -1, "a" => [], "aw" => 0,
          "nm" => @rp_head["names"], "sk" => [1, 1, 1, 1], "hu" => 15, "ow" => -1, "mb" => [],
          "sc" => d["scores"], "ri" => ri, "dora" => rp_bytes(dora),
          "r" => [d["wind"], d["number"], d["honba"], sticks, d["dealer"], wall, cur, 1],
          "kw" => [], "fu" => [], "hc" => [], "sh" => [], "sd" => drawn, "ls" => ls }
    s = 0
    while s < 4
      v["kw"] << rp_bytes(rivers[s])
      fu = []
      j = 0
      while j < melds[s].size
        me = melds[s][j]
        fu << me[0] << me[1] << me[2].size
        fu += me[2]
        j += 1
      end
      v["fu"] << rp_bytes(fu)
      left = counts[s].dup
      left[drawn[s]] -= 1 if drawn[s] >= 0
      hand = rp_sorted(left)
      v["sh"] << rp_bytes(hand)
      v["hc"] << hand.size + (drawn[s] >= 0 ? 1 : 0)
      s += 1
    end
    if end_step
      v["st"] = "result"
      v["sc"] = d["end"] if d["end"].is_a?(Array)
      v["res"] = rp_result(d) if d["result"]
      if @rp_step > moves.size + (d["result"] ? 1 : 0)
        v["st"] = "over"
        v["fin"] = @rp_head["final"]
        v["fr"] = @rp_head["reason"]
        # The end alone (the last round's result was the step before; three
        # winners and the ranks would not fit one box).
        v.delete("res")
      end
    end
    v
  end

  # Tile counts (38 kinds) of a list of codes, and back to codes in a
  # hand's order.
  def rp_count(list)
    c = Array.new(38, 0)
    i = 0
    while i < list.size
      c[list[i]] += 1 if list[i] >= 0
      i += 1
    end
    c
  end

  def rp_uncount(c, list)
    i = 0
    while i < list.size
      c[list[i]] -= 1 if list[i] >= 0
      i += 1
    end
  end

  def rp_sorted(c)
    out = []
    i = 0
    while i < RP_ORDER.size
      k = c[RP_ORDER[i]]
      while k > 0
        out << RP_ORDER[i]
        k -= 1
      end
      i += 1
    end
    out
  end

  # The drawn tile goes into the hand when anything else happens.
  def rp_take_drawn(counts, drawn, s)
    drawn[s] = -1
  end

  def rp_bytes(list)
    b = ("\x00" * list.size).b
    i = 0
    while i < list.size
      b.setbyte(i, list[i] & 0xFF)
      i += 1
    end
    b
  end

  # The record's result as the view's "res".
  def rp_result(d)
    r = d["result"]
    res = { "k" => r["kind"].to_s, "d" => r["changes"], "rd" => [d["wind"], d["number"], d["honba"]] }
    res["f"] = r["from"] unless r["from"].nil?
    res["tp"] = r["tenpai"] if r["tenpai"]
    res["ng"] = r["nagashi"] if r["nagashi"]
    res["rs"] = r["ron_seats"] if r["ron_seats"]
    if r["wins"].is_a?(Array)
      res["wn"] = []
      r["wins"].each do |w|
        sorted = rp_sorted(rp_count(rp_codes(w["hand"])))
        x = { "w" => w["seat"], "y" => w["yaku"], "pt" => w["points"], "h" => rp_bytes(sorted), "ag" => rp_code(w["tile"]) }
        x["yh"] = w["yaku_han"] if w["yaku_han"]
        x["pa"] = w["pao"] if w["pao"]
        res["wn"] << x
      end
    end
    res
  end

  # "12/80 南 name 打5m": the step and what the move was.
  def rp_line
    return @rp_note.to_s unless @rp_data
    return @rp_note.to_s unless @rp_note.to_s.empty?
    d = @rp_data
    moves = d["moves"]
    s = "#{@rp_step}/#{moves.size}"
    return "#{s} #{t(:mj_rp_deal)}" if @rp_step == 0
    return "#{s} #{t(:mj_over)}" if @rp_step > moves.size + (d["result"] ? 1 : 0)
    return "#{s} #{d['result'] ? t(:mj_rp_result) : t(:mj_draw)}" if @rp_step > moves.size
    m = moves[@rp_step - 1]
    return "#{s} #{t(:mj_dora)} #{tile_text(m[2])}" if m[1] == 8
    who = "#{wind_of(m[0])} #{fit(seat_name(m[0]), u(80))}"
    what = case m[1]
           when 0 then "ツモ #{tile_text(m[2])}"
           when 1 then "打 #{tile_text(m[2])}#{m[3] ? ' (ツモ切り)' : ''}"
           when 2 then "#{t(:mj_riichi)} 打 #{tile_text(m[2])}"
           when 3 then "#{t(:mj_chi)} #{tile_text(m[2])}"
           when 4 then "#{t(:mj_pon)} #{tile_text(m[2])}"
           when 5, 6, 7 then "#{t(:mj_kan)}"
           when 9 then "#{t(:mj_tsumo)} #{tile_text(m[2])}"
           when 10 then "#{t(:mj_ron)} #{tile_text(m[2])}"
           else t(:mj_kyuushu)
           end
    "#{s} #{who} #{what}"
  end

  # ------------------------------------------------------------ drawing

  def rp_draw
    t0 = Machine.board_millis
    if @rp_state == :view && @rp_data
      rp_draw_view
    else
      @xl_items = nil
      @buttons = []
      clear_user_area(C_FELT)
      fnt(12, @k)
      rp_draw_list
    end
    @gfx.present
    perf_note(:draw, Machine.board_millis - t0)
  end

  # The record at its step, drawn as the spectators' table on a view made
  # for it (the live view and seat are put back after).
  def rp_draw_view
    live = @view
    seat = @seat
    note = @note
    leave = @str[:mj_leave]
    back = @str[:mj_back_lobby]
    @view = rp_build
    @seat = nil
    @str[:mj_leave] = t(:mj_rp_back)
    @str[:mj_back_lobby] = ""
    begin
      if @xl
        xl_draw
      else
        @xl_items = nil
        @buttons = []
        clear_user_area(C_FELT)
        fnt(12, @k)
        if @big
          @note = rp_line
          draw_watch
        else
          draw_info
          draw_seats
          draw_rivers
          draw_melds
          text(@x0 + 2, @y0 + @h - 14, fit(rp_line, @w - 4), C_TURN)
        end
        draw_shout
        draw_result if (@view["res"] || @view["fin"]) && !@shout
      end
    ensure
      @view = live
      @seat = seat
      @note = note
      @str[:mj_leave] = leave
      @str[:mj_back_lobby] = back
    end
  end

  # The list of the recent games.
  def rp_draw_list
    x = @x0 + u(8)
    y = @y0 + u(8)
    parts = @rp.to_s.split("/")
    text(x, y, fit("#{t(:mj_rp_title)}  #{parts[0]}/#{parts[2]}", @w - u(120)))
    bl = t(:mj_rp_back)
    bw = tw(bl) + u(12)
    button(B_RP_BACK, [], @x0 + @w - bw - u(8), y - u(3), bl)
    text(x, y + u(18), fit(t(:mj_rp_keys), @w - u(16)), C_DIM)
    y += u(40)
    unless @rp_note.to_s.empty?
      text(x, y, fit(@rp_note.to_s, @w - u(16)), C_TURN)
      y += u(18)
    end
    rows = (@y0 + @h - y - u(4)) / u(18)
    rows = 1 if rows < 1
    n = @rp_list.size
    @rp_top = @rp_sel - rows + 1 if @rp_sel >= @rp_top + rows
    @rp_top = @rp_sel if @rp_sel < @rp_top
    seg = (@w - u(16) - u(@big ? 112 : 70)) / 4
    i = @rp_top
    while i < n && i < @rp_top + rows
      e = @rp_list[i]
      chosen = i == @rp_sel
      @gfx.fill_rect(x - u(2), y - u(2), @w - u(12), u(17), C_SEL) if chosen
      @buttons << [B_RP_ROW, x - u(2), y - u(2), @w - u(12), u(17), [i]]
      at = e["at"].to_s
      at = at.byteslice(5, at.bytesize - 5).to_s if at.bytesize > 5
      head = @big ? "#{at} #{e['game'] == 'tonpuu' ? '東風' : '半荘'}" : at
      text(x, y, head, chosen ? C_TURN : C_TEXT)
      cx = x + u(@big ? 112 : 70)
      nm = e["names"].is_a?(Array) ? e["names"] : []
      rk = e["ranks"].is_a?(Array) ? e["ranks"] : []
      sc = e["scores"].is_a?(Array) ? e["scores"] : []
      r = 1
      while r <= 4
        s = rk.index(r)
        if s
          pts = sc[s].to_s
          name = fit(nm[s].to_s, seg - tw("#{r} ") - tw(pts) - u(20))
          text(cx, y, "#{r} #{name}", nm[s].to_s == @name ? C_TURN : C_TEXT)
          text(cx + seg - tw(pts) - u(6), y, pts, C_DIM)
        end
        cx += seg
        r += 1
      end
      y += u(18)
      i += 1
    end
  end

  # ------------------------------------------------------------ the base methods, with a record open

  def draw_screen
    @rp ? rp_draw : rp_base_draw_screen
  end

  def run_key(k)
    @rp ? rp_key(k) : rp_base_run_key(k)
  end

  def run_move(m)
    rp_base_run_move(m) unless @rp
  end

  def run_click(c)
    return rp_base_run_click(c) unless @rp
    i = 0
    while i < @buttons.size
      b = @buttons[i]
      if c[0] >= b[1] && c[0] < b[1] + b[3] && c[1] >= b[2] && c[1] < b[2] + b[4]
        run_button(b[0], b[5])
        return
      end
      i += 1
    end
    rp_go(@rp_step + 1) if @rp_state == :view && @rp_data && @rp_f.nil?
  end

  def run_button(type, tiles)
    return rp_base_run_button(type, tiles) unless @rp
    if type == B_RP_ROW
      @rp_sel = tiles[0]
      rp_call(:head, @rp_list[@rp_sel]["id"]) if @rp_list[@rp_sel]
    elsif type == B_RP_BACK || type == B_LEAVE
      rp_close
    end
    @dirty = true
  end

  def status_line
    @rp ? "#{rp_line}  #{t(:mj_rp_view_keys)}" : rp_base_status_line
  end

  def on_update
    rp_poll if @rp
    rp_base_on_update
  end

  def rp_strings
    @str[:mj_rp_title] = "直近の結果"
    @str[:mj_rp_back] = "閉じる Esc"
    @str[:mj_rp_loading] = "読み込み中"
    @str[:mj_rp_none] = "まだ終局した対局が無い"
    @str[:mj_rp_deal] = "配牌"
    @str[:mj_rp_result] = "結果"
    @str[:mj_rp_keys] = "↑↓ 選ぶ  Enter 牌譜  牌譜: → 次 ← 前 ↓ 次の局 ↑ 局の頭"
    @str[:mj_rp_view_keys] = "(→ ← ↓ ↑ Esc)"
  end
end
