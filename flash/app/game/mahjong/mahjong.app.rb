# Mahjong table (doc/ruby_asterism, MJ1): the screen of a riichi mahjong
# table run by a game host (the asterism-games repository) on the same
# Zenoh network. Step 1: this player and three CPUs.
#
# - The host holds the wall and the hands. This app finds its table (an
#   object <host>/games/mahjong*), sits (sit(name) -> seat and token) and
#   asks its own view with the token (view(token)). The host puts the public
#   state on asterism/<host>/games/<table>/state after every change; a newer
#   version there makes this app ask its view again (and it asks every
#   VIEW_EVERY_MS anyway). An action is act(token, sv, type, tiles): the
#   host takes only what its list for this seat (the view's "a") has.
# - The connection is the machine's (FmrbAsterism.settings, as othello). A
#   line that drops or a host that restarts: the app keeps asking, and the
#   same node gets its seat and token back from sit.
# - Modern (Tab5, P4-Nano) and the browser build: 640x360 (fullscreen_hires).
#   A smaller screen (the Linux sim, 426x240) gets a compact table with the
#   discards as text. Retro (S3) has no table: the app says so.
#
# The tiles are sprite sheets (8-bit BMPs whose pixel values are RGB332;
# made by tools/mahjong_tilegen.rb --sheets, then edited by hand: the sheets
# are the source of truth). Tile numbers (one byte on the wire):
#   0..8 1m..9m, 9 0m (red five), 10..18 1p..9p, 19 0p, 20..28 1s..9s,
#   29 0s, 30..36 1z..7z (E S W N white green red), 37 back.
# Cells, without gaps, transparent colour RGB332 0xE3:
#   tiles_hand_0.bmp / tiles_hand_1.bmp (210x224 each, 5 x 4 cells of 42x56):
#     sheet n / 20, k = n % 20, src_x = (k % 5) * 42, src_y = (k / 5) * 56
#   tiles_river.bmp (240x128, 10 x 4 cells of 24x32):
#     src_x = (n % 10) * 24, src_y = (n / 10) * 32
#
# Keys: left / right choose a tile, Enter discards it (the drawn tile when
# none is chosen) or goes on after a result, Esc passes a call; r riichi
# (then Enter: the chosen tile, or the first that declares), t tsumo / ron,
# p pon, c chi, k kan, n nine kinds. A click on a tile discards it; the
# buttons are what the host offers now.

class MahjongApp < FmrbApp
  APP = "game"            # this app's Asterism application name (the G0 range)
  HOST_APP = "games"      # the host's application
  CALL_TIMEOUT = 3.0
  RECONNECT_MS = 3000
  VIEW_EVERY_MS = 3000
  SCAN_EVERY_MS = 2000
  WIFI_WAIT_MS = 20000
  MEM_EVERY_MS = 30000
  APP_DIR = "/app/game/mahjong"
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

  def setup_strings
    FmrbI18n.add({
      "en" => {
        mj_title: "Mahjong", mj_connecting: "Connecting", mj_searching: "Looking for a game host (games/mahjong*)",
        mj_sitting: "Taking a seat", mj_full: "The table is full", mj_lost: "Line dropped: connecting again",
        mj_host_away: "The host does not answer: waiting", mj_retro: "This table needs Modern or the browser build",
        mj_left: "left", mj_riichi: "Riichi", mj_tsumo: "Tsumo", mj_ron: "Ron", mj_pon: "Pon", mj_chi: "Chi",
        mj_kan: "Kan", mj_pass: "Pass", mj_kyuushu: "9 kinds", mj_next: "Next", mj_pick: "Pick the riichi tile",
        mj_draw: "Draw", mj_tenpai: "Tenpai", mj_over: "Game over", mj_you: "You", mj_wait: "waiting",
        mj_rest: " s", mj_honba: "honba", mj_sticks: "sticks", mj_dora: "Dora",
        mj_kyuushu_end: "Nine kinds: redeal",
        mj_ms: "ms", mj_resp: "reply"
      },
      "ja" => {
        mj_title: "麻雀", mj_connecting: "つないでいます", mj_searching: "ゲームホストを探しています (games/mahjong*)",
        mj_sitting: "席に着いています", mj_full: "卓が満席", mj_lost: "網から切れた: つなぎ直し中",
        mj_host_away: "ホストが答えない: 待っています", mj_retro: "この卓は Modern とブラウザ版で動きます",
        mj_left: "残り", mj_riichi: "リーチ", mj_tsumo: "ツモ", mj_ron: "ロン", mj_pon: "ポン", mj_chi: "チー",
        mj_kan: "カン", mj_pass: "パス", mj_kyuushu: "九種九牌", mj_next: "次へ", mj_pick: "リーチの牌を選ぶ",
        mj_draw: "流局", mj_tenpai: "聴牌", mj_over: "終局", mj_you: "自分", mj_wait: "待ち",
        mj_rest: "秒", mj_honba: "本場", mj_sticks: "供託", mj_dora: "ドラ",
        mj_kyuushu_end: "九種九牌で流局",
        mj_ms: "ms", mj_resp: "応答"
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
    layout
    if @unsupported
      draw_screen
      return
    end
    load_sheets
    st = ::FmrbAsterism.settings
    @id = st.node
    @locator = st.router
    @creds = st.credentials
    @creds_error = st.error
    @name = st.name.to_s.empty? ? @id : st.name.to_s
    @net = :connecting
    @net_note = ""
    @retry_at = 0
    @table = nil          # "<host>/games/<name>"
    @full = []            # tables that said they are full
    @scan_at = 0
    @sub = nil
    @token = nil
    @seat = nil
    @sit_f = nil
    @sit_at = 0
    @view_f = nil
    @act_f = nil
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
    @second = 0
    @mem_at = 0
    draw_screen
  end

  def on_resize(w, h)
    layout
    @dirty = true
  end

  def layout
    @x0 = @user_area_x0
    @y0 = @user_area_y0
    @w = @user_area_width
    @h = @user_area_height
    @big = @w >= 640 && @h >= 360
  end

  def load_sheets
    @sheets = nil
    names = ["tiles_hand_0.bmp", "tiles_hand_1.bmp", "tiles_river.bmp"]
    i = 0
    while i < names.size
      @gfx.sync_file("#{APP_DIR}/#{names[i]}", dest: "#{CACHE_DIR}/#{names[i]}")
      i += 1
    end
    h0 = SpriteImage.new(@gfx, width: 210, height: 224, transparent_color: KEY_COLOR, use_transparent: true)
    h0.load_bmp("#{CACHE_DIR}/tiles_hand_0.bmp")
    h1 = SpriteImage.new(@gfx, width: 210, height: 224, transparent_color: KEY_COLOR, use_transparent: true)
    h1.load_bmp("#{CACHE_DIR}/tiles_hand_1.bmp")
    rv = SpriteImage.new(@gfx, width: 240, height: 128, transparent_color: KEY_COLOR, use_transparent: true)
    rv.load_bmp("#{CACHE_DIR}/tiles_river.bmp")
    @sheets = [h0, h1, rv]
  rescue => e
    Log.error("mahjong: tiles: #{e.message}")
    @sheets = nil
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
      @sub = nil
      @scan_at = 0
      subscribe_state if @table
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
    @sit_f = nil
    @view_f = nil
    @act_f = nil
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

  def proxy
    ::Asterism[@table, timeout: CALL_TIMEOUT]
  end

  # The first table of a host on the network.
  def find_table
    now = Machine.board_millis
    return if now - @scan_at < SCAN_EVERY_MS
    @scan_at = now
    found = nil
    ::Asterism.each("*/#{HOST_APP}/*").each do |px|
      p = px.asterism_path
      o = p.split("/")[2].to_s
      found = p if found.nil? && o.start_with?("mahjong") && @full.index(p).nil?
    end
    @full = [] if found.nil? # every table was full: ask them all again
    return unless found
    @table = found
    Log.info("mahjong: table #{@table}")
    subscribe_state
    @dirty = true
  rescue ::Asterism::Error => e
    Log.info("mahjong: scan: #{e.message}")
  end

  def subscribe_state
    @sub = ::Asterism.session.subscribe("asterism/#{@table}/state", depth: 4)
  rescue ::Asterism::Error => e
    Log.info("mahjong: subscribe: #{e.message}")
    @sub = nil
  end

  def watch_state
    return unless @sub
    @sub.each_pending do |key, payload, att|
      begin
        pv = ::Asterism::Codec.unpack(payload)
        if pv.is_a?(Hash)
          v = pv["v"].to_i
          @want_v = v if v > @want_v
          @host_away = false
          @view = pv if @token.nil? # a spectator's picture until seated
        end
      rescue => e
        Log.info("mahjong: state: #{e.message}")
      end
    end
  end

  def sit
    return if @sit_f || @table.nil?
    @sit_f = proxy.async.sit(@name)
  rescue ::Asterism::Error => e
    @sit_f = nil
  end

  def collect_sit
    f = @sit_f
    return unless f && f.done?
    @sit_f = nil
    @sit_at = Machine.board_millis + VIEW_EVERY_MS # when it did not work
    begin
      r = f.value
      if r.is_a?(Hash) && r["token"]
        @token = r["token"].to_s
        @seat = r["seat"].to_i
        @host_away = false
        @note = ""
        Log.info("mahjong: seat #{@seat} (#{f.took_ms} ms)")
        ask_view
      elsif r.is_a?(Hash) && r["error"] == "full"
        # Try the next table of the network.
        @note = "#{t(:mj_full)}: #{@table}"
        @full << @table
        @table = nil
        begin
          @sub.close if @sub
        rescue
        end
        @sub = nil
      end
    rescue ::Asterism::Error => e
      @host_away = true
      Log.info("mahjong: sit: #{e.class}")
    end
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
        # The host lost our seat (a new state): sit again.
        @token = nil
        @sit_at = 0
        @seat = nil
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
    dl = v["dl"]
    @deadline = dl.is_a?(Array) && dl[0] == @seat ? Machine.board_millis + dl[1].to_i : nil
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
    @view ? bytes_of(@view["h"]) : []
  end

  def drawn
    d = @view ? @view["dt"] : nil
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

  def offers_of(type)
    out = []
    a = @view ? @view["a"] : nil
    return out unless a.is_a?(Array)
    i = 0
    while i < a.size
      out << a[i] if a[i].is_a?(Array) && a[i][0] == type
      i += 1
    end
    out
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

  def seat_at(r)
    ((@seat || 0) + r) % 4
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
    sc = k[0]
    ch = k[1]
    n = hand_codes.size + (drawn >= 0 ? 1 : 0)
    if sc == FmrbConst::KEY_LEFT
      @sel = @sel <= 0 ? n - 1 : @sel - 1
      @dirty = true
    elsif sc == FmrbConst::KEY_RIGHT
      @sel = @sel >= n - 1 ? 0 : @sel + 1
      @dirty = true
    elsif sc == FmrbConst::KEY_ENTER || ch == 13 || ch == 10
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
    idx = hand_index_at(x, y)
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
    return draw_text_tile(c, x, y, 42, 56) unless @sheets
    k = c % 20
    @gfx.draw_tile(@sheets[c < 20 ? 0 : 1].id, (k % 5) * 42, (k / 5) * 56, 42, 56, dst_x: x, dst_y: y)
  end

  def small_tile(c, x, y)
    return draw_text_tile(c, x, y, 24, 32) unless @sheets
    @gfx.draw_tile(@sheets[2].id, (c % 10) * 24, (c / 10) * 32, 24, 32, dst_x: x, dst_y: y)
  end

  def draw_text_tile(c, x, y, w, h)
    @gfx.fill_rect(x, y, w - 1, h - 1, C_TEXT)
    @gfx.draw_text(x + 2, y + 2, tile_text(c), 0x00)
  end

  def text(x, y, s, color = C_TEXT)
    @gfx.draw_text(x, y, s, color)
  end

  # Pixel width with the 12 px font (ASCII 6, the rest 12).
  def tw(s)
    w = 0
    i = 0
    n = s.bytesize
    while i < n
      b = s.getbyte(i)
      if b < 0x80
        w += 6
        i += 1
      else
        w += 12
        i += b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2)
      end
    end
    w
  end

  def button(type, tiles, x, y, label)
    w = tw(label) + 12
    @buttons << [type, x, y, w, 20, tiles]
    @gfx.fill_round_rect(x, y, w, 20, 4, C_BTN)
    text(x + 6, y + 4, label)
    w
  end

  def draw_screen
    @buttons = []
    clear_user_area(C_FELT)
    if @unsupported
      text(@x0 + 8, @y0 + 8, t(:mj_title))
      text(@x0 + 8, @y0 + 26, t(:mj_retro))
    elsif @view.nil? || @view["st"] == "wait" || @token.nil?
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
    if @lat_pending
      @lat = @lat_pending
      @lat_pending = nil
      Log.info("mahjong: act -> screen #{@lat} ms")
    end
  end

  def draw_lobby
    x = @x0 + 8
    y = @y0 + 8
    text(x, y, "#{t(:mj_title)}  #{@name}")
    line = if @net == :connecting
             "#{t(:mj_connecting)}: #{@locator}"
           elsif @net == :down || reconnecting?
             "#{t(:mj_lost)} #{@net_note}"
           elsif @table.nil?
             t(:mj_searching)
           elsif @host_away
             t(:mj_host_away)
           else
             "#{t(:mj_sitting)}: #{@table}"
           end
    text(x, y + 20, line, C_DIM)
    text(x, y + 40, @note, C_TURN) unless @note.empty?
  end

  def status_line
    return "#{t(:mj_lost)}" if @net != :up || reconnecting?
    return t(:mj_host_away) if @host_away
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

  # Name, wind, score, riichi of each seat.
  def draw_seats
    v = @view
    nm = v["nm"].is_a?(Array) ? v["nm"] : []
    sc = v["sc"].is_a?(Array) ? v["sc"] : []
    ri = v["ri"].to_i
    r = v["r"]
    cur = r.is_a?(Array) && r.size > 6 ? r[6].to_i : -1
    s = 0
    while s < 4
      label = "#{wind_of(s)} #{s == @seat ? t(:mj_you) : nm[s].to_s} #{sc[s]}"
      label = "#{label} #{t(:mj_riichi)}" if (ri >> s) & 1 == 1
      col = s == cur && v["st"] == "play" ? C_TURN : C_TEXT
      p = seat_label_pos(rel(s))
      text(p[0], p[1], label, col)
      s += 1
    end
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

  # The result of a round (or the game) over the middle of the table.
  def draw_result
    res = @view["res"]
    fin = @view["fin"]
    nm = @view["nm"].is_a?(Array) ? @view["nm"] : []
    bx = @x0 + (@big ? 150 : 40)
    by = @y0 + (@big ? 62 : 30)
    bw = @big ? 340 : 346
    bh = @big ? 206 : 132
    @gfx.fill_rect(bx, by, bw, bh, C_BOX)
    @gfx.draw_rect(bx, by, bw, bh, C_TURN)
    x = bx + 6
    y = by + 4
    if res.is_a?(Hash)
      k = res["k"].to_s
      head = if k == "tsumo_agari" then "#{seat_name(res['w'])} #{t(:mj_tsumo)}"
             elsif k == "ron_agari" then "#{seat_name(res['w'])} #{t(:mj_ron)} <- #{seat_name(res['f'])}"
             elsif k == "kyuushu_kyuuhai" then t(:mj_kyuushu_end)
             else t(:mj_draw)
             end
      text(x, y, head, C_TURN)
      y += 16
      if res["h"]
        hc = bytes_of(res["h"])
        i = 0
        while i < hc.size
          if @big
            small_tile(hc[i], x + i * 23, y)
          else
            text(x + i * 13, y, tile_text(hc[i]))
          end
          i += 1
        end
        ag = res["ag"].to_i
        if @big
          small_tile(ag, x + hc.size * 23 + 6, y)
        else
          text(x + hc.size * 13 + 4, y, tile_text(ag), C_TURN)
        end
        y += @big ? 36 : 15
        ys = res["y"].is_a?(Array) ? res["y"] : []
        i = 0
        while i < ys.size
          text(x + (i % 3) * 110, y + (i / 3) * 14, ys[i].to_s)
          i += 1
        end
        y += ((ys.size + 2) / 3) * 14 + 2
        text(x, y, res["pt"].to_s, C_TURN)
        y += 16
      elsif res["tp"].is_a?(Array)
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
      d = res["d"].is_a?(Array) ? res["d"] : []
      i = 0
      while i < 4 && i < d.size
        dv = d[i].to_i
        text(x + (i % 2) * 165, y + (i / 2) * 14, "#{seat_name(i)} #{dv > 0 ? '+' : ''}#{dv}")
        i += 1
      end
    end
    if fin.is_a?(Array) && @view["st"] == "over"
      y = by + bh - 74
      text(x, y, t(:mj_over), C_TURN)
      i = 0
      while i < fin.size
        f = fin[i]
        text(x, y + 16 + i * 13, "#{f[1]}. #{seat_name(f[0])} #{f[2]} (#{f[3].to_i / 10}.#{(f[3].to_i % 10).abs})")
        i += 1
      end
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
        if @table.nil?
          find_table
        else
          watch_state
          sit if @token.nil? && now >= @sit_at
          collect_sit
          collect_act
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
      # The VM pool, now and then, for the report (doc/ruby_asterism/report/mj1.md).
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
