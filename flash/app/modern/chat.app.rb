# Chat (doc/asterism_chat): rooms of text over Asterism, kept by a chat
# host (the gem asterism-chat) at <node>/chat/room. The wire format is
# doc/asterism_chat/report/c0.md: hello (the display name, the rooms and
# the limits), rooms, post and history; the topic <host>/chat/news says
# {"room", "n"} for each new message (the number only: the text is fetched
# with history(after:), one way for news and for catching up).
#
# What the device keeps: the display name and, per room, the number of the
# last message seen (the unread count is the room's last minus it), in
# /home/chat/settings.toml; and, of the open room only, at most HOLD
# messages (newest when opened, older ones fetched when scrolled back, the
# far end dropped past HOLD).
#
# Every call is async (a Future checked in on_update), one at a time, so
# the window never waits on the network (the browser build is cooperative).
#
# The class is in four files: this one (the host, the calls, the
# settings), chat/talk.rb (the messages of the open room and their lines),
# chat/screen.rb (the drawing and the input) and chat/stamps.rb (the
# pictures: the picker, putting and fetching them, drawing them), required
# at the end.
class ChatApp < FmrbApp
  APP_NAME = "chat"
  HOST = "*/chat/room"
  SETTINGS_DIR = "/home/chat"
  SETTINGS = "/home/chat/settings.toml"
  HOLD = 50              # messages of the open room kept on the device
  CALL_TIMEOUT = 6.0
  LINK_RETRY_MS = 8000
  SCAN_MS = 3000
  SCAN_FAST_MS = 3000
  SCAN_STEP_MS = 300
  HELLO_RETRY_MS = 10000
  ROOMS_EVERY_MS = 60000
  SAVE_AFTER_MS = 5000
  TEXT_MAX = 200         # until hello says
  NAME_MAX = 8

  # Shown when the host refuses without a message of its own.
  REFUSAL = {
    "not_allowed" => "この端末は、このチャットを使えるように登録されていません。",
    "no_room" => "その部屋はありません。",
    "empty" => "送る文がありません。",
    "too_long" => "長すぎます。",
    "bad_name" => "その表示名は使えません。",
    "no_name" => "先に表示名を決めてください。",
    "limit" => "今日送れる数を使い切りました。また明日送ってください。",
    "stopping" => "チャットホストが止まるところです。少し待ってからもう一度送ってください。",
    "too_big" => "その絵はスタンプには大きすぎます。",
    "bad_stamp" => "その絵はスタンプにできません。",
    "stamp_limit" => "今日送れる新しい絵の数を使い切りました。また明日送ってください。",
    "stamp_full" => "チャットホストの絵の置き場がいっぱいです。家の人に伝えてください。",
  }
  NOT_FOUND = "チャットホストが見つかりません。家の人に、チャットホストが動いているか聞いてください。"

  # How stamp_get is asked to send a picture (MessagePack str or bin; the
  # device reads both the same: report/c3.md).
  STAMP_AS = "bin"

  # ------------------------------------------------------------ lifecycle

  def on_create
    @gfx.set_font(:ja, 12)
    @hits = FmrbUI::HitAreas.new
    # the host
    @host = nil           # its room (a proxy)
    @host_path = nil
    @host_node = ""
    @hello = nil
    @hello_at = 0
    @tz = 0
    @text_max = TEXT_MAX
    @name_max = NAME_MAX
    @call = nil
    @call_kind = nil
    @call_room = nil
    @next_scan = 0
    @scan_fast_until = 0
    @rooms_at = 0
    @need_rooms = false
    @link_state = nil
    @link_failed = false
    # the rooms: ids in the host's order, titles, last numbers
    @rooms = []
    @titles = {}
    @last = {}
    # the settings
    @name = ""
    @seen = {}            # "<host>/<room>" => the last number seen
    @seen_dirty = false
    @save_at = 0
    load_settings
    # a post on its way: [room, text, cid] or, a stamp, [room, nil, cid,
    # sha, path]; whether it is being sent, and whether its picture is to be
    # put first (the host said no_stamp)
    @out = nil
    @out_busy = false
    @out_put = false
    stamps_reset
    @cid_seq = 0
    # the screen
    @face = @name.empty? ? :name : :list   # :list, :talk, :name (narrow)
    @cur = nil            # the open room
    @sel = 0
    @input = ""
    @u8_buf = ""
    @u8_need = 0
    @in_cx = 4
    @tc = -2
    @status = ""
    talk_reset
    layout
    set_status("つないでいます…")
    @link = FmrbAsterism::Link.new(self, APP_NAME, tag: "chat", retry_ms: LINK_RETRY_MS)
    draw_all
  end

  def on_destroy
    save_settings if @seen_dirty
    @link.close if @link
  end

  def on_resize(w, h)
    @tc = -2              # the area's origin may have moved: tell it again
    was = @talk_w
    layout
    rewrap if @talk_w != was
    draw_all
  end

  # ------------------------------------------------------------ the link

  def link_up(link)
    forget_host
    @next_scan = 0
    # the network's liveliness comes in a moment after a connect: looked
    # at often for a while before "not found" is said
    @scan_fast_until = Machine.board_millis + SCAN_FAST_MS
    set_status("チャットホストを探しています…")
    begin
      link.conn.subscribe("*/chat/news", depth: 16) { |v, info| news_came(v, info) }
    rescue ::Asterism::Error => e
      Log.info("chat: subscribe: #{e.message}")
    end
  end

  def link_down(link)
    forget_host
  end

  def forget_host
    @host = nil
    @host_path = nil
    @hello = nil
    @call = nil
    @call_kind = nil
    @out_busy = false
    @hello_at = 0
  end

  def link_text(st)
    case st
    when :waiting_wifi then "Wi-Fi につながるのを待っています…"
    when :connecting
      @link_failed ? "ネットワーク (ルータ) につながりません。もう一度つないでいます…" : "つないでいます…"
    when :waiting_screen then "つないでいます…"
    when :still_there then "前のつながりが残っています。少し待ちます…"
    when :retrying, :lost, :failed
      @link_failed = true
      "ネットワーク (ルータ) につながりません。少し待ってからもう一度つなぎます。"
    else "つながっていません。"
    end
  end

  def on_update
    now = Machine.board_millis
    save_settings if @seen_dirty && now >= @save_at
    up = @link.update
    unless up
      st = @link.state
      if st != @link_state
        @link_state = st
        forget_host
        set_status(link_text(st))
        Log.info("chat: link #{st} #{@link.reason}")
      end
      return 100
    end
    @link_state = :connected
    @link_failed = false
    collect_call
    next_step(now) if @call.nil?
    redraw_due
    @call ? 50 : 100
  end

  # The next call, when none is on its way.
  def next_step(now)
    if @host.nil?
      scan(now)
    elsif @hello.nil?
      return if now < @hello_at
      info = { "fw" => FmrbConst::OS_VERSION, "lang" => "ja", "host" => FmrbAsterism.host }
      info["name"] = @name unless @name.empty?
      start_call(:hello, nil, info)
    elsif @out && !@out_busy && !@name.empty?
      @out_busy = true
      if @out_put
        put_stamp
      elsif @out[3]
        start_call(:post, @out[0], { "room" => @out[0], "stamp" => @out[3], "cid" => @out[2] })
      else
        start_call(:post, @out[0], { "room" => @out[0], "text" => @out[1], "cid" => @out[2] })
      end
    elsif @cur && @t_open
      start_call(:open, @cur, { "room" => @cur })
    elsif @cur && @t_after
      start_call(:after, @cur, { "room" => @cur, "after" => @m_n.empty? ? 0 : @m_n[@m_n.size - 1] })
    elsif @cur && @t_before && !@m_n.empty?
      start_call(:before, @cur, { "room" => @cur, "before" => @m_n[0] })
    elsif @cur && (sha = next_wanted)
      start_call(:stamp_get, sha, { "sha" => sha, "as" => STAMP_AS })
    elsif @need_rooms || now >= @rooms_at
      @need_rooms = false
      @rooms_at = now + ROOMS_EVERY_MS
      start_call(:rooms, nil)
    elsif now >= @next_scan
      @next_scan = now + SCAN_MS
      still_there?
    end
  end

  # Looks for the host (the liveliness the connection follows).
  def scan(now)
    return if now < @next_scan
    fast = now < @scan_fast_until
    @next_scan = now + (fast ? SCAN_STEP_MS : SCAN_MS)
    found = @link.conn.each(HOST)
    if found.empty?
      set_status(NOT_FOUND) unless fast
      return
    end
    @host_path = found[0].asterism_path
    @host_node = @host_path.split("/")[0].to_s
    @host = @link.conn[@host_path, timeout: CALL_TIMEOUT]
    @hello = nil
    @hello_at = 0
    Log.info("chat: found #{@host_path}")
  rescue ::Asterism::Error => e
    Log.info("chat: scan: #{e.message}")
  end

  def still_there?
    found = @link.conn.each(HOST)
    i = 0
    while i < found.size
      return true if found[i].asterism_path == @host_path
      i += 1
    end
    Log.info("chat: #{@host_path} left")
    forget_host
    set_status(NOT_FOUND)
    false
  rescue ::Asterism::Error
    false
  end

  # ------------------------------------------------------------ calls

  def start_call(kind, room, *args)
    name = kind == :open || kind == :after || kind == :before ? "history" : kind.to_s
    @call = @host.async.asterism_call(name, *args)
    @call_kind = kind
    @call_room = room
  rescue ::Asterism::Error => e
    @call = nil
    call_failed(kind, e)
  end

  def collect_call
    f = @call
    return unless f && f.done?
    kind = @call_kind
    room = @call_room
    @call = nil
    @call_kind = nil
    v = nil
    begin
      v = f.value
    rescue ::Asterism::Error => e
      call_failed(kind, e)
      return
    end
    took(kind, room, v)
  end

  # No answer: the host or the network is gone. A post waits and goes
  # again with the same cid (the host answers it once).
  def call_failed(kind, e)
    Log.info("chat: #{kind}: #{e.class}: #{e.message}")
    forget_host
    @next_scan = Machine.board_millis + SCAN_MS
    set_status("チャットホストから返事がありません。つながったら続きを読みます。")
  end

  def took(kind, room, v)
    case kind
    when :hello then took_hello(v)
    when :rooms then took_rooms(v)
    when :post then took_post(v)
    when :open, :after, :before then took_history(kind, room, v)
    when :stamp_put then took_stamp_put(v)
    when :stamp_get then took_stamp_get(room, v)
    end
  end

  # An Integer of the host's answers (and of the Hashes here), 0 for nil
  # or anything else: this mruby has no NilClass#to_i.
  def int(x)
    x.is_a?(Integer) ? x : 0
  end

  def error_of(v)
    v.is_a?(Hash) ? v["error"] : nil
  end

  def message_of(v)
    m = v.is_a?(Hash) ? v["message"].to_s : ""
    return m unless m.empty?
    REFUSAL[error_of(v).to_s] || "チャットホストが断りました (#{error_of(v)})。"
  end

  def took_hello(v)
    e = error_of(v)
    if e || !v.is_a?(Hash)
      @hello = nil
      @hello_at = Machine.board_millis + HELLO_RETRY_MS
      m = message_of(v)
      if e == "bad_name"
        # the name kept here is not taken: choose another
        @name = ""
        show_face(:name)
      end
      m = "#{m} (この端末の名前: #{@link.node})" if e == "not_allowed"
      set_status(m)
      Log.info("chat: hello refused: #{e}")
      return
    end
    @hello = v
    @tz = int(v["tz"])
    t = int(v["text_max"])
    @text_max = t > 0 ? t : TEXT_MAX
    t = int(v["name_max"])
    @name_max = t > 0 ? t : NAME_MAX
    t = int(v["stamp_px"])
    @stamp_px = t > 0 ? t : STAMP_PX
    t = int(v["stamp_bytes"])
    @stamp_bytes = t > 0 ? t : STAMP_BYTES
    n = v["name"].to_s
    if @name.empty? && !n.empty?
      # this device's name, kept by the host (the settings were lost)
      @name = n
      save_settings
      show_face(:list) if @face == :name
    end
    take_rooms(v["rooms"])
    @rooms_at = Machine.board_millis + ROOMS_EVERY_MS
    # catch up with what came while away
    @t_after = true if @cur && !@t_open && !@t_cut
    Log.info("chat: hello #{@host_path} name=#{n} rooms=#{@rooms.size} used=#{v['used']}/#{v['per_day']}")
    host = v["host"].to_s
    if @face == :name
      set_status("表示名を打って Enter を押してください。")
    else
      set_status("つながりました#{host.empty? ? '' : " (#{host})"}。")
    end
    draw_all
  end

  def took_rooms(v)
    return set_status(message_of(v)) if error_of(v) || !v.is_a?(Hash)
    take_rooms(v["rooms"])
    draw_list_area
  end

  # [{"id", "title", "last"}, ...]
  def take_rooms(list)
    return unless list.is_a?(Array)
    @rooms = []
    @titles = {}
    i = 0
    while i < list.size
      r = list[i]
      i += 1
      next unless r.is_a?(Hash)
      id = r["id"].to_s
      @rooms << id
      t = r["title"].to_s
      @titles[id] = t.empty? ? id : t
      set_last(id, int(r["last"]))
    end
    @sel = @rooms.size - 1 if @sel >= @rooms.size
    @sel = 0 if @sel < 0
    if @cur && !@titles.key?(@cur)
      @cur = nil
      talk_reset
      show_face(:list) if @face == :talk
    end
  end

  def set_last(room, n)
    @last[room] = n if n > int(@last[room])
  end

  def took_post(v)
    out = @out
    @out_busy = false
    e = error_of(v)
    if e == "no_stamp" && out && out[3] && !@out_put
      # the host does not have the picture: put it, then post again
      @out_put = true
      set_status("絵を送っています…")
      return
    end
    if e || !v.is_a?(Hash)
      @out = nil
      if e == "no_name"
        show_face(:name)
      elsif out && out[1] && @input.empty?
        # the text comes back to the input line, to be changed and sent again
        @input = out[1]
      end
      set_status(message_of(v))
      draw_input
      Log.info("chat: post refused: #{e}")
      return
    end
    @out = nil
    room = v["room"].to_s
    n = int(v["n"])
    set_last(room, n)
    @t_after = true if room == @cur && !@t_cut && !@t_open
    set_status("")
    @list_dirty = true
  end

  # ------------------------------------------------------------ news

  # {"room", "n"} on <host>/chat/news (from the connection's poll). A value
  # after others that did not come, or from a new run of the host: the
  # rooms' numbers are asked again.
  def news_came(v, info)
    return unless v.is_a?(Hash)
    parts = info.path.to_s.split("/")
    node = parts[0] == "asterism" ? parts[1].to_s : parts[0].to_s
    return if !@host_node.empty? && node != @host_node
    room = v["room"].to_s
    n = int(v["n"])
    if @titles.key?(room)
      set_last(room, n)
      @t_after = true if room == @cur && !@t_cut && !@t_open && (@m_n.empty? || n > @m_n[@m_n.size - 1])
      @list_dirty = true
    else
      @need_rooms = true
    end
    @need_rooms = true if info.lost > 0 || info.restarted?
  rescue => e
    Log.info("chat: news: #{e.message}")
  end

  # ------------------------------------------------------------ posting

  def send_input
    if @face == :name
      return choose_name
    end
    return unless @cur
    text = @input.strip
    return if text.empty?
    return set_status("前の発言を送っています。少し待ってください。") if @out
    return show_face(:name) if @name.empty?
    @cid_seq += 1
    @out = [@cur, text, "#{Machine.board_millis}-#{@cid_seq}"]
    @out_busy = false
    @out_put = false
    @input = ""
    draw_input
    set_status(@hello ? "送っています…" : "チャットホストにつながっていません。つながったら送ります。")
  end

  def choose_name
    s = @input.strip
    return set_status("表示名を入れてください。") if s.empty?
    return set_status("表示名は #{@name_max} 文字までです。") if s.length > @name_max
    @name = s
    save_settings
    @input = ""
    # told to the host with the next hello
    @hello = nil
    @hello_at = 0
    show_face(@cur ? :talk : :list)
    set_status(@host ? "表示名を伝えています…" : "表示名を決めました。つながったらチャットホストに伝えます。")
  end

  # ------------------------------------------------------------ the settings

  def seen_key(room)
    "#{@host_node}/#{room}"
  end

  # The unread count of a room (the messages before the first visit count).
  def unread(room)
    u = int(@last[room]) - int(@seen[seen_key(room)])
    u < 0 ? 0 : u
  end

  def mark_seen(room, n)
    k = seen_key(room)
    return if n <= int(@seen[k])
    @seen[k] = n
    unless @seen_dirty
      @seen_dirty = true
      @save_at = Machine.board_millis + SAVE_AFTER_MS
    end
  end

  def load_settings
    return unless File.exist?(SETTINGS)
    conf = FmrbToml.load(SETTINGS)
    return unless conf
    @name = conf["name"].to_s
    s = conf["seen"]
    @seen = s if s.is_a?(Hash)
  rescue => e
    Log.info("chat: settings: #{e.message}")
  end

  def save_settings
    @seen_dirty = false
    Dir.mkdir(SETTINGS_DIR) unless Dir.exist?(SETTINGS_DIR)
    out = "# Chat (doc/asterism_chat): the display name and the last message seen per room\n".dup
    out << "name = #{FmrbToml.dump_value(@name)}\n" unless @name.empty?
    out << "\n[seen]\n"
    keys = @seen.keys
    i = 0
    while i < keys.size
      out << "#{FmrbToml.dump_key(keys[i].to_s)} = #{int(@seen[keys[i]])}\n"
      i += 1
    end
    File.open(SETTINGS, "w") { |f| f.write(out) }
  rescue => e
    Log.info("chat: save: #{e.message}")
  end
end

# the messages and the screen (one class, three files)
require "/app/modern/chat/talk"
require "/app/modern/chat/screen"
require "/app/modern/chat/stamps"
