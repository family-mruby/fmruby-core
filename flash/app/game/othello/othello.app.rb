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
# - The connection is the machine's (FmrbAsterism.settings, as othello and
#   mahjong): the router, the ID, a credentials .tar, the room's seat in
#   the browser build. The player's name is the profile's (the ID without one).
# - The same game between two machines without a host is "Othello
#   (direct)", flash/app/test/othello.app.rb (this file copies its board's
#   drawing).
# - Keys. The list: up / down choose, Enter joins. At the table: arrows move
#   the cursor, Enter plays there (in the lobby: the owner starts), s sits or
#   stands, c puts the CPU in the empty seat (the owner), Esc leaves (asks
#   to resign first while playing a seat); y / n answer. A click on a square
#   plays there; the buttons take clicks.

class OthelloTableApp < FmrbApp
  APP = "game"            # this app's Asterism application name (the G0 range)
  HOST_APP = "games"      # the host's application
  CALL_TIMEOUT = 3.0
  RECONNECT_MS = 3000
  VIEW_EVERY_MS = 3000
  # Tables are looked for on on_join (for RESCAN_MS), else every LIST_EVERY_MS.
  LIST_EVERY_MS = 30000
  RESCAN_MS = 3000
  RESCAN_STEP_MS = 300
  # The host gone: a view now and then (should on_join not come).
  HOST_GONE_VIEW_MS = 30000
  HOST_LEFT = "ホストとのつながりが切れた"
  WIFI_WAIT_MS = 20000
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

  # The host's answers the screen shows.
  REASONS = {
    "token" => "卓の人ではない", "not seated" => "席に着いていない", "stale" => "盤が古い: 読み直し中",
    "illegal" => "そこには置けない", "not your turn" => "相手の番", "not playing" => "対局中ではない",
    "not waiting" => "対局中は席を変えられない", "taken" => "その席は埋まっている", "no seat" => "その席は無い",
    "not owner" => "卓の主だけができる", "seats" => "2 席がそろっていない", "no human" => "人が 1 人もいない",
    "no change" => "変わらない", "full" => "卓が満員", "spectators" => "観戦の人数が上限",
    "identity" => "この部屋での名乗りが合わない (部屋のリンクを開き直す)"
  }

  def on_create
    @gfx.set_font(:ja, 12)
    @started = Machine.board_millis
    st = ::FmrbAsterism.settings
    @id = st.node
    @locator = st.router
    @creds = st.credentials
    @creds_error = st.error
    @net = :connecting
    @net_note = ""
    @retry_at = 0
    @sub = nil
    @tables = {}          # "<host>/games/<name>" => [st, names] from its last put
    @table_list = []
    @list_sel = 0
    @scan_at = 0
    @rescan_until = 0     # look for tables until then (a node joined)
    @host_gone = false    # the host of this app's table left the network
    @gone_at = 0
    @table = nil          # the table this app is at
    @join_f = nil
    @cmd_f = nil
    @view_f = nil
    @token = nil
    @view = nil
    @view_at = 0
    @want_v = 0
    @deadline = nil
    @confirm = false
    @note = ""
    @cx = 3
    @cy = 3
    @keys = []
    @clicks = []
    @buttons = []
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

  def my_name
    pr = ::Asterism.profile
    n = pr ? pr["name"] : nil
    n && !n.to_s.empty? ? n.to_s : @id
  rescue
    @id
  end

  # ------------------------------------------------------------ network

  def network_ready?
    return true if FmrbConst::PLATFORM != "esp32"
    info = FmrbApp.wifi_info
    return true if info.nil? || info[:connected]
    Machine.board_millis - @started > WIFI_WAIT_MS
  end

  def connect
    begin
      ::Asterism.close
      raise ::Asterism::Error, @creds_error if @creds_error
      ::Asterism.connect(@locator, node: @id, app: APP, credentials: @creds)
      # Who is there (forgotten by Asterism.close, so given at each connect).
      ::Asterism.on_join { |n| node_joined(n) }
      ::Asterism.on_leave { |n| node_left(n) }
      @net = :up
      @net_note = ""
      @scan_at = 0
      @rescan_until = Machine.board_millis + RESCAN_MS
      subscribe_states
    rescue ::Asterism::Error => e
      @net = :down
      @net_note = e.message.to_s
      @retry_at = Machine.board_millis + RECONNECT_MS
    end
    Log.info("othello_table: #{@id} #{@net} (#{@locator}) #{@net_note}")
    @dirty = true
  end

  def lost(why)
    Log.info("othello_table: lost: #{why}")
    @net = :down
    @net_note = why
    @retry_at = Machine.board_millis + RECONNECT_MS
    @sub = nil
    @join_f = nil
    @cmd_f = nil
    @view_f = nil
    begin
      ::Asterism.close
    rescue
    end
    @dirty = true
  end

  def reconnecting?
    @net == :up && ::Asterism.respond_to?(:reconnecting?) && ::Asterism.reconnecting?
  rescue
    false
  end

  def exchange
    lost("asterism: #{::Asterism.lost_reason}") unless ::Asterism.poll
  rescue => e
    lost("#{e.class}: #{e.message}")
  end

  def proxy(path = @table)
    ::Asterism[path, timeout: CALL_TIMEOUT]
  end

  def othello?(p)
    p.split("/")[2].to_s.start_with?("othello")
  end

  # The othello tables of the hosts on the network (Asterism.each reads
  # the liveliness the app already follows: nothing is sent).
  def find_tables
    now = Machine.board_millis
    every = now < @rescan_until ? RESCAN_STEP_MS : LIST_EVERY_MS
    return if now - @scan_at < every
    @scan_at = now
    ::Asterism.each("*/#{HOST_APP}/*").each do |px|
      p = px.asterism_path
      add_table(p) if othello?(p)
    end
  rescue ::Asterism::Error => e
    Log.info("othello_table: scan: #{e.message}")
  end

  def add_table(p)
    return if @tables.key?(p)
    @tables[p] = nil
    @table_list = @tables.keys.sort
    Log.info("othello_table: table #{p} on the list")
    @dirty = true
  end

  def host_of(p)
    p.split("/")[0]
  end

  # on_leave: a host's tables leave the list; at its table: connection lost.
  def node_left(n)
    gone = @table_list.select { |p| host_of(p) == n }
    unless gone.empty?
      gone.each { |p| @tables.delete(p) }
      @table_list = @tables.keys.sort
      @list_sel = 0 if @list_sel >= @table_list.size
      Log.info("othello_table: #{n} left the network: #{gone.size} table(s) off the list")
      @dirty = true
    end
    if @table && host_of(@table) == n
      @host_gone = true
      @gone_at = Machine.board_millis
      @note = HOST_LEFT
      @dirty = true
    end
  end

  # on_join: look for tables now; this table's host back: ask the view.
  def node_joined(n)
    @rescan_until = Machine.board_millis + RESCAN_MS
    @scan_at = 0
    if @table && host_of(@table) == n && @host_gone
      @host_gone = false
      @note = ""
      @view_f = nil
      @view_at = 0
      Log.info("othello_table: #{n} is back")
      @dirty = true
    end
  end

  def subscribe_states
    @sub = ::Asterism.session.subscribe("asterism/*/#{HOST_APP}/*/state", depth: 8)
  rescue ::Asterism::Error => e
    Log.info("othello_table: subscribe: #{e.message}")
    @sub = nil
  end

  def watch_states
    return unless @sub
    @sub.each_pending do |key, payload, att|
      begin
        k = key.to_s
        next unless k.start_with?("asterism/") && k.end_with?("/state")
        p = k.byteslice(9, k.bytesize - 15)
        next unless othello?(p)
        pv = ::Asterism::Codec.unpack(payload)
        next unless pv.is_a?(Hash)
        add_table(p)
        @tables[p] = [pv["st"].to_s, pv["nm"].is_a?(Array) ? pv["nm"] : []]
        @dirty = true if @table.nil?
        if p == @table
          v = pv["v"].to_i
          @want_v = v if v > @want_v
          @view = pv if @token.nil?
        end
      rescue => e
        Log.info("othello_table: state: #{e.message}")
      end
    end
  end

  def join(p)
    return if @join_f || p.nil?
    @table = p
    @join_f = proxy(p).async.join(my_name)
    @note = ""
    @dirty = true
  rescue ::Asterism::Error => e
    @join_f = nil
    @table = nil
    @note = e.message.to_s
  end

  def collect_join
    f = @join_f
    return unless f && f.done?
    @join_f = nil
    begin
      case f.value
      in Hash => r if r["token"]
        @token = r["token"].to_s
        @view = nil
        @want_v = 0
        @confirm = false
        @host_gone = false
        ask_view
        Log.info("othello_table: joined #{@table} (#{f.took_ms} ms)")
      in Hash => r
        @note = reason(r["error"])
        @table = nil
      in r
        @note = reason(r)
        @table = nil
      end
    rescue ::Asterism::Error => e
      @note = "ホストが答えない"
      @table = nil
    end
    @dirty = true
  end

  def leave_table
    begin
      proxy.async.leave(@token) if @token
    rescue ::Asterism::Error => e
    end
    @table = nil
    @token = nil
    @view = nil
    @view_f = nil
    @cmd_f = nil
    @deadline = nil
    @confirm = false
    @host_gone = false
    @note = ""
    @dirty = true
  end

  def ask_view
    return if @view_f || @token.nil?
    @view_at = Machine.board_millis
    @view_f = proxy.async.view(@token)
  rescue ::Asterism::Error => e
    @view_f = nil
  end

  def collect_view
    f = @view_f
    return unless f && f.done?
    @view_f = nil
    f_sent = @view_at
    begin
      case f.value
      in Hash => v if v["error"]
        Log.info("othello_table: the host does not know us: back to the list")
        leave_table
        @note = "卓から外れた"
      in Hash => v
        @view = v
        dl = v["dl"]
        @deadline = dl ? Machine.board_millis + dl.to_i : nil
        @confirm = false unless v["st"] == "play"
        # Answered a view sent after the host left: it is back.
        @host_gone = false if @host_gone && f_sent > @gone_at
        @dirty = true
      else
      end
    rescue ::Asterism::Error => e
      @note = "ホストが答えない: 待っています"
      @dirty = true
    end
  end

  # One call at a time (sit, stand, cpu, start, move, resign); the view is
  # asked again when it is answered.
  def send_cmd(what)
    return if @cmd_f || @token.nil?
    tok = @token
    px = proxy.async
    @cmd_f = case what
             when :sit then px.sit(tok, -1)
             when :stand then px.stand(tok)
             when :cpu then px.cpu(tok, -1, true)
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
    @cmd_f = proxy.async.move(@token, v["n"].to_i, code)
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
    @view_f = nil
    ask_view
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
    @token && me >= 0 && me < 2
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
    if @table.nil?
      if sc == FmrbConst::KEY_UP
        @list_sel -= 1 if @list_sel > 0
      elsif sc == FmrbConst::KEY_DOWN
        @list_sel += 1 if @list_sel < @table_list.size - 1
      elsif enter
        join(@table_list[@list_sel])
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
    end
  end

  def button_at(x, y)
    i = 0
    while i < @buttons.size
      b = @buttons[i]
      return b[0] if x >= b[1] && x < b[1] + b[3] && y >= b[2] && y < b[2] + b[4]
      i += 1
    end
    nil
  end

  def run_click(c)
    x = c[0]
    y = c[1]
    @dirty = true
    b = button_at(x, y)
    return run_button(b) if b
    if @table.nil?
      row = (y - @list_y) / 15
      if y >= @list_y && row >= 0 && row < @table_list.size
        if row == @list_sel
          join(@table_list[row])
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
    when :join then join(@table_list[@list_sel])
    when :sit then send_cmd(:sit)
    when :stand then send_cmd(:stand)
    when :cpu then send_cmd(:cpu)
    when :start then send_cmd(:start)
    when :resign then @confirm = true
    when :yes then send_cmd(:resign)
    when :no then @confirm = false
    when :leave then leave_table
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

  # The width of s as drawn (a wide character counts 2 columns of 6 px).
  def text_w(s)
    w = 0
    i = 0
    while i < s.length
      w += s[i, 1].bytesize > 1 ? 12 : 6
      i += 1
    end
    w
  end

  # s cut to fit cols columns (a wide character counts 2).
  def fit(s, cols)
    s = s.to_s
    out = ""
    used = 0
    i = 0
    while i < s.length
      ch = s[i, 1]
      w = ch.bytesize > 1 ? 2 : 1
      break if used + w > cols
      out += ch
      used += w
      i += 1
    end
    out
  end

  def button(id, x, y, w, label)
    @buttons << [id, x, y, w, 16]
    @gfx.fill_round_rect(x, y, w, 16, 3, theme_accent)
    @gfx.draw_text(x + (w - text_w(label)) / 2, y + 2, label, theme_fg_light)
  end

  def text(x, y, s, color = nil)
    @gfx.draw_text(x, y, s, color || theme_fg)
  end

  def draw_screen
    @buttons = []
    clear_user_area
    if @table
      draw_board
      draw_panel
    else
      draw_list
    end
    @gfx.present
  end

  def net_line
    return "つないでいます: #{@locator}" if @net == :connecting
    return "網から切れた: つなぎ直し中" if @net == :down || reconnecting?
    "#{@id} (#{@locator})"
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
    cols = (@user_area_width - 8) / 6
    text(x, y, fit("オセロ (卓): 卓を選ぶ (Enter で入る)", cols))
    text(x, y + 15, fit(net_line, cols), theme_border)
    if @table_list.empty?
      text(x, @list_y, fit(@net == :up ? "ゲームホストを探しています (games/othello*)" : "-", cols), theme_border)
    end
    i = 0
    while i < @table_list.size && i < 8
      p = @table_list[i]
      t = @tables[p]
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
        text(x, yy, fit(line, cols), theme_fg_light)
      else
        text(x, yy, fit(line, cols))
      end
      i += 1
    end
    by = @y0 + @user_area_height - 40
    text(x, by - 2, fit(@note, cols), theme_border)
    button(:join, x, by + 16, 90, "入る[Enter]") unless @table_list.empty?
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

  def draw_panel
    x = @px
    y = @y0 + 3
    n = @pw / 6
    v = @view
    text(x, y, fit(@table.split("/")[2].to_s + (v ? "  " + status_word(v["st"]) : ""), n))
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
      tags += " (自分)" if i == me
      tags += " 途切れ" if (aw >> i) & 1 == 1
      label = name_of(i) + tags
      label = "CPU" + tags if sk[i] == 2 && name_of(i) == "(空き)"
      num = cnt ? " #{cnt[i]}" : ""
      color = v["tn"] == i ? theme_accent : theme_fg
      text(x + 14, yy, fit(label, n - 6) + num, color)
      i += 1
    end
    yy = y + 50
    st = v["st"]
    case st
    in "play"
      who = v["tn"] == me ? "あなたの番" : (v["tn"] == 0 ? "黒の番" : "白の番")
      text(x, yy, fit("#{who} (#{v['n'].to_i + 1} 手目)", n), v["tn"] == me ? C_CURSOR : theme_fg)
      if @deadline
        left = (@deadline - Machine.board_millis + 999) / 1000
        left = 0 if left < 0
        text(x, yy + 15, fit("残り #{left} 秒 (切れたら CPU が打つ)", n), theme_border)
      end
    in "over"
      res = v["res"].is_a?(Hash) ? v["res"] : {}
      w = res["w"].to_i
      word = w < 0 ? "引き分け" : "#{w == 0 ? '黒' : '白'} (#{name_of(w)}) の勝ち"
      word = "あなたの勝ち" if w >= 0 && w == me
      word = "あなたの負け" if w >= 0 && me >= 0 && me < 2 && w != me
      text(x, yy, fit("終局 #{res['b']}-#{res['wc']}#{res['why'] == 'resign' ? ' 投了' : ''}", n), theme_accent)
      text(x, yy + 15, fit(word, n), theme_accent)
    else
      text(x, yy, fit(seated? ? "席に着いた (色は始めるときに決まる)" : "観戦中: 空いた席に座れる", n), theme_border)
      text(x, yy + 15, fit("卓の主 (*) が始める", n), theme_border)
    end
    last = v["last"]
    text(x, yy + 30, fit("最後: #{last == '--' ? 'パス' : last}", n), theme_border) if last && st == "play"
    text(x, yy + 45, fit(@note, n), theme_border)
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
    button(:cpu, x + 66, by2, 60, "CPU[C]") if owner? && (sk[0] == 0 || sk[1] == 0)
    button(:start, x, by, 60, "始める") if owner? && sk[0] != 0 && sk[1] != 0
    button(:leave, x + 66, by, 60, "出る")
  end

  # ------------------------------------------------------------ the loop

  def on_update
    now = Machine.board_millis
    if @net == :connecting
      connect if network_ready?
    elsif @net == :down
      connect if now >= @retry_at
    end
    if @net == :up
      exchange
      if @net == :up
        subscribe_states if @sub.nil?
        watch_states
        find_tables
        if @table
          collect_join
          collect_cmd
          collect_view
          ask_view if @token && @view_f.nil? && view_due?(now)
        end
      end
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

  # A newer version, or the view that says this app is still there.
  def view_due?(now)
    return now - @view_at >= HOST_GONE_VIEW_MS if @host_gone
    @want_v > (@view ? @view["v"].to_i : 0) || now - @view_at >= VIEW_EVERY_MS
  end

  def on_destroy
    leave_table if @token
    ::Asterism.close
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
