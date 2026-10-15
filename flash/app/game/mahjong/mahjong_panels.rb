# Mahjong table: the local rules of the lobby, the spectators' score line
# and the result of a round (MJ3b, doc/ruby_asterism/report/mj3.md). Part
# of MahjongApp (mahjong.app.rb requires this file first, as mahjong_xl.rb;
# an app file is read whole and may be at most 64 KB).

class MahjongApp < FmrbApp
  # ------------------------------------------------------------ local rules
  #
  # The view's "ru" (asterism-games README "Local rules"): one value per
  # item, in this order. The owner changes them in the lobby (the host takes
  # rule(token, item, value) before the game only); everybody sees them.
  B_RULE = 108
  RULE_TURNS = [10, 20, 30, 60]
  RULE_AKA = [0, 3, 4]

  def setup_rule_strings
    @rule_names = ["対局", "喰いタン", "赤ドラ", "ダブロン", "三家和", "役満の複合", "数え役満",
                   "切り上げ満貫", "トビ", "西入", "持ち時間"]
    @rule_sel = 0
  end

  def rules
    ru = @view ? @view["ru"] : nil
    ru.is_a?(Array) ? ru : nil
  end

  # The value of item i as the screen says it.
  def rule_text(i, v)
    return v == 1 ? "東風" : "半荘" if i == 0
    return v == 0 ? "なし" : "#{v}枚" if i == 2
    return v == 1 ? "流局" : "全員和了" if i == 4
    return "#{v}秒" if i == 10
    v == 1 ? "あり" : "なし"
  end

  def rule_choices(i)
    return RULE_AKA if i == 2
    return RULE_TURNS if i == 10
    [0, 1]
  end

  # The owner steps item i to the next (step 1) or the last (-1) choice.
  def rule_step(i, step)
    ru = rules
    return unless ru && owner? && i < ru.size
    list = rule_choices(i)
    k = list.index(ru[i])
    k = k.nil? ? 0 : (k + step) % list.size
    return if @cmd_f || @token.nil?
    @cmd_f = proxy.async.rule(@token, i, list[k])
  rescue ::Asterism::Error => e
    @cmd_f = nil
    @note = e.message.to_s
    @dirty = true
  end

  # The lobby's keys for the rules: up / down choose, left / right change
  # (the owner). True when the key was one of them.
  def rule_key(k)
    ru = rules
    return false unless ru && @big
    sc = k[0]
    if sc == FmrbConst::KEY_UP || sc == FmrbConst::KEY_DOWN
      @rule_sel = (@rule_sel + (sc == FmrbConst::KEY_UP ? -1 : 1)) % ru.size
      @dirty = true
    elsif sc == FmrbConst::KEY_LEFT || sc == FmrbConst::KEY_RIGHT
      rule_step(@rule_sel, sc == FmrbConst::KEY_LEFT ? -1 : 1)
    else
      return false
    end
    true
  end

  # The rules beside the seats (640x360, and twice that at 1280x720): one
  # row an item; the owner's rows are buttons (a click steps the value).
  def draw_rules
    ru = rules
    return unless ru
    unless @big
      # The small screen: one line under the title.
      s = "#{rule_text(0, ru[0])} 喰タン#{rule_text(1, ru[1])} 赤#{ru[2]} #{rule_text(10, ru[10])}"
      text(@x0 + u(8), @y0 + u(22), s, C_DIM) if net_line.nil?
      return
    end
    x = @x0 + u(452)
    w = @w - u(460)
    y = @y0 + u(36)
    own = owner?
    text(x, y, own ? "ルール (上下 左右キー)" : "ルール (卓の主が決める)", C_DIM)
    y += u(18)
    i = 0
    while i < ru.size
      sel = own && i == @rule_sel
      @gfx.fill_rect(x - u(2), y - u(2), w, u(17), sel ? C_SEL : C_BOX) if own
      dim = i == 9 && ru[0] == 1 # the west round: a hanchan's only
      text(x + u(2), y, @rule_names[i], dim ? C_DIM : C_TEXT)
      v = rule_text(i, ru[i])
      text_spaced(x + w - tw(v) - u(8), y, v, dim ? C_DIM : (sel ? C_TURN : C_TEXT))
      @buttons << [B_RULE, x - u(2), y - u(2), w, u(17), [i]] if own
      y += u(18)
      i += 1
    end
  end

  # ------------------------------------------------------------ spectators

  # "手番: 南 name" (nil: nobody's turn now).
  def turn_text
    r = @view ? @view["r"] : nil
    return nil unless r.is_a?(Array) && r.size > 7 && @view["st"] == "play"
    s = r[6].to_i
    "手番: #{wind_of(s)} #{seat_name(s)}"
  end

  # The 640x360 spectator: the four seats in the hand's place, the seat to
  # act framed: wind, name, points and riichi.
  def draw_score_strip
    v = @view
    nm = v["nm"].is_a?(Array) ? v["nm"] : []
    sc = v["sc"].is_a?(Array) ? v["sc"] : []
    ri = v["ri"].to_i
    r = v["r"]
    cur = r.is_a?(Array) && r.size > 6 && v["st"] == "play" ? r[6].to_i : -1
    cw = (@w - 8) / 4
    # One line under the buttons (the bottom seat's melds keep the last 36 px).
    y = hand_y + 2
    s = 0
    while s < 4
      x = @x0 + 4 + s * cw
      turn = s == cur
      @gfx.fill_rect(x, y, cw - 4, 20, turn ? C_SEL : C_BOX)
      @gfx.draw_rect(x, y, cw - 4, 20, C_TURN) if turn
      col = away?(s) ? C_AWAY : (turn ? C_TURN : C_TEXT)
      tail = " #{sc[s]}"
      tail = "#{tail} #{t(:mj_riichi)}" if (ri >> s) & 1 == 1
      head = "#{wind_of(s)} "
      text_spaced(x + 4, y + 4, "#{head}#{fit(nm[s].to_s, cw - 12 - tw(head) - tw(tail))}#{tail}", col)
      s += 1
    end
  end

  # ------------------------------------------------------------ the result

  # "東1局 0本場" of the round a result is about.
  def round_head(res)
    rd = res["rd"]
    return "" unless rd.is_a?(Array) && rd.size > 2
    "#{WIND[rd[0].to_i % 4]}#{rd[1]}#{t(:mj_kyoku)} #{rd[2]}#{t(:mj_honba)}"
  end

  # One yaku with its han: "断么九 1翻", "ドラ 2翻", "大三元 役満" (the
  # engine's dora names carry their count, "ドラ 2": the han says it).
  def yaku_label(w, i)
    ys = w["y"]
    yh = w["yh"]
    s = ys[i].to_s
    return s unless yh.is_a?(Array) && i < yh.size
    h = yh[i].to_i
    parts = s.split(" ")
    if parts.size > 1
      last = parts[parts.size - 1]
      digits = last.bytesize > 0
      k = 0
      while k < last.bytesize
        b = last.getbyte(k)
        digits = false if b < 48 || b > 57
        k += 1
      end
      s = parts[0, parts.size - 1].join(" ") if digits
    end
    h < 0 ? "#{s} 役満" : "#{s} #{h}翻"
  end

  # The melds of seat s as tile lists (a closed kan with its ends face down).
  def meld_lists(s)
    out = []
    ms = xl_melds(s)
    i = 0
    while i < ms.size
      tl = ms[i][2].dup
      if ms[i][0] == 3
        tl[0] = 37
        tl[tl.size - 1] = 37
      end
      out << tl
      i += 1
    end
    out
  end

  # The points of seat i: [label, before -> after, change].
  def score_line(i, res)
    d = res["d"].is_a?(Array) ? res["d"] : []
    sc = @view["sc"].is_a?(Array) ? @view["sc"] : []
    dv = d[i].to_i
    after = sc[i].to_i
    tp = res["tp"]
    mark = ""
    if tp.is_a?(Array) && res["k"] == "ryuukyoku"
      mark = tp.index(i) ? " #{t(:mj_tenpai)}" : " ノーテン"
    end
    ["#{wind_of(i)} #{fit(seat_name(i), @xl ? 200 : 100)}#{mark}", "#{after - dv} → #{after}",
     dv == 0 ? "±0" : (dv > 0 ? "+#{dv}" : dv.to_s)]
  end

  def delta_color(i, res)
    d = res["d"].is_a?(Array) ? res["d"] : []
    dv = d[i].to_i
    dv > 0 ? C_KEEP : (dv < 0 ? C_AWAY : C_DIM)
  end

  # The result of a round (or the game) over the middle of the 640x360
  # table: the round, each winner (the hand, the yaku with their han, the
  # fu, han and points), or the draw; then every seat's points before and
  # after, with who was tenpai. A double ron has one block per winner, in
  # turn order from the discarder.
  def draw_result
    res = @view["res"]
    fin = @view["fin"]
    wins = res.is_a?(Hash) && res["wn"].is_a?(Array) ? res["wn"] : []
    multi = wins.size > 1
    over = fin.is_a?(Array) && @view["st"] == "over"
    if @big
      bx = @x0 + 60
      bw = 520
      by = @y0 + 6
      bh = multi ? 348 : 296
    else
      bx = @x0 + 20
      bw = @w - 40
      by = @y0 + 22
      bh = @h - 64
    end
    @gfx.fill_rect(bx, by, bw, bh, C_BOX)
    @gfx.draw_rect(bx, by, bw, bh, C_TURN)
    x = bx + 8
    y = by + 6
    if res.is_a?(Hash)
      k = res["k"].to_s
      text(x, y, round_head(res), C_DIM)
      y += 16
      if k == "tsumo_agari" || k == "ron_agari"
        i = 0
        while i < wins.size
          y = draw_win(wins[i], k, res["f"], x, y, multi, bw - 16)
          i += 1
        end
      else
        head = if k == "kyuushu_kyuuhai" then t(:mj_kyuushu_end)
               elsif k == "sanchahou" then t(:mj_sanchahou)
               else t(:mj_draw)
               end
        text(x, y, head, C_TURN)
        y += 18
      end
      unless over
        i = 0
        while i < 4
          l = score_line(i, res)
          cy = y + i * 15
          text(x, cy, l[0])
          text(x + bw * 48 / 100, cy, l[1], C_DIM)
          text(x + bw * 80 / 100, cy, l[2], delta_color(i, res))
          i += 1
        end
      end
    end
    if over
      y = by + bh - 84
      text(x, y, t(:mj_over), C_TURN)
      i = 0
      while i < fin.size
        text_spaced(x, y + 16 + i * 13, fin_line(fin[i]))
        i += 1
      end
      text(x, y + 70, t(:mj_back_lobby), C_DIM)
    end
  end

  # One winner's block; returns the y below it. With several winners the
  # yaku go in four columns.
  def draw_win(w, k, from, x, y, multi, bw)
    text(x, y, win_head(w, k, from), C_TURN)
    y += 16
    hc = bytes_of(w["h"])
    i = 0
    while i < hc.size
      if @big
        small_tile(hc[i], x + i * 23, y)
      else
        text(x + i * 13, y, tile_text(hc[i]))
      end
      i += 1
    end
    ag = w["ag"].to_i
    cx = x + hc.size * (@big ? 23 : 13) + 6
    if @big
      small_tile(ag, cx, y)
      @gfx.draw_rect(cx, y, 23, 31, C_LAST)
    else
      text(cx - 2, y, tile_text(ag), C_TURN)
    end
    cx += @big ? 23 : 13
    ms = meld_lists(w["w"].to_i)
    i = 0
    while i < ms.size
      cx += 8
      j = 0
      while j < ms[i].size
        if @big
          small_tile(ms[i][j], cx, y)
        else
          text(cx, y, tile_text(ms[i][j]), C_DIM)
        end
        cx += @big ? 23 : 13
        j += 1
      end
      i += 1
    end
    y += @big ? 36 : 15
    ys = w["y"].is_a?(Array) ? w["y"] : []
    cols = multi ? 4 : 3
    cw = bw / cols
    i = 0
    while i < ys.size
      text_spaced(x + (i % cols) * cw, y + (i / cols) * 14, yaku_label(w, i))
      i += 1
    end
    y += ((ys.size + cols - 1) / cols) * 14 + 2
    text_spaced(x, y, w["pt"].to_s, C_TURN)
    y + (multi ? 16 : 20)
  end

  # "<winner> ツモ" / "<winner> ロン (<who dealt in> から)".
  def win_head(w, k, from)
    return "#{seat_name(w['w'])} #{t(:mj_tsumo)}" if k == "tsumo_agari"
    "#{seat_name(w['w'])} #{t(:mj_ron)} (#{seat_name(from)} #{t(:mj_from)})"
  end

  # A line of the final ranking: "1位 <name> 31200 (+21.2)".
  def fin_line(f)
    pt = f[3].to_i
    sign = pt < 0 ? "-" : "+"
    pt = -pt if pt < 0
    "#{f[1]}#{t(:mj_rank)} #{seat_name(f[0])} #{f[2]} (#{sign}#{pt / 10}.#{pt % 10})"
  end
end
