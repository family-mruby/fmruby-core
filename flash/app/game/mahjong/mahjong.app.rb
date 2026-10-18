# Mahjong table (doc/ruby_asterism, MJ1-MJ2): the screen of a riichi mahjong
# table run by a game host (the asterism-games repository) on the same
# Zenoh network. Up to four players at a table, the other seats CPUs.
#
# - The host holds the wall and the hands and has one or more tables
#   (objects <host>/games/mahjong*). This app lists them (the host puts each
#   table's public state on asterism/<host>/games/<table>/state after every
#   change and every 5 s), joins one (join(name) -> a token) and waits in its
#   lobby: sit / stand, the table's owner fills empty seats with CPUs and
#   starts. In the game it asks its own view with the token (view(token, assist)) when
#   a put shows a newer version, and every VIEW_EVERY_MS anyway (which also
#   tells the host it is still there). An action is act(token, sv, type,
#   tiles): the host takes only what its list for this seat (the view's "a")
#   has. A member without a seat watches (the table without hands).
# - The connection is the machine's (FmrbAsterism.settings, as othello). A
#   line that drops or a host that restarts: the app keeps asking. Who is
#   there comes from Asterism (GS3): a host that leaves the network
#   (on_leave) takes its tables off the list and, at its table, shows the
#   connection lost and stops asking; when it is back (on_join) the app
#   looks for its tables again at once (and every LIST_EVERY_MS anyway,
#   should the word not come) and asks its view. When the
#   app starts again it asks each table mine() and marks in the list the
#   tables it has a seat at; Enter goes back to the seat (the same node gets
#   its token back). It does not go in by itself.
# - Modern (Tab5, P4-Nano), the Modern Linux sim and the browser build:
#   640x360 (fullscreen_hires). A smaller screen (426x240, where the mode is
#   not available) gets a compact table with the discards as text, and only
#   the river sheet.
# - The browser build at 1280x720 (MJ-L, doc/ruby_asterism/report/mj_layout.md)
#   gets a conventional table: the four rivers around a centre board, each
#   turned to its player, the opponents' hands as rows of tile sides, the
#   melds with the called tile sideways. It draws only what changed (each
#   tile is an item; a change clears and redraws the items that differ and
#   whatever a clear touched). The lists, the lobby and the status screens
#   are the 640x360 ones at twice the size. Its sheets are in large/, made
#   from the two sheets below by tools/mahjong_tilegen.rb --layout-sheets
#   (whole-number scaling and quarter turns; boards leave large/ out).
# - Retro (S3): its image leaves this app out (config/storage_exclude_naryav3.txt).
#   A Retro sim keeps the whole tree, so the app still says it has no table.
#
# The tiles are sprite sheets (8-bit BMPs whose pixel values are RGB332;
# made by tools/mahjong_tilegen.rb --sheets, then edited by hand: the sheets
# are the source of truth). Tile numbers (one byte on the wire):
#   0..8 1m..9m, 9 0m (red five), 10..18 1p..9p, 19 0p, 20..28 1s..9s,
#   29 0s, 30..36 1z..7z (E S W N white green red), 37 back.
# Both sheets are 10 x 4 cells without gaps, transparent colour RGB332 0xE3:
#   tiles_hand.bmp (420x224, cells of 42x56), tiles_river.bmp (240x128,
#   cells of 24x32); tile n is at src_x = (n % 10) * w, src_y = (n / 10) * h.
# The hand sheet is wider than 256 px, which Retro's sprite loader refuses
# (the Linux sim's takes it); it is loaded only for the big table, which a
# Retro screen never shows.
#
# Keys. The list of tables: up / down choose, Enter joins. The lobby: 1-4
# sit in that seat, s stand, c fill the empty seats with CPUs (the owner),
# Enter start (the owner), Esc leave the table, up / down choose a seat or
# a local rule (the seats come before the first rule) and left / right
# change it (the owner: a seat goes round empty, CPU and the AI players
# the host offers, as a click on its button does; a click on a rule steps
# it).
# The game: left / right choose
# a tile, Enter discards it (the drawn tile when none is chosen) or goes on
# after a result, Esc passes a call (a spectator: leaves); r riichi (then
# Enter: the chosen tile, or the first that declares), t tsumo / ron, p pon,
# c chi, k kan, n nine kinds. A click on a tile discards it; the buttons are
# what the host offers now. In the lobby and the game, a (or the help's
# button) steps the help level 0-3 (ASSIST_FILE keeps it on this machine).
# The list and the lobby: n edits the player's name (asked before the first
# join, kept in ASSIST_FILE); the lobby's rules have two pages (Tab). A
# spectator sees the four hands (MJ5: mahjong_watch.rb at 640x360).

# The 1280x720 table (mahjong_xl.rb) is read only when the screen is that
# big (load_xl_sheets in mahjong_panels.rb).
# The local rules, the result of a round (MJ3b), the 640x360 layout's parts.
require "/app/game/mahjong/mahjong_panels"
# The 640x360 spectators' screen (MJ5, mahjong_watch.rb) is read when it is
# first shown (watch_load): a seated player never needs it.

class MahjongApp < FmrbApp
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
  PROBE_EVERY_MS = 15000
  WIFI_WAIT_MS = 20000
  MEM_EVERY_MS = 30000
  APP_DIR = "/app/game/mahjong"
  XL_DIR = "large/"       # the 1280x720 table's sheets (under APP_DIR)
  PERF_ON = "/home/mahjong_perf.on"     # present: the times go to PERF_OUT
  PERF_OUT = "/home/mahjong_perf.txt"
  CACHE_DIR = "/cache/app/mahjong"
  KEY_COLOR = 0xE3        # the sheets' transparent colour
  # The table's own colours (a game draws its own picture).
  C_FELT = 0x0C
  C_TEXT = 0xFF
  C_DIM = 0xB6
  C_TURN = 0xFC
  C_RED = 0xE0
  C_BTN = 0x4E
  C_BOX = 0x04
  C_LAST = 0xFC
  C_SEL = 0x2D
  C_KEEP = 0x1F           # the help at level 3: a discard that keeps the least shanten (cyan)
  C_CHOSEN = 0x03         # the chosen hand tile's frame (FmrbGfx::BLUE)
  C_AWAY = 0xF4           # a seat whose player is away (orange: red does not read on the felt)
  # Action numbers (asterism-games Engine).
  A_DAHAI = 1
  A_RIICHI = 2
  A_TSUMO = 3
  A_ANKAN = 4
  A_KAKAN = 5
  A_PON = 6
  A_CHI = 7
  A_MINKAN = 8
  A_RON = 9
  A_SKIP = 10
  A_KYUUSHU = 11
  A_NEXT = 12
  # The screen's own buttons (not the host's actions).
  B_JOIN = 100
  B_SIT = 101
  B_STAND = 102
  B_CPU_ON = 103
  B_CPU_OFF = 104
  B_START = 105
  B_LEAVE = 106
  B_ASSIST = 107
  B_NAME = 110
  # A seat's button in the lobby (the owner): [seat]; it goes round empty,
  # CPU and the AI players offered (mahjong_panels.rb seat_step).
  B_SEAT = 111
  B_RECORDS = 112         # the recent games (MJ6, mahjong_replay.rb)
  # The help this screen asks the host for (asterism-games README "Assist"):
  # 0 none, 1 the riichi discards and their waits, 2 every discard that
  # leaves the hand tenpai, 3 as 2 and else the shanten and the acceptance.
  # The player's own choice, kept in ASSIST_FILE on this machine.
  ASSIST_FILE = "/home/mahjong.toml"
  ASSIST_DEFAULT = 1
  ASSIST_MAX = 3

  # The words on the screen are Japanese whatever the machine's language
  # (mj_layout.md 8): a riichi table speaks the game's own words (リーチ,
  # ポン, 九種九牌), and the app keeps its table to itself (FmrbI18n and the
  # other apps are left as they are).
  def setup_strings
    @str = {
      mj_title: "ヒデ雀", mj_connecting: "つないでいます", mj_searching: "ゲームホストを探しています (games/mahjong*)",
      mj_joining: "卓に入っています", mj_full: "卓が満員", mj_lost: "網から切れた: つなぎ直し中",
      mj_host_away: "ホストが答えない: 待っています",
      mj_host_left: "ホストとのつながりが切れた", mj_retro: "この卓は Modern とブラウザ版で動きます",
      mj_left: "残り", mj_riichi: "リーチ", mj_tsumo: "ツモ", mj_ron: "ロン", mj_pon: "ポン", mj_chi: "チー",
      mj_kan: "カン", mj_pass: "パス", mj_kyuushu: "九種九牌", mj_next: "次へ", mj_pick: "リーチ: 切る牌を選ぶ (Esc でやめる)",
      mj_draw: "流局", mj_tenpai: "テンパイ", mj_over: "終局", mj_you: "自分",
      mj_rest: "秒", mj_honba: "本場", mj_sticks: "供託", mj_dora: "ドラ", mj_kyoku: "局",
      mj_kyuushu_end: "九種九牌で流局", mj_sanchahou: "三家和で流局",
      mj_rank: "位", mj_from: "から", mj_records: "直近の結果 H",
      mj_tables: "卓 (Enter で入る)", mj_mine: "[自分の席あり]", mj_seat: "席", mj_empty: "空き",
      mj_sit: "座る", mj_stand: "立つ S", mj_cpu: "CPU", mj_fill: "CPU で埋める C",
      mj_start: "始める Enter", mj_leave: "卓を出る Esc", mj_owner: "卓の主", mj_away: "途切れ",
      mj_watchers: "観戦:", mj_lobby: "待ち合わせ", mj_playing: "対局中", mj_ended: "終局",
      mj_humans: "参加", mj_cpus: "CPU", mj_free: "空き", mj_members: "人", mj_ai: "AI",
      mj_watching: "観戦中 (Esc で出る)", mj_others: "ほかの人の答えを待っています",
      mj_owner_starts: "4 席がそろったら卓の主が始める",
      mj_back_lobby: "次へ: 卓の主が待ち合わせに戻す",
      mj_waits: "待ち", mj_total: "計", mj_mai: "枚", mj_help: "の牌を切るとテンパイ",
      mj_assist: "アシスト", mj_shanten: "向聴", mj_ukeire: "受け入れ", mj_kinds: "種",
      mj_keep: "の牌は向聴数が変わらない", mj_no_wait: "テンパイ (待ちの牌が残っていない)"
    }
    @assist_names = ["なし", "リーチ", "テンパイ", "向聴"]
    @assist_notes = ["アシストを出さない", "リーチのとき、切る牌の印と待ちを出す",
                     "テンパイになる打牌に印と待ちを出す (ダマ・鳴いた手も)",
                     "テンパイの印に加え、向聴数と打牌ごとの受け入れを出す"]
    # The host's answers (act, sit, ...) that the screen shows.
    @reasons = {
      "token" => "卓の人ではない", "not seated" => "席に着いていない", "stale" => "画面が古い: 読み直し中",
      "not offered" => "今はできない", "not playing" => "対局中ではない", "not waiting" => "待ち合わせ中ではない",
      "taken" => "その席は埋まっている", "no seat" => "その席は無い", "humans" => "人の席がもう無い",
      "not owner" => "卓の主だけができる", "seats" => "4 席がそろっていない", "no human" => "人が 1 人もいない",
      "no change" => "変わらない", "full" => "卓が満員", "spectators" => "観戦の人数が上限",
      "value" => "選べない値", "no rule" => "その項目は無い", "no ai" => "その AI は今は出せない (鍵か 1 日の上限)",
      "identity" => "この部屋での名乗りが合わない (部屋のリンクを開き直す)"
    }
    setup_rule_strings
  end

  def t(k)
    @str[k] || k.to_s
  end

  # A reply of the host as the screen says it.
  def reason(r)
    s = r.to_s
    @reasons[s] || s
  end

  # ------------------------------------------------------------ start

  def on_create
    setup_strings
    @gfx.set_font(:ja, 12)
    @started = Machine.board_millis
    @dirty = true
    @keys = []
    @clicks = []
    @buttons = []
    @unsupported = FmrbConst::HW_FAMILY != "modern"
    @hand_sheet = nil
    @river_sheet = nil
    @xl_sheets = false
    @turned = nil           # the small tiles in four turns (load_turned_sheets)
    @xl_items = nil         # the 1280x720 table's drawn items (nil: draw it all)
    @fw = 12                # the full-width character width of the text drawn now
    @perf_on = perf_wanted?
    @perf = []
    @xl_n_items = 0         # what the last drawing of the 1280x720 table drew
    @xl_n_px = 0
    layout
    if @unsupported
      draw_screen
      return
    end
    @river_sheet = load_sheet("tiles_river.bmp", 240, 128)
    load_hand_sheet
    load_xl_sheets
    st = ::FmrbAsterism.settings
    @id = st.node
    @locator = st.router
    @creds = st.credentials
    @creds_error = st.error
    @assist = load_assist
    @name_machine = st.name.to_s       # config.toml's name: what the first name prompt starts from
    @name = @name_set || (@name_machine.empty? ? @id : @name_machine) # shown; join waits for @name_set
    @pending_join = nil
    @name_edit = nil      # the name being typed in the lobby (nil: not editing)
    @rename_f = nil
    @net = :connecting
    @net_note = ""
    @retry_at = 0
    @tables = {}          # "<host>/games/<name>" => its last public state (or {})
    @table_list = []
    @list_sel = 0
    @probed = []          # tables asked mine()
    @mine = {}            # tables where this node has a seat (marked in the list)
    @probe_f = nil
    @probe_path = nil
    @probe_at = Machine.board_millis
    @scan_at = 0
    @rescan_until = 0     # look for tables until then (a node joined)
    @host_gone = false    # the host of this app's table left the network
    @gone_at = 0
    @sub = nil
    @table = nil          # the table this app is at
    @join_f = nil
    @token = nil
    @seat = nil           # nil: no seat (the lobby or watching)
    @view_f = nil
    @act_f = nil
    @cmd_f = nil
    @view = nil
    @view_at = 0
    @want_v = 0           # a put said there is this version
    @host_away = false
    @note = ""
    @sel = -1             # the chosen hand tile (lifted): by the keys or under the mouse
    @mouse_sel = false    # @sel follows the mouse (moving off the hand drops it)
    @move = nil           # the last mouse position not yet looked at
    @waits = {}           # the help: discard tile => [[wait, left], ...] (the view's "wt")
    @uk = {}              # the help (level 3): discard tile => [shanten, [[tile, left], ...]] (the view's "uk")
    @uk_min = 9           # the least shanten after a discard (the tiles marked)
    @hand_redraw = nil    # the 640x360 table: only these hand tiles changed
    @pick = false         # choosing the riichi tile
    @act_t0 = nil         # when the last action was sent (for the latency)
    @act_sv = 0
    @lat = nil
    @deadline = nil
    @bank_ms = 0          # the bank's ms in the time left of the own move (README "Time")
    @others = false       # the host waits for other seats' calls
    @second = 0
    @mem_at = 0
    @title_until = Machine.board_millis + TITLE_MS # the title (MJ8) while the network comes up
    draw_screen
  end

  def on_resize(w, h)
    layout
    unless @unsupported
      load_hand_sheet
      load_xl_sheets
    end
    @xl_items = nil
    @dirty = true
  end

  def layout
    @x0 = @user_area_x0
    @y0 = @user_area_y0
    @w = @user_area_width
    @h = @user_area_height
    @big = @w >= 640 && @h >= 360
    @xl = @w >= 1280 && @h >= 720
    @k = @xl ? 2 : 1        # the lists and the lobby: the 640x360 screens at twice the size
  end

  # A sheet as a SpriteImage, or nil (the tiles are then drawn as text).
  # dir: a directory under APP_DIR (the cache keeps the file name only).
  def load_sheet(name, w, h, dir = "")
    t0 = Machine.board_millis
    @gfx.sync_file("#{APP_DIR}/#{dir}#{name}", dest: "#{CACHE_DIR}/#{name}")
    img = SpriteImage.new(@gfx, width: w, height: h, transparent_color: KEY_COLOR, use_transparent: true)
    img.load_bmp("#{CACHE_DIR}/#{name}")
    Log.info("mahjong: tiles: #{name} #{Machine.board_millis - t0} ms")
    img
  rescue => e
    Log.error("mahjong: tiles: #{name}: #{e.message}")
    nil
  end

  # The hand sheet and mahjong_640.rb, the first time the table is big (the
  # 640x360 table).
  def load_hand_sheet
    return unless @big && !@xl
    m640_load
    @hand_sheet = load_sheet("tiles_hand.bmp", 420, 224) if @hand_sheet.nil?
  end

  # ------------------------------------------------------------ network

  def uptime_ms
    Machine.board_millis - @started
  end

  def network_ready?
    return true if FmrbConst::PLATFORM != "esp32"
    info = FmrbApp.wifi_info
    return true if info.nil? || info[:connected]
    uptime_ms > WIFI_WAIT_MS
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
    Log.info("mahjong: #{@id} #{@net} (#{@locator}) #{@net_note}")
    @dirty = true
  end

  def lost(why)
    Log.info("mahjong: lost: #{why}")
    @net = :down
    @net_note = why
    @retry_at = Machine.board_millis + RECONNECT_MS
    @sub = nil
    @join_f = nil
    @view_f = nil
    @act_f = nil
    @cmd_f = nil
    @probe_f = nil
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

  # The tables of the hosts on the network (Asterism.each reads the
  # liveliness the app already follows: nothing is sent).
  def find_tables
    now = Machine.board_millis
    every = now < @rescan_until ? RESCAN_STEP_MS : LIST_EVERY_MS
    return if now - @scan_at < every
    @scan_at = now
    ::Asterism.each("*/#{HOST_APP}/*").each do |px|
      p = px.asterism_path
      o = p.split("/")[2].to_s
      add_table(p) if o.start_with?("mahjong")
    end
  rescue ::Asterism::Error => e
    Log.info("mahjong: scan: #{e.message}")
  end

  def add_table(p)
    return if @tables.key?(p)
    @tables[p] = {}
    @table_list = @tables.keys.sort
    Log.info("mahjong: table #{p} on the list")
    @dirty = true
  end

  def host_of(p)
    p.split("/")[0]
  end

  # on_leave: a host's tables leave the list; at its table: connection lost.
  def node_left(n)
    gone = @table_list.select { |p| host_of(p) == n }
    unless gone.empty?
      gone.each do |p|
        @tables.delete(p)
        @mine.delete(p)
        @probed.delete(p)
      end
      @table_list = @tables.keys.sort
      @list_sel = 0 if @list_sel >= @table_list.size
      Log.info("mahjong: #{n} left the network: #{gone.size} table(s) off the list")
      @dirty = true
    end
    if @table && host_of(@table) == n
      @host_gone = true
      @gone_at = Machine.board_millis
      @dirty = true
    end
  end

  # on_join: look for tables now; this table's host back: ask the view.
  def node_joined(n)
    @rescan_until = Machine.board_millis + RESCAN_MS
    @scan_at = 0
    if @table && host_of(@table) == n && @host_gone
      @host_gone = false
      @host_away = false
      @view_f = nil
      @view_at = 0
      Log.info("mahjong: #{n} is back")
      @dirty = true
    end
  end

  # Every table's state, one subscription.
  def subscribe_states
    @sub = ::Asterism.session.subscribe("asterism/*/#{HOST_APP}/*/state", depth: 8)
  rescue ::Asterism::Error => e
    Log.info("mahjong: subscribe: #{e.message}")
    @sub = nil
  end

  def watch_states
    return unless @sub
    @sub.each_pending do |key, payload, att|
      begin
        k = key.to_s
        next unless k.start_with?("asterism/") && k.end_with?("/state")
        p = k.byteslice(9, k.bytesize - 15)
        o = p.split("/")[2].to_s
        next unless o.start_with?("mahjong")
        pv = ::Asterism::Codec.unpack(payload)
        next unless pv.is_a?(Hash)
        add_table(p)
        @tables[p] = { "st" => pv["st"], "sk" => pv["sk"], "mb" => (pv["mb"].is_a?(Array) ? pv["mb"].size : 0) }
        @dirty = true if @table.nil?
        if p == @table
          v = pv["v"].to_i
          @want_v = v if v > @want_v
          @host_away = false
          @view = pv if @token.nil?
        end
      rescue => e
        Log.info("mahjong: state: #{e.message}")
      end
    end
  end

  # A table found for the first time (and every table again every
  # PROBE_EVERY_MS while the list is shown): was this node at it (the app
  # started again, or the host seated it)? Then the list marks it; Enter
  # goes back to the seat (join gives the same node its token back). The
  # app does not go in by itself, so a player who wants to watch another
  # table is not taken back to its own.
  def probe_tables
    if @probe_f
      return unless @probe_f.done?
      f = @probe_f
      p = @probe_path
      @probe_f = nil
      begin
        r = f.value
        had = @mine[p]
        if r.is_a?(Hash) && r["token"]
          @mine[p] = true
        else
          @mine.delete(p)
        end
        @dirty = true if had != @mine[p]
      rescue ::Asterism::Error => e
        @probed.delete(p) # ask again later
      end
      return
    end
    now = Machine.board_millis
    if now - @probe_at >= PROBE_EVERY_MS
      @probe_at = now
      @probed = []
    end
    i = 0
    while i < @table_list.size
      p = @table_list[i]
      if @probed.index(p).nil?
        @probed << p
        @probe_path = p
        @probe_f = proxy(p).async.mine
        return
      end
      i += 1
    end
  rescue ::Asterism::Error => e
    @probe_f = nil
  end

  def join(p)
    return if @join_f
    # A name first (MJ5): the table shows it; asked once, kept on this machine.
    unless @name_set
      @pending_join = p
      name_begin
      return
    end
    @table = p
    @join_f = proxy(p).async.join(@name)
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
      r = f.value
      if r.is_a?(Hash) && r["token"]
        enter_table(@table, r)
        Log.info("mahjong: joined #{@table} (#{f.took_ms} ms)")
      else
        e = r.is_a?(Hash) ? r["error"] : nil
        @note = e == "full" ? "#{t(:mj_full)}: #{@table}" : reason(e || r)
        @table = nil
      end
    rescue ::Asterism::Error => e
      @note = t(:mj_host_away)
      @table = nil
    end
    @dirty = true
  end

  def enter_table(p, r)
    @table = p
    @token = r["token"].to_s
    s = r["seat"].to_i
    @seat = s >= 0 ? s : nil
    @view = nil
    @want_v = 0
    @host_away = false
    @host_gone = false
    @note = ""
    ask_view
    @dirty = true
  end

  # Back to the list of tables.
  def leave_table
    begin
      proxy.async.leave(@token) if @token
    rescue ::Asterism::Error => e
    end
    @table = nil
    @token = nil
    @seat = nil
    @view = nil
    @view_f = nil
    @act_f = nil
    @cmd_f = nil
    @host_gone = false
    @note = ""
    @dirty = true
  end

  def ask_view
    return if @view_f || @token.nil?
    @view_at = Machine.board_millis
    # The help level always goes with the call (asterism-games README "Assist").
    @view_f = proxy.async.view(@token, @assist)
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
        # The host lost our membership (a new state): back to the list.
        Log.info("mahjong: the host does not know us: back to the list")
        @token = nil
        @table = nil
        @seat = nil
        @view = nil
        @probed = []
        @mine = {}
        @dirty = true
      in Hash => v
        take_view(v)
      else
      end
      @host_away = false
      # Answered a view sent after the host left: it is back.
      @host_gone = false if @host_gone && f_sent > @gone_at
    rescue ::Asterism::Error => e
      @host_away = true
      Log.info("mahjong: view: #{e.class}")
      @dirty = true
    end
  end

  def take_view(v)
    old = @view
    @view = v
    @want_v = v["v"].to_i if v["v"].to_i > @want_v
    me = v["me"]
    @seat = me.is_a?(Integer) && me >= 0 ? me : nil
    # dl: the seats whose time runs (bits), the ms left, the bank's ms.
    # (nil first: the app's VM calls deconstruct on whatever it is given,
    # so an array pattern must not meet a nil.)
    bits, left, bank = case v["dl"]
                       in nil then [0, 0, 0]
                       in [Integer => b, Integer => l, Integer => k, *] then [b, l, k]
                       in [Integer => b, Integer => l] then [b, l, 0]
                       else [0, 0, 0]
                       end
    mine = @seat ? (1 << @seat) : 0
    @deadline = bits & mine != 0 ? Machine.board_millis + left : nil
    @bank_ms = @deadline ? bank : 0
    # In the calls, the screen waits for the others whenever this seat has
    # nothing (more) to answer; who they are it is not told (a host before
    # MJ5 gives their bits, this one the three others' alike).
    @others = bits & mine == 0
    @pick = false unless offer(A_RIICHI)
    @waits = parse_waits(v["wt"])
    parse_uk(v["uk"])
    n = hand_codes.size
    @sel = -1 if @sel >= n
    if @act_t0 && v["sv"].to_i > @act_sv
      @lat_pending = Machine.board_millis - @act_t0
      @act_t0 = nil
    end
    took_view(v, old)
    @dirty = true if old.nil? || old["v"] != v["v"]
  end

  # The view's "wt" (asterism-games README "Views"): per riichi discard the
  # tile, the number of waits n and n pairs [wait, left].
  def parse_waits(s)
    out = {}
    b = bytes_of(s)
    i = 0
    while i + 1 < b.size
      n = b[i + 1]
      list = []
      j = 0
      while j < n
        list << [b[i + 2 + j * 2], b[i + 3 + j * 2]]
        j += 1
      end
      out[b[i]] = list
      i += 2 + n * 2
    end
    out
  end

  # The view's "uk": per discard the tile, the shanten after it, n and n
  # pairs [tile, left] (the tiles that bring the shanten down).
  def parse_uk(s)
    @uk = {}
    @uk_min = 9
    b = bytes_of(s)
    i = 0
    while i + 2 < b.size
      n = b[i + 2]
      list = []
      j = 0
      while j < n
        list << [b[i + 3 + j * 2], b[i + 4 + j * 2]]
        j += 1
      end
      @uk[b[i]] = [b[i + 1], list]
      @uk_min = b[i + 1] if b[i + 1] < @uk_min
      i += 3 + n * 2
    end
  end

  # The acceptance after discarding the chosen tile, [shanten, list] or nil.
  def sel_uk
    return nil if @uk.empty? || @sel < 0
    c = hand_at(@sel)
    c ? @uk[c] : nil
  end

  # The mark of hand tile c: :tenpai (a discard that leaves it tenpai),
  # :keep (level 3: one that keeps the least shanten) or nil.
  def help_mark(c)
    return :tenpai if @waits[c]
    u = @uk[c]
    u && u[0] == @uk_min ? :keep : nil
  end

  # ------------------------------------------------------------ the help level

  # ASSIST_FILE: "assist = N" and the player's name ("name = \"...\"",
  # mahjong_panels.rb), each on a line of its own. Sets @name_set too.
  def load_assist
    lv = ASSIST_DEFAULT
    @name_set = nil
    File.open(ASSIST_FILE, "r") do |f|
      f.read.to_s.split("\n").each do |l|
        i = l.index("=")
        next unless i
        k = l[0, i].strip
        v = l[i + 1, l.size].to_s.strip
        lv = v.to_i if k == "assist"
        if k == "name" && v.size >= 2 && v[0] == "\"" && v[v.size - 1] == "\""
          n = clean_player_name(v[1, v.size - 2])
          @name_set = n unless n.empty?
        end
      end
    end
    lv < 0 || lv > ASSIST_MAX ? ASSIST_DEFAULT : lv
  rescue
    ASSIST_DEFAULT
  end

  def save_assist
    f = File.open(ASSIST_FILE, "w")
    f.write("# The mahjong table's help (0 none, 1 riichi, 2 tenpai, 3 shanten)\nassist = #{@assist}\n")
    f.write("# The name shown at the table (none: the machine's)\nname = \"#{@name_set}\"\n") if @name_set
    f.close
  rescue => e
    Log.info("mahjong: #{ASSIST_FILE}: #{e.message}")
  end

  # The next level (3 goes back to 0); the view is asked again at once.
  def cycle_assist
    @assist = @assist >= ASSIST_MAX ? 0 : @assist + 1
    save_assist
    @waits = {}
    @uk = {}
    @uk_min = 9
    @view_at = 0
    @want_v = (@view ? @view["v"].to_i : 0) + 1 if @token
    @xl_items = nil
    @dirty = true
    Log.info("mahjong: assist #{@assist}")
  end

  def assist_label
    "#{t(:mj_assist)}: #{@assist_names[@assist]} A"
  end

  # The waits after discarding the chosen tile (nil: no riichi help for it).
  def sel_waits
    return nil if @waits.empty? || @sel < 0
    c = hand_at(@sel)
    c ? @waits[c] : nil
  end

  # Sends an action; tiles are tile numbers.
  def act(type, tiles)
    return if @act_f || @token.nil? || @view.nil?
    s = ("\x00" * tiles.size).b
    i = 0
    while i < tiles.size
      s.setbyte(i, tiles[i])
      i += 1
    end
    @act_t0 = Machine.board_millis
    @act_type = type
    @act_sv = @view["sv"].to_i
    @act_f = proxy.async.act(@token, @act_sv, type, s)
    @pick = false
  rescue ::Asterism::Error => e
    @act_f = nil
    @note = e.message.to_s
    @dirty = true
  end

  def collect_act
    f = @act_f
    return unless f && f.done?
    @act_f = nil
    begin
      r = f.value
      if r == "ok"
        ask_view
      else
        @note = reason(r)
        @act_t0 = nil
        ask_view
        @dirty = true
      end
    rescue ::Asterism::Error => e
      @host_away = true
      @act_t0 = nil
      @dirty = true
    end
  end

  # A lobby call: sit / stand / cpu / start; ai (a kind of "ak") puts that
  # AI player in the seat (in place of the CPU or AI player there).
  def lobby(kind, seat, ai = nil)
    return if @cmd_f || @token.nil?
    px = proxy
    @cmd_f = if kind == B_SIT then px.async.sit(@token, seat)
             elsif kind == B_STAND then px.async.stand(@token)
             elsif kind == B_CPU_ON then px.async.cpu(@token, seat, ai || true)
             elsif kind == B_CPU_OFF then px.async.cpu(@token, seat, false)
             else px.async.start(@token)
             end
  rescue ::Asterism::Error => e
    @cmd_f = nil
    @note = e.message.to_s
    @dirty = true
  end

  def collect_cmd
    f = @cmd_f
    return unless f && f.done?
    @cmd_f = nil
    begin
      r = f.value
      @note = r == "ok" ? "" : reason(r)
    rescue ::Asterism::Error => e
      @host_away = true
    end
    ask_view
    @dirty = true
  end

  # ------------------------------------------------------------ the view

  def bytes_of(s)
    out = []
    return out unless s.is_a?(String)
    i = 0
    n = s.bytesize
    while i < n
      out << s.getbyte(i)
      i += 1
    end
    out
  end

  def hand_codes
    @view && @seat ? bytes_of(@view["h"]) : []
  end

  def drawn
    d = @view && @seat ? @view["dt"] : nil
    d.is_a?(Integer) ? d : -1
  end

  # The offer [type, tiles] of a type, or nil.
  def offer(type)
    a = @view ? @view["a"] : nil
    return nil unless a.is_a?(Array)
    i = 0
    while i < a.size
      return a[i] if a[i].is_a?(Array) && a[i][0] == type
      i += 1
    end
    nil
  end

  # Whether tile code c may be discarded now (or chosen for riichi).
  def can_discard?(c)
    o = offer(@pick ? A_RIICHI : A_DAHAI)
    return false unless o
    bytes_of(o[1]).index(c) ? true : false
  end

  def discard(c)
    return unless can_discard?(c)
    act(@pick ? A_RIICHI : A_DAHAI, [c])
  end

  # The hand tile at index i (the drawn tile is the last).
  def hand_at(i)
    h = hand_codes
    return h[i] if i < h.size
    i == h.size && drawn >= 0 ? drawn : nil
  end

  def rel(seat)
    (seat - (@seat || 0)) % 4
  end

  WIND = ["東", "南", "西", "北"]

  def wind_of(seat)
    r = @view ? @view["r"] : nil
    oya = r.is_a?(Array) && r.size > 4 ? r[4].to_i : 0
    WIND[(seat - oya) % 4]
  end

  def tile_text(c)
    return "--" if c.nil? || c < 0
    return "##" if c >= 37
    return ["東", "南", "西", "北", "白", "發", "中"][c - 30] if c >= 30
    n = c % 10
    s = ["m", "p", "s"][c / 10]
    n == 9 ? "0#{s}" : "#{n + 1}#{s}"
  end

  # The screen: :status (connecting, joining), :list, :lobby, :table.
  def mode
    return :status if @net != :up && @table.nil?
    return :list if @table.nil?
    return :status if @token.nil? || @view.nil?
    @view["st"] == "wait" ? :lobby : :table
  end

  # The host waits for calls on a discard (r[7] == 2).
  def calls_phase?
    r = @view ? @view["r"] : nil
    r.is_a?(Array) && r.size > 7 && r[7] == 2
  end

  def owner?
    @view && @seat && @view["ow"] == @seat
  end

  def seat_kinds
    sk = @view ? @view["sk"] : nil
    sk.is_a?(Array) ? sk : [0, 0, 0, 0]
  end

  def away?(seat)
    @view && (@view["aw"].to_i >> seat) & 1 == 1
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    return if title_skip(t)
    if t == :key_down
      @keys << [ev[:scancode], ev[:character] || 0]
    elsif t == :mouse_up && ev[:button] == 1
      @clicks << [ev[:x], ev[:y]]
    elsif t == :mouse_move
      @move = [ev[:x], ev[:y]]
    end
  end

  def run_key(k)
    m = mode
    if m == :list
      list_key(k)
    elsif m == :lobby
      lobby_key(k)
    elsif m == :table
      if @seat.nil?
        leave_table if k[0] == FmrbConst::KEY_ESC || k[1] == 27
      else
        table_key(k)
      end
    end
  end

  def enter?(k)
    k[0] == FmrbConst::KEY_ENTER || k[1] == 13 || k[1] == 10
  end

  def list_key(k)
    return name_key(k) if @name_edit
    ch = k[1]
    return name_begin if ch == 110 || ch == 78
    n = @table_list.size
    return if n == 0
    return rp_open(@table_list[@list_sel % n]) if ch == 104 || ch == 72
    if k[0] == FmrbConst::KEY_UP
      @list_sel = (@list_sel - 1) % n
      @dirty = true
    elsif k[0] == FmrbConst::KEY_DOWN
      @list_sel = (@list_sel + 1) % n
      @dirty = true
    elsif enter?(k)
      join(@table_list[@list_sel % n])
    end
  end

  def lobby_key(k)
    return name_key(k) if @name_edit
    ch = k[1]
    ch += 32 if ch >= 65 && ch <= 90
    return if seat_key(k) || rule_key(k)
    if k[0] == FmrbConst::KEY_ESC || ch == 27
      leave_table
    elsif enter?(k)
      lobby(B_START, -1) if owner?
    elsif ch >= 49 && ch <= 52
      lobby(B_SIT, ch - 49)
    elsif ch == 115
      lobby(B_STAND, -1)
    elsif ch == 99
      lobby(B_CPU_ON, -1) if owner?
    elsif ch == 97
      cycle_assist
    elsif ch == 110
      name_begin
    elsif ch == 104
      rp_open(@table)
    end
  end

  # The recent games and their records (MJ6): a file of its own, read
  # only when opened (the game itself never loads it).
  def rp_open(p)
    return unless p
    require "#{APP_DIR}/mahjong_replay"
    watch_load
    rp_start(p)
  end

  def table_key(k)
    sc = k[0]
    ch = k[1]
    n = hand_codes.size + (drawn >= 0 ? 1 : 0)
    if sc == FmrbConst::KEY_LEFT
      choose(@sel <= 0 ? n - 1 : @sel - 1, false)
    elsif sc == FmrbConst::KEY_RIGHT
      choose(@sel >= n - 1 ? 0 : @sel + 1, false)
    elsif enter?(k)
      if offer(A_NEXT)
        act(A_NEXT, [])
      else
        c = hand_at(@sel < 0 ? n - 1 : @sel)
        if @pick && !can_discard?(c)
          o = offer(A_RIICHI)
          c = o ? o[1].getbyte(0) : nil
        end
        discard(c) if c
      end
    elsif sc == FmrbConst::KEY_ESC || ch == 27
      if @pick
        @pick = false
        @dirty = true
      elsif offer(A_SKIP)
        act(A_SKIP, [])
      end
    else
      shortcut(ch)
    end
  end

  # Letters for the buttons: r riichi, t tsumo / ron, p pon, c chi (the
  # first), k kan (the first), n nine kinds.
  def shortcut(ch)
    ch += 32 if ch >= 65 && ch <= 90
    return cycle_assist if ch == 97
    types = if ch == 114 then [A_RIICHI]
            elsif ch == 116 then [A_TSUMO, A_RON]
            elsif ch == 112 then [A_PON]
            elsif ch == 99 then [A_CHI]
            elsif ch == 107 then [A_ANKAN, A_KAKAN, A_MINKAN]
            elsif ch == 110 then [A_KYUUSHU]
            else []
            end
    i = 0
    while i < types.size
      o = offer(types[i])
      if o
        run_button(o[0], bytes_of(o[1]))
        return
      end
      i += 1
    end
  end

  # The hand tile under the mouse is chosen (lifted) while the mouse is on
  # it; off the hand, a tile the mouse chose is let go (one the keys chose
  # stays).
  def run_move(m)
    return unless mode == :table && @seat
    idx = @xl ? xl_hand_index_at(m[0], m[1]) : hand_index_at(m[0], m[1])
    if idx
      choose(idx, true)
    elsif @mouse_sel && @sel >= 0
      choose(-1, true)
    end
  end

  # Chooses hand tile i (-1: none). The 1280x720 table redraws what changed
  # anyway; the 640x360 one redraws the two tiles alone unless the help line
  # (the waits or the acceptance) changes too.
  def choose(i, mouse)
    @mouse_sel = mouse
    return if i == @sel || (i >= 0 && answering_call?)
    old = @sel
    ow = [sel_waits, sel_uk]
    @sel = i
    if @xl || !@big
      @dirty = true
    elsif ow != [sel_waits, sel_uk] || @dirty || marks_differ?(old, i)
      @dirty = true
    else
      @hand_redraw = [] if @hand_redraw.nil?
      @hand_redraw << old if old >= 0
      @hand_redraw << i if i >= 0
    end
  end

  def run_click(c)
    x = c[0]
    y = c[1]
    i = 0
    while i < @buttons.size
      b = @buttons[i]
      if x >= b[1] && x < b[1] + b[3] && y >= b[2] && y < b[2] + b[4]
        run_button(b[0], b[5])
        return
      end
      i += 1
    end
    return unless mode == :table && @seat
    idx = @xl ? xl_hand_index_at(x, y) : hand_index_at(x, y)
    return if idx.nil?
    choose(idx, true)
    cd = hand_at(idx)
    discard(cd) if cd
    @dirty = true
  end

  def run_button(type, tiles)
    if type == A_RIICHI
      @pick = !@pick
      @dirty = true
    elsif type == B_JOIN
      join(@table_list[tiles[0]]) if tiles[0] < @table_list.size
    elsif type == B_LEAVE
      leave_table
    elsif type == B_ASSIST
      cycle_assist
    elsif type == B_NAME
      @name_edit ? name_done : name_begin
    elsif type == B_RULE
      @rule_sel = tiles[0]
      @seat_sel = nil
      rule_step(tiles[0], 1)
    elsif type == B_RULE_PAGE
      rule_flip(tiles[0])
    elsif type == B_SEAT
      @seat_sel = tiles[0]
      seat_step(tiles[0], 1)
    elsif type == B_RECORDS
      rp_open(@table || @table_list[tiles[0]])
    elsif type >= B_SIT
      lobby(type, tiles.empty? ? -1 : tiles[0])
    else
      act(type, tiles)
    end
  end

  # ------------------------------------------------------------ drawing

  def tile_w
    @big ? 42 : 24
  end

  def tile_h
    @big ? 56 : 32
  end

  def hand_y
    @y0 + @h - tile_h - 4
  end

  def hand_x(i)
    x = @x0 + 6 + i * tile_w
    x += 8 if i >= hand_codes.size
    x
  end

  def hand_index_at(x, y)
    return nil if y < hand_y - 8 || y >= hand_y + tile_h
    n = hand_codes.size + (drawn >= 0 ? 1 : 0)
    i = 0
    while i < n
      hx = hand_x(i)
      return i if x >= hx && x < hx + tile_w
      i += 1
    end
    nil
  end

  def big_tile(c, x, y)
    return draw_text_tile(c, x, y, 42, 56) unless @hand_sheet
    @gfx.draw_tile(@hand_sheet.id, (c % 10) * 42, (c / 10) * 56, 42, 56, dst_x: x, dst_y: y)
  end

  def small_tile(c, x, y)
    return draw_text_tile(c, x, y, 24, 32) unless @river_sheet
    @gfx.draw_tile(@river_sheet.id, (c % 10) * 24, (c / 10) * 32, 24, 32, dst_x: x, dst_y: y)
  end

  def draw_text_tile(c, x, y, w, h)
    @gfx.fill_rect(x, y, w - 1, h - 1, C_TEXT)
    @gfx.draw_text(x + 2, y + 2, tile_text(c), 0x00)
  end

  def text(x, y, s, color = C_TEXT)
    @gfx.draw_text(x, y, s, color)
  end

  # The font: px 12 or 16 (efont), scaled by size (1 or 2).
  def fnt(px, size)
    @gfx.set_font(:ja, px)
    @gfx.set_text_size(size)
    @fw = px * size
  end

  # The 640x360 screens' pixels at this screen's scale.
  def u(n)
    n * @k
  end

  # Pixel width with the font set by fnt (ASCII half the full width).
  def tw(s)
    w = 0
    i = 0
    n = s.bytesize
    half = @fw / 2
    while i < n
      b = s.getbyte(i)
      if b < 0x80
        w += half
        i += 1
      else
        w += @fw
        i += b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2)
      end
    end
    w
  end

  # Text with 2 px between a half-width character and a full-width one
  # after it. The 12 px font draws some digits up to the right edge of their
  # cell ("2" fills columns 1-5 of 6) and kanji from the left edge of theirs,
  # so "2翻" has no blank column between them and the 2 reads as covered.
  def text_spaced(x, y, s, color = C_TEXT)
    run = ""
    wide = false
    n = s.bytesize
    i = 0
    while i < n
      b = s.getbyte(i)
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2))
      ch = s.byteslice(i, len)
      if !run.empty? && (b >= 0x80) != wide
        text(x, y, run, color)
        x += tw(run)
        x += 2 if !wide && run.getbyte(run.bytesize - 1) != 32
        run = ""
      end
      wide = b >= 0x80
      run << ch
      i += len
    end
    text(x, y, run, color) unless run.empty?
  end

  # label: a text, or a call's [head, tiles, key] (MJ8: the tiles as pictures).
  def button(type, tiles, x, y, label, h = u(20))
    w = btn_label_w(label) + u(12)
    @buttons << [type, x, y, w, h, tiles]
    @gfx.fill_round_rect(x, y, w, h, u(4), C_BTN)
    btn_content(label, x, y, w, h)
    w
  end

  def draw_screen
    return title_draw if @title_until
    t0 = Machine.board_millis
    @mk = mark_kind
    m = @unsupported ? :retro : mode
    if @xl && m == :table
      xl_draw
      @gfx.present
      perf_note(:draw, Machine.board_millis - t0)
      note_latency
      return
    end
    @xl_items = nil
    @buttons = []
    clear_user_area(C_FELT)
    fnt(12, @k)
    if m == :table && watching?
      watch_load
      draw_watch
      draw_shout
      draw_result if (@view["res"] || @view["fin"]) && !@shout
    elsif m == :retro
      text(@x0 + 8, @y0 + 8, t(:mj_title))
      text(@x0 + 8, @y0 + 26, t(:mj_retro))
    elsif m == :status
      draw_status
    elsif m == :list
      draw_list
    elsif m == :lobby
      draw_lobby
    else
      draw_info
      draw_seats
      draw_rivers
      draw_melds
      draw_hand
      draw_controls
      draw_shout
      draw_result if (@view["res"] || @view["fin"]) && !@shout
    end
    @gfx.present
    perf_note(:draw, Machine.board_millis - t0) if m == :table
    note_latency
  end

  def note_latency
    return unless @lat_pending
    @lat = @lat_pending
    @lat_pending = nil
    Log.info("mahjong: act -> screen #{@lat} ms")
    perf_note(:lat, @lat)
  end

  # ------------------------------------------------------------ measuring
  #
  # With /home/mahjong_perf.on present, the time of every table drawing and
  # of every action to its screen go to /home/mahjong_perf.txt (one line
  # each: "draw ms items px" / "lat ms action"), rewritten every 20 lines.

  def perf_wanted?
    File.exist?(PERF_ON)
  rescue
    false
  end

  def perf_note(kind, ms)
    return unless @perf_on
    @perf << (kind == :draw ? "draw #{ms} #{@xl ? @xl_n_items : 0} #{@xl ? @xl_n_px : 0}" : "lat #{ms} #{@act_type}")
    @perf.shift if @perf.size > 5000
    return unless @perf.size % 20 == 0
    begin
      f = File.open(PERF_OUT, "w")
      f.write(@perf.join("\n") + "\n")
      f.close
    rescue => e
      Log.info("mahjong: perf: #{e.message}")
    end
  end

  def net_line
    if @net == :connecting
      "#{t(:mj_connecting)}: #{@locator}"
    elsif @net == :down || reconnecting?
      "#{t(:mj_lost)} #{@net_note}"
    elsif @host_gone
      t(:mj_host_left)
    elsif @host_away
      t(:mj_host_away)
    else
      nil
    end
  end

  def draw_status
    x = @x0 + u(8)
    y = @y0 + u(8)
    text(x, y, "#{t(:mj_title)}  #{@name}")
    line = net_line || "#{t(:mj_joining)}: #{@table}"
    text(x, y + u(20), line, C_DIM)
    text(x, y + u(40), @note, C_TURN) unless @note.empty?
  end

  # The tables on the network, with what their last put said.
  def draw_list
    x = @x0 + u(8)
    y = @y0 + u(8)
    text(x, y, t(:mj_title))
    nx = x + tw(t(:mj_title)) + u(12)
    draw_name_button(nx, y - u(3), @x0 + @w - nx - u(8))
    l = net_line
    text(x, y + u(18), l, C_DIM) if l
    y += u(40)
    if @table_list.empty?
      text(x, y, t(:mj_searching), C_DIM)
    else
      text(x, y, t(:mj_tables), C_DIM)
      rb = t(:mj_records)
      button(B_RECORDS, [@list_sel % @table_list.size], @x0 + @w - tw(rb) - u(20), y - u(5), rb)
      y += u(18)
      i = 0
      while i < @table_list.size
        p = @table_list[i]
        s = @tables[p] || {}
        @gfx.fill_rect(x - u(2), y - u(3), @w - u(16), u(20), C_SEL) if i == @list_sel % @table_list.size
        # The host's app is always games: show <host>/<table>.
        parts = p.split("/")
        line = "#{parts[0]}/#{parts[2]}  #{table_status(s)}"
        line = "#{line}  #{t(:mj_mine)}" if @mine[p]
        text(x + u(2), y + u(1), fit(line, @w - u(20)), i == @list_sel % @table_list.size ? C_TURN : C_TEXT)
        @buttons << [B_JOIN, x - u(2), y - u(3), @w - u(16), u(20), [i]]
        y += u(22)
        i += 1
      end
    end
    text(x, @y0 + @h - u(18), @note, C_TURN) unless @note.empty?
  end

  # The AI players the host offers (the lobby view's "ak"; none without
  # its keys), and whether a seat is one ("ai").
  def ai_kinds
    ak = @view ? @view["ak"] : nil
    ak.is_a?(Array) ? ak : []
  end

  def ai_seat?(s)
    ai = @view ? @view["ai"] : nil
    ai.is_a?(Array) && !ai[s].to_s.empty?
  end

  def ai_label(kind)
    kind == "claude" ? "Claude" : (kind == "jev" ? "Jev" : kind.to_s)
  end

  def table_status(s)
    st = s["st"].to_s
    return "" if st.empty?
    sk = s["sk"].is_a?(Array) ? s["sk"] : []
    hn = 0
    cn = 0
    i = 0
    while i < sk.size
      hn += 1 if sk[i] == 1
      cn += 1 if sk[i] == 2
      i += 1
    end
    head = st == "wait" ? t(:mj_lobby) : (st == "over" ? t(:mj_ended) : t(:mj_playing))
    line = "#{head}  #{t(:mj_humans)} #{hn} / #{t(:mj_cpus)} #{cn} / #{t(:mj_free)} #{4 - hn - cn}"
    @big ? "#{line}  (#{s['mb'].to_i}#{t(:mj_members)})" : line
  end

  # The lobby: the four seats, who watches, the owner's buttons.
  def draw_lobby
    v = @view
    x = @x0 + u(8)
    y = @y0 + u(8)
    text(x, y, "#{t(:mj_title)}  #{t(:mj_lobby)}  #{@table}")
    l = net_line
    text(x, y + u(16), l, C_DIM) if l
    nm = v["nm"].is_a?(Array) ? v["nm"] : []
    sk = seat_kinds
    ow = v["ow"].to_i
    row_h = u(@big ? 30 : 24)
    y = @y0 + u(@big ? 44 : 36)
    bx = @x0 + u(@big ? 330 : 230)
    s = 0
    while s < 4
      @gfx.fill_rect(x - u(2), y - u(4), (rules && @big ? u(436) : @w - u(16)), row_h - u(4), C_SEL) if s == @seat
      who = sk[s] == 0 ? t(:mj_empty) : nm[s].to_s
      label = "#{t(:mj_seat)}#{s + 1}  #{who}"
      label = "#{label}  [#{t(:mj_ai)}]" if ai_seat?(s)
      label = "#{label}  [#{t(:mj_owner)}]" if s == ow
      label = "#{label}  (#{t(:mj_away)})" if away?(s)
      text(x + u(2), y + u(2), label, sk[s] == 0 ? C_DIM : (s == @seat ? C_TURN : C_TEXT))
      cx = bx
      if s == @seat
        cx += button(B_STAND, [], cx, y - u(2), t(:mj_stand)) + u(4)
      elsif sk[s] != 1
        cx += button(B_SIT, [s], cx, y - u(2), "#{t(:mj_sit)} #{s + 1}") + u(4)
      end
      draw_seat_state(s, cx, y - u(2)) if sk[s] != 1
      y += row_h
      s += 1
    end
    # Members without a seat.
    mb = v["mb"].is_a?(Array) ? v["mb"] : []
    w = ""
    i = 0
    while i < mb.size
      # [name, seat (-1: none), flags (1 away, 2 owner)]
      case mb[i]
      in [name, Integer => seat, Integer => flags, *] if seat < 0
        w = "#{w} #{name}"
        w = "#{w}(#{t(:mj_away)})" if flags & 1 == 1
      else
      end
      i += 1
    end
    text(x + u(2), y + u(2), fit("#{t(:mj_watchers)}#{w}", rules && @big ? u(436) : @w - u(16)), C_DIM)
    draw_rules
    y += u(22)
    cx = x
    if owner?
      cx += button(B_CPU_ON, [], cx, y, t(:mj_fill)) + u(6) if sk.index(0)
      cx += button(B_START, [], cx, y, t(:mj_start)) + u(6) if sk.index(0).nil?
    end
    cx += button(B_LEAVE, [], cx, y, t(:mj_leave)) + u(6)
    button(B_RECORDS, [], cx, y, t(:mj_records)) if @big
    text(x + u(2), y + u(26), t(:mj_owner_starts), C_DIM) unless owner?
    text(x + u(2), y + u(44), @note, C_TURN) unless @note.empty?
    y += u(@big ? 68 : 62)
    cx = x + button(B_ASSIST, [], x, y, assist_label) + u(8)
    text(cx, y + u(4), @assist_notes[@assist], C_DIM) if @big
    if @big
      draw_name_button(x, y + u(28), @w - u(16))
    else
      draw_name_button(cx, y, @x0 + @w - cx - u(8))
    end
  end

  # 640x360: the round on two lines at the top left (clear of the opposite
  # seat's name), the dora at the top right (draw_dora). The small table:
  # one line, the dora after it.
  def draw_info
    v = @view
    x = @x0 + (@big ? 4 : 92)
    y = @y0 + 2
    # r: [wind, hand, repeats, sticks, dealer, tiles left, ...]
    case v["r"]
    in nil then return
    in [wind, hand, repeats, sticks, *rest]
      head = "#{WIND[wind.to_i % 4]}#{hand}#{t(:mj_kyoku)}  #{repeats}#{t(:mj_honba)}"
      tail = "#{t(:mj_sticks)}#{sticks}"
      tail = "#{tail}  #{t(:mj_left)}#{rest[1]}" if rest.size > 1
    else
      return
    end
    if @big
      text_spaced(x, y, head)
      text_spaced(x, y + 16, tail)
      return draw_dora
    end
    s = "#{head}  #{tail}"
    text_spaced(x, y, s)
    dx = x + tw(s) + 20
    d = bytes_of(v["dora"])
    text(dx, y, t(:mj_dora))
    dx += tw(t(:mj_dora)) + 4
    i = 0
    while i < d.size
      text(dx + i * 14, y, tile_text(d[i]), C_TURN)
      i += 1
    end
  end

  # 640x360: the dora indicators right-aligned against the assist button
  # (LAYOUT640 :dora), so up to five of them fit.
  def draw_dora
    d = bytes_of(@view["dora"])
    r = layout640(:dora)
    y = @y0 + r[1]
    dx = @x0 + r[0] + r[2] - (tw(t(:mj_dora)) + 4 + d.size * 24)
    text(dx, y + 8, t(:mj_dora))
    dx += tw(t(:mj_dora)) + 4
    i = 0
    while i < d.size
      small_tile(d[i], dx + i * 24, y)
      same_mark(d[i], dx + i * 24, y, 24, 32)
      i += 1
    end
  end

  # Name, wind, score, riichi, away of each seat.
  def draw_seats
    v = @view
    nm = v["nm"].is_a?(Array) ? v["nm"] : []
    sc = v["sc"].is_a?(Array) ? v["sc"] : []
    ri = v["ri"].to_i
    r = v["r"]
    cur = r.is_a?(Array) && r.size > 6 ? r[6].to_i : -1
    s = 0
    while s < 4
      tail = " #{sc[s]}"
      tail = "#{tail} #{t(:mj_riichi)}" if (ri >> s) & 1 == 1
      away = away?(s)
      tail = "#{tail} (#{t(:mj_away)})" if away
      head = "#{wind_of(s)} "
      name = s == @seat ? t(:mj_you) : nm[s].to_s
      rl = rel(s)
      # The name gives way so the label stays in its place (the left and the
      # across labels are on one line).
      room = (@big ? [160, 144, 200, 144][rl] : (rl == 2 ? 230 : 86)) - tw(head) - tw(tail)
      label = "#{head}#{fit(name, room)}#{tail}"
      turn = s == cur && v["st"] == "play"
      p = seat_label_pos(rl)
      if rl == 0 && @big
        own_label(p[0], p[1], label, away, turn) # MJ8: by the hand, white on a box
      else
        text(p[0], p[1], label, away ? C_AWAY : (turn ? C_TURN : C_TEXT))
        # The seat to act: underlined too (a spectator has no hand to tell it).
        @gfx.fill_rect(p[0], p[1] + 13, tw(label), 2, C_TURN) if turn
      end
      s += 1
    end
  end

  # s cut (by characters) to at most w pixels, with ".." when cut.
  def fit(s, w)
    return s if tw(s) <= w
    out = ""
    i = 0
    n = s.bytesize
    while i < n
      b = s.getbyte(i)
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2))
      ch = s.byteslice(i, len)
      break if tw(out) + tw(ch) + 12 > w
      out << ch
      i += len
    end
    "#{out}.."
  end

  def seat_label_pos(rl)
    if @big
      return [[@x0 + 4, @y0 + 250], [@x0 + 492, @y0 + 38], [@x0 + 152, @y0 + 22], [@x0 + 4, @y0 + 38]][rl]
    end
    [[@x0 + 92, @y0 + 160], [@x0 + 336, @y0 + 14], [@x0 + 92, @y0 + 14], [@x0 + 2, @y0 + 14]][rl]
  end

  # River areas: [x, y, tiles per row, rows].
  def river_area(rl)
    if @big
      return [[@x0 + 176, @y0 + 170, 12, 3], [@x0 + 492, @y0 + 120, 6, 4],
              [@x0 + 176, @y0 + 72, 12, 3], [@x0 + 4, @y0 + 120, 6, 4]][rl]
    end
    [[@x0 + 92, @y0 + 120, 18, 3], [@x0 + 336, @y0 + 40, 7, 9],
     [@x0 + 92, @y0 + 40, 18, 3], [@x0 + 2, @y0 + 40, 7, 9]][rl]
  end

  def draw_rivers
    kw = @view["kw"]
    return unless kw.is_a?(Array)
    ls = @view["ls"]
    s = 0
    while s < 4
      a = river_area(rel(s))
      b = bytes_of(kw[s])
      cw = @big ? 24 : 13
      ch = @big ? 32 : 13
      # The tiles past the last row's room close up in it.
      n = 0
      b.each { |c| n += 1 if c & 0x80 == 0 }
      before = (a[3] - 1) * a[2] # the tiles of the rows before the last
      lens = []
      (n - before).times { lens << cw } if n > before
      offs = close_up(lens, a[2] * cw)
      i = 0
      k = 0
      while i < b.size
        c = b[i]
        if c & 0x80 == 0 # a tile called away is not in the river
          if k < before
            x = a[0] + (k % a[2]) * cw
            y = a[1] + (k / a[2]) * ch
          else
            x = a[0] + offs[k - before]
            y = a[1] + (a[3] - 1) * ch
          end
          code = c & 0x3F
          if @big
            small_tile(code, x, y)
            same_mark(code, x, y, 24, 32)
            @gfx.fill_rect(x + 2, y + 28, 20, 3, C_RED) if c & 0x40 != 0
          else
            text(x, y, tile_text(code), c & 0x40 != 0 ? C_RED : C_TEXT)
          end
          last = i == b.size - 1 && ls.is_a?(Array) && ls[0] == s
          @gfx.draw_rect(x, y, cw - 1, ch - 1, C_LAST) if last
          k += 1
        end
        i += 1
      end
      s += 1
    end
  end

  # Melds: [kind, from, n, tiles...]; kind 3 is a closed kan.
  def draw_melds
    fu = @view["fu"]
    return unless fu.is_a?(Array)
    return draw_melds640 if @big
    s = 0
    while s < 4
      b = bytes_of(fu[s])
      rl = rel(s)
      tiles = []
      i = 0
      while i + 2 < b.size
        n = b[i + 2]
        j = 0
        while j < n
          c = b[i + 3 + j]
          c = 37 if b[i] == 3 && (j == 0 || j == n - 1)
          tiles << c
          j += 1
        end
        tiles << -1
        i += 3 + n
      end
      draw_meld_tiles(rl, tiles)
      s += 1
    end
  end

  # The small table's melds: the own ones as short texts ("3m-5m" a chi,
  # "3mx3" a pon, "3mx4" a kan), the others' as a count (640x360:
  # draw_melds640, mahjong_panels.rb).
  def draw_meld_tiles(rl, tiles)
    return if tiles.empty?
    ms = []
    cur = []
    tiles.each do |c|
      if c < 0
        ms << cur
        cur = []
      else
        cur << c
      end
    end
    if rl == 0
      s = ""
      ms.each do |m|
        f = m[0] == 37 ? m[1] : m[0]
        s = "#{s} #{m[0] == m[1] || m[0] == 37 ? "#{tile_text(f)}x#{m.size}" : "#{tile_text(m[0])}-#{tile_text(m[m.size - 1])}"}"
      end
      text(@x0 + 250, @y0 + 160, s.strip, C_DIM)
    else
      k = 0
      ms.each { |m| k += 1 if m.size == 4 }
      s = "鳴#{ms.size}"
      s = "#{s} 槓#{k}" if k > 0
      text(rl == 2 ? @x0 + 92 : (rl == 1 ? @x0 + 336 : @x0 + 2), @y0 + 27, s, C_DIM)
    end
  end

  def draw_hand
    n = hand_codes.size + (drawn >= 0 ? 1 : 0)
    i = 0
    while i < n
      draw_hand_tile(i)
      i += 1
    end
  end

  SEL_LIFT = 6            # the 640x360 table: the chosen tile is this much higher

  # Hand tile i: lifted and framed in blue when chosen; the help marks the
  # discards that leave the hand tenpai (a triangle above the tile); while
  # picking the riichi tile, the others get a bar.
  def draw_hand_tile(i)
    c = hand_at(i)
    return unless c
    x = hand_x(i)
    y = hand_y
    y -= SEL_LIFT if i == @sel
    if @big
      big_tile(c, x, y)
    else
      small_tile(c, x, y)
    end
    chosen_frame(x, y, tile_w, tile_h) if i == @sel
    mk = help_mark(c)
    if mk
      assist_mark(x, y, tile_w, mk == :keep ? C_KEEP : C_TURN)
    elsif @pick && !can_discard?(c)
      @gfx.fill_rect(x + 2, y + tile_h - 8, tile_w - 4, 3, C_DIM)
    end
  end

  # A downward triangle in a w wide box at (x, y) (r: its half width): the
  # legend of the 1280x720 help line (the hand tiles use assist_mark).
  def tenpai_mark(x, y, w, r, col = C_TURN)
    cx = x + w / 2
    @gfx.fill_triangle(cx - r - 1, y + 2, cx + r + 1, y + 2, cx, y + r + r + 3, 0x00)
    @gfx.fill_triangle(cx - r, y + 3, cx + r, y + 3, cx, y + r + r + 1, col)
  end

  # The 640x360 table: only the chosen tile moved (the mouse or the keys).
  def redraw_hand_tiles(list)
    i = 0
    while i < list.size
      k = list[i]
      x = hand_x(k)
      top = hand_y - SEL_LIFT - mark_rise
      @gfx.fill_rect(x, top, tile_w, hand_y + tile_h - top, C_FELT)
      draw_hand_tile(k)
      i += 1
    end
    @gfx.present
  end

  # "待ち 1s:3 4s:3 計6枚" for the chosen tile (nil: no help).
  def waits_text
    w = sel_waits
    return uk_text unless w
    s = t(:mj_waits)
    sum = 0
    i = 0
    while i < w.size
      s = "#{s} #{tile_text(w[i][0])}:#{w[i][1]}"
      sum += w[i][1]
      i += 1
    end
    "#{s} #{t(:mj_total)}#{sum}#{t(:mj_mai)}"
  end

  # "1向聴 受け入れ 2m 5p 8s 3種10枚" for the chosen tile (level 3; the
  # tiles only while the line stays short).
  def uk_text
    u = sel_uk
    return nil unless u
    return t(:mj_no_wait) if u[0] == 0
    l = u[1]
    sum = 0
    tl = ""
    i = 0
    while i < l.size
      sum += l[i][1]
      tl = "#{tl} #{tile_text(l[i][0])}"
      i += 1
    end
    head = "#{u[0]}#{t(:mj_shanten)} #{t(:mj_ukeire)}"
    tail = "#{l.size}#{t(:mj_kinds)}#{sum}#{t(:mj_mai)}"
    l.size <= 8 ? "#{head}#{tl} #{tail}" : "#{head} #{tail}"
  end

  def button_label(type, tl)
    case type
    when A_RIICHI then "#{t(:mj_riichi)} R"
    when A_TSUMO then "#{t(:mj_tsumo)} T"
    when A_RON then "#{t(:mj_ron)} T"
    when A_PON then "#{t(:mj_pon)} P"
    when A_CHI then "#{t(:mj_chi)} #{tile_text(tl[0])}#{tile_text(tl[1])} C"
    when A_ANKAN, A_KAKAN then "#{t(:mj_kan)} #{tile_text(tl[0])} K"
    when A_MINKAN then "#{t(:mj_kan)} K"
    when A_SKIP then "#{t(:mj_pass)} Esc"
    when A_KYUUSHU then "#{t(:mj_kyuushu)} N"
    when A_NEXT then "#{t(:mj_next)} Enter"
    end
  end

  def seat_name(s)
    return "-" unless s.is_a?(Integer)
    return t(:mj_you) if s == @seat
    nm = @view["nm"]
    nm.is_a?(Array) ? nm[s].to_s : s.to_s
  end

  # ------------------------------------------------------------ the loop

  def on_update
    now = Machine.board_millis
    title_tick(now)
    return 500 if @unsupported
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
        if @table.nil?
          probe_tables
        else
          collect_join
          collect_act
          collect_cmd
          collect_view
          if @rename_f && @rename_f.done?
            @rename_f = nil
            ask_view
          end
          if @token && @view_f.nil? && view_due?(now)
            ask_view
          end
        end
      end
    end
    run_key(@keys.shift) until @keys.empty?
    if @move
      run_move(@move)
      @move = nil
    end
    run_click(@clicks.shift) until @clicks.empty?
    if now >= @mem_at
      # The VM pool, now and then, for the report (doc/ruby_asterism/report/mj2.md).
      @mem_at = now + MEM_EVERY_MS
      Log.info("mahjong: pool #{FmrbApp.pool_used / 1024} KB (#{FmrbApp.pool_usage}%)")
    end
    shout_tick(now)
    if @deadline && (@deadline - now) / 1000 != @second
      @second = (@deadline - now) / 1000
      @dirty = true
    end
    if @dirty
      @dirty = false
      @hand_redraw = nil
      draw_screen
    elsif @hand_redraw
      redraw_hand_tiles(@hand_redraw)
      @hand_redraw = nil
    end
    30
  end

  # A newer version, or the view that says this app is still there.
  def view_due?(now)
    return now - @view_at >= HOST_GONE_VIEW_MS if @host_gone
    @want_v > (@view ? @view["v"].to_i : 0) || now - @view_at >= VIEW_EVERY_MS
  end

  def on_destroy
    ::Asterism.close unless @unsupported
    Log.info("mahjong: closed")
  rescue => e
    Log.info("mahjong: close: #{e.message}")
  end
end

begin
  app = MahjongApp.new
  app.start
rescue => e
  puts "mahjong: #{e.message}"
end
