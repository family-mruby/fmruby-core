# Othello over Asterism (doc/ruby_asterism, G1): two machines on the same
# Zenoh network play each other. Each game is an object of its own node,
# <ID>/game/othello, and calls the other player's (opponent.move(...)).
#
# - Connects with the machine's settings (FmrbAsterism.settings, CF1:
#   /home/asterism/config.toml and the credentials .tar beside it): the
#   router, else the machine's zenoh_router setting (the browser build's
#   ws/localhost:7449, or ?zenoh= from the page's URL), else the .tar's,
#   else tcp/zenohd:7447. The ID comes from the settings, the mDNS name,
#   the asterism_node setting (the browser's wasm-XXXXXX) or the board name.
#   A tls/ router connects straight to a cloud router with mutual TLS
#   (G2a): the .tar gives the certificate, and its name is the ID.
# - One connection (G1b): Asterism (app "game", object "othello"), and
#   Asterism.session, its Zenoh session, puts the state on
#   asterism/<ID>/game/othello/state after every move (and every
#   STATE_EVERY_MS during a game), for asterism-console and for spectators.
#   All keys stay under asterism/<ID>/game/** (the G0 ACL). One session also
#   gets through a router door whose pass lets one connection in.
# - The player's name is the profile's name (FmrbAsterism, saved in
#   /home/asterism/config.toml for every application of the machine); F2 or
#   the name button in the lobby changes it (Japanese through kana input),
#   Enter saves, Esc leaves it. Without one, the node ID stands for it.
# - The object's methods (who called comes from the call itself,
#   Asterism.current_call.from: protocol 1 has the caller's ID in the key):
#     info                              -> {"name", "status", "opponent"}
#     invite(name, gid)                 -> "asked" | "busy"
#     answer(gid, accept, your_color, name) -> true | false
#                                          (true: the inviter started the game)
#     cancel(gid)                       -> true | false
#     move(gid, n, x, y)                -> "ok" | the reason it was refused
#                                          (x = -1: a pass; n = moves so far)
#     resign(gid)                       -> true | false
#     state                             -> Hash (the same as the put)
# - Both sides keep the board and check every move they receive: the right
#   game, the right player, its turn, the move number and a legal square. A
#   move is applied by its player only after the other side said "ok". When
#   that answer is lost, or a move number does not fit, the player asks the
#   other's state and adopts the longer history when the shorter one is its
#   start and every move replays legally; otherwise the game ends.
# - The opponent leaving (its othello object gone): the game waits GRACE_MS
#   (60 s) for it, then ends. When it comes back in time, the states are
#   compared as above and the game goes on. A restarted app has lost its
#   game: the game ends. A router that restarts (an apply on the console, a
#   line that drops) cuts both players for a few seconds: the Zenoh binding
#   connects again by itself (Asterism.reconnecting? meanwhile; a page
#   gives up after a few minutes of tries) and the opponent comes back, so
#   the screen says "the line dropped, waiting" rather than "left". 60 s
#   covers a router restart (about 1-10 s, report/g1_5b.md) with room for a
#   browser page's spaced tries. Losing our own connection for good (an
#   older binding without reconnection): connect again every RECONNECT_MS,
#   same wait.
# - Who moves first: the invited player, on accepting, draws the colours.
#   Black moves first.
# - Keys: arrows move the cursor, Enter places (or passes when there is no
#   legal square, or invites in the lobby), Esc resigns (asks first) or
#   cancels; y / n accept or decline an invitation. A click on a square places
#   there; the buttons on the right take clicks too.
# - On a small screen (display_width under 320, the AtomS3R's 128x128) there
#   is no game: it shows the latest state put by any game on the network.

class OthelloBoard
  DX = [-1, 0, 1, -1, 1, -1, 0, 1]
  DY = [-1, -1, -1, 0, 0, 1, 1, 1]

  attr_reader :cells

  def initialize
    @cells = Array.new(64, 0)
    @cells[27] = 2
    @cells[28] = 1
    @cells[35] = 1
    @cells[36] = 2
  end

  def at(x, y)
    @cells[y * 8 + x]
  end

  # How many discs of the other colour a disc of colour c at (x, y) turns.
  def flips(x, y, c)
    return 0 if x < 0 || x > 7 || y < 0 || y > 7 || @cells[y * 8 + x] != 0
    o = 3 - c
    total = 0
    d = 0
    while d < 8
      dx = DX[d]
      dy = DY[d]
      cx = x + dx
      cy = y + dy
      n = 0
      while cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && @cells[cy * 8 + cx] == o
        cx += dx
        cy += dy
        n += 1
      end
      total += n if n > 0 && cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && @cells[cy * 8 + cx] == c
      d += 1
    end
    total
  end

  # Places a disc (the move must be legal) and turns the others.
  def place(x, y, c)
    o = 3 - c
    d = 0
    while d < 8
      dx = DX[d]
      dy = DY[d]
      cx = x + dx
      cy = y + dy
      n = 0
      while cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && @cells[cy * 8 + cx] == o
        cx += dx
        cy += dy
        n += 1
      end
      if n > 0 && cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && @cells[cy * 8 + cx] == c
        cx = x + dx
        cy = y + dy
        while @cells[cy * 8 + cx] == o
          @cells[cy * 8 + cx] = c
          cx += dx
          cy += dy
        end
      end
      d += 1
    end
    @cells[y * 8 + x] = c
  end

  # The legal squares of colour c, as y * 8 + x.
  def moves(c)
    out = []
    i = 0
    while i < 64
      out << i if @cells[i] == 0 && flips(i % 8, i / 8, c) > 0
      i += 1
    end
    out
  end

  def any_move?(c)
    i = 0
    while i < 64
      return true if @cells[i] == 0 && flips(i % 8, i / 8, c) > 0
      i += 1
    end
    false
  end

  def count(c)
    n = 0
    i = 0
    while i < 64
      n += 1 if @cells[i] == c
      i += 1
    end
    n
  end

  # Eight strings of eight characters: "." empty, "B" black, "W" white.
  def rows
    out = []
    y = 0
    while y < 8
      s = ""
      x = 0
      while x < 8
        v = @cells[y * 8 + x]
        s << (v == 1 ? "B" : (v == 2 ? "W" : "."))
        x += 1
      end
      out << s
      y += 1
    end
    out
  end
end

# A game as a history of moves: two characters each, "d3" (column a-h, row
# 1-8) or "--" for a pass. Replaying it gives the board, whose turn it is,
# and whether it is over; a history with an illegal move replays to nil.
class OthelloGame
  attr_reader :board, :hist, :turn, :over

  def self.code(x, y)
    return "--" if x < 0
    "abcdefgh"[x, 1] + (y + 1).to_s
  end

  def self.replay(hist)
    g = OthelloGame.new
    i = 0
    while i + 1 < hist.length
      c = hist[i, 2]
      if c == "--"
        x = -1
        y = -1
      else
        x = "abcdefgh".index(c[0, 1])
        y = c[1, 1].to_i - 1
        return nil if x.nil? || y < 0 || y > 7
      end
      return nil unless g.apply(g.turn, x, y)
      i += 2
    end
    g
  end

  def initialize
    @board = OthelloBoard.new
    @hist = ""
    @turn = 1
    @over = false
  end

  def n
    @hist.length / 2
  end

  def last
    return nil if @hist.empty?
    @hist[@hist.length - 2, 2]
  end

  # Whether colour c may play (x, y) now (x = -1: pass).
  def legal?(c, x, y)
    return false if @over || c != @turn
    return !@board.any_move?(c) if x < 0
    @board.flips(x, y, c) > 0
  end

  # Ends it without a move (a resignation, an abandoned game).
  def end!
    @over = true
  end

  # Plays it when legal; returns whether it did.
  def apply(c, x, y)
    return false unless legal?(c, x, y)
    @board.place(x, y, c) if x >= 0
    @hist << OthelloGame.code(x, y)
    @turn = 3 - c
    @over = true if !@board.any_move?(@turn) && !@board.any_move?(c)
    true
  end
end

class OthelloApp < FmrbApp
  CONF_FILE = "/etc/system_conf.toml"
  APP = "game"
  OBJ = "othello"
  CALL_TIMEOUT = 3.0
  GRACE_MS = 60000
  INVITE_MS = 60000
  RECONNECT_MS = 3000
  SCAN_EVERY_MS = 2000
  INFO_EVERY_MS = 3000
  STATE_EVERY_MS = 10000
  WIFI_WAIT_MS = 20000
  # The game's own colours (a board game draws its own picture).
  C_BOARD = 0x10
  C_LINE = 0x00
  C_BLACK = 0x00
  C_WHITE = 0xFF
  C_CURSOR = 0xE0
  C_HINT = 0x6D
  C_LAST = 0xFC

  # The exposed object. Its methods run from Asterism.poll (the update loop)
  # and never wait.
  class Remote
    def initialize(app)
      @app = app
    end

    def info
      @app.r_info
    end

    def invite(name, gid)
      @app.r_invite(caller_id, name.to_s, gid.to_s)
    end

    def answer(gid, accept, your_color, name)
      @app.r_answer(caller_id, gid.to_s, accept ? true : false, your_color.to_s, name.to_s)
    end

    def cancel(gid)
      @app.r_cancel(caller_id, gid.to_s)
    end

    def move(gid, n, x, y)
      @app.r_move(caller_id, gid.to_s, n.to_i, x.to_i, y.to_i)
    end

    def resign(gid)
      @app.r_resign(caller_id, gid.to_s)
    end

    # The calling node's ID, from the call being answered.
    def caller_id
      c = ::Asterism.current_call
      c ? c.from.to_s : ""
    end

    def state
      @app.state_hash
    end
  end

  # ------------------------------------------------------------ settings

  def conf_int(key)
    conf = FmrbToml.load(CONF_FILE)
    v = conf ? conf[key] : nil
    v.is_a?(Integer) ? v : 0
  end

  def on_create
    @started = Machine.board_millis
    st = ::FmrbAsterism.settings
    @id = st.node
    @locator = st.router
    @creds = st.credentials
    @creds_error = st.error
    width = conf_int("display_width")
    @small = width > 0 && width < 320
    request_fullscreen(true) if @small
    @gfx.set_font(:ja, 12) unless @small
    @net = :connecting      # :connecting / :up / :down
    @net_note = ""
    @retry_at = 0
    @was_reconnecting = false
    @zs = nil
    @watch = nil
    @state_key = "asterism/#{@id}/#{APP}/#{OBJ}/state"
    @scan = true
    @scan_at = 0
    @peers = []             # other nodes with game/othello alive
    @pinfo = {}             # node => [name, status, opponent]
    @info_f = nil
    @info_i = 0
    @info_at = 0
    @sel = 0
    @mode = :lobby          # :lobby / :game
    @note = ""
    @inv_out = nil          # [to, gid, at]
    @inv_out_f = nil
    @inv_in = nil           # [from, name, gid, at]
    @answer_f = nil         # [gid, from, name, my_color, future]
    @misc_f = []
    # the game
    @g = nil                # OthelloGame
    @gid = nil
    @opp = nil
    @opp_name = ""
    @my = 1
    @result = ""            # English, for the state
    @end_note = ""          # Japanese, for the screen
    @move_f = nil           # [code, x, y, future]
    @sync_f = nil
    @need_sync = false
    @sync_periodic = false  # the timer asked (no note on the screen)
    @sync_quiet = false
    @lost_at = nil          # when the opponent (or our network) went
    @confirm = false        # asking "resign?"
    @put_at = 0
    @puts = 0
    @cx = 3
    @cy = 3
    @hints = []
    @keys = []
    @clicks = []
    @hits = FmrbUI::HitAreas.new
    @edit = nil             # the name being typed (a String), or nil
    @u8 = nil               # a UTF-8 character coming in byte by byte (kana input)
    @u8_need = 0
    @spec = nil             # small screen: the latest state seen
    @spec_from = ""
    @dirty = true
    @second = 0
    layout
    draw_screen
  end

  def on_resize(w, h)
    layout
    @dirty = true
  end

  def uptime_ms
    Machine.board_millis - @started
  end

  def my_name
    pr = ::Asterism.profile
    n = pr ? pr["name"] : nil
    n ? n.to_s : @id
  rescue
    @id
  end

  # ------------------------------------------------------------ network

  # A board started at boot may not have WiFi yet: wait for it (up to
  # WIFI_WAIT_MS). The sim and the browser have their network at once.
  def network_ready?
    return true if FmrbConst::PLATFORM != "esp32"
    info = FmrbApp.wifi_info
    return true if info.nil? || info[:connected]
    uptime_ms > WIFI_WAIT_MS
  end

  def connect
    t0 = Machine.board_millis
    begin
      ::Asterism.close
      raise ::Asterism::Error, @creds_error if @creds_error
      ::Asterism.connect(@locator, node: @id, app: APP, credentials: @creds)
      ::Asterism.expose(OBJ, OthelloApp::Remote.new(self),
                        methods: { info: 0, invite: 2, answer: 4, cancel: 1, move: 4, resign: 1, state: 0 }) unless @small
      ::Asterism.on_join { |n| @scan = true }
      ::Asterism.on_leave { |n| @scan = true }
      @zs = ::Asterism.session # Asterism's: closed with Asterism.close
      @watch = @small ? @zs.subscribe("asterism/*/#{APP}/#{OBJ}/state", depth: 4) : nil
      @net = :up
      @net_note = ""
      @scan = true
      @need_sync = true if @g && !@g.over
      put_state
    rescue ::Asterism::Error => e
      @net = :down
      @net_note = e.message.to_s
      @retry_at = Machine.board_millis + RECONNECT_MS
      @lost_at ||= Machine.board_millis if @g && !@g.over
    end
    Log.info("othello: #{@id} #{@net} (#{@locator}, #{Machine.board_millis - t0} ms) #{@net_note}")
    @dirty = true
  end

  def lost(why)
    Log.info("othello: lost: #{why}")
    @net = :down
    @net_note = why
    @retry_at = Machine.board_millis + RECONNECT_MS
    @peers = []
    @info_f = nil
    @inv_out_f = nil
    @answer_f = nil
    @move_f = nil
    @sync_f = nil
    @misc_f = []
    @zs = nil # Asterism.close closes it, with @watch
    @watch = nil
    begin
      ::Asterism.close
    rescue
    end
    @lost_at ||= Machine.board_millis if @g && !@g.over
    @dirty = true
  end

  def exchange
    unless ::Asterism.poll
      lost("asterism: #{::Asterism.lost_reason}")
      return
    end
    r = reconnecting?
    if r != @was_reconnecting
      @was_reconnecting = r
      Log.info(r ? "othello: the line dropped, connecting again" : "othello: connected again")
      @dirty = true
    end
  rescue => e
    lost("#{e.class}: #{e.message}")
  end

  def othello(node)
    ::Asterism["#{node}/#{APP}/#{OBJ}", timeout: CALL_TIMEOUT].async
  end

  # Fire and forget (cancel, resign): looked at only to drop it.
  def send_misc(node)
    @misc_f << yield(othello(node))
  rescue ::Asterism::Error => e
    Log.info("othello: send to #{node}: #{e.message}")
  end

  def put_state
    return unless @zs
    @zs.put(@state_key, ::Asterism::Codec.pack(state_hash))
    @puts += 1
    @put_at = Machine.board_millis
  rescue => e
    Log.info("othello: put: #{e.message}")
  end

  def color_name(c)
    c == 1 ? "black" : "white"
  end

  def state_hash
    g = @g
    unless g
      return { "game" => nil, "node" => @id, "name" => my_name, "status" => status_word }
    end
    black = @my == 1 ? @id : @opp
    white = @my == 1 ? @opp : @id
    { "game" => @gid, "node" => @id, "name" => my_name, "status" => status_word,
      "black" => black, "white" => white, "turn" => g.over ? nil : color_name(g.turn),
      "n" => g.n, "last" => g.last, "hist" => g.hist, "board" => g.board.rows,
      "black_count" => g.board.count(1), "white_count" => g.board.count(2),
      "over" => g.over, "result" => @result, "opponent_away" => !@lost_at.nil? }
  end

  def status_word
    return "watching" if @small
    return (@g.over ? "over" : "playing") if @g
    return "inviting" if @inv_out
    return "invited" if @inv_in
    "idle"
  end

  # ------------------------------------------------------------ the object

  def r_info
    { "name" => my_name, "status" => status_word, "opponent" => (@g && !@g.over) ? @opp : nil }
  end

  def r_invite(from, name, gid)
    return "busy" if from == @id
    return "busy" if (@g && !@g.over) || @inv_out || @inv_in || @answer_f
    @inv_in = [from, name.empty? ? from : name, gid, Machine.board_millis]
    @note = ""
    @mode = :lobby
    @dirty = true
    Log.info("othello: invited by #{from} (#{gid})")
    "asked"
  end

  def r_answer(from, gid, accept, your_color, name)
    o = @inv_out
    return false unless o && o[0] == from && o[1] == gid
    @inv_out = nil
    @inv_out_f = nil
    if accept
      start_game(gid, from, name.empty? ? from : name, your_color == "black" ? 1 : 2)
    else
      @note = "#{peer_name(from)} に断られた"
      @dirty = true
    end
    true
  end

  def r_cancel(from, gid)
    i = @inv_in
    return false unless i && i[0] == from && i[2] == gid
    @inv_in = nil
    @note = "#{i[1]} が誘いを取り消した"
    @dirty = true
    true
  end

  def r_move(from, gid, n, x, y)
    g = @g
    return "no game" unless g && gid == @gid
    return "not your game" unless from == @opp
    return "game over" if g.over
    if n < g.n
      # Sent again after a lost answer: the same move is fine.
      return g.hist[n * 2, 2] == OthelloGame.code(x, y) ? "ok" : "out of sync"
    end
    if n > g.n
      @need_sync = true
      return "out of sync"
    end
    return "not your turn" unless g.turn == 3 - @my
    return "illegal" unless g.apply(3 - @my, x, y)
    Log.info("othello: #{from} played #{g.last} (#{g.n})")
    after_move
    "ok"
  end

  def r_resign(from, gid)
    return false unless @g && gid == @gid && from == @opp
    return true if @g.over
    finish("#{color_name(3 - @my)} resigned", "#{@opp_name} が投了した。あなたの勝ち")
    true
  end

  # ------------------------------------------------------------ the game

  def start_game(gid, opp, name, my)
    @g = OthelloGame.new
    @gid = gid
    @opp = opp
    @opp_name = name
    @my = my
    @result = ""
    @end_note = ""
    @lost_at = nil
    @confirm = false
    @move_f = nil
    @sync_f = nil
    @need_sync = false
    @inv_in = nil
    @inv_out = nil
    @mode = :game
    @cx = 3
    @cy = 3
    @note = "#{name} と対戦 (#{my == 1 ? '黒' : '白'}、黒が先)"
    Log.info("othello: game #{gid} with #{opp}, #{color_name(my)}")
    after_move
  end

  def after_move
    g = @g
    @hints = (!g.over && g.turn == @my) ? g.board.moves(@my) : []
    if g.over && @result.empty?
      b = g.board.count(1)
      w = g.board.count(2)
      mine = @my == 1 ? b : w
      theirs = @my == 1 ? w : b
      word = mine > theirs ? "あなたの勝ち" : (mine < theirs ? "あなたの負け" : "引き分け")
      win = b > w ? "black wins" : (b < w ? "white wins" : "draw")
      @result = "#{win} #{b}-#{w}"
      @end_note = "終局 黒 #{b} - 白 #{w}。#{word}"
      Log.info("othello: game #{@gid} over: #{@result}")
    end
    put_state
    @dirty = true
  end

  def finish(result, note)
    return unless @g
    @g.end!
    @result = result
    @end_note = note
    @hints = []
    @lost_at = nil
    @confirm = false
    @move_f = nil
    Log.info("othello: game #{@gid} ends: #{result}")
    put_state
    @dirty = true
  end

  def back_to_lobby
    @g = nil
    @gid = nil
    @opp = nil
    @mode = :lobby
    @note = ""
    @scan = true
    put_state
    @dirty = true
  end

  def my_turn?
    @g && !@g.over && @g.turn == @my && @move_f.nil? && @sync_f.nil? && @lost_at.nil? && @net == :up
  end

  def play(x, y)
    return unless my_turn?
    g = @g
    unless g.legal?(@my, x, y)
      @note = x < 0 ? "置ける所があるのでパスできない" : "そこには置けない"
      @dirty = true
      return
    end
    n = g.n
    gid = @gid
    @move_f = [OthelloGame.code(x, y), x, y, othello(@opp).move(gid, n, x, y)]
    @note = "送っています: #{OthelloGame.code(x, y)}"
    @dirty = true
  rescue ::Asterism::Error => e
    @move_f = nil
    @note = "送れない: #{e.message}"
    @dirty = true
  end

  def collect_move
    m = @move_f
    return unless m && m[3].done?
    @move_f = nil
    f = m[3]
    begin
      v = f.value
      if v == "ok"
        @g.apply(@my, m[1], m[2])
        @note = "#{m[0] == '--' ? 'パス' : m[0]} (#{f.took_ms} ms)"
        Log.info("othello: played #{m[0]} (#{@g.n}), #{f.took_ms} ms")
        after_move
      else
        @note = "相手が断った: #{v}"
        Log.info("othello: #{m[0]} refused: #{v}")
        @need_sync = true
      end
    rescue ::Asterism::Error => e
      @note = "答えが無い (#{e.class}): 盤を合わせる"
      Log.info("othello: #{m[0]}: #{e.class}: #{e.message}")
      @need_sync = true
    end
    @dirty = true
  end

  # Asks the opponent's state and compares the histories (see the top).
  def sync
    return unless @g && !@g.over && @need_sync && @sync_f.nil? && @move_f.nil? && @net == :up
    return if @lost_at
    @need_sync = false
    @sync_quiet = @sync_periodic
    @sync_periodic = false
    @sync_f = othello(@opp).state
  rescue ::Asterism::Error => e
    @sync_f = nil
    @need_sync = true
  end

  def collect_sync
    f = @sync_f
    return unless f && f.done?
    @sync_f = nil
    begin
      st = f.value
    rescue ::Asterism::Error => e
      Log.info("othello: sync: #{e.class}: #{e.message}")
      @need_sync = true
      return
    end
    return unless @g && !@g.over
    unless st.is_a?(Hash) && st["game"] == @gid
      finish("abandoned: #{@opp} lost the game", "相手の対局が無くなったので終わり")
      return
    end
    theirs = st["hist"].to_s
    mine = @g.hist
    if theirs == mine
      @note = "盤が合っている (#{@g.n} 手)" unless @sync_quiet
    elsif theirs.length > mine.length && theirs.start_with?(mine)
      ng = OthelloGame.replay(theirs)
      if ng
        @g = ng
        @note = "相手の盤に合わせた (#{ng.n} 手)"
        Log.info("othello: synced to #{ng.n} moves")
        after_move
        finish_by_state(st) if st["over"] && !ng.over
      else
        finish("abandoned: boards differ", "盤が合わないので終わり")
      end
    elsif mine.length > theirs.length && mine.start_with?(theirs)
      # They are behind; they catch up when they ask.
      Log.info("othello: #{@opp} is behind (#{theirs.length / 2} < #{mine.length / 2})")
    else
      finish("abandoned: boards differ", "盤が合わないので終わり")
    end
    finish_by_state(st) if st["over"] && @g && !@g.over && theirs == @g.hist
    @dirty = true
  end

  # The other side ended a game we still play (a resignation whose call was
  # lost): take its result.
  def finish_by_state(st)
    r = st["result"].to_s
    finish(r, "相手の側で終わっている: #{r}")
  end

  def check_opponent(alive)
    return unless @g && !@g.over
    now = Machine.board_millis
    if alive
      if @lost_at && @net == :up
        @lost_at = nil
        @need_sync = true
        @note = "#{@opp_name} が戻った。盤を合わせる"
        Log.info("othello: #{@opp} is back")
        @dirty = true
      end
    elsif @lost_at.nil?
      @lost_at = now
      @note = ""
      Log.info("othello: #{@opp} left")
      put_state
      @dirty = true
    end
  end

  def check_grace
    return unless @g && !@g.over && @lost_at
    if Machine.board_millis - @lost_at >= GRACE_MS
      if @net == :up && !reconnecting?
        finish("abandoned: #{@opp} left", "相手が戻らなかったので終わり")
      else
        finish("abandoned: network lost", "網に戻れなかったので終わり")
      end
    end
  end

  # ------------------------------------------------------------ the lobby

  def peer_name(node)
    i = @pinfo[node]
    i && !i[0].to_s.empty? ? i[0].to_s : node
  end

  def scan
    now = Machine.board_millis
    return unless @scan || now - @scan_at >= SCAN_EVERY_MS
    @scan = false
    @scan_at = now
    list = []
    ::Asterism.each("*/#{APP}/#{OBJ}").each do |px|
      n = px.asterism_path.split("/")[0]
      list << n unless n == @id
    end
    @dirty = true if list != @peers
    @peers = list
    @sel = list.size - 1 if @sel >= list.size
    @sel = 0 if @sel < 0
    check_opponent(list.index(@opp) ? true : false) if @g
    i = @inv_in
    if i && !list.index(i[0])
      @inv_in = nil
      @note = "#{i[1]} がいなくなった"
      @dirty = true
    end
    o = @inv_out
    if o && !list.index(o[0])
      @inv_out = nil
      @inv_out_f = nil
      @note = "#{peer_name(o[0])} がいなくなった"
      @dirty = true
    end
  rescue ::Asterism::Error => e
    Log.info("othello: scan: #{e.message}")
  end

  # One info call at a time, round the peers.
  def ask_info
    f = @info_f
    if f && f[1].done?
      @info_f = nil
      begin
        v = f[1].value
        if v.is_a?(Hash)
          old = @pinfo[f[0]]
          now = [v["name"].to_s, v["status"].to_s, v["opponent"].to_s]
          @pinfo[f[0]] = now
          @dirty = true if old != now && @mode == :lobby
        end
      rescue ::Asterism::Error => e
        Log.info("othello: info #{f[0]}: #{e.class}")
      end
    end
    return if @info_f || @peers.empty?
    now = Machine.board_millis
    return if now - @info_at < INFO_EVERY_MS / @peers.size
    @info_at = now
    @info_i = 0 if @info_i >= @peers.size
    node = @peers[@info_i]
    @info_i += 1
    @info_f = [node, othello(node).info]
  rescue ::Asterism::Error => e
    @info_f = nil
  end

  def invite
    return if @inv_out || @inv_in || @peers.empty? || @net != :up
    to = @peers[@sel]
    gid = "#{@id}-#{Machine.board_millis}"
    name = my_name
    @inv_out = [to, gid, Machine.board_millis]
    @inv_out_f = othello(to).invite(name, gid)
    @note = ""
    @dirty = true
  rescue ::Asterism::Error => e
    @inv_out = nil
    @note = "誘えない: #{e.message}"
    @dirty = true
  end

  def collect_invite
    f = @inv_out_f
    return unless f && f.done?
    @inv_out_f = nil
    o = @inv_out
    begin
      v = f.value
      return if v == "asked"
      @note = v == "busy" ? "#{peer_name(o ? o[0] : '?')} は今は受けられない" : "誘えない: #{v}"
    rescue ::Asterism::Error => e
      @note = "誘えない: #{e.class}"
    end
    @inv_out = nil
    @dirty = true
  end

  def cancel_invite
    o = @inv_out
    return unless o
    @inv_out = nil
    @inv_out_f = nil
    gid = o[1]
    send_misc(o[0]) { |p| p.cancel(gid) }
    @note = "誘いを取り消した"
    @dirty = true
  end

  def reply_invite(accept)
    i = @inv_in
    return unless i && @answer_f.nil?
    @inv_in = nil
    gid = i[2]
    name = my_name
    if accept
      # The invited side draws the colours.
      mine = (Machine.board_millis / 7) % 2 == 0 ? 1 : 2
      theirs = color_name(3 - mine)
      @answer_f = [gid, i[0], i[1], mine, othello(i[0]).answer(gid, true, theirs, name)]
      @note = "始めています..."
    else
      send_misc(i[0]) { |p| p.answer(gid, false, "", name) }
      @note = "断った"
    end
    @dirty = true
  rescue ::Asterism::Error => e
    @answer_f = nil
    @note = "答えられない: #{e.message}"
    @dirty = true
  end

  def collect_answer
    a = @answer_f
    return unless a && a[4].done?
    @answer_f = nil
    begin
      ok = a[4].value
      Log.info("othello: answer to #{a[1]}: #{ok} (#{a[4].took_ms} ms)")
      if ok
        start_game(a[0], a[1], a[2], a[3])
      else
        @note = "#{a[2]} は誘いをやめていた"
      end
    rescue ::Asterism::Error => e
      # The inviter may have started: start here too and compare the states
      # (the game ends at once when the inviter has no such game).
      start_game(a[0], a[1], a[2], a[3])
      @need_sync = true
    end
    @dirty = true
  end

  def check_invites
    now = Machine.board_millis
    if @inv_out && now - @inv_out[2] > INVITE_MS
      cancel_invite
      @note = "答えが無いので誘いを取り消した"
    end
    if @inv_in && now - @inv_in[3] > INVITE_MS
      @inv_in = nil
      @note = "誘いの期限が切れた"
      @dirty = true
    end
    @misc_f = @misc_f.reject { |f| f.done? } unless @misc_f.empty?
  end

  def resign
    return unless @g && !@g.over
    gid = @gid
    send_misc(@opp) { |p| p.resign(gid) } if @net == :up
    finish("#{color_name(@my)} resigned", "投了した。あなたの負け")
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
    enter = sc == FmrbConst::KEY_ENTER || ch == 13 || ch == 10
    esc = sc == FmrbConst::KEY_ESC || ch == 27
    if @edit
      edit_key(sc, ch, enter, esc)
      @dirty = true
      return
    end
    if @inv_in
      reply_invite(true) if enter || ch == 121 || ch == 89   # y
      reply_invite(false) if esc || ch == 110 || ch == 78    # n
      return
    end
    if @mode == :lobby
      if @inv_out
        cancel_invite if esc
      elsif sc == FmrbConst::KEY_F2
        start_edit
      elsif sc == FmrbConst::KEY_UP
        @sel -= 1 if @sel > 0
      elsif sc == FmrbConst::KEY_DOWN
        @sel += 1 if @sel < @peers.size - 1
      elsif enter
        invite
      end
      @dirty = true
      return
    end
    g = @g
    return unless g
    if g.over
      back_to_lobby if enter || esc
      return
    end
    if @confirm
      if enter || ch == 121 || ch == 89
        resign
      elsif esc || ch == 110 || ch == 78
        @confirm = false
      end
      @dirty = true
      return
    end
    if esc
      @confirm = true
    elsif sc == FmrbConst::KEY_UP
      @cy = (@cy + 7) % 8
    elsif sc == FmrbConst::KEY_DOWN
      @cy = (@cy + 1) % 8
    elsif sc == FmrbConst::KEY_LEFT
      @cx = (@cx + 7) % 8
    elsif sc == FmrbConst::KEY_RIGHT
      @cx = (@cx + 1) % 8
    elsif enter
      if my_turn? && !g.board.any_move?(@my)
        play(-1, -1)
      else
        play(@cx, @cy)
      end
    end
    @dirty = true
  end

  def run_click(c)
    x = c[0]
    y = c[1]
    b = @hits.id_at(x, y)
    if b
      run_button(b)
      @dirty = true
      return
    end
    if @mode == :lobby && !@inv_out && !@inv_in
      row = (y - @list_y) / 15
      if x >= @x0 && y >= @list_y && row >= 0 && row < @peers.size
        @sel = row
        @dirty = true
      end
      return
    end
    if @mode == :game && @g && !@g.over && !@confirm
      cx = (x - @bx) / @cell
      cy = (y - @by) / @cell
      if x >= @bx && y >= @by && cx < 8 && cy < 8
        @cx = cx
        @cy = cy
        play(cx, cy)
        @dirty = true
      end
    end
  end

  def run_button(id)
    case id
    when :invite then invite
    when :cancel then cancel_invite
    when :accept then reply_invite(true)
    when :decline then reply_invite(false)
    when :pass then play(-1, -1)
    when :resign then @confirm = true
    when :yes then resign
    when :no then @confirm = false
    when :lobby then back_to_lobby
    when :name then start_edit
    when :name_save then save_name
    when :name_cancel then @edit = nil
    end
  end

  # ------------------------------------------------------------ the name

  def start_edit
    pr = ::Asterism.profile
    @edit = (pr && pr["name"] ? pr["name"].to_s : "").dup
    @u8_need = 0
    @dirty = true
  end

  # Keys while the name is typed. Kana input sends a composed character as
  # its UTF-8 bytes, one key event each, with no scancode.
  def edit_key(sc, ch, enter, esc)
    if enter
      save_name
    elsif esc
      @edit = nil
    elsif sc == FmrbConst::KEY_BACKSPACE || ch == 8
      @u8_need = 0
      n = @edit.length
      @edit = @edit[0, n - 1] if n > 0
    elsif ch >= 0x80
      c = utf8_byte(ch)
      add_to_name(c) if c
    elsif ch >= 32 && ch <= 126
      @u8_need = 0
      add_to_name(ch.chr)
    end
  end

  def add_to_name(c)
    return if @edit.bytesize + c.bytesize > ::Asterism::Profile::NAME_MAX
    @edit = @edit + c
  end

  # One byte of a UTF-8 character; the character once it is whole.
  def utf8_byte(b)
    if b >= 0xC0
      @u8_need = b >= 0xF0 ? 3 : (b >= 0xE0 ? 2 : 1)
      @u8 = " ".dup
      @u8.setbyte(0, b)
      return nil
    end
    return nil if @u8_need == 0 # a continuation with nothing in front
    one = " ".dup
    one.setbyte(0, b)
    @u8 = @u8 + one
    @u8_need -= 1
    @u8_need == 0 ? @u8 : nil
  end

  # Saves the name for every application of this machine and puts it in
  # the profile now (the console and the other players see it).
  def save_name
    name = @edit.to_s.strip
    @edit = nil
    ::FmrbAsterism.save_name(name)
    @note = name.empty? ? "名前を消した (ID を使う)" : "名前を「#{name}」にした"
    Log.info("othello: name saved (#{name.bytesize} bytes)")
  rescue => e
    @note = "名前を保存できない: #{e.message}"
  ensure
    @dirty = true
  end

  # ------------------------------------------------------------ the screen

  def layout
    @x0 = @user_area_x0
    @y0 = @user_area_y0
    uw = @user_area_width
    uh = @user_area_height
    c = (uh - 6) / 8
    m = (uw - 136) / 8
    c = m if m < c
    c = 8 if c < 8
    @cell = c
    @bx = @x0 + 3
    @by = @y0 + 3
    @px = @bx + c * 8 + 6
    @pw = @x0 + uw - @px - 2
    @list_y = @y0 + 34
    @line_h = 14
  end

  def button(id, x, y, w, label)
    @gfx.draw_button(x, y, w, 16, label, theme_accent, theme_fg_light)
    @hits.add(id, x, y, w, 16)
  end

  def text(x, y, s, color = nil)
    @gfx.draw_text(x, y, s, color || theme_fg)
  end

  def draw_screen
    @hits.clear
    return draw_small if @small
    clear_user_area
    if @mode == :game && @g
      draw_board
      draw_panel
    else
      draw_lobby
    end
    @gfx.present
  end

  def net_line
    return "つないでいます: #{@locator}" if @net == :connecting
    return "網から切れた: つなぎ直し中" if @net == :down || reconnecting?
    "#{@id} (#{@locator})"
  end

  # The binding is opening our lost connection again (asterism with
  # reconnection; an older one has no reconnecting?).
  def reconnecting?
    @net == :up && ::Asterism.respond_to?(:reconnecting?) && ::Asterism.reconnecting?
  rescue
    false
  end

  def draw_lobby
    x = @x0 + 4
    y = @y0 + 3
    lw = @user_area_width - 8
    text(x, y, "オセロ: 相手を選んで誘う")
    me = my_name
    text(x + 160, y, fit("名前: #{me}", lw - 162), me == @id ? theme_border : theme_fg)
    text(x, y + 15, fit(net_line, lw), theme_border)
    ly = @list_y
    if @peers.empty?
      text(x, ly, @net == :up ? "相手を探しています (同じ網で othello を開く)" : "-", theme_border)
    end
    i = 0
    while i < @peers.size && i < 8
      n = @peers[i]
      inf = @pinfo[n]
      st = inf ? inf[1] : "?"
      word = st == "idle" ? "待っている" : (st == "playing" ? "対戦中" : (st == "over" ? "終局を表示中" : (st == "inviting" || st == "invited" ? "誘い中" : st)))
      label = inf && !inf[0].empty? && inf[0] != n ? "#{inf[0]} (#{n})" : n
      yy = ly + i * 15
      if i == @sel
        @gfx.fill_rect(@x0 + 2, yy - 1, @user_area_width - 4, 14, theme_accent)
        text(x, yy, fit("#{label}  #{word}", lw), theme_fg_light)
      else
        text(x, yy, fit("#{label}  #{word}", lw))
      end
      i += 1
    end
    by = @y0 + @user_area_height - 40
    if @edit
      text(x, by - 2, fit("名前: Enter で保存、Esc でやめる、空なら ID", lw), theme_accent)
      @gfx.fill_rect(x, by + 15, 196, 18, theme_fg_light)
      @gfx.draw_rect(x, by + 15, 196, 18, theme_accent)
      shown = @edit
      shown = shown[1, shown.length] while shown.length > 0 && fit(shown, 180) != shown
      text(x + 3, by + 18, "#{shown}_")
      button(:name_save, x + 202, by + 16, 60, "保存")
      button(:name_cancel, x + 268, by + 16, 70, "やめる")
    elsif @inv_in
      text(x, by - 2, fit("#{@inv_in[1]} から誘い。受ける?", lw), theme_accent)
      button(:accept, x, by + 16, 90, "受ける (Y)")
      button(:decline, x + 98, by + 16, 90, "断る (N)")
    elsif @inv_out
      text(x, by - 2, fit("#{peer_name(@inv_out[0])} を誘っています...", lw), theme_accent)
      button(:cancel, x, by + 16, 110, "取り消す (Esc)")
    else
      text(x, by - 2, fit(@note, lw), theme_border)
      button(:invite, x, by + 16, 90, "誘う (Enter)") unless @peers.empty?
      button(:name, x + 98, by + 16, 90, "名前 (F2)")
    end
  end

  def draw_board
    c = @cell
    g = @g
    @gfx.fill_rect(@bx, @by, c * 8 + 1, c * 8 + 1, C_BOARD)
    i = 0
    while i <= 8
      @gfx.draw_line(@bx + i * c, @by, @bx + i * c, @by + c * 8, C_LINE)
      @gfx.draw_line(@bx, @by + i * c, @bx + c * 8, @by + i * c, C_LINE)
      i += 1
    end
    r = c / 2 - 2
    r = 2 if r < 2
    last = g.last
    lx = -1
    ly = -1
    if last && last != "--"
      lx = "abcdefgh".index(last[0, 1])
      ly = last[1, 1].to_i - 1
    end
    cells = g.board.cells
    i = 0
    while i < 64
      v = cells[i]
      x = i % 8
      y = i / 8
      mx = @bx + x * c + c / 2
      my = @by + y * c + c / 2
      if v == 1
        @gfx.fill_circle(mx, my, r, C_BLACK)
      elsif v == 2
        @gfx.fill_circle(mx, my, r, C_WHITE)
        @gfx.draw_circle(mx, my, r, C_LINE)
      end
      @gfx.fill_rect(mx - 1, my - 1, 3, 3, C_LAST) if x == lx && y == ly
      i += 1
    end
    j = 0
    while j < @hints.size
      h = @hints[j]
      @gfx.fill_circle(@bx + (h % 8) * c + c / 2, @by + (h / 8) * c + c / 2, 2, C_HINT)
      j += 1
    end
    unless g.over
      @gfx.draw_rect(@bx + @cx * c + 1, @by + @cy * c + 1, c - 1, c - 1, C_CURSOR)
      @gfx.draw_rect(@bx + @cx * c + 2, @by + @cy * c + 2, c - 3, c - 3, C_CURSOR)
    end
  end

  def draw_panel
    g = @g
    x = @px
    y = @y0 + 3
    n = @pw / 6
    me = @my == 1 ? "黒" : "白"
    them = @my == 1 ? "白" : "黒"
    mc = g.board.count(@my)
    tc = g.board.count(3 - @my)
    @gfx.fill_circle(x + 5, y + 6, 5, @my == 1 ? C_BLACK : C_WHITE)
    @gfx.draw_circle(x + 5, y + 6, 5, theme_border)
    text(x + 14, y, "あなた #{mc}"[0, n])
    @gfx.fill_circle(x + 5, y + 21, 5, @my == 1 ? C_WHITE : C_BLACK)
    @gfx.draw_circle(x + 5, y + 21, 5, theme_border)
    text(x + 14, y + 15, fit(@opp_name, @pw - 42) + " #{tc}")
    yy = y + 34
    if g.over
      text(x, yy, "終わり", theme_accent)
      wrap(x, yy + 15, @end_note, @pw, 3)
      button(:lobby, x, @y0 + @user_area_height - 20, 100, "ロビーへ")
      return
    end
    if @lost_at
      left = (GRACE_MS - (Machine.board_millis - @lost_at)) / 1000
      left = 0 if left < 0
      what = @net == :up && !reconnecting? ? "相手の通信が途切れた" : "網につなぎ直しています"
      text(x, yy, what, C_CURSOR)
      text(x, yy + 15, "戻るのを待つ: あと #{left} 秒"[0, n], C_CURSOR)
    elsif @move_f
      text(x, yy, "送っています...", theme_accent)
    elsif g.turn == @my
      text(x, yy, "あなたの番 (#{me})", theme_accent)
      text(x, yy + 15, "置ける所が無い: パス", C_CURSOR) unless g.board.any_move?(@my)
    else
      text(x, yy, "相手の番 (#{them})")
    end
    l = g.last
    text(x, yy + 30, "#{g.n} 手目 最後: #{l ? (l == '--' ? 'パス' : l) : '-'}"[0, n], theme_border)
    wrap(x, yy + 45, @note, @pw, 2)
    by = @y0 + @user_area_height - 20
    if @confirm
      text(x, by - 16, "投了しますか?", C_CURSOR)
      button(:yes, x, by, 60, "はい")
      button(:no, x + 66, by, 60, "いいえ")
    else
      button(:pass, x, by, 60, "パス") if my_turn? && !g.board.any_move?(@my)
      button(:resign, x + 66, by, 60, "投了")
    end
  end

  # s cut to w pixels in the current font (no mark).
  def fit(s, w)
    @gfx.fit_text(s, w)
  end

  # Draws s in up to max_lines lines of w pixels, 14 apart, cut anywhere.
  def wrap(x, y, s, w, max_lines)
    rest = s.to_s
    row = 0
    while row < max_lines && !rest.empty?
      line = @gfx.fit_text(rest, w)
      break if line.empty?
      text(x, y + row * 14, line, theme_border)
      rest = rest.byteslice(line.bytesize, rest.bytesize - line.bytesize).to_s
      row += 1
    end
  end

  # Small screens: the latest state any game put, without playing.
  def draw_small
    clear_user_area(FmrbGfx::BLACK)
    x = @user_area_x0
    y = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    st = @spec
    unless st && st["board"].is_a?(Array)
      @gfx.draw_text(x + 2, y + 2, "othello", FmrbGfx::WHITE)
      @gfx.draw_text(x + 2, y + 14, "too small to play", FmrbGfx::WHITE)
      @gfx.draw_text(x + 2, y + 26, @net == :up ? "watching..." : "net #{@net}", FmrbGfx::WHITE)
      @gfx.present
      return
    end
    c = ((w < h ? w : h) - 18) / 8
    bx = x + (w - c * 8) / 2
    by = y + 1
    @gfx.fill_rect(bx, by, c * 8, c * 8, C_BOARD)
    rows = st["board"]
    yy = 0
    while yy < 8 && yy < rows.size
      row = rows[yy].to_s
      xx = 0
      while xx < 8
        ch = row[xx, 1]
        cx = bx + xx * c + c / 2
        cy = by + yy * c + c / 2
        @gfx.fill_circle(cx, cy, c / 2 - 1, C_BLACK) if ch == "B"
        @gfx.fill_circle(cx, cy, c / 2 - 1, C_WHITE) if ch == "W"
        xx += 1
      end
      yy += 1
    end
    line = "B#{st['black_count']} W#{st['white_count']} #{st['over'] ? 'end' : (st['turn'] || '-')}"
    @gfx.draw_text(x + 2, by + c * 8 + 3, line[0, w / 6], FmrbGfx::WHITE)
    @gfx.present
  end

  def watch_states
    return unless @watch
    @watch.each_pending do |key, payload, att|
      begin
        st = ::Asterism::Codec.unpack(payload)
        if st.is_a?(Hash) && st["board"]
          @spec = st
          @spec_from = key
          @dirty = true
        end
      rescue => e
        Log.info("othello: watch: #{e.message}")
      end
    end
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
        if @small
          watch_states
        else
          scan
          run_key(@keys.shift) until @keys.empty?
          run_click(@clicks.shift) until @clicks.empty?
          ask_info if @mode == :lobby
          collect_invite
          collect_answer
          collect_move
          sync
          collect_sync
          check_invites
          if @g && !@g.over && now - @put_at >= STATE_EVERY_MS
            # Also compare the boards now and then while waiting: an
            # opponent restarted between two scans lost its game.
            if @g.turn != @my && !@need_sync
              @need_sync = true
              @sync_periodic = true
            end
            put_state
          end
        end
      end
    else
      # Offline: the keys still move the cursor and leave the game screen.
      run_key(@keys.shift) until @keys.empty?
      run_click(@clicks.shift) until @clicks.empty?
    end
    check_grace
    if @lost_at && now / 1000 != @second
      @second = now / 1000
      @dirty = true
    end
    if @dirty
      @dirty = false
      draw_screen
    end
    50
  end

  def on_destroy
    ::Asterism.close
    Log.info("othello: closed")
  rescue => e
    Log.info("othello: close: #{e.message}")
  end
end

begin
  app = OthelloApp.new
  app.start
rescue => e
  puts "othello: #{e.message}"
end
