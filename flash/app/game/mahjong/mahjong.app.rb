# Mahjong table (doc/ruby_asterism, MJ1-MJ2): the screen of a riichi mahjong
# table run by a game host (the asterism-games repository) on the same
# Zenoh network. Up to four players at a table, the other seats CPUs.
#
# - The host holds the wall and the hands and has one or more tables
#   (objects <host>/games/mahjong*). This app lists them (the host puts each
#   table's public state on asterism/<host>/games/<table>/state after every
#   change and every 5 s), joins one (join(name) -> a token) and waits in its
#   lobby: sit / stand, the table's owner fills empty seats with CPUs and
#   starts. In the game it asks its own view with the token (view(token)) when
#   a put shows a newer version, and every VIEW_EVERY_MS anyway (which also
#   tells the host it is still there). An action is act(token, sv, type,
#   tiles): the host takes only what its list for this seat (the view's "a")
#   has. A member without a seat watches (the table without hands).
# - The connection is the machine's (FmrbAsterism.settings, as othello). A
#   line that drops or a host that restarts: the app keeps asking. When the
#   app starts again it asks each table mine() and goes back to the table it
#   was at (the same node gets its token back).
# - Modern (Tab5, P4-Nano) and the browser build: 640x360 (fullscreen_hires).
#   A smaller screen (the Linux sim, 426x240) gets a compact table with the
#   discards as text, and only the river sheet.
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
# The hand sheet is wider than 256 px, which the sim's sprite loader (the
# same code as Retro's) refuses; it is loaded only for the big table, which
# the sim never shows.
#
# Keys. The list of tables: up / down choose, Enter joins. The lobby: 1-4
# sit in that seat, s stand, c fill the empty seats with CPUs (the owner),
# Enter start (the owner), Esc leave the table. The game: left / right choose
# a tile, Enter discards it (the drawn tile when none is chosen) or goes on
# after a result, Esc passes a call (a spectator: leaves); r riichi (then
# Enter: the chosen tile, or the first that declares), t tsumo / ron, p pon,
# c chi, k kan, n nine kinds. A click on a tile discards it; the buttons are
# what the host offers now.

# The 1280x720 table (methods of this class, in a file of its own: an app
# file is read whole and may be at most 64 KB).
require "/app/game/mahjong/mahjong_xl"

class MahjongApp < FmrbApp
  APP = "game"            # this app's Asterism application name (the G0 range)
  HOST_APP = "games"      # the host's application
  CALL_TIMEOUT = 3.0
  RECONNECT_MS = 3000
  VIEW_EVERY_MS = 3000
  SCAN_EVERY_MS = 2000
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

  def setup_strings
    FmrbI18n.add({
      "en" => {
        mj_title: "Mahjong", mj_connecting: "Connecting", mj_searching: "Looking for a game host (games/mahjong*)",
        mj_joining: "Joining", mj_full: "The table is full", mj_lost: "Line dropped: connecting again",
        mj_host_away: "The host does not answer: waiting", mj_retro: "This table needs Modern or the browser build",
        mj_left: "left", mj_riichi: "Riichi", mj_tsumo: "Tsumo", mj_ron: "Ron", mj_pon: "Pon", mj_chi: "Chi",
        mj_kan: "Kan", mj_pass: "Pass", mj_kyuushu: "9 kinds", mj_next: "Next", mj_pick: "Pick the riichi tile",
        mj_draw: "Draw", mj_tenpai: "Tenpai", mj_over: "Game over", mj_you: "You", mj_wait: "waiting",
        mj_rest: " s", mj_honba: "honba", mj_sticks: "sticks", mj_dora: "Dora",
        mj_kyuushu_end: "Nine kinds: redeal", mj_sanchahou: "Three rons: a draw",
        mj_ms: "ms", mj_resp: "reply",
        mj_tables: "Tables (Enter joins)", mj_no_tables: "No table yet", mj_seat: "Seat", mj_empty: "empty",
        mj_sit: "Sit", mj_stand: "Stand S", mj_cpu: "CPU", mj_cpu_off: "Free", mj_fill: "Fill with CPUs C",
        mj_start: "Start Enter", mj_leave: "Leave Esc", mj_owner: "owner", mj_away: "away",
        mj_watchers: "Watching:", mj_lobby: "Lobby", mj_playing: "playing", mj_ended: "game over",
        mj_humans: "players", mj_cpus: "CPUs", mj_free: "free", mj_members: "at the table",
        mj_watching: "Watching (Esc leaves)", mj_others: "waiting for the others",
        mj_owner_starts: "The owner starts when the four seats are taken",
        mj_back_lobby: "Next: the owner goes back to the lobby"
      },
      "ja" => {
        mj_title: "麻雀", mj_connecting: "つないでいます", mj_searching: "ゲームホストを探しています (games/mahjong*)",
        mj_joining: "卓に入っています", mj_full: "卓が満員", mj_lost: "網から切れた: つなぎ直し中",
        mj_host_away: "ホストが答えない: 待っています", mj_retro: "この卓は Modern とブラウザ版で動きます",
        mj_left: "残り", mj_riichi: "リーチ", mj_tsumo: "ツモ", mj_ron: "ロン", mj_pon: "ポン", mj_chi: "チー",
        mj_kan: "カン", mj_pass: "パス", mj_kyuushu: "九種九牌", mj_next: "次へ", mj_pick: "リーチの牌を選ぶ",
        mj_draw: "流局", mj_tenpai: "聴牌", mj_over: "終局", mj_you: "自分", mj_wait: "待ち",
        mj_rest: "秒", mj_honba: "本場", mj_sticks: "供託", mj_dora: "ドラ",
        mj_kyuushu_end: "九種九牌で流局", mj_sanchahou: "三家和で流局",
        mj_ms: "ms", mj_resp: "応答",
        mj_tables: "卓 (Enter で入る)", mj_no_tables: "卓がまだ無い", mj_seat: "席", mj_empty: "空き",
        mj_sit: "座る", mj_stand: "立つ S", mj_cpu: "CPU", mj_cpu_off: "空ける", mj_fill: "CPU で埋める C",
        mj_start: "始める Enter", mj_leave: "卓を出る Esc", mj_owner: "卓の主", mj_away: "途切れ",
        mj_watchers: "観戦:", mj_lobby: "待ち合わせ", mj_playing: "対局中", mj_ended: "終局",
        mj_humans: "人", mj_cpus: "CPU", mj_free: "空き", mj_members: "人が卓に",
        mj_watching: "観戦中 (Esc で出る)", mj_others: "ほかの人の答えを待っています",
        mj_owner_starts: "4 席がそろったら卓の主が始める",
        mj_back_lobby: "次へ: 卓の主が待ち合わせに戻す"
      }
    })
  end

  def t(k)
    FmrbI18n.t(k)
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
    @name = st.name.to_s.empty? ? @id : st.name.to_s
    @net = :connecting
    @net_note = ""
    @retry_at = 0
    @tables = {}          # "<host>/games/<name>" => its last public state (or {})
    @table_list = []
    @list_sel = 0
    @probed = []          # tables asked mine()
    @probe_f = nil
    @probe_path = nil
    @probe_at = Machine.board_millis
    @scan_at = 0
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
    @sel = -1
    @pick = false         # choosing the riichi tile
    @act_t0 = nil         # when the last action was sent (for the latency)
    @act_sv = 0
    @lat = nil
    @deadline = nil
    @others = false       # the host waits for other seats' calls
    @second = 0
    @mem_at = 0
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

  # The hand sheet, the first time the table is big (the 640x360 table).
  def load_hand_sheet
    @hand_sheet = load_sheet("tiles_hand.bmp", 420, 224) if @big && !@xl && @hand_sheet.nil?
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
      @net = :up
      @net_note = ""
      @scan_at = 0
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

  # The tables of the hosts on the network.
  def find_tables
    now = Machine.board_millis
    return if now - @scan_at < SCAN_EVERY_MS
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
    @dirty = true
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
  # started again, or the host seated it)? Then go back to it.
  def probe_tables
    if @probe_f
      return unless @probe_f.done?
      f = @probe_f
      p = @probe_path
      @probe_f = nil
      begin
        r = f.value
        if @table.nil? && r.is_a?(Hash) && r["token"]
          Log.info("mahjong: back at #{p}")
          enter_table(p, r)
        end
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
        @note = r.is_a?(Hash) && r["error"] == "full" ? "#{t(:mj_full)}: #{@table}" : r.to_s
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
    begin
      v = f.value
      if v.is_a?(Hash) && v["error"]
        # The host lost our membership (a new state): back to the list.
        Log.info("mahjong: the host does not know us: back to the list")
        @token = nil
        @table = nil
        @seat = nil
        @view = nil
        @probed = []
        @dirty = true
      elsif v.is_a?(Hash)
        take_view(v)
      end
      @host_away = false
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
    dl = v["dl"]
    mine = @seat ? (1 << @seat) : 0
    bits = dl.is_a?(Array) ? dl[0].to_i : 0
    @deadline = bits & mine != 0 ? Machine.board_millis + dl[1].to_i : nil
    @others = bits & ~mine != 0 && bits & mine == 0
    @pick = false unless offer(A_RIICHI)
    n = hand_codes.size
    @sel = -1 if @sel >= n
    if @act_t0 && v["sv"].to_i > @act_sv
      @lat_pending = Machine.board_millis - @act_t0
      @act_t0 = nil
    end
    @dirty = true if old.nil? || old["v"] != v["v"]
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
        @note = r.to_s
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

  # A lobby call: sit / stand / cpu / start.
  def lobby(kind, seat)
    return if @cmd_f || @token.nil?
    px = proxy
    @cmd_f = if kind == B_SIT then px.async.sit(@token, seat)
             elsif kind == B_STAND then px.async.stand(@token)
             elsif kind == B_CPU_ON then px.async.cpu(@token, seat, true)
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
      @note = r == "ok" ? "" : r.to_s
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
    if t == :key_down
      @keys << [ev[:scancode], ev[:character] || 0]
    elsif t == :mouse_up && ev[:button] == 1
      @clicks << [ev[:x], ev[:y]]
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
    n = @table_list.size
    return if n == 0
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
    ch = k[1]
    ch += 32 if ch >= 65 && ch <= 90
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
    end
  end

  def table_key(k)
    sc = k[0]
    ch = k[1]
    n = hand_codes.size + (drawn >= 0 ? 1 : 0)
    if sc == FmrbConst::KEY_LEFT
      @sel = @sel <= 0 ? n - 1 : @sel - 1
      @dirty = true
    elsif sc == FmrbConst::KEY_RIGHT
      @sel = @sel >= n - 1 ? 0 : @sel + 1
      @dirty = true
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
    @sel = idx
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

  def button(type, tiles, x, y, label)
    w = tw(label) + u(12)
    @buttons << [type, x, y, w, u(20), tiles]
    @gfx.fill_round_rect(x, y, w, u(20), u(4), C_BTN)
    text(x + u(6), y + u(4), label)
    w
  end

  def draw_screen
    t0 = Machine.board_millis
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
    if m == :retro
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
      draw_result if @view["res"] || @view["fin"]
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
    text(x, y, "#{t(:mj_title)}  #{@name}")
    l = net_line
    text(x, y + u(18), l, C_DIM) if l
    y += u(40)
    if @table_list.empty?
      text(x, y, t(:mj_searching), C_DIM)
    else
      text(x, y, t(:mj_tables), C_DIM)
      y += u(18)
      i = 0
      while i < @table_list.size
        p = @table_list[i]
        s = @tables[p] || {}
        @gfx.fill_rect(x - u(2), y - u(3), @w - u(16), u(20), C_SEL) if i == @list_sel % @table_list.size
        # The host's app is always games: show <host>/<table>.
        parts = p.split("/")
        line = "#{parts[0]}/#{parts[2]}  #{table_status(s)}"
        text(x + u(2), y + u(1), line, i == @list_sel % @table_list.size ? C_TURN : C_TEXT)
        @buttons << [B_JOIN, x - u(2), y - u(3), @w - u(16), u(20), [i]]
        y += u(22)
        i += 1
      end
    end
    text(x, @y0 + @h - u(18), @note, C_TURN) unless @note.empty?
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
    @big ? "#{line}  (#{s['mb'].to_i} #{t(:mj_members)})" : line
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
      @gfx.fill_rect(x - u(2), y - u(4), @w - u(16), row_h - u(4), C_SEL) if s == @seat
      who = sk[s] == 0 ? t(:mj_empty) : nm[s].to_s
      label = "#{t(:mj_seat)}#{s + 1}  #{who}"
      label = "#{label}  [#{t(:mj_owner)}]" if s == ow
      label = "#{label}  (#{t(:mj_away)})" if away?(s)
      text(x + u(2), y + u(2), label, sk[s] == 0 ? C_DIM : (s == @seat ? C_TURN : C_TEXT))
      cx = bx
      if s == @seat
        cx += button(B_STAND, [], cx, y - u(2), t(:mj_stand)) + u(4)
      elsif sk[s] != 1
        cx += button(B_SIT, [s], cx, y - u(2), "#{t(:mj_sit)} #{s + 1}") + u(4)
      end
      if owner? && sk[s] == 0
        button(B_CPU_ON, [s], cx, y - u(2), t(:mj_cpu))
      elsif owner? && sk[s] == 2
        button(B_CPU_OFF, [s], cx, y - u(2), t(:mj_cpu_off))
      end
      y += row_h
      s += 1
    end
    # Members without a seat.
    mb = v["mb"].is_a?(Array) ? v["mb"] : []
    w = ""
    i = 0
    while i < mb.size
      m = mb[i]
      if m.is_a?(Array) && m[1].to_i < 0
        w = "#{w} #{m[0]}"
        w = "#{w}(#{t(:mj_away)})" if m[2].to_i & 1 == 1
      end
      i += 1
    end
    text(x + u(2), y + u(2), "#{t(:mj_watchers)}#{w}", C_DIM)
    y += u(22)
    cx = x
    if owner?
      cx += button(B_CPU_ON, [], cx, y, t(:mj_fill)) + u(6) if sk.index(0)
      cx += button(B_START, [], cx, y, t(:mj_start)) + u(6) if sk.index(0).nil?
    end
    cx += button(B_LEAVE, [], cx, y, t(:mj_leave)) + u(6)
    text(x + u(2), y + u(26), t(:mj_owner_starts), C_DIM) unless owner?
    text(x + u(2), y + u(44), @note, C_TURN) unless @note.empty?
  end

  def status_line
    return "#{t(:mj_lost)}" if @net != :up || reconnecting?
    return t(:mj_host_away) if @host_away
    return "#{t(:mj_watching)} #{@note}" if @seat.nil?
    return "#{t(:mj_others)} #{@note}" if @others && @view["st"] == "play" && calls_phase?
    @note
  end

  def draw_info
    v = @view
    r = v["r"]
    x = @x0 + (@big ? 152 : 92)
    y = @y0 + 2
    return unless r.is_a?(Array)
    ja = FmrbI18n.lang == "ja"
    s = "#{WIND[r[0].to_i % 4]}#{r[1]}#{ja ? '局' : ''}  #{ja ? "#{r[2]}#{t(:mj_honba)}" : "#{t(:mj_honba)} #{r[2]}"}"
    s = "#{s}  #{t(:mj_sticks)} #{r[3]}"
    s = "#{s}  #{t(:mj_left)} #{r[5]}" if r.size > 5
    text(x, y + (@big ? 8 : 0), s)
    dx = x + tw(s) + 12
    d = bytes_of(v["dora"])
    text(dx, y + (@big ? 8 : 0), t(:mj_dora))
    dx += tw(t(:mj_dora)) + 4
    i = 0
    while i < d.size
      if @big
        small_tile(d[i], dx + i * 24, y)
      else
        text(dx + i * 14, y, tile_text(d[i]), C_TURN)
      end
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
      room = (@big ? (rl == 2 ? 330 : 144) : (rl == 2 ? 230 : 86)) - tw(head) - tw(tail)
      label = "#{head}#{fit(name, room)}#{tail}"
      col = away ? C_AWAY : (s == cur && v["st"] == "play" ? C_TURN : C_TEXT)
      p = seat_label_pos(rl)
      text(p[0], p[1], label, col)
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
      return [[@x0 + 4, @y0 + 10], [@x0 + 492, @y0 + 38], [@x0 + 152, @y0 + 40], [@x0 + 4, @y0 + 38]][rl]
    end
    [[@x0 + 92, @y0 + 160], [@x0 + 336, @y0 + 14], [@x0 + 92, @y0 + 14], [@x0 + 2, @y0 + 14]][rl]
  end

  # River areas: [x, y, tiles per row, rows].
  def river_area(rl)
    if @big
      return [[@x0 + 176, @y0 + 170, 12, 3], [@x0 + 492, @y0 + 120, 6, 5],
              [@x0 + 176, @y0 + 72, 12, 3], [@x0 + 4, @y0 + 120, 6, 5]][rl]
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
      i = 0
      k = 0
      while i < b.size
        c = b[i]
        if c & 0x80 == 0 # a tile called away is not in the river
          x = a[0] + (k % a[2]) * cw
          y = a[1] + (k / a[2]) * ch
          code = c & 0x3F
          if @big
            small_tile(code, x, y)
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

  def draw_meld_tiles(rl, tiles)
    return if tiles.empty?
    if @big
      if rl == 0
        x = @x0 + @w - 4 - meld_width(tiles)
        y = @y0 + @h - 36
        per = 99
      elsif rl == 2
        x = @x0 + 488 - meld_width(tiles)
        y = @y0 + 36
        per = 99
      else
        x = rl == 1 ? @x0 + 492 : @x0 + 4
        y = @y0 + 54
        per = 6
      end
      k = 0
      i = 0
      row = 0
      cx = x
      while i < tiles.size
        c = tiles[i]
        if c < 0
          cx += 4
        else
          if k >= per
            row += 1
            k = 0
            cx = x
          end
          small_tile(c, cx, y + row * 32)
          cx += 24
          k += 1
        end
        i += 1
      end
    else
      s = ""
      i = 0
      while i < tiles.size
        s = tiles[i] < 0 ? "#{s} " : "#{s}#{tile_text(tiles[i])}"
        i += 1
      end
      p = rl == 0 ? [@x0 + 250, @y0 + 160] : (rl == 2 ? [@x0 + 92, @y0 + 27] : [rl == 1 ? @x0 + 336 : @x0 + 2, @y0 + 27])
      text(p[0], p[1], s, C_DIM)
    end
  end

  def meld_width(tiles)
    w = 0
    i = 0
    while i < tiles.size
      w += tiles[i] < 0 ? 4 : 24
      i += 1
    end
    w
  end

  def draw_hand
    h = hand_codes
    n = h.size + (drawn >= 0 ? 1 : 0)
    i = 0
    while i < n
      c = hand_at(i)
      x = hand_x(i)
      y = hand_y
      y -= 6 if i == @sel
      if @big
        big_tile(c, x, y)
      else
        small_tile(c, x, y)
      end
      if !can_discard?(c) && (offer(A_DAHAI) || @pick)
        # dim what may not be discarded (riichi: all but the choices)
        @gfx.fill_rect(x + 2, y + tile_h - 8, tile_w - 4, 3, C_DIM) if @pick
      end
      i += 1
    end
  end

  def draw_controls
    x0 = @x0 + (@big ? 152 : 2)
    y = hand_y - 26
    bx = x0
    list = @view["a"].is_a?(Array) ? @view["a"] : []
    i = 0
    while i < list.size
      o = list[i]
      tl = bytes_of(o[1])
      label = button_label(o[0], tl)
      bx += button(o[0], tl, bx, y, label) + 4 if label
      i += 1
    end
    bx += button(B_LEAVE, [], bx, y, t(:mj_leave)) + 4 if @seat.nil?
    # The time left, the note and the last reply time, after the buttons.
    msg = status_line.to_s
    msg = "#{t(:mj_pick)} #{msg}" if @pick
    if @deadline
      left = (@deadline - Machine.board_millis) / 1000
      left = 0 if left < 0
      msg = "#{left}#{t(:mj_rest)} #{msg}"
    end
    msg = "#{msg} #{t(:mj_resp)} #{@lat}#{t(:mj_ms)}" if @lat
    text(bx + 4, y + 4, msg, C_TURN) unless msg.strip.empty?
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

  # The result of a round (or the game) over the middle of the table. A
  # double ron has one block per winner, in turn order from the discarder.
  def draw_result
    res = @view["res"]
    fin = @view["fin"]
    wins = res.is_a?(Hash) && res["wn"].is_a?(Array) ? res["wn"] : []
    multi = wins.size > 1
    bx = @x0 + (@big ? 150 : 40)
    by = @y0 + (@big ? (multi ? 40 : 62) : 30)
    bw = @big ? 340 : 346
    bh = @big ? (multi ? 270 : 206) : (multi ? 200 : 132)
    @gfx.fill_rect(bx, by, bw, bh, C_BOX)
    @gfx.draw_rect(bx, by, bw, bh, C_TURN)
    x = bx + 6
    y = by + 4
    if res.is_a?(Hash)
      k = res["k"].to_s
      if k == "tsumo_agari" || k == "ron_agari"
        i = 0
        while i < wins.size
          y = draw_win(wins[i], k, res["f"], x, y, multi)
          i += 1
        end
      else
        head = if k == "kyuushu_kyuuhai" then t(:mj_kyuushu_end)
               elsif k == "sanchahou" then t(:mj_sanchahou)
               else t(:mj_draw)
               end
        text(x, y, head, C_TURN)
        y += 16
        if res["tp"].is_a?(Array)
          tp = res["tp"]
          s = ""
          i = 0
          while i < tp.size
            s = "#{s} #{seat_name(tp[i])}"
            i += 1
          end
          text(x, y, "#{t(:mj_tenpai)}:#{s}")
          y += 16
        end
      end
      d = res["d"].is_a?(Array) ? res["d"] : []
      i = 0
      while i < 4 && i < d.size
        dv = d[i].to_i
        text(x + (i % 2) * 165, y + (i / 2) * 14, "#{seat_name(i)} #{dv > 0 ? '+' : ''}#{dv}")
        i += 1
      end
    end
    if fin.is_a?(Array) && @view["st"] == "over"
      y = by + bh - 88
      text(x, y, t(:mj_over), C_TURN)
      i = 0
      while i < fin.size
        f = fin[i]
        text(x, y + 16 + i * 13, "#{f[1]}. #{seat_name(f[0])} #{f[2]} (#{f[3].to_i / 10}.#{(f[3].to_i % 10).abs})")
        i += 1
      end
      text(x, y + 72, t(:mj_back_lobby), C_DIM)
    end
  end

  # One winner's block; returns the y below it. Compact (multi): the yaku in
  # four columns.
  def draw_win(w, k, from, x, y, multi)
    head = k == "tsumo_agari" ? "#{seat_name(w['w'])} #{t(:mj_tsumo)}" : "#{seat_name(w['w'])} #{t(:mj_ron)} <- #{seat_name(from)}"
    text(x, y, head, C_TURN)
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
    if @big
      small_tile(ag, x + hc.size * 23 + 6, y)
    else
      text(x + hc.size * 13 + 4, y, tile_text(ag), C_TURN)
    end
    y += @big ? 36 : 15
    ys = w["y"].is_a?(Array) ? w["y"] : []
    cols = multi ? 4 : 3
    cw = multi ? 82 : 110
    i = 0
    while i < ys.size
      text(x + (i % cols) * cw, y + (i / cols) * 14, ys[i].to_s)
      i += 1
    end
    y += ((ys.size + cols - 1) / cols) * 14 + 2
    text_spaced(x, y, w["pt"].to_s, C_TURN)
    y + 16
  end

  def seat_name(s)
    return "-" unless s.is_a?(Integer)
    return t(:mj_you) if s == @seat
    nm = @view["nm"]
    nm.is_a?(Array) ? nm[s].to_s : s.to_s
  end

  # ------------------------------------------------------------ the loop

  def on_update
    return 500 if @unsupported
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
        if @table.nil?
          probe_tables
        else
          collect_join
          collect_act
          collect_cmd
          collect_view
          if @token && @view_f.nil? && (@want_v > (@view ? @view["v"].to_i : 0) || now - @view_at >= VIEW_EVERY_MS)
            ask_view
          end
        end
      end
    end
    run_key(@keys.shift) until @keys.empty?
    run_click(@clicks.shift) until @clicks.empty?
    if now >= @mem_at
      # The VM pool, now and then, for the report (doc/ruby_asterism/report/mj2.md).
      @mem_at = now + MEM_EVERY_MS
      Log.info("mahjong: pool #{FmrbApp.pool_used / 1024} KB (#{FmrbApp.pool_usage}%)")
    end
    if @deadline && now / 1000 != @second
      @second = now / 1000
      @dirty = true
    end
    if @dirty
      @dirty = false
      draw_screen
    end
    30
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
