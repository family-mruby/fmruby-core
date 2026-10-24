# Othello table (doc/ruby_asterism, GS2): the screen of an othello table run
# by a game host (the asterism-games repository, README "The othello
# table") on the same Zenoh network. Two seats and the spectators; the host
# holds the board, checks the moves and plays the CPU.
#
# - The host has one or more tables (objects <host>/games/othello*) and puts
#   each table's view on asterism/<host>/games/<table>/state after every
#   change and every 5 s. This app lists them, joins one (join(name) -> a
#   token) and asks its own view (view(token): the table's view and "me",
#   its seat) when a put shows a newer version, and every VIEW_EVERY_MS
#   anyway (which also tells the host it is still there, where the host
#   does not see the nodes come and go).
# - Who is there comes from Asterism (GS3): a host that leaves the network
#   (on_leave) takes its tables off the list and, at its table, shows the
#   connection lost and stops asking; when it is back (on_join) the app
#   looks for its tables again at once (and every LIST_EVERY_MS anyway,
#   should the word not come) and asks its view.
# - In the lobby: sit / stand, the owner (the first who sat) puts the CPU in
#   the empty seat and starts. The colours are drawn at the start (seat 0
#   is black and moves first). A move is move(token, n, "d3") with n the
#   view's "n" (moves so far); the host passes for a side with no legal
#   move. With a time per move (the host's turn_seconds) the CPU plays a
#   move whose time ran out. After the game the board and the result stay
#   until someone sits, stands or the owner starts again.
# - AI players (GS13, asterism-games 0.17.0): when the host offers one
#   (the lobby view's "ak": ["jev"] with its key, absent from older hosts)
#   the owner may put Jev in the other seat (Jev[A]) or turn the CPU there
#   into Jev; CPU[C] turns it back. The seats' "ai" names an AI seat
#   ("[AI]"), and its move is shown as thinking. A host without "ak" gives
#   the screen it always had.
# - The connection is the machine's (FmrbAsterism.settings, as othello and
#   mahjong): the router, the ID, a credentials .tar, the room's seat in
#   the browser build. The player's name is the profile's (the ID without one).
#   Staying connected, the list, joining, leaving and the view are the
#   table client's (FmrbAsterism::TableClient, doc/app_refactor R4); this
#   file has the game: the seats, the moves, the screen.
# - The same game between two machines without a host is "Othello
#   (direct)", flash/app/test/othello.app.rb (this file copies its board's
#   drawing).
# - A screen narrower than the window (Retro, 320x240: the window of
#   .app.toml is 372x222 for Modern) asks for the whole screen instead, and
#   the board and the panel are laid out on it (a smaller board); the list
#   then has a close button (Ctrl+Q closes it too). Modern keeps its window.
# - Keys. The list: up / down choose, Enter joins. At the table: arrows move
#   the cursor, Enter plays there (in the lobby: the owner starts), s sits or
#   stands, c puts the CPU in the empty seat (the owner; Jev back into the
#   CPU), a puts Jev in the other seat (the owner, when offered), Esc leaves (asks
#   to resign first while playing a seat); y / n answer. A click on a square
#   plays there; the buttons take clicks.

class OthelloTableApp < FmrbApp
  HOST_LEFT = "ホストとのつながりが切れた"
  MEM_EVERY_MS = 30000
  COLS = "abcdefgh"
  # The game's own colours (a board game draws its own picture).
  C_BOARD = 0x10
  C_LINE = 0x00
  C_BLACK = 0x00
  C_WHITE = 0xFF
  C_CURSOR = 0xE0
  C_HINT = 0x6D
  C_LAST = 0xFC
  # The window's width in .app.toml: a screen narrower than it is taken whole.
  WINDOW_W = 372
  CONF_FILE = "/etc/system_conf.toml"

  # The host's answers the screen shows.
  REASONS = {
    "token" => "卓の人ではない", "not seated" => "席に着いていない", "stale" => "盤が古い: 読み直し中",
    "illegal" => "そこには置けない", "not your turn" => "相手の番", "not playing" => "対局中ではない",
    "not waiting" => "対局中は席を変えられない", "taken" => "その席は埋まっている", "no seat" => "その席は無い",
    "not owner" => "卓の主だけができる", "seats" => "2 席がそろっていない", "no human" => "人が 1 人もいない",
    "no change" => "変わらない", "full" => "卓が満員", "spectators" => "観戦の人数が上限",
    "identity" => "この部屋での名乗りが合わない (部屋のリンクを開き直す)",
    "no ai" => "その AI は今は選べない"
  }

  def on_create
    @gfx.set_font(:ja, 12)
    dw = conf_int("display_width")
    request_fullscreen(true) if dw > 0 && dw < WINDOW_W
    # The tables ("<host>/games/othello*", tables[p] = [st, names] from its
    # last put), the table this app is at, its token: the table client's.
    @tc = ::FmrbAsterism::TableClient.new(self, "othello", tag: "othello_table")
    @id = @tc.link.node
    @list_sel = 0
    @cmd_f = nil
    @view = nil
    @deadline = nil
    @confirm = false
    @note = ""
    @cx = 3
    @cy = 3
    @keys = []
    @clicks = []
    @buttons = FmrbButtons.new(@gfx)
    @second = 0
    @mem_at = 0
    @dirty = true
    layout
    draw_screen
  end

  def on_resize(w, h)
    layout
    @dirty = true
  end

  # An integer setting of /etc/system_conf.toml (0 when it is not there).
  def conf_int(key)
    conf = FmrbToml.load(CONF_FILE)
    v = conf ? conf[key] : nil
    v.is_a?(Integer) ? v : 0
  end

  def my_name
    pr = ::Asterism.profile
    n = pr ? pr["name"] : nil
    n && !n.to_s.empty? ? n.to_s : @id
  rescue
    @id
  end

  # ------------------------------------------------------------ network
  # The connection, the list, joining and the view are @tc's
  # (FmrbAsterism::TableClient); these are its calls and the game's own.

  # A put of an othello table: [st, names] for the list; before the token
  # the table's own put is the view.
  def table_state(p, pv)
    @dirty = true if @tc.table.nil?
    @view = pv if p == @tc.table && @tc.token.nil?
    [pv["st"].to_s, pv["nm"].is_a?(Array) ? pv["nm"] : []]
  end

  def tables_changed(added)
    @list_sel = 0 if !added && @list_sel >= @tc.table_list.size
    @dirty = true
  end

  def table_link_up(link)
    @dirty = true
  end

  def table_link_down(link)
    @cmd_f = nil
    @dirty = true
  end

  def table_host_gone
    @note = HOST_LEFT
    @dirty = true
  end

  def table_host_back
    @note = ""
    @dirty = true
  end

  def join(p)
    return if @tc.joining? || p.nil?
    err = @tc.join(p, my_name)
    @note = err || ""
    @dirty = true
  end

  def table_joined(r)
    @view = nil
    @confirm = false
    @dirty = true
  end

  def table_refused(why)
    @note = why ? reason(why) : "ホストが答えない"
    @dirty = true
  end

  def leave_table
    @tc.leave
    @view = nil
    @cmd_f = nil
    @deadline = nil
    @confirm = false
    @note = ""
    @dirty = true
  end

  def table_dropped
    leave_table
    @note = "卓から外れた"
  end

  def table_view(v)
    @view = v
    dl = v["dl"]
    @deadline = dl ? Machine.board_millis + dl.to_i : nil
    @confirm = false unless v["st"] == "play"
    @dirty = true
  end

  def table_view_failed
    @note = "ホストが答えない: 待っています"
    @dirty = true
  end

  # One call at a time (sit, stand, cpu, start, move, resign); the view is
  # asked again when it is answered.
  def send_cmd(what)
    tok = @tc.token
    return if @cmd_f || tok.nil?
    px = @tc.proxy.async
    o = other_seat
    @cmd_f = case what
             when :sit then px.sit(tok, -1)
             when :stand then px.stand(tok)
             when :cpu then o >= 0 && ai_seat?(o) ? px.cpu(tok, o, true) : px.cpu(tok, -1, true)
             when :ai then ai_seat?(o) ? px.cpu(tok, o, true) : px.cpu(tok, o, ai_kinds[0])
             when :start then px.start(tok)
             when :resign then px.resign(tok)
             end
    @confirm = false
    @dirty = true
  rescue ::Asterism::Error => e
    @cmd_f = nil
    @note = e.message.to_s
  end

  def play(x, y)
    v = @view
    return unless my_turn? && @cmd_f.nil?
    code = COLS[x, 1] + (y + 1).to_s
    unless legal?(code)
      @note = "そこには置けない"
      @dirty = true
      return
    end
    @cmd_f = @tc.proxy.async.move(@tc.token, v["n"].to_i, code)
    @note = ""
    @dirty = true
  rescue ::Asterism::Error => e
    @cmd_f = nil
    @note = e.message.to_s
  end

  def collect_cmd
    f = @cmd_f
    return unless f && f.done?
    @cmd_f = nil
    begin
      r = f.value
      @note = r == "ok" ? "" : reason(r)
    rescue ::Asterism::Error => e
      @note = "ホストが答えない"
    end
    @tc.refresh_view
    @dirty = true
  end

  def reason(r)
    s = r.to_s
    REASONS[s] || s
  end

  # ------------------------------------------------------------ the view

  def me
    @view ? @view["me"].to_i : -1
  end

  def seated?
    @tc.token && me >= 0 && me < 2
  end

  def owner?
    seated? && @view["ow"] == me
  end

  def playing?
    @view && @view["st"] == "play"
  end

  def my_turn?
    playing? && seated? && @view["tn"] == me
  end

  def legal?(code)
    lg = @view ? @view["lg"].to_s : ""
    i = 0
    while i + 1 < lg.length
      return true if lg[i, 2] == code
      i += 2
    end
    false
  end

  # The AI players the host offers (the lobby view's "ak"; none from a
  # host without them), as the mahjong app reads them.
  def ai_kinds
    ak = @view ? @view["ak"] : nil
    ak.is_a?(Array) ? ak : []
  end

  def ai_seat?(i)
    ai = @view ? @view["ai"] : nil
    i >= 0 && ai.is_a?(Array) && !ai[i].to_s.empty?
  end

  def ai_label(kind)
    kind == "claude" ? "Claude" : (kind == "jev" ? "Jev" : kind.to_s)
  end

  # The seat that is not this member's (-1 without a seat).
  def other_seat
    seated? ? 1 - me : -1
  end

  # Whether the owner's Jev[A] does something: an AI player offered and
  # the other seat empty or the CPU's (Jev there: back to the CPU).
  def ai_choice?
    return false unless owner?
    o = other_seat
    sk = @view["sk"].is_a?(Array) ? @view["sk"] : [0, 0]
    return true if ai_seat?(o)
    !ai_kinds.empty? && sk[o] != 1
  end

  def name_of(i)
    nm = @view ? @view["nm"] : nil
    n = nm.is_a?(Array) ? nm[i].to_s : ""
    n.empty? ? "(空き)" : n
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    if t == :key_down
      @keys << [ev[:scancode], ev[:character] || 0]
    elsif t == :mouse_up && ev[:button] == 1
      @clicks << [ev[:x], ev[:y]]
    end
  end

  def run_key(k)
    k => [sc, ch]
    enter = sc == FmrbConst::KEY_ENTER || ch == 13 || ch == 10
    esc = sc == FmrbConst::KEY_ESC || ch == 27
    @dirty = true
    if @tc.table.nil?
      if sc == FmrbConst::KEY_UP
        @list_sel -= 1 if @list_sel > 0
      elsif sc == FmrbConst::KEY_DOWN
        @list_sel += 1 if @list_sel < @tc.table_list.size - 1
      elsif enter
        join(@tc.table_list[@list_sel])
      end
      return
    end
    if @confirm
      if enter || ch == 121 || ch == 89
        send_cmd(:resign)
      elsif esc || ch == 110 || ch == 78
        @confirm = false
      end
      return
    end
    if esc
      if playing? && seated?
        @confirm = true
      else
        leave_table
      end
    elsif sc == FmrbConst::KEY_UP
      @cy = (@cy + 7) % 8
    elsif sc == FmrbConst::KEY_DOWN
      @cy = (@cy + 1) % 8
    elsif sc == FmrbConst::KEY_LEFT
      @cx = (@cx + 7) % 8
    elsif sc == FmrbConst::KEY_RIGHT
      @cx = (@cx + 1) % 8
    elsif enter
      if playing?
        play(@cx, @cy)
      elsif owner?
        send_cmd(:start)
      end
    elsif ch == 115 || ch == 83 # s
      send_cmd(seated? ? :stand : :sit) unless playing?
    elsif ch == 99 || ch == 67  # c
      send_cmd(:cpu) unless playing?
    elsif ch == 97 || ch == 65  # a
      send_cmd(:ai) if !playing? && ai_choice?
    end
  end

  def run_click(c)
    x = c[0]
    y = c[1]
    @dirty = true
    b = @buttons.id_at(x, y)
    return run_button(b) if b
    if @tc.table.nil?
      row = (y - @list_y) / 15
      if y >= @list_y && row >= 0 && row < @tc.table_list.size
        if row == @list_sel
          join(@tc.table_list[row])
        else
          @list_sel = row
        end
      end
      return
    end
    return unless playing? && !@confirm
    cx = (x - @bx) / @cell
    cy = (y - @by) / @cell
    if x >= @bx && y >= @by && cx < 8 && cy < 8
      @cx = cx
      @cy = cy
      play(cx, cy)
    end
  end

  def run_button(id)
    case id
    when :join then join(@tc.table_list[@list_sel])
    when :sit then send_cmd(:sit)
    when :stand then send_cmd(:stand)
    when :cpu then send_cmd(:cpu)
    when :ai then send_cmd(:ai)
    when :start then send_cmd(:start)
    when :resign then @confirm = true
    when :yes then send_cmd(:resign)
    when :no then @confirm = false
    when :leave then leave_table
    when :quit then stop
    end
  end

  # ------------------------------------------------------------ the screen

  def layout
    @x0 = @user_area_x0
    @y0 = @user_area_y0
    uw = @user_area_width
    uh = @user_area_height
    c = (uh - 6) / 8
    m = (uw - 150) / 8
    c = m if m < c
    c = 8 if c < 8
    @cell = c
    @bx = @x0 + 3
    @by = @y0 + 3
    @px = @bx + c * 8 + 6
    @pw = @x0 + uw - @px - 2
    @list_y = @y0 + 34
  end

  # A button of the panel and the list (theme colours, 16 px high).
  def button(id, x, y, w, label)
    @buttons.button(id, x, y, w, 16, label, theme_accent, theme_fg_light)
  end

  def text(x, y, s, color = nil)
    @gfx.draw_text(x, y, s, color || theme_fg)
  end

  def draw_screen
    @buttons.clear
    clear_user_area
    if @tc.table
      draw_board
      draw_panel
    else
      draw_list
    end
    @gfx.present
  end

  def net_line
    net = @tc.net
    return "つないでいます: #{@tc.link.router}" if net == :connecting
    return "網から切れた: つなぎ直し中" if net == :down || @tc.reconnecting?
    "#{@id} (#{@tc.link.router})"
  end

  def status_word(st)
    case st
    in "wait" then "待ち合わせ"
    in "play" then "対局中"
    in "over" then "終局"
    else "-"
    end
  end

  def draw_list
    x = @x0 + 4
    y = @y0 + 3
    w = @user_area_width - 8
    text(x, y, @gfx.fit_text("オセロ (卓): 卓を選ぶ (Enter で入る)", w))
    text(x, y + 15, @gfx.fit_text(net_line, w), theme_border)
    if @tc.table_list.empty?
      text(x, @list_y, @gfx.fit_text(@tc.net == :up ? "ゲームホストを探しています (games/othello*)" : "-", w), theme_border)
    end
    i = 0
    while i < @tc.table_list.size && i < 8
      p = @tc.table_list[i]
      t = @tc.tables[p]
      line = p
      if t
        nm = t[1]
        a = nm[0].to_s
        b = nm[1].to_s
        line = "#{p}  #{status_word(t[0])}  #{a.empty? ? '-' : a} / #{b.empty? ? '-' : b}"
      end
      yy = @list_y + i * 15
      if i == @list_sel
        @gfx.fill_rect(@x0 + 2, yy - 1, @user_area_width - 4, 14, theme_accent)
        text(x, yy, @gfx.fit_text(line, w), theme_fg_light)
      else
        text(x, yy, @gfx.fit_text(line, w))
      end
      i += 1
    end
    by = @y0 + @user_area_height - 40
    text(x, by - 2, @gfx.fit_text(@note, w), theme_border)
    button(:join, x, by + 16, 90, "入る[Enter]") unless @tc.table_list.empty?
    # Taking the whole screen there is no window frame to close it by.
    button(:quit, x + 96, by + 16, 90, "閉じる[Ctrl+Q]") if fullscreen?
  end

  def draw_board
    c = @cell
    @gfx.fill_rect(@bx, @by, c * 8 + 1, c * 8 + 1, C_BOARD)
    i = 0
    while i <= 8
      @gfx.draw_line(@bx + i * c, @by, @bx + i * c, @by + c * 8, C_LINE)
      @gfx.draw_line(@bx, @by + i * c, @bx + c * 8, @by + i * c, C_LINE)
      i += 1
    end
    v = @view
    bd = v ? v["bd"] : nil
    return unless bd.is_a?(String) && bd.length == 64
    r = c / 2 - 2
    r = 2 if r < 2
    last = v["last"].to_s
    lx = last.length == 2 && last != "--" ? COLS.index(last[0, 1]) : nil
    ly = lx ? last[1, 1].to_i - 1 : -1
    i = 0
    while i < 64
      ch = bd[i, 1]
      x = i % 8
      y = i / 8
      mx = @bx + x * c + c / 2
      my = @by + y * c + c / 2
      if ch == "B"
        @gfx.fill_circle(mx, my, r, C_BLACK)
      elsif ch == "W"
        @gfx.fill_circle(mx, my, r, C_WHITE)
        @gfx.draw_circle(mx, my, r, C_LINE)
      end
      @gfx.fill_rect(mx - 1, my - 1, 3, 3, C_LAST) if x == lx && y == ly
      i += 1
    end
    if my_turn?
      lg = v["lg"].to_s
      j = 0
      while j + 1 < lg.length
        hx = COLS.index(lg[j, 1])
        hy = lg[j + 1, 1].to_i - 1
        @gfx.fill_circle(@bx + hx * c + c / 2, @by + hy * c + c / 2, 2, C_HINT) if hx
        j += 2
      end
    end
    if playing?
      @gfx.draw_rect(@bx + @cx * c + 1, @by + @cy * c + 1, c - 1, c - 1, C_CURSOR)
      @gfx.draw_rect(@bx + @cx * c + 2, @by + @cy * c + 2, c - 3, c - 3, C_CURSOR)
    end
  end

  # The panel right of the board. Every line stays inside @pw: a seat's name
  # gives way (to "…") before its marks and its count, and a long line of
  # the three slots under the seats or the note wraps to a second line,
  # pushing the rest down.
  def draw_panel
    x = @px
    y = @y0 + 3
    v = @view
    text(x, y, @gfx.fit_text(@tc.table.split("/")[2].to_s + (v ? "  " + status_word(v["st"]) : ""), @pw, "…"))
    unless v
      text(x, y + 15, "入っています...", theme_border)
      return
    end
    cnt = v["cnt"].is_a?(Array) ? v["cnt"] : nil
    sk = v["sk"].is_a?(Array) ? v["sk"] : [0, 0]
    aw = v["aw"].to_i
    i = 0
    while i < 2
      yy = y + 17 + i * 15
      @gfx.fill_circle(x + 5, yy + 6, 5, i == 0 ? C_BLACK : C_WHITE)
      @gfx.draw_circle(x + 5, yy + 6, 5, theme_border)
      tags = ""
      tags += "*" if v["ow"] == i
      tags += " [AI]" if ai_seat?(i)
      tags += " (自分)" if i == me
      tags += " 途切れ" if (aw >> i) & 1 == 1
      name = name_of(i)
      name = "CPU" if sk[i] == 2 && name == "(空き)"
      num = cnt ? " #{cnt[i]}" : ""
      color = v["tn"] == i ? theme_accent : theme_fg
      room = @pw - 14 - @gfx.text_width(tags + num)
      text(x + 14, yy, @gfx.fit_text(name, room, "…") + tags + num, color)
      i += 1
    end
    # Three slots (one or two lines each), then the note.
    yy = y + 50
    st = v["st"]
    case st
    in "play"
      tn = v["tn"].to_i
      who = v["tn"] == me ? "あなたの番" : (v["tn"] == 0 ? "黒の番" : "白の番")
      who = "#{name_of(tn)} が考え中" if ai_seat?(tn)
      yy = @gfx.draw_text_lines(x, yy, "#{who} (#{v['n'].to_i + 1} 手目)", @pw, v["tn"] == me ? C_CURSOR : theme_fg, 15)
      if @deadline
        left = (@deadline - Machine.board_millis + 999) / 1000
        left = 0 if left < 0
        yy = @gfx.draw_text_lines(x, yy, "残り #{left} 秒 (切れたら CPU が打つ)", @pw, theme_border, 15)
      else
        yy += 15
      end
    in "over"
      res = v["res"].is_a?(Hash) ? v["res"] : {}
      w = res["w"].to_i
      word = w < 0 ? "引き分け" : "#{w == 0 ? '黒' : '白'} (#{name_of(w)}) の勝ち"
      word = "あなたの勝ち" if w >= 0 && w == me
      word = "あなたの負け" if w >= 0 && me >= 0 && me < 2 && w != me
      yy = @gfx.draw_text_lines(x, yy, "終局 #{res['b']}-#{res['wc']}#{res['why'] == 'resign' ? ' 投了' : ''}", @pw, theme_accent, 15)
      yy = @gfx.draw_text_lines(x, yy, word, @pw, theme_accent, 15)
    else
      yy = @gfx.draw_text_lines(x, yy, seated? ? "席に着いた (色は始めるときに決まる)" : "観戦中: 空いた席に座れる", @pw, theme_border, 15)
      yy = @gfx.draw_text_lines(x, yy, "卓の主 (*) が始める", @pw, theme_border, 15)
    end
    last = v["last"]
    text(x, yy, @gfx.fit_text("最後: #{last == '--' ? 'パス' : last}", @pw, "…"), theme_border) if last && st == "play"
    @gfx.draw_text_lines(x, yy + 15, @note, @pw, theme_border, 15)
    draw_buttons(x)
  end

  def draw_buttons(x)
    by = @y0 + @user_area_height - 20
    by2 = by - 20
    if @confirm
      text(x, by2 + 2, "投了しますか?", C_CURSOR)
      button(:yes, x, by, 60, "はい")
      button(:no, x + 66, by, 60, "いいえ")
      return
    end
    if playing?
      button(:resign, x, by, 60, "投了") if seated?
      button(:leave, x + 66, by, 60, "出る") unless seated?
      return
    end
    sk = @view["sk"].is_a?(Array) ? @view["sk"] : [0, 0]
    if seated?
      button(:stand, x, by2, 60, "立つ[S]")
    elsif sk[0] != 1 || sk[1] != 1
      button(:sit, x, by2, 60, "座る[S]")
    end
    button(:cpu, x + 66, by2, 60, "CPU[C]") if owner? && (sk[0] == 0 || sk[1] == 0 || ai_seat?(other_seat))
    if ai_choice? && !ai_seat?(other_seat)
      button(:ai, x + 66, by2 - 20, 60, "#{ai_label(ai_kinds[0])}[A]")
    end
    button(:start, x, by, 60, "始める") if owner? && sk[0] != 0 && sk[1] != 0
    button(:leave, x + 66, by, 60, "出る")
  end

  # ------------------------------------------------------------ the loop

  def on_update
    now = Machine.board_millis
    if @tc.update
      collect_cmd if @tc.table
      @tc.update_view(now)
    end
    run_key(@keys.shift) until @keys.empty?
    run_click(@clicks.shift) until @clicks.empty?
    if now >= @mem_at
      # The VM pool, now and then, for the report (doc/ruby_asterism/report/gs2.md).
      @mem_at = now + MEM_EVERY_MS
      Log.info("othello_table: pool #{FmrbApp.pool_used / 1024} KB (#{FmrbApp.pool_usage}%)")
    end
    if @deadline && (@deadline - now) / 1000 != @second
      @second = (@deadline - now) / 1000
      @dirty = true
    end
    if @dirty
      @dirty = false
      draw_screen
    end
    50
  end

  def on_destroy
    leave_table if @tc.token
    @tc.close
    Log.info("othello_table: closed")
  rescue => e
    Log.info("othello_table: close: #{e.message}")
  end
end

begin
  app = OthelloTableApp.new
  app.start
rescue => e
  puts "othello_table: #{e.message}"
end
