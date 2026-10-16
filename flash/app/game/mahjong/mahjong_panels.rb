# Mahjong table: the local rules of the lobby, the result of a round (MJ3b,
# doc/ruby_asterism/report/mj3.md; two pages of rules and the worst cases
# fitted in MJ5, mj5.md) and the parts of the 640x360 table. Part
# of MahjongApp (mahjong.app.rb requires this file first, as mahjong_xl.rb;
# an app file is read whole and may be at most 64 KB).

class MahjongApp < FmrbApp
  # ------------------------------------------------------------ local rules
  #
  # The view's "ru" and "rx" (asterism-games README "Local rules"): one
  # value per item, in this order (items 0-10 in "ru", 11-14 in "rx"; a
  # host without "rx" has the first eleven only). The owner changes them in
  # the lobby (the host takes rule(token, item, value) before the game
  # only); everybody sees them. Item 9 is the extra round ("延長"): the
  # west round of a hanchan and the south round of a tonpuu game (a host
  # before MJ-E2 has the west round only). Item 10 is the time (MJ-E3,
  # README "Time"): its value is a code, the seconds for a move plus 1000
  # times the bank's ("5+20" is 20005, a fixed 20 s is 20); "ru" has the
  # seconds for a move, "tb" the bank, "tc" the codes to choose from. A
  # host before MJ-E3 has none of the two: fixed times, RULE_TURNS. Items
  # 15 (the CPUs' level) and 16 (the spectators' delay, seconds; "dc" the
  # choices) are in "ry" (MJ5).
  #
  # The screen shows the items in an order of its own, in groups on two
  # pages (RULE_PAGES; a host's items that the screen does not know, or a
  # host without some of these, are left out).
  B_RULE = 108
  B_RULE_PAGE = 109
  RULE_TURNS = [10, 20, 30, 60]
  RULE_AKA = [0, 3, 4]
  RULE_DELAYS = [0, 60, 180, 300]
  RULE_PAGES = [
    [["対局の形", [0, 9]], ["持ち時間", [10]], ["ドラ・役", [2, 1, 5, 13, 6, 7]]],
    [["和了・流局", [3, 4, 11, 12]], ["終局", [8, 14]], ["CPU・観戦", [15, 16]]]
  ]

  def setup_rule_strings
    @rule_names = ["半荘・東風", "喰いタン", "赤ドラ", "ダブロン", "三家和", "役満の複合", "数え役満",
                   "切り上げ満貫", "トビ", "延長", "持ち時間", "途中流局", "流し満貫", "ダブル役満",
                   "テンパイ止め", "CPU の強さ", "観戦の遅れ"]
    @rule_sel = 0   # the item chosen (its number)
    @rule_page = 0
  end

  def rules
    ru = @view ? @view["ru"] : nil
    return nil unless ru.is_a?(Array)
    rx = @view["rx"]
    ry = @view["ry"]
    ru += rx if rx.is_a?(Array)
    ru += ry if rx.is_a?(Array) && ry.is_a?(Array)
    ru
  end

  # The pages as the host's items allow: [[heading, [items]], ...] each.
  def rule_pages
    n = rules ? rules.size : 0
    out = []
    RULE_PAGES.each do |page|
      groups = []
      page.each do |head, items|
        have = []
        items.each { |i| have << i if i < n }
        groups << [head, have] unless have.empty?
      end
      out << groups unless groups.empty?
    end
    out
  end

  # Every item shown, in the screen's order: [[item, page], ...].
  def rule_order
    out = []
    pages = rule_pages
    pi = 0
    while pi < pages.size
      pages[pi].each { |_h, items| items.each { |i| out << [i, pi] } }
      pi += 1
    end
    out
  end

  # The other page, its first item chosen.
  def rule_flip(page = nil)
    pages = rule_pages
    return if pages.size < 2
    @rule_page = page.nil? ? (@rule_page + 1) % pages.size : page % pages.size
    @rule_sel = pages[@rule_page][0][1][0]
    @dirty = true
  end

  # Item i's value in ru (item 10: its code, with the bank of "tb").
  def rule_value(ru, i)
    return ru[i] unless i == 10
    tb = @view["tb"]
    ru[i] + (tb.is_a?(Integer) ? tb : 0) * 1000
  end

  # The value of item i as the screen says it (item 10: the code).
  def rule_text(i, v)
    return v == 1 ? "東風" : "半荘" if i == 0
    return v == 0 ? "なし" : "#{v}枚" if i == 2
    return v == 1 ? "流局" : "全員和了" if i == 4
    return (v >= 1000 ? "#{v % 1000}+#{v / 1000}" : v.to_s) + "秒" if i == 10
    return ["弱い", "ふつう", "強い"][v] || v.to_s if i == 15
    return v == 0 ? "なし" : (v % 60 == 0 ? "#{v / 60}分" : "#{v}秒") if i == 16
    v == 1 ? "あり" : "なし"
  end

  def rule_choices(i)
    return RULE_AKA if i == 2
    return [0, 1, 2] if i == 15
    if i == 16
      dc = @view["dc"]
      return dc.is_a?(Array) && dc.size > 0 ? dc : RULE_DELAYS
    end
    if i == 10
      tc = @view["tc"]
      return tc.is_a?(Array) && tc.size > 0 ? tc : RULE_TURNS
    end
    [0, 1]
  end

  # The time left on the player's own move, in whole seconds rounded up:
  # "3+20秒" (what is left of the move's own time and the bank; "dl" has
  # the bank's ms as its third number, README "Time") or "12秒".
  def time_left_text
    ms = @deadline - Machine.board_millis
    ms = 0 if ms < 0
    move = ms - @bank_ms
    if @bank_ms > 0
      return "#{(move + 999) / 1000}+#{@bank_ms / 1000}#{t(:mj_rest)}" if move > 0
      return "0+#{(ms + 999) / 1000}#{t(:mj_rest)}"
    end
    "#{(ms + 999) / 1000}#{t(:mj_rest)}"
  end

  # The owner steps item i to the next (step 1) or the last (-1) choice.
  def rule_step(i, step)
    ru = rules
    return unless ru && owner? && i < ru.size
    list = rule_choices(i)
    k = list.index(rule_value(ru, i))
    k = k.nil? ? 0 : (k + step) % list.size
    return if @cmd_f || @token.nil?
    @cmd_f = proxy.async.rule(@token, i, list[k])
  rescue ::Asterism::Error => e
    @cmd_f = nil
    @note = e.message.to_s
    @dirty = true
  end

  # The lobby's keys for the rules: up / down choose (on to the other page
  # past the end of one), Tab turns the page, left / right change (the
  # owner). True when the key was one of them.
  def rule_key(k)
    ru = rules
    return false unless ru && @big
    sc = k[0]
    if sc == FmrbConst::KEY_UP || sc == FmrbConst::KEY_DOWN
      order = rule_order
      return true if order.empty?
      pos = 0
      j = 0
      while j < order.size
        pos = j if order[j][0] == @rule_sel
        j += 1
      end
      pos = (pos + (sc == FmrbConst::KEY_UP ? -1 : 1)) % order.size
      @rule_sel = order[pos][0]
      @rule_page = order[pos][1]
      @dirty = true
    elsif sc == FmrbConst::KEY_TAB || k[1] == 9
      rule_flip
    elsif sc == FmrbConst::KEY_LEFT || sc == FmrbConst::KEY_RIGHT
      rule_step(@rule_sel, sc == FmrbConst::KEY_LEFT ? -1 : 1)
    else
      return false
    end
    true
  end

  # The rules beside the seats (640x360, and twice that at 1280x720): the
  # page's groups, a heading and a row an item; the owner's rows are
  # buttons (a click steps the value). The page tabs under the title (a
  # click, or Tab).
  def draw_rules
    ru = rules
    return unless ru
    unless @big
      # The small screen: one line under the title.
      s = "#{rule_text(0, ru[0])} 喰タン#{rule_text(1, ru[1])} 赤#{ru[2]} #{rule_text(10, rule_value(ru, 10))}"
      text(@x0 + u(8), @y0 + u(22), s, C_DIM) if net_line.nil?
      return
    end
    x = @x0 + u(452)
    w = @w - u(460)
    y = @y0 + u(36)
    own = owner?
    text(x, y, own ? "ルール (上下 左右 Tab)" : "ルール (卓の主が決める)", C_DIM)
    y += u(17)
    pages = rule_pages
    @rule_page = 0 if @rule_page >= pages.size
    if pages.size > 1
      tw2 = w / pages.size
      pi = 0
      while pi < pages.size
        on = pi == @rule_page
        @gfx.fill_rect(x - u(2) + pi * tw2, y - u(2), tw2 - u(2), u(16), on ? C_SEL : C_BOX)
        @gfx.draw_rect(x - u(2) + pi * tw2, y - u(2), tw2 - u(2), u(16), C_TURN) if on
        l = "#{pi + 1} / #{pages.size}"
        text(x - u(2) + pi * tw2 + (tw2 - tw(l)) / 2, y, l, on ? C_TURN : C_DIM)
        @buttons << [B_RULE_PAGE, x - u(2) + pi * tw2, y - u(2), tw2 - u(2), u(16), [pi]]
        pi += 1
      end
      y += u(19)
    end
    (pages[@rule_page] || []).each do |head, items|
      text(x, y, head, C_DIM)
      y += u(16)
      items.each do |i|
        sel = own && i == @rule_sel
        @gfx.fill_rect(x - u(2), y - u(2), w, u(16), sel ? C_SEL : C_BOX) if own
        v = rule_text(i, rule_value(ru, i))
        text(x + u(6), y, fit(@rule_names[i], w - tw(v) - u(18)), C_TEXT)
        text_spaced(x + w - tw(v) - u(8), y, v, sel ? C_TURN : C_TEXT)
        @buttons << [B_RULE, x - u(2), y - u(2), w, u(16), [i]] if own
        y += u(17)
      end
    end
  end

  # ------------------------------------------------------------ the player's name (MJ5)
  #
  # Set in the list of tables or the lobby (n, or a click on its button),
  # kept on this machine in ASSIST_FILE (name = "..."; the browser build
  # keeps /home across a reload), and sent with join: the host keeps a
  # member's name and a join again from the same node changes it, at the
  # seat too. A name is needed before joining a table: the first Enter on a
  # table asks for it (starting from the machine's name, config.toml) and
  # joins once it is given. The node ID only tells the seats apart. Typed with the keyboard;
  # kana arrive as their UTF-8 bytes one key at a time when the machine's
  # kana input is on (Ctrl+Space), as in the editor. At most NAME_CHARS
  # characters and NAME_BYTES bytes (the host's limit); only characters the
  # font has: ASCII but the quote and the backslash, kana, kanji, full-width
  # forms.
  NAME_CHARS = 12
  NAME_BYTES = 32

  # s without what a name may not have, cut to the limits.
  def clean_player_name(s, strip = true)
    out = ""
    n = 0
    i = 0
    b = s.to_s
    while i < b.bytesize && n < NAME_CHARS
      c = b.getbyte(i)
      len = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2))
      ch = b.byteslice(i, len)
      out << ch if name_char_ok?(ch) && out.bytesize + len <= NAME_BYTES
      n += 1 if name_char_ok?(ch)
      i += len
    end
    strip ? out.strip : out
  end

  def name_char_ok?(ch)
    c = ch.getbyte(0)
    return c >= 0x20 && c <= 0x7E && c != 34 && c != 92 if ch.bytesize == 1
    return false unless ch.bytesize == 3
    cp = ((c & 0x0F) << 12) | ((ch.getbyte(1) & 0x3F) << 6) | (ch.getbyte(2) & 0x3F)
    (cp >= 0x3041 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xFF01 && cp <= 0xFF5E)
  end

  def name_begin
    @name_edit = (@name_set || @name_machine).to_s.dup
    @u8 = ""
    @u8_left = 0
    @dirty = true
  end

  # A key while the name is typed: Enter keeps it, Esc goes back, Backspace
  # takes the last character off, a printable byte (or the bytes of a kana)
  # goes on the end.
  def name_key(k)
    sc = k[0]
    ch = k[1]
    if enter?(k)
      name_done
    elsif sc == FmrbConst::KEY_ESC || ch == 27
      @name_edit = nil
      @pending_join = nil
    elsif sc == 0x2A || ch == 8 || ch == 127
      e = @name_edit
      i = e.bytesize - 1
      i -= 1 while i > 0 && (e.getbyte(i) & 0xC0) == 0x80
      @name_edit = i > 0 ? e.byteslice(0, i) : ""
    elsif ch >= 0x80
      # One byte of a kana; the character goes on once it is whole.
      if ch >= 0xC0
        @u8 = "x".dup
        @u8.setbyte(0, ch)
        @u8_left = ch >= 0xF0 ? 3 : (ch >= 0xE0 ? 2 : 1)
      elsif @u8_left > 0
        b = "x".dup
        b.setbyte(0, ch)
        @u8 << b
        @u8_left -= 1
        name_add(@u8) if @u8_left == 0
      end
    elsif ch >= 0x20
      b = "x".dup
      b.setbyte(0, ch)
      name_add(b)
    end
    @dirty = true
  end

  def name_add(chr)
    s = clean_player_name(@name_edit + chr, false)
    @name_edit = s if s.bytesize > @name_edit.bytesize
  end

  # The name typed is the name: kept, and told to the table (join again
  # from this node: the same membership, the new name).
  def name_done
    n = clean_player_name(@name_edit)
    @dirty = true
    return if n.empty? # a name is needed: the prompt stays
    @name_edit = nil
    changed = n != @name_set
    @name = n
    @name_set = n
    save_assist if changed
    if @pending_join
      p = @pending_join
      @pending_join = nil
      return join(p)
    end
    return unless changed && @table && @token
    @rename_f = proxy.async.join(@name)
  rescue ::Asterism::Error => e
    @note = e.message.to_s
  end

  # The name's button in the lobby (w: the room for it).
  def draw_name_button(x, y, w)
    l = if @name_edit
          "#{@pending_join ? '名前を入れてから入る' : '名前'}: #{@name_edit}_  (Enter で決める Esc でやめる)"
        elsif @name_set
          "名前: #{@name}  N"
        else
          "名前: (まだ無い: 卓に入る前に決める)  N"
        end
    fnt(12, @k)
    l = fit(l, w - u(12))
    bw = button(B_NAME, [], x, y, l)
    @gfx.draw_rect(x, y, bw, u(20), C_TURN) if @name_edit
  end

  # ------------------------------------------------------------ the result

  # The head of a round that ended without a win: the draw, an abortive
  # draw by name, and who made nagashi mangan.
  def draw_head(res)
    k = res["k"].to_s
    head = if k == "kyuushu_kyuuhai" then t(:mj_kyuushu_end)
           elsif k == "sanchahou" then t(:mj_sanchahou)
           elsif k == "suufon_renda" then "四風連打で流局"
           elsif k == "suukaikan" then "四槓散了で流局"
           elsif k == "suucha_riichi" then "四家立直で流局"
           else t(:mj_draw)
           end
    ng = res["ng"]
    return head unless ng.is_a?(Array) && ng.size > 0
    names = []
    ng.each { |s| names << seat_name(s.to_i) }
    "#{head}  流し満貫: #{names.join(' ')}"
  end

  # Pao after the points: "包: <name> (大三元)", "" when none.
  def pao_text(w)
    pa = w["pa"]
    return "" unless pa.is_a?(Array) && pa.size > 0
    out = []
    pa.each { |p| out << "#{seat_name(p[0].to_i)} (#{p[1]})" if p.is_a?(Array) }
    "  包: #{out.join(' ')}"
  end

  # "終局" and why the game ended, when the host says (the view's "fr").
  def over_head
    r = @view["fr"].to_s
    why = if r == "tobi" then "トビ"
          elsif r == "all_last_oya_agari" then "アガリ止め"
          elsif r == "all_last_oya_tenpai" then "テンパイ止め"
          elsif r == "sudden_death" then "サドンデス"
          elsif r == "west" then "西4局まで"
          elsif r == "rounds" then "局数の上限"
          elsif r == "normal_end" then "最後の局まで"
          else ""
          end
    why.empty? ? t(:mj_over) : "#{t(:mj_over)} (#{why})"
  end

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
    return "#{s} ダブル役満" if h <= -2
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
  # turn order from the discarder: then the box is wider and higher, the
  # points go on the winner's line, the yaku in five columns and the four
  # seats' points in two.
  def draw_result
    res = @view["res"]
    fin = @view["fin"]
    wins = res.is_a?(Hash) && res["wn"].is_a?(Array) ? res["wn"] : []
    multi = wins.size > 1
    over = fin.is_a?(Array) && @view["st"] == "over"
    if @big
      bx = @x0 + (multi ? 20 : 60)
      bw = multi ? 600 : 520
      by = @y0 + (multi ? 4 : 6)
      bh = multi ? 352 : 296
    else
      bx = @x0 + 20
      bw = @w - 40
      by = @y0 + 22
      bh = @h - 64
    end
    fnt(12, 1)
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
        text_spaced(x, y, fit(draw_head(res), bw - 16), C_TURN)
        y += 18
      end
      unless over
        cols = multi ? 2 : 1
        cw = (bw - 16) / cols
        i = 0
        while i < 4
          l = score_line(i, res)
          cx = x + (i % cols) * cw
          cy = y + (i / cols) * 15
          text(cx, cy, fit(l[0], cw * 40 / 100 - 4))
          text(cx + cw * 42 / 100, cy, l[1], C_DIM)
          text(cx + cw * 80 / 100, cy, l[2], delta_color(i, res))
          i += 1
        end
      end
    end
    if over
      y = by + bh - 84
      text(x, y, over_head, C_TURN)
      i = 0
      while i < fin.size
        text_spaced(x, y + 16 + i * 13, fit(fin_line(fin[i]), bw - 16))
        i += 1
      end
      text(x, y + 70, t(:mj_back_lobby), C_DIM)
    end
  end

  # One winner's block; returns the y below it. With several winners the
  # points go on the head line and the yaku in five columns.
  def draw_win(w, k, from, x, y, multi, bw)
    pt = w["pt"].to_s
    head = win_head(w, k, from)
    if multi
      text(x, y, fit(head, bw - tw(pt) - 16), C_TURN)
      text_spaced(x + bw - tw(pt) - 8, y, pt, C_TURN)
    else
      text(x, y, fit(head, bw), C_TURN)
    end
    y += 16
    hc = bytes_of(w["h"])
    step = @big ? 23 : 13
    i = 0
    while i < hc.size
      if @big
        small_tile(hc[i], x + i * step, y)
      else
        text(x + i * 13, y, tile_text(hc[i]))
      end
      i += 1
    end
    ag = w["ag"].to_i
    cx = x + hc.size * step + 6
    if @big
      small_tile(ag, cx, y)
      @gfx.draw_rect(cx, y, 23, 31, C_LAST)
    else
      text(cx - 2, y, tile_text(ag), C_TURN)
    end
    cx += step
    ms = meld_lists(w["w"].to_i)
    # The melds after the winning tile, closer when they would not fit.
    n = 0
    ms.each { |m| n += m.size }
    mstep = step
    mstep = (x + bw - cx - ms.size * 8) / n if n > 0 && cx + n * step + ms.size * 8 > x + bw
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
        cx += mstep
        j += 1
      end
      cx += step - mstep
      i += 1
    end
    y += @big ? 36 : 15
    ys = w["y"].is_a?(Array) ? w["y"] : []
    cols = multi ? 5 : 3
    cw = bw / cols
    i = 0
    while i < ys.size
      text_spaced(x + (i % cols) * cw, y + (i / cols) * 14, fit(yaku_label(w, i), cw - 6))
      i += 1
    end
    y += ((ys.size + cols - 1) / cols) * 14 + 2
    pa = pao_text(w)
    unless multi
      text_spaced(x, y, fit(pt, bw), C_TURN)
      if !pa.empty? && tw(pt) + 4 + tw(pa) <= bw
        text_spaced(x + tw(pt) + 4, y, pa, C_AWAY)
        pa = ""
      end
      y += 16
    end
    unless pa.empty?
      text_spaced(x, y, fit(pa.strip, bw), C_AWAY)
      y += 14
    end
    y + (multi ? 4 : 4)
  end

  # ------------------------------------------------------------ the parts (MJ5)

  # The 640x360 table's parts, [x, y, w, h] from the user area's corner (the
  # overflow checker, tools/mahjong_overflow, holds every drawing to them;
  # the melds and the rivers are laid out in them). The spectators' screen
  # has its own (WATCH, mahjong_watch.rb).
  LAYOUT640 = {
    info: [152, 2, 352, 32], assist: [504, 2, 136, 20], controls: [152, 274, 484, 20], hand: [6, 294, 630, 62],
    label0: [4, 10, 144, 16], label1: [492, 38, 144, 16], label2: [152, 22, 200, 16], label3: [4, 38, 144, 16],
    melds0: [200, 324, 436, 32], melds1: [492, 54, 144, 64], melds2: [152, 38, 336, 32], melds3: [4, 54, 144, 64],
    river0: [176, 170, 288, 96], river1: [492, 120, 144, 128], river2: [176, 72, 288, 96], river3: [4, 120, 144, 128],
    rules: [448, 34, 192, 326]
  }

  def layout640(key)
    LAYOUT640[key] || WATCH[key]
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
