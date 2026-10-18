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
    @seat_sel = nil # the seat chosen by the keys (nil: a rule is)
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

  # ------------------------------------------------------------ the seats (GS5c)
  #
  # The owner sets each seat that is not a player's with its button: empty,
  # CPU, then the AI players the host offers (the lobby view's "ak", in its
  # order; a kind without its key, or past the day's limit, is not there),
  # and empty again. One call each (asterism-games 0.12.1 turns a CPU or
  # AI seat into another kind at once; a refused AI leaves it as it was).
  # The others see the state only.

  # A seat's state: "" empty, "cpu", an AI player's kind; nil a player's.
  def seat_state(s)
    k = seat_kinds[s]
    return nil if k == 1
    return "" if k == 0
    ai = @view["ai"]
    kind = ai.is_a?(Array) ? ai[s].to_s : ""
    kind.empty? ? "cpu" : kind
  end

  def seat_label(st)
    return t(:mj_empty) if st == ""
    st == "cpu" ? t(:mj_cpu) : ai_label(st)
  end

  # The owner turns seat s to the next state (step 1) or the last (-1).
  def seat_step(s, step)
    st = seat_state(s)
    return unless owner? && st
    list = ["", "cpu"] + ai_kinds
    i = list.index(st)
    # An AI player no longer offered: on to empty, or back to a CPU.
    nx = i.nil? ? (step > 0 ? "" : "cpu") : list[(i + step) % list.size]
    if nx == ""
      lobby(B_CPU_OFF, s)
    else
      lobby(B_CPU_ON, s, nx == "cpu" ? nil : nx)
    end
  end

  # The seat's button (the owner) or its state (the others), at x, y.
  def draw_seat_state(s, x, y)
    l = seat_label(seat_state(s))
    if owner?
      w = button(B_SEAT, [s], x, y, l)
      @gfx.draw_round_rect(x, y, w, u(20), u(4), C_TURN) if s == @seat_sel
    else
      text(x + u(6), y + u(4), l, C_DIM)
    end
  end

  # The owner's keys for the seats: up / down choose a seat (the seats come
  # before the first rule: up from it, or down from the last), left / right
  # change the chosen one. True when the key was one of them.
  def seat_key(k)
    return false unless owner?
    sc = k[0]
    order = rules && @big ? rule_order : []
    if sc == FmrbConst::KEY_TAB || k[1] == 9
      @seat_sel = nil
      return false
    end
    up = sc == FmrbConst::KEY_UP
    if up || sc == FmrbConst::KEY_DOWN
      if @seat_sel.nil?
        unless order.empty?
          edge = up ? order[0][0] : order[order.size - 1][0]
          return false unless @rule_sel == edge
        end
        @seat_sel = up ? 3 : 0
      elsif !order.empty? && @seat_sel == (up ? 0 : 3)
        pos = up ? order.size - 1 : 0
        @rule_sel = order[pos][0]
        @rule_page = order[pos][1]
        @seat_sel = nil
      else
        @seat_sel = (@seat_sel + (up ? -1 : 1)) % 4
      end
      @dirty = true
      return true
    end
    return false if @seat_sel.nil?
    return false unless sc == FmrbConst::KEY_LEFT || sc == FmrbConst::KEY_RIGHT
    seat_step(@seat_sel, sc == FmrbConst::KEY_LEFT ? -1 : 1)
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
        sel = own && @seat_sel.nil? && i == @rule_sel
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

  # ------------------------------------------------------------ the chosen tile and the help marks
  #
  # Here and not in mahjong.app.rb: with these few methods more in the main
  # file, the Linux sim's spawn failed with NoMemoryError right after the
  # compile (comments of the same size did not), so the main file is kept
  # from growing (doc/ruby_asterism/report/mj5.md, chapter 14).

  # The help mark's half width and how far its tip comes down over the
  # tile's top edge (1280x720, 640x360, the small table).
  def mark_r
    @xl ? 7 : (@big ? 4 : 3)
  end

  def mark_overlap
    @xl ? 6 : (@big ? 3 : 2)
  end

  # How far the mark reaches above the tile's top.
  def mark_rise
    mark_r * 2 + 2 - mark_overlap
  end

  # The help mark of a hand tile at (x, y) (its top left, w wide): a
  # downward triangle above the tile's middle, its tip over the top edge.
  def assist_mark(x, y, w, col)
    r = mark_r
    cx = x + w / 2
    tip = y + mark_overlap - 1
    @gfx.fill_triangle(cx - r - 1, tip - r - r - 1, cx + r + 1, tip - r - r - 1, cx, tip, 0x00)
    @gfx.fill_triangle(cx - r, tip - r - r, cx + r, tip - r - r, cx, tip - 2, col)
  end

  # The chosen tile's frame, inside the tile (the 1280x720 hand 4 px, its
  # river 3, the 640x360 tiles and the small ones of both tables 2, the
  # small table 1), its corners cut as the tile's. Also the frame of a tile
  # of the chosen one's kind (MJ8).
  def chosen_frame(x, y, w, h)
    t = w >= 80 ? 4 : (w >= 40 ? (@xl ? 3 : 2) : (@big ? 2 : 1))
    @gfx.fill_rect(x + t, y, w - t - t, t, C_CHOSEN)
    @gfx.fill_rect(x + t, y + h - t, w - t - t, t, C_CHOSEN)
    @gfx.fill_rect(x, y + t, t, h - t - t, C_CHOSEN)
    @gfx.fill_rect(x + w - t, y + t, t, h - t - t, C_CHOSEN)
    return if t < 2
    k = t / 2
    @gfx.fill_rect(x + k, y + k, t, t, C_CHOSEN)
    @gfx.fill_rect(x + w - k - t, y + k, t, t, C_CHOSEN)
    @gfx.fill_rect(x + k, y + h - k - t, t, t, C_CHOSEN)
    @gfx.fill_rect(x + w - k - t, y + h - k - t, t, t, C_CHOSEN)
  end

  # ------------------------------------------------------------ the parts (MJ5)

  # The 640x360 table's parts, [x, y, w, h] from the user area's corner (the
  # overflow checker, tools/mahjong_overflow, holds every drawing to them;
  # the melds and the rivers are laid out in them). The spectators' screen
  # has its own (WATCH, mahjong_watch.rb).
  LAYOUT640 = {
    info: [152, 2, 352, 32], assist: [504, 2, 136, 20], controls: [4, 253, 552, 47], clock: [560, 250, 76, 34], hand: [6, 287, 630, 69],
    label0: [4, 250, 168, 16], label1: [492, 38, 144, 16], label2: [152, 22, 200, 16], label3: [4, 38, 144, 16],
    melds0: [200, 308, 436, 48], melds1: [492, 54, 144, 64], melds2: [152, 38, 336, 32], melds3: [4, 54, 144, 64],
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

  # ------------------------------------------------------------ the controls (640x360 and the small table)

  # The buttons the host offers and what the screen has to say. 640x360
  # (MJ8): the buttons right-aligned against the clock (LAYOUT640 :clock),
  # the line left of them; a call's buttons show their tiles (call_label):
  # while a discard is answered the whole row is CALL_BTN_H high and stands
  # on the hand's top (no tile is lifted then), on the own move a kan's
  # button is that high and the others the usual. The small table: the
  # texts from the left, the time left in the line.
  def draw_controls
    y = hand_y - SEL_LIFT - mark_rise - u(20) # clear of the help marks over the lifted tile
    list = @view["a"].is_a?(Array) ? @view["a"] : []
    labels = []
    i = 0
    while i < list.size
      o = list[i]
      tl = bytes_of(o[1])
      label = @big ? call_label(o[0], tl) : button_label(o[0], tl)
      labels << [o[0], tl, label] if label
      i += 1
    end
    labels << [B_LEAVE, [], t(:mj_leave)] if @seat.nil?
    msg = status_line.to_s
    wt = waits_text
    msg = "#{t(:mj_pick)} #{msg}" if @pick && wt.nil?
    msg = "#{wt} #{msg}" if wt
    if @big
      draw_buttons640(labels, y, msg)
      draw_assist if @seat
      draw_clock
      return
    end
    bx = @x0 + 2
    labels.each { |l| bx += button(l[0], l[1], bx, y, l[2]) + 4 }
    msg = "#{time_left_text} #{msg}" if @deadline
    text(bx + 4, y + 4, fit(msg, @x0 + @w - bx - 8), C_TURN) unless msg.strip.empty? || bx + 40 > @x0 + @w
  end

  # The help level's button, at the top right of the 640x360 table.
  def draw_assist
    fnt(12, 1)
    button(B_ASSIST, [], @x0 + @w - tw(assist_label) - 16, @y0 + 2, assist_label)
  end

  # ------------------------------------------------------------ the clock (MJ6)
  #
  # The own time in big figures (asterism-games README "Time"): the move's
  # own seconds and the bank after it ("5+20"), only the bank once the
  # move's own time is up ("+17"). White, yellow from CLOCK_YELLOW_MS of the
  # move's own time (2 s: the default move is only 5 s), red in the bank. The same for the answer to a call (no bank).
  # 640x360: right of the controls, above the drawn tile (LAYOUT640
  # :clock); 1280x720: at the end of the buttons (mahjong_xl.rb).

  CLOCK_YELLOW_MS = 2000

  def clock_parts
    return nil unless @deadline && @seat && mode == :table
    ms = @deadline - Machine.board_millis
    ms = 0 if ms < 0
    move = ms - @bank_ms
    return ["+#{(ms + 999) / 1000}", C_RED] if @bank_ms > 0 && move <= 0
    sec = (move + 999) / 1000
    [@bank_ms > 0 ? "#{sec}+#{@bank_ms / 1000}" : sec.to_s, move <= CLOCK_YELLOW_MS ? C_TURN : C_TEXT]
  end

  # The clock in a dark box at (x, y), w x h: the figures px (scaled by
  # size), at 12 px when they do not fit.
  def clock_box(c, x, y, w, h, px, size)
    @gfx.fill_round_rect(x, y, w, h, 4, C_BOX)
    @gfx.draw_round_rect(x, y, w, h, 4, c[1])
    fnt(px, size)
    fnt(12, size) if tw(c[0]) > w - 8
    text(x + (w - tw(c[0])) / 2, y + (h - @fw) / 2, c[0], c[1])
  end

  def draw_clock
    c = clock_parts
    return unless c
    r = LAYOUT640[:clock]
    clock_box(c, @x0 + r[0], @y0 + r[1], r[2], r[3], 12, 2)
    fnt(12, 1)
  end

  # ------------------------------------------------------------ declarations (MJ6)
  #
  # The view's "cl" (asterism-games README "Views"): the last declaration,
  # [type, the state version, the seat(s)]. A new one is shown for
  # SHOUT_MS in big letters at the seat that made it, by its river (where
  # the player sits on this screen); a win's result waits until it goes.
  # The first view of a table shows none (that one is old news).
  SHOUT_MS = 1000
  # The middle of each seat's river (rl 0-3) from the user area's corner:
  # 640x360, its spectators' screen (mahjong_watch.rb), 1280x720.
  SHOUT_640 = [[320, 218], [564, 184], [320, 120], [76, 184]]
  SHOUT_WATCH = [[320, 262], [452, 198], [320, 134], [188, 198]]
  SHOUT_XL = [[640, 436], [928, 268], [640, 172], [352, 300]]

  def shout_take(v, old)
    cl = v["cl"]
    n = cl.is_a?(Array) && cl.size > 2 ? cl[1] : nil
    return if n.nil? || n == @cl_n
    @cl_n = n
    return if old.nil?
    @shout = [cl[0], cl[2, cl.size - 2], Machine.board_millis + SHOUT_MS]
    @dirty = true
  end

  def shout_tick(now)
    return unless @shout && now >= @shout[2]
    @shout = nil
    @dirty = true
  end

  def shout_label(type)
    return t(:mj_ron) if type == A_RON || type == 13
    return t(:mj_kyuushu) if type == A_KYUUSHU
    return t(:mj_kan) if type == A_ANKAN || type == A_KAKAN || type == A_MINKAN
    return t(:mj_riichi) if type == A_RIICHI
    return t(:mj_tsumo) if type == A_TSUMO
    type == A_PON ? t(:mj_pon) : t(:mj_chi)
  end

  # The shout of each seat that declared (1280x720 draws it from xl_draw).
  def draw_shout
    return unless @shout
    seats = @shout[1]
    l = shout_label(@shout[0])
    out = []
    i = 0
    while i < seats.size
      rl = rel(seats[i].to_i)
      if @xl
        c = SHOUT_XL[rl]
        out << draw_shout_box(l, c[0], c[1], 16, 3)
      elsif @big
        c = (watching? || @rp ? SHOUT_WATCH : SHOUT_640)[rl]
        out << draw_shout_box(l, c[0], c[1], 16, 2)
      else
        a = river_area(rl)
        out << draw_shout_box(l, a[0] - @x0 + 40, a[1] - @y0 + 14, 12, 1)
      end
      i += 1
    end
    fnt(12, @k)
    out
  end

  # One shout centred on (cx, cy) from the user area's corner, kept on the
  # screen. Returns its rectangle.
  def draw_shout_box(label, cx, cy, px, size)
    fnt(px, size)
    pad = px * size / 3
    w = tw(label) + pad * 2
    h = @fw + pad
    x = @x0 + cx - w / 2
    y = @y0 + cy - h / 2
    x = @x0 + 2 if x < @x0 + 2
    x = @x0 + @w - w - 2 if x + w > @x0 + @w - 2
    win = @shout[0] == A_RON || @shout[0] == A_TSUMO || @shout[0] == 13
    @gfx.fill_round_rect(x, y, w, h, pad / 2 + 2, C_BOX)
    @gfx.draw_round_rect(x, y, w, h, pad / 2 + 2, win ? C_RED : C_TURN)
    text(x + pad, y + pad / 2, label, C_TURN)
    [x, y, w, h]
  end

  # ------------------------------------------------------------ the guide (MJ8)
  #
  # The status line says what the table waits for now: whose turn it is
  # ("南家 (Claude) の番です"), or that the others are answering a discard
  # (who may call is not told), and only on the player's own move what to
  # do. A reply of the host to the player's action goes before it until the
  # state moves on (took_view).

  def status_line
    return t(:mj_lost) if @net != :up || reconnecting?
    return t(:mj_host_left) if @host_gone
    return t(:mj_host_away) if @host_away
    g = guide
    @note.empty? ? g : "#{@note}  #{g}"
  end

  def guide
    v = @view
    return "" unless v && v["st"] == "play"
    r = v["r"]
    cur = r.is_a?(Array) && r.size > 7 ? r[6].to_i : -1
    turn = cur >= 0 ? "#{wind_of(cur)}家 (#{seat_name(cur)}) の番です" : ""
    return "#{t(:mj_watching)}  #{turn}" if @seat.nil?
    if calls_phase?
      return t(:mj_others) if @others
      return "鳴くかパス (Esc) を選んでください" unless offer(A_RON)
      return offer(A_PON) || offer(A_CHI) || offer(A_MINKAN) ? "ロン・鳴く・パス (Esc) から選んでください" : "ロンかパス (Esc) を選んでください"
    end
    return turn unless cur == @seat
    offer(A_DAHAI) ? "自分の番: 切る牌をクリック (左右と Enter でも)" : ""
  end

  # This seat answers a discard now (the call buttons are up).
  def answering_call?
    @seat && @view && @view["st"] == "play" && calls_phase? && !@others ? true : false
  end

  # After take_view: the host's reply goes once the state moves on; no hand
  # tile stays chosen while a discard is answered; the declarations (MJ6).
  def took_view(v, old)
    @note = "" if old && old["sv"] != v["sv"] && v["st"] != "wait"
    @sel = -1 if answering_call?
    shout_take(v, old)
  end

  # ------------------------------------------------------------ the call buttons (MJ8)
  #
  # Pon, chi and the kans show their tiles as pictures, the river's 24x32
  # (both tables): [head, tiles, key]. Pon and chi show the two tiles from
  # the hand (the offer's), a closed or added kan its tile, an open kan the
  # discard (the offer has no tiles). Other buttons are texts.

  CALL_BTN_H = 34   # the 640x360 (and small) table's row of call buttons

  def call_label(type, tl)
    tiles = nil
    if type == A_ANKAN || type == A_KAKAN
      tiles = [tl[0]] unless tl.empty?
    elsif type == A_PON || type == A_CHI || type == A_MINKAN
      tiles = tl unless tl.empty?
      ls = @view["ls"]
      if tiles.nil? && ls.is_a?(Array) && ls.size > 1
        c = ls[1].to_i & 0x3F
        tiles = type == A_PON ? [c, c] : [c]
      end
    end
    return button_label(type, tl) unless tiles
    return [t(:mj_pon), tiles, "P"] if type == A_PON
    type == A_CHI ? [t(:mj_chi), tiles, "C"] : [t(:mj_kan), tiles, "K"]
  end

  # The width of a button's contents (a text, or [head, tiles, key]).
  def btn_label_w(l)
    return tw(l) unless l.is_a?(Array)
    gap = @fw / 3
    w = tw(l[0]) + gap + l[1].size * 24
    l[2] ? w + gap + tw(l[2]) : w
  end

  # A button's contents centred in its box.
  def btn_content(l, x, y, w, h)
    ty = y + (h - @fw) / 2
    unless l.is_a?(Array)
      text(x + (w - tw(l)) / 2, ty, l)
      return
    end
    gap = @fw / 3
    cx = x + (w - btn_label_w(l)) / 2
    text(cx, ty, l[0])
    cx += tw(l[0]) + gap
    i = 0
    while i < l[1].size
      small_tile(l[1][i], cx, y + (h - 32) / 2)
      cx += 24
      i += 1
    end
    text(cx + gap, ty, l[2]) if l[2]
  end

  def controls_width(labels, gap)
    total = 0
    labels.each { |l| total += btn_label_w(l[2]) + u(12) + gap }
    total - gap
  end

  # ------------------------------------------------------------ the same tiles (MJ8)
  #
  # While a hand tile is chosen, every river tile, meld tile and dora
  # indicator of its kind (a red five is a five) gets the chosen tile's blue
  # frame; @mk is the kind (draw_screen sets it), nil when none is chosen.

  def tile_kind(c)
    c < 30 && c % 10 == 9 ? c - 5 : c
  end

  def mark_kind
    return nil unless @seat && @sel >= 0 && !@rp && mode == :table
    c = hand_at(@sel)
    c ? tile_kind(c) : nil
  end

  def same?(c)
    @mk ? tile_kind(c) == @mk : false
  end

  def same_mark(c, x, y, w, h)
    chosen_frame(x, y, w, h) if @mk && tile_kind(c) == @mk
  end

  # ------------------------------------------------------------ the title (MJ8)
  #
  # "ヒデ雀" when the app opens, while the network comes up (on_update goes
  # on): until the list of tables is ready, at most TITLE_MS. Its drawing is
  # in mahjong_title.rb, read when the app opens and taken out of the class
  # again when the title goes (the VM of a game does not hold it).

  TITLE_MS = 2000

  def title_draw
    unless @title_in
      @title_in = true
      require "#{APP_DIR}/mahjong_title"
    end
    draw_title
  end

  # Any key or click: the title goes at the next update (and the event is
  # not the list's).
  def title_skip(t)
    return false unless @title_until
    @title_until = 1 if t == :key_down || t == :mouse_up
    true
  end

  def title_tick(now)
    return unless @title_until && (now >= @title_until || list_ready?)
    @title_until = nil
    @title_drawn = nil
    title_unload
    @dirty = true
    draw_screen if @unsupported
  end

  # ------------------------------------------------------------ shared with the 1280x720 table
  #
  # Used by every screen (mahjong_xl.rb is read only at 1280x720).

  XL_STACK = [[0, -8], [-8, 0], [0, 8], [8, 0]] # an added kan's tile over the sideways one (rl 0-3)

  # The 1280x720 table's file and sheets, when the screen is that big.
  def load_xl_sheets
    return unless @xl
    unless @xl_in
      @xl_in = true
      require "#{APP_DIR}/mahjong_xl"
    end
    xl_load_sheets
  end

  # The small tiles turned for the four seats ([r0, r90, r180, r270]): the
  # 1280x720 table's opponents and the 640x360 spectators (mahjong_watch.rb);
  # side_only: only r270, the sideways tile of the 640x360 melds (MJ8). Each
  # sheet is tried once.
  def load_turned_sheets(side_only = false)
    return if @turned_all
    @turned ||= [@river_sheet, nil, nil, nil]
    unless @turned_side
      @turned_side = true
      @turned[3] = load_sheet("tiles_river_r270.bmp", 320, 96, XL_DIR)
    end
    return if side_only
    @turned_all = true
    @turned[1] = load_sheet("tiles_river_r90.bmp", 320, 96, XL_DIR)
    @turned[2] = load_sheet("tiles_river_r180.bmp", 240, 128, XL_DIR)
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

  # The 640x360 table's own drawing (mahjong_640.rb), read when the screen
  # is that size (load_hand_sheet): the 1280x720 table's VM never holds it.
  def m640_load
    return if @m640_in
    @m640_in = true
    require "#{APP_DIR}/mahjong_640"
  end

  # ------------------------------------------------------------ the spectators' screen

  def watching?
    @big && !@xl && @seat.nil? && mode == :table
  end

  # The 640x360 spectators' screen is read the first time it is wanted
  # (watching, a record): a seated player's VM never holds it.
  def watch_load
    return if @watch_in || !@big || @xl
    @watch_in = true
    require "#{APP_DIR}/mahjong_watch"
  end
end
