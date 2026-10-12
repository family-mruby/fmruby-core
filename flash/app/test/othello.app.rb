# Othello over Asterism (doc/ruby_asterism, G1): two machines on the same
# Zenoh network play each other. Each game is an object of its own node,
# <ID>/game/othello, and calls the other player's (opponent.move(...)).
#
# - Connects like asterism_demo: the first line of /home/zenoh_echo.txt is the
#   locator, else the machine's zenoh_router setting (the browser build's
#   ws/localhost:7449, or ?zenoh= from the page's URL), else tcp/zenohd:7447.
#   The ID comes from /home/asterism_node.txt, the mDNS name, the
#   asterism_node setting (the browser's wasm-XXXXXX) or the board name.
# - Two sessions: Asterism (app "game", object "othello") and a plain Zenoh
#   session that puts the state on asterism/<ID>/game/othello/state after
#   every move (and every STATE_EVERY_MS during a game), for asterism-console
#   and for spectators. All keys stay under asterism/<ID>/game/** (the G0 ACL).
# - The object's methods (every caller passes its own node ID first, since
#   Asterism does not tell who called):
#     info                              -> {"name", "status", "opponent"}
#     invite(from, name, gid)           -> "asked" | "busy"
#     answer(from, gid, accept, your_color, name) -> true | false
#                                          (true: the inviter started the game)
#     cancel(from, gid)                 -> true | false
#     move(from, gid, n, x, y)          -> "ok" | the reason it was refused
#                                          (x = -1: a pass; n = moves so far)
#     resign(from, gid)                 -> true | false
#     state                             -> Hash (the same as the put)
# - Both sides keep the board and check every move they receive: the right
#   game, the right player, its turn, the move number and a legal square. A
#   move is applied by its player only after the other side said "ok". When
#   that answer is lost, or a move number does not fit, the player asks the
#   other's state and adopts the longer history when the shorter one is its
#   start and every move replays legally; otherwise the game ends.
# - The opponent leaving (its othello object gone): the game waits GRACE_MS
#   for it, then ends. When it comes back in time, the states are compared as
#   above and the game goes on. A restarted app has lost its game: the game
#   ends. Losing our own connection: reconnect every RECONNECT_MS, same wait.
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
  DEFAULT_LOCATOR = "tcp/zenohd:7447"
  LOCATOR_FILE = "/home/zenoh_echo.txt"
  NODE_FILE = "/home/asterism_node.txt"
  CONF_FILE = "/etc/system_conf.toml"
  APP = "game"
  OBJ = "othello"
  CALL_TIMEOUT = 3.0
  GRACE_MS = 30000
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

    def invite(from, name, gid)
      @app.r_invite(from.to_s, name.to_s, gid.to_s)
    end

    def answer(from, gid, accept, your_color, name)
      @app.r_answer(from.to_s, gid.to_s, accept ? true : false, your_color.to_s, name.to_s)
    end

    def cancel(from, gid)
      @app.r_cancel(from.to_s, gid.to_s)
    end

    def move(from, gid, n, x, y)
      @app.r_move(from.to_s, gid.to_s, n.to_i, x.to_i, y.to_i)
    end

    def resign(from, gid)
      @app.r_resign(from.to_s, gid.to_s)
    end

    def state
      @app.state_hash
    end
  end

  # ------------------------------------------------------------ settings

  def first_line(path)
    text = File.open(path, "r") { |f| f.read }
    text.split("\n")[0].to_s.strip
  rescue
    ""
  end

  def conf_value(key)
    text = File.open(CONF_FILE, "r") { |f| f.read }
    text.split("\n").each do |line|
      next unless line.start_with?(key)
      a = line.index("\"")
      b = a ? line.index("\"", a + 1) : nil
      return b ? line[a + 1, b - a - 1].to_s.strip : ""
    end
    ""
  rescue
    ""
  end

  def conf_int(key)
    text = File.open(CONF_FILE, "r") { |f| f.read }
    text.split("\n").each do |line|
      next unless line.start_with?(key)
      eq = line.index("=")
      return eq ? line[eq + 1, line.length].to_s.strip.to_i : 0
    end
    0
  rescue
    0
  end

  def on_create
    @started = Machine.board_millis
    @id = first_line(NODE_FILE)
    if @id.empty?
      info = FmrbApp.wifi_info
      @id = info ? info[:hostname].to_s : ""
    end
    @id = conf_value("asterism_node") if @id.empty?
    @id = FmrbConst::BOARD if @id.empty?
    @locator = first_line(LOCATOR_FILE)
    @locator = conf_value("zenoh_router") if @locator.empty?
    @locator = DEFAULT_LOCATOR if @locator.empty?
    width = conf_int("display_width")
    @small = width > 0 && width < 320
    request_fullscreen(true) if @small
    @gfx.set_font(:ja, 12) unless @small
    @net = :connecting      # :connecting / :up / :down
    @net_note = ""
    @retry_at = 0
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
    @buttons = []
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
      ::Asterism.connect(@locator, node: @id, app: APP)
      ::Asterism.expose(OBJ, OthelloApp::Remote.new(self),
                        methods: { info: 0, invite: 3, answer: 5, cancel: 2, move: 5, resign: 2, state: 0 }) unless @small
      ::Asterism.on_join { |n| @scan = true }
      ::Asterism.on_leave { |n| @scan = true }
      @zs.close if @zs
      @zs = Asterism::Zenoh::Session.open(@locator)
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
    begin
      @zs.close if @zs
    rescue
    end
    @zs = nil
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
    lost("zenoh: closed") if @zs && !@zs.poll
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
    id = @id
    @move_f = [OthelloGame.code(x, y), x, y, othello(@opp).move(id, gid, n, x, y)]
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
      if @net == :up
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
    id = @id
    name = my_name
    @inv_out = [to, gid, Machine.board_millis]
    @inv_out_f = othello(to).invite(id, name, gid)
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
    id = @id
    send_misc(o[0]) { |p| p.cancel(id, gid) }
    @note = "誘いを取り消した"
    @dirty = true
  end

  def reply_invite(accept)
    i = @inv_in
    return unless i && @answer_f.nil?
    @inv_in = nil
    gid = i[2]
    id = @id
    name = my_name
    if accept
      # The invited side draws the colours.
      mine = (Machine.board_millis / 7) % 2 == 0 ? 1 : 2
      theirs = color_name(3 - mine)
      @answer_f = [gid, i[0], i[1], mine, othello(i[0]).answer(id, gid, true, theirs, name)]
      @note = "始めています..."
    else
      send_misc(i[0]) { |p| p.answer(id, gid, false, "", name) }
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
    id = @id
    send_misc(@opp) { |p| p.resign(id, gid) } if @net == :up
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
    if @inv_in
      reply_invite(true) if enter || ch == 121 || ch == 89   # y
      reply_invite(false) if esc || ch == 110 || ch == 78    # n
      return
    end
    if @mode == :lobby
      if @inv_out
        cancel_invite if esc
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
    b = button_at(x, y)
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
    end
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
    @buttons << [id, x, y, w, 16]
    @gfx.fill_round_rect(x, y, w, 16, 3, theme_accent)
    tw = label.length * 6
    i = 0
    wide = 0
    while i < label.length
      wide += 1 if label[i, 1].bytesize > 1
      i += 1
    end
    tw += wide * 6
    @gfx.draw_text(x + (w - tw) / 2, y + 2, label, theme_fg_light)
  end

  def text(x, y, s, color = nil)
    @gfx.draw_text(x, y, s, color || theme_fg)
  end

  def draw_screen
    @buttons = []
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
    return "網から切れた: つなぎ直し中" if @net == :down
    "#{@id} (#{@locator})"
  end

  def draw_lobby
    x = @x0 + 4
    y = @y0 + 3
    text(x, y, "オセロ: 相手を選んで誘う")
    text(x, y + 15, net_line[0, 60], theme_border)
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
        text(x, yy, "#{label}  #{word}"[0, 60], theme_fg_light)
      else
        text(x, yy, "#{label}  #{word}"[0, 60])
      end
      i += 1
    end
    by = @y0 + @user_area_height - 40
    if @inv_in
      text(x, by - 2, "#{@inv_in[1]} から誘い。受ける?"[0, 60], theme_accent)
      button(:accept, x, by + 16, 90, "受ける (Y)")
      button(:decline, x + 98, by + 16, 90, "断る (N)")
    elsif @inv_out
      text(x, by - 2, "#{peer_name(@inv_out[0])} を誘っています..."[0, 60], theme_accent)
      button(:cancel, x, by + 16, 110, "取り消す (Esc)")
    else
      text(x, by - 2, @note[0, 60], theme_border)
      button(:invite, x, by + 16, 90, "誘う (Enter)") unless @peers.empty?
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
    text(x + 14, y + 15, "#{@opp_name}"[0, n - 7] + " #{tc}")
    yy = y + 34
    if g.over
      text(x, yy, "終わり", theme_accent)
      wrap(x, yy + 15, @end_note, n, 3)
      button(:lobby, x, @y0 + @user_area_height - 20, 100, "ロビーへ")
      return
    end
    if @lost_at
      left = (GRACE_MS - (Machine.board_millis - @lost_at)) / 1000
      left = 0 if left < 0
      what = @net == :up ? "相手が切れた" : "網から切れた"
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
    wrap(x, yy + 45, @note, n, 2)
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

  # Draws s in lines of at most n columns (a wide character counts 2).
  def wrap(x, y, s, n, max_lines)
    line = ""
    cols = 0
    row = 0
    i = 0
    while i < s.length && row < max_lines
      ch = s[i, 1]
      w = ch.bytesize > 1 ? 2 : 1
      if cols + w > n
        text(x, y + row * 14, line, theme_border)
        row += 1
        line = ""
        cols = 0
      end
      line << ch
      cols += w
      i += 1
    end
    text(x, y + row * 14, line, theme_border) if row < max_lines && !line.empty?
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
    @zs.close if @zs
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
