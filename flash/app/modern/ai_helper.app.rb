# AI helper (doc/ai_coding): asks the parent's AI helper gem (fmrb-tutor)
# over Asterism. The device never talks to an LLM provider and holds no key:
# its only partner is <node>/tutor/helper, found on the network.
#
# Calls (report a1.md 7.1): hello, then pending (answers that came while the
# app was away), and for a question ask -> status every second -> result ->
# ack. Every call is async (a Future checked in on_update), one at a time,
# so the window never waits on the network.
#
# Screen: the answers on top (scrolls with the wheel, Up/Down, PageUp/Down),
# a status line, the input line (kana and kanji input arrive as UTF-8
# bytes), and the buttons.
class AiHelperApp < FmrbApp
  APP_NAME = "ai_helper"
  HELPER = "*/tutor/helper"
  LINE_H = 13
  MAX_LINES = 400
  INPUT_MAX = 200        # characters
  POLL_MS = 1000
  SCAN_MS = 2000
  HELLO_RETRY_MS = 10000
  CALL_TIMEOUT = 6.0
  LINK_RETRY_MS = 8000
  BTN_H = 15

  # ------------------------------------------------------------ lifecycle

  def on_create
    @gfx.set_font(:ja, 12)
    @hits = FmrbUI::HitAreas.new
    @lines = []
    @colors = []
    @top = 0
    @input = ""
    @u8_buf = ""
    @u8_need = 0
    @status = ""
    @ai = nil
    @ai_path = nil
    @hello = nil
    @hello_at = 0
    @modes = []
    @call = nil
    @call_kind = nil
    @ticket = nil
    @phase = :idle
    @cancelled = false
    @pending = []
    @need_pending = false
    @next_poll = 0
    @next_scan = 0
    @asked_at = 0
    @poll_n = 0
    @poll_sum = 0
    @poll_max = 0
    @link_state = nil
    @want_cancel = false
    layout
    add_text("AI", "AI ヘルパーです。下の行に質問を打って Enter か「送る」を押してください。答えるのは AI (人ではありません) です。")
    set_status("つないでいます…")
    # connect blocks the app for a few seconds when the router is not
    # there: try less often than the Link's default
    @link = FmrbAsterism::Link.new(self, APP_NAME, tag: "ai_helper", retry_ms: LINK_RETRY_MS)
    @link_failed = false
    draw_all
  end

  def on_destroy
    @link.close if @link
  end

  def on_resize
    layout
    draw_all
  end

  # ------------------------------------------------------------ the link

  def link_up(link)
    @ai = nil
    @hello = nil
    @next_scan = 0
    set_status("AI の係を探しています…")
  end

  def link_down(link)
    forget_ai
  end

  def forget_ai
    @ai = nil
    @ai_path = nil
    @hello = nil
    @call = nil
    @call_kind = nil
  end

  # What the Link's state means for the child (shown when it changes).
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
    up = @link.update
    unless up
      st = @link.state
      if st != @link_state
        @link_state = st
        forget_ai
        set_status(link_text(st))
        Log.info("ai_helper: link #{st} #{@link.reason}")
      end
      return 100
    end
    @link_state = :connected
    @link_failed = false
    collect_call
    next_step if @call.nil?
    @call ? 50 : 100
  end

  # The next call, when none is on its way.
  def next_step
    now = Machine.board_millis
    if @ai.nil?
      scan(now)
    elsif @hello.nil?
      return if now < @hello_at
      start_call(:hello, { "fw" => FmrbConst::OS_VERSION, "lang" => "ja", "host" => FmrbAsterism.host })
    elsif @want_cancel && @ticket
      @want_cancel = false
      start_call(:cancel, @ticket)
    elsif @need_pending
      @need_pending = false
      start_call(:pending)
    elsif @phase == :poll
      return if now < @next_poll
      @next_poll = now + POLL_MS
      start_call(:status, @ticket)
    elsif @phase == :result
      start_call(:result, @ticket)
    elsif @phase == :ack
      start_call(:ack, @ticket)
    elsif @phase == :idle && !@pending.empty?
      take_pending
    elsif @phase == :idle && now >= @next_scan
      @next_scan = now + SCAN_MS
      still_there?
    end
  end

  # Looks for the helper (the liveliness the connection follows: nothing is
  # sent).
  def scan(now)
    return if now < @next_scan
    @next_scan = now + SCAN_MS
    found = @link.conn.each(HELPER)
    if found.empty?
      set_status("AI の係が見つかりません。親の PC で AI の係が動いているか確かめてください。")
      return
    end
    @ai_path = found[0].asterism_path
    @ai = @link.conn[@ai_path, timeout: CALL_TIMEOUT]
    @hello = nil
    @hello_at = 0
    Log.info("ai_helper: found #{@ai_path}")
  rescue ::Asterism::Error => e
    Log.info("ai_helper: scan: #{e.message}")
  end

  # The helper is still on the network (between requests).
  def still_there?
    found = @link.conn.each(HELPER)
    i = 0
    while i < found.size
      return true if found[i].asterism_path == @ai_path
      i += 1
    end
    Log.info("ai_helper: #{@ai_path} left")
    forget_ai
    set_status("AI の係が見つかりません。親の PC で AI の係が動いているか確かめてください。")
    false
  rescue ::Asterism::Error
    false
  end

  # ------------------------------------------------------------ calls

  def start_call(kind, *args)
    @call = @ai.async.asterism_call(kind.to_s, *args)
    @call_kind = kind
  rescue ::Asterism::Error => e
    @call = nil
    call_failed(kind, e)
  end

  def collect_call
    f = @call
    return unless f && f.done?
    kind = @call_kind
    @call = nil
    @call_kind = nil
    v = nil
    begin
      v = f.value
    rescue ::Asterism::Error => e
      call_failed(kind, e)
      return
    end
    took = f.took_ms.to_i
    if kind == :status
      @poll_n += 1
      @poll_sum += took
      @poll_max = took if took > @poll_max
    end
    take(kind, v)
  end

  # A call that got no answer: the helper is gone or the network is. What
  # was asked is not lost: the helper keeps it, and pending (after the next
  # hello) brings it back.
  def call_failed(kind, e)
    Log.info("ai_helper: #{kind}: #{e.class}: #{e.message}")
    had = @phase != :idle
    forget_ai
    @phase = :idle
    @next_scan = Machine.board_millis + SCAN_MS
    if had
      set_status("AI の係から返事がありません。つながったら答えを受け取りに行きます。")
    else
      set_status("AI の係から返事がありません。親の PC で AI の係が動いているか確かめてください。")
    end
  end

  def take(kind, v)
    case kind
    when :hello then took_hello(v)
    when :pending then took_pending(v)
    when :ask then took_ask(v)
    when :status then took_status(v)
    when :result then took_result(v)
    when :ack then took_ack(v)
    when :new_talk then took_new_talk(v)
    when :cancel then took_cancel(v)
    end
  end

  def error_of(v)
    v.is_a?(Hash) ? v["error"] : nil
  end

  def message_of(v)
    m = v.is_a?(Hash) ? v["message"].to_s : ""
    m.empty? ? "AI の係が断りました (#{error_of(v)})。" : m
  end

  def took_hello(v)
    if error_of(v) || !v.is_a?(Hash)
      @hello = nil
      @hello_at = Machine.board_millis + HELLO_RETRY_MS
      m = message_of(v)
      m = "#{m} (この端末の名前: #{@link.node})" if error_of(v) == "not_allowed"
      set_status(m)
      Log.info("ai_helper: hello refused: #{error_of(v)}")
      return
    end
    @hello = v
    @modes = v["modes"].is_a?(Array) ? v["modes"] : []
    name = v["name"].to_s
    used = v["used"].to_i
    max = v["max"].to_i
    Log.info("ai_helper: hello #{@ai_path} modes=#{@modes.join(',')} piece=#{v['piece']} used=#{used}/#{max}")
    @need_pending = true
    set_status("つながりました#{name.empty? ? '' : " (#{name})"}。今日 #{used} / #{max} 回。") if @phase == :idle
  end

  # [[ticket, state], ...]: answers to this machine not taken yet.
  def took_pending(v)
    @pending = []
    return unless v.is_a?(Array)
    i = 0
    while i < v.size
      t = v[i]
      @pending << t[0].to_s if t.is_a?(Array) && t.size > 0
      i += 1
    end
    Log.info("ai_helper: pending #{@pending.size}")
    # a request this run was already waiting for goes on as it was
    if @ticket && @pending.index(@ticket)
      @pending.delete(@ticket)
      @phase = :poll
      @next_poll = 0
    elsif @ticket && @phase == :idle
      add_text("AI", "前の頼みの答えは、AI の係が持っていませんでした。もう一度送ってください。")
      @ticket = nil
    end
  end

  def reset_poll_stats
    @poll_n = 0
    @poll_sum = 0
    @poll_max = 0
  end

  def take_pending
    reset_poll_stats
    @ticket = @pending.shift
    @cancelled = false
    @asked_at = Machine.board_millis
    @phase = :poll
    @next_poll = 0
    add_text("AI", "前に頼んだ答えを受け取ります。")
  end

  # ------------------------------------------------------------ asking

  def send_input
    text = @input.strip
    return set_status("前の頼みがまだ終わっていません。") if @phase != :idle
    return set_status("AI の係につながっていません。") unless @hello
    return set_status("質問を打ってください。") if text.empty?
    add_text("あなた", text)
    @input = ""
    draw_input
    req = { "mode" => "ask", "text" => text, "fw" => FmrbConst::OS_VERSION, "lang" => "ja" }
    @phase = :asking
    reset_poll_stats
    @cancelled = false
    @asked_at = Machine.board_millis
    start_call(:ask, req)
    set_status("送っています…")
  end

  def took_ask(v)
    if error_of(v)
      @phase = :idle
      add_text("AI", message_of(v))
      set_status(message_of(v))
      return
    end
    @ticket = v["ticket"].to_s
    @phase = :poll
    @next_poll = Machine.board_millis + 300
    set_status("受け付けました。")
    Log.info("ai_helper: ticket #{@ticket}")
  end

  STATE_TEXT = {
    "queued" => "順番を待っています",
    "thinking" => "考えています",
    "checking" => "検査しています",
  }

  def took_status(v)
    if error_of(v)
      # the helper no longer has it (restarted)
      add_text("AI", "前の頼みの答えは、AI の係が持っていませんでした。もう一度送ってください。")
      finish_request
      return
    end
    st = v["state"].to_s
    if st == "done" || st == "failed" || st == "cancelled"
      @phase = :result
      return
    end
    secs = (Machine.board_millis - @asked_at) / 1000
    word = STATE_TEXT[st] || st
    set_status(@cancelled ? "やめています… (#{secs} 秒)" : "#{word}… (#{secs} 秒)")
  end

  def took_result(v)
    secs = (Machine.board_millis - @asked_at) / 1000
    Log.info("ai_helper: result #{@ticket} after #{secs} s, status #{@poll_n} calls avg #{@poll_n > 0 ? @poll_sum / @poll_n : 0} ms max #{@poll_max} ms pool #{FmrbApp.pool_used}")
    if @cancelled
      set_status("やめました。")
      @phase = :ack
      return
    end
    if error_of(v)
      if error_of(v) == "not_ready"
        @phase = :poll
        return
      end
      add_text("AI", message_of(v))
      set_status(error_of(v) == "cancelled" ? "やめました。" : "うまくいきませんでした。")
      @phase = :ack
      return
    end
    add_text("AI", v["explain"].to_s)
    set_status("答えが届きました (#{secs} 秒)。")
    @phase = :ack
  end

  def took_ack(v)
    finish_request
  end

  def finish_request
    @ticket = nil
    @phase = :idle
    @cancelled = false
  end

  def new_talk
    return set_status("AI の係につながっていません。") unless @hello && @call.nil?
    start_call(:new_talk)
  end

  def took_new_talk(v)
    @lines = []
    @colors = []
    @top = 0
    add_text("AI", "新しい話を始めます。前のやり取りは AI に送りません。")
    set_status("新しい話にしました。")
  end

  def cancel_request
    return set_status("今は頼んでいません。") if @ticket.nil? || @phase == :idle
    return if @cancelled
    @cancelled = true
    set_status("やめています…")
    # the cancel goes between two status calls
    if @call.nil?
      start_call(:cancel, @ticket)
    else
      @want_cancel = true
    end
  end

  def took_cancel(v)
    @want_cancel = false
  end

  # ------------------------------------------------------------ the text

  # Adds str (may hold newlines) under the label, wrapped to the area, and
  # shows its first line.
  def add_text(who, str)
    color = who == "あなた" ? theme_accent : theme_fg
    first = @lines.size
    ls = wrap("#{who}: #{str}", @area_w - 8)
    i = 0
    while i < ls.size
      push_line(ls[i], color)
      i += 1
    end
    push_line("", color)
    while @lines.size > MAX_LINES
      @lines.shift
      @colors.shift
      first -= 1
    end
    first = 0 if first < 0
    rows = answer_rows
    # the start of the new text, or the end when it is short
    @top = @lines.size - rows
    @top = first if @top > first
    @top = 0 if @top < 0
    draw_answers if @ready
  end

  def push_line(s, color)
    @lines << s
    @colors << color
  end

  # s cut into lines of w pixels at its newlines and where it gets too
  # wide. Fixed-pitch widths of the 12px Japanese font: ASCII 6, the rest 12.
  def wrap(s, w)
    out = []
    n = s.bytesize
    i = 0
    start = 0
    used = 0
    while i < n
      b = s.getbyte(i)
      if b == 10
        out << s.byteslice(start, i - start).to_s
        i += 1
        start = i
        used = 0
        next
      end
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : (b >= 0xC0 ? 2 : 1)))
      cw = b < 0x80 ? 6 : 12
      if used + cw > w && i > start
        out << s.byteslice(start, i - start).to_s
        start = i
        used = 0
      end
      used += cw
      i += len
    end
    out << s.byteslice(start, n - start).to_s if start < n
    out
  end

  def text_px(s)
    n = s.bytesize
    i = 0
    px = 0
    while i < n
      b = s.getbyte(i)
      if b < 0x80
        px += 6
      elsif b >= 0xC0
        px += 12
      end
      i += 1
    end
    px
  end

  # The end of s that fits in w pixels.
  def tail_fit(s, w)
    return s if text_px(s) <= w
    i = s.bytesize
    px = 0
    while i > 0
      j = i - 1
      j -= 1 while j > 0 && (s.getbyte(j) & 0xC0) == 0x80
      cw = s.getbyte(j) < 0x80 ? 6 : 12
      break if px + cw > w
      px += cw
      i = j
    end
    s.byteslice(i, s.bytesize - i).to_s
  end

  def set_status(s)
    return if s == @status
    @status = s
    draw_status if @ready
  end

  # ------------------------------------------------------------ drawing

  def layout
    @area_w = @user_area_width
    @area_h = @user_area_height
    @ans_y = 2
    @btn_y = @area_h - BTN_H - 1
    @in_y = @btn_y - BTN_H - 3
    @st_y = @in_y - LINE_H * 2 - 1
    @ans_h = @st_y - @ans_y - 2
    @ready = true
  end

  def answer_rows
    r = @ans_h / LINE_H
    r < 1 ? 1 : r
  end

  def draw_all
    clear_user_area
    draw_answers(false)
    draw_status(false)
    draw_input(false)
    draw_buttons(false)
    @gfx.present
  end

  def draw_answers(show = true)
    @gfx.fill_rect(0, @ans_y, @area_w, @ans_h, theme_bg)
    rows = answer_rows
    i = 0
    while i < rows && @top + i < @lines.size
      @gfx.draw_text(3, @ans_y + i * LINE_H, @lines[@top + i], @colors[@top + i])
      i += 1
    end
    # where the view is in the whole
    if @lines.size > rows
      bar_h = @ans_h * rows / @lines.size
      bar_h = 6 if bar_h < 6
      bar_y = @ans_y + (@ans_h - bar_h) * @top / (@lines.size - rows)
      @gfx.fill_rect(@area_w - 3, @ans_y, 2, @ans_h, theme_bg)
      @gfx.fill_rect(@area_w - 3, bar_y, 2, bar_h, theme_border)
    end
    @gfx.present if show
  end

  def draw_status(show = true)
    @gfx.fill_rect(0, @st_y - 1, @area_w, LINE_H * 2 + 1, theme_bg)
    @gfx.draw_line(0, @st_y - 2, @area_w - 1, @st_y - 2, theme_border)
    ls = wrap(@status, @area_w - 6)
    @gfx.draw_text(3, @st_y, ls[0].to_s, theme_border)
    @gfx.draw_text(3, @st_y + LINE_H, tail_fit_head(ls[1].to_s, @area_w - 6), theme_border) if ls.size > 1
    @gfx.present if show
  end

  # The start of s that fits (the status line keeps its beginning).
  def tail_fit_head(s, w)
    return s if text_px(s) <= w
    n = s.bytesize
    i = 0
    px = 0
    while i < n
      b = s.getbyte(i)
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2))
      cw = b < 0x80 ? 6 : 12
      break if px + cw > w - 12
      px += cw
      i += len
    end
    s.byteslice(0, i).to_s + ".."
  end

  def draw_input(show = true)
    h = BTN_H
    @gfx.fill_rect(1, @in_y, @area_w - 2, h, theme_bg)
    @gfx.draw_rect(1, @in_y, @area_w - 2, h, theme_border)
    t = tail_fit(@input, @area_w - 14)
    @gfx.draw_text(4, @in_y + 2, t, theme_fg)
    cx = 4 + text_px(t)
    @gfx.draw_line(cx, @in_y + 2, cx, @in_y + h - 3, theme_fg)
    @gfx.present if show
  end

  BUTTONS = [[:send, "送る", 40], [:stop, "やめる", 46], [:talk, "新しい話", 58]]

  def draw_buttons(show = true)
    @hits.clear
    @gfx.fill_rect(0, @btn_y, @area_w, BTN_H, theme_bg)
    x = 1
    i = 0
    while i < BUTTONS.size
      b = BUTTONS[i]
      @gfx.draw_button(x, @btn_y, b[2], BTN_H, b[1], theme_accent, theme_fg_light)
      @hits.add(b[0], x, @btn_y, b[2], BTN_H)
      x += b[2] + 3
      i += 1
    end
    @gfx.present if show
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    if t == :mouse_down
      press(@hits.id_at(ev[:x], ev[:y]))
    elsif t == :mouse_wheel
      rows = wheel_rows(ev)
      scroll(-rows) if rows
    elsif t == :key_down
      key(ev)
    end
  end

  def press(id)
    case id
    when :send then send_input
    when :stop then cancel_request
    when :talk then new_talk
    end
  end

  def scroll(d)
    rows = answer_rows
    top = @top + d
    max = @lines.size - rows
    top = max if top > max
    top = 0 if top < 0
    return if top == @top
    @top = top
    draw_answers
  end

  def key(ev)
    return if ev_ctrl?(ev)
    case ev[:scancode].to_i
    when 0x52 then return scroll(-1)
    when 0x51 then return scroll(1)
    when 0x4B then return scroll(-answer_rows)
    when 0x4E then return scroll(answer_rows)
    end
    ch = ev[:character].to_i
    return if ch == 0
    if ch >= 0x80
      s = utf8_feed(ch)
      type_char(s) if s
      return
    end
    @u8_need = 0
    if ch == 10 || ch == 13
      send_input
    elsif ch == 8
      n = @input.length
      @input = @input[0, n - 1].to_s if n > 0
      draw_input
    elsif ch == 27
      cancel_request
    elsif ch >= 32 && ch <= 126
      type_char(ch.chr)
    end
  end

  def type_char(s)
    return if @input.length >= INPUT_MAX
    @input = @input + s
    draw_input
  end

  # One byte of a UTF-8 character (kana input sends them one per event).
  def utf8_feed(byte)
    if @u8_need == 0
      if byte >= 0xF0
        @u8_need = 3
      elsif byte >= 0xE0
        @u8_need = 2
      elsif byte >= 0xC0
        @u8_need = 1
      else
        return nil
      end
      @u8_buf = one_byte(byte)
      return nil
    end
    if byte < 0x80 || byte >= 0xC0
      @u8_need = 0
      return utf8_feed(byte)
    end
    @u8_buf += one_byte(byte)
    @u8_need -= 1
    return nil if @u8_need > 0
    @u8_buf
  end

  def one_byte(b)
    s = " ".dup
    s.setbyte(0, b)
    s
  end
end
