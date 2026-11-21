# Chat (doc/asterism_chat): the screen, required by chat.app.rb after its
# class body. One window that can be resized: narrower than WIDE_MIN it
# shows the room list or the talk of one room (the faces :list and :talk),
# wider it shows both, the list on the left. The face :name (choosing the
# display name) takes the whole window either way.
class ChatApp < FmrbApp
  WIDE_MIN = 380         # the user area's width from which list and talk sit side by side
  LIST_W = 112           # the list's column when they do
  TOP_H = 16
  LINE_H = 13
  ROW_H = 16
  BTN_H = 15

  # ------------------------------------------------------------ the faces

  def list_on?
    @face != :name && (@wide || @face == :list)
  end

  def talk_on?
    @face != :name && (@wide || @face == :talk)
  end

  def input_on?
    @face == :name || (talk_on? && !@cur.nil?)
  end

  def show_face(f)
    @face = f
    @input = f == :name ? @name.dup : ""
    @u8_need = 0
    layout
    draw_all
  end

  # ------------------------------------------------------------ layout

  def layout
    @area_w = @user_area_width
    @area_h = @user_area_height
    @wide = @area_w >= WIDE_MIN
    if @wide && @face != :name
      @px = LIST_W + 1
    else
      @px = 0
    end
    @pw = @area_w - @px
    @in_y = @area_h - BTN_H - 1
    @st_y = @in_y - LINE_H * 2 - 2
    @st_y = @area_h - LINE_H * 2 - 1 unless input_on?
    @ans_y = TOP_H + 2
    @ans_h = @st_y - 2 - @ans_y
    # the messages' width does not follow the face (they stay wrapped
    # while the name is being chosen)
    @talk_w = (@wide ? @area_w - LIST_W - 1 : @area_w) - 10
    @list_y = TOP_H + 2
    @list_h = (@wide ? @area_h - 2 : @st_y - 2) - @list_y
    @ready = true
  end

  def talk_rows
    r = @ans_h / LINE_H
    r < 1 ? 1 : r
  end

  # ------------------------------------------------------------ drawing

  def draw_all
    return unless @ready
    layout
    clear_user_area
    @hits.clear
    draw_top
    if @face == :name
      draw_name_face
    else
      draw_list(false) if list_on?
      draw_talk(false) if talk_on?
      draw_stamp_button if talk_on? && @cur
      @gfx.draw_line(LIST_W, 0, LIST_W, @area_h - 1, theme_border) if @wide
    end
    draw_status(false)
    draw_input(false) if input_on?
    draw_send if input_on?
    tell_text_cursor
    @gfx.present
  end

  def bar_colors
    @bar_bg ||= FmrbColors.shade(FmrbConst::THEME_MENU_BG, theme_bg)
    @bar_fg ||= theme_fg_light
  end

  # The band on top: what is shown, and the buttons that change it.
  def draw_top
    bar_colors
    @gfx.fill_rect(0, 0, @area_w, TOP_H, @bar_bg)
    if @face == :name
      @gfx.draw_text(5, 2, "表示名を決める", @bar_fg)
      return
    end
    if @wide
      @gfx.draw_text(5, 2, "部屋", @bar_fg)
    end
    x = @px + 3
    if !@wide && @face == :list
      @gfx.draw_text(x + 2, 2, "部屋", @bar_fg)
    else
      unless @wide
        bar_button(:back, x, "<")
        x += 20
      end
      t = @cur ? @titles[@cur].to_s : "チャット"
      @gfx.draw_text(x + 2, 2, head_fit(t, @area_w - x - 110), @bar_fg)
    end
    # the display name and the button that changes it, on the right
    if list_on? || @wide
      bw = text_px("名前") + 10
      bx = @area_w - bw - 2
      bar_button(:name, bx, "名前")
      n = @name.empty? ? "(名前なし)" : @name
      nx = bx - 4 - text_px(n)
      @gfx.draw_text(nx, 2, n, @bar_fg) if nx > x + 40
    end
  end

  def bar_button(id, x, label)
    w = text_px(label) + 10
    @gfx.draw_rect(x, 1, w, TOP_H - 2, @bar_fg)
    @gfx.draw_text(x + 5, 2, label, @bar_fg)
    @hits.add(id, x, 0, w, TOP_H)
  end

  def draw_name_face
    msg = "チャットで使う名前を決めてください (#{@name_max} 文字まで)。ほかの人の画面に、発言と一緒に出ます。あとから右上の「名前」で変えられます。"
    sp = wrap(msg, @area_w - 8, @area_w - 8)
    i = 0
    y = TOP_H + 6
    while i < sp.size && y + LINE_H < @st_y
      @gfx.draw_text(4, y, msg.byteslice(sp[i], sp[i + 1]).to_s, theme_fg)
      y += LINE_H
      i += 2
    end
  end

  # The rooms (and their unread counts).
  def draw_list(show = true)
    return unless list_on?
    w = @wide ? LIST_W : @area_w
    @gfx.fill_rect(0, @list_y, w, @list_h, theme_bg)
    if @rooms.empty?
      @gfx.draw_text(4, @list_y + 2, @wide ? "(まだ無い)" : "(部屋はつながってから出ます)", theme_border)
    end
    i = 0
    while i < @rooms.size
      y = @list_y + i * ROW_H
      break if y + ROW_H > @list_y + @list_h
      id = @rooms[i]
      on = @wide ? id == @cur : i == @sel
      fg = theme_fg
      if on
        @gfx.fill_rect(1, y, w - 2, ROW_H, theme_accent)
        fg = theme_fg_light
      end
      u = unread(id)
      tag = u > 99 ? "(99+)" : "(#{u})"
      tw = u > 0 ? text_px(tag) + 4 : 0
      @gfx.draw_text(5, y + 2, head_fit(@titles[id].to_s, w - 10 - tw), fg)
      @gfx.draw_text(w - 4 - text_px(tag), y + 2, tag, on ? fg : theme_accent) if u > 0
      i += 1
    end
    @gfx.present if show
  end

  # The list again after numbers changed (news, a post, the rooms).
  def draw_list_area
    @list_dirty = false
    draw_list if list_on?
  end

  def redraw_due
    draw_list_area if @list_dirty
  end

  # The messages of the open room. The node mark goes after a name that
  # another node also uses among the messages on the screen.
  def draw_talk(show = true)
    return unless talk_on?
    if @picking && @cur
      draw_picker
      @gfx.present if show
      return
    end
    made = @spr_made
    t0 = Machine.board_millis
    @gfx.fill_rect(@px, @ans_y, @pw, @ans_h, theme_bg)
    x0 = @px + 3
    if @cur.nil?
      @gfx.draw_text(x0, @ans_y + 2, @wide ? "左の部屋を選んでください。" : "", theme_border)
    elsif @m_n.empty?
      @gfx.draw_text(x0, @ans_y + 2, @t_open ? "読んでいます…" : "まだ発言がありません。", theme_border)
    else
      draw_lines(x0)
    end
    @gfx.present if show
    # how much a screen with many kinds of stamps costs (report/c3.md 4)
    Log.info("chat: talk drawn, #{@spr_made - made} stamp images loaded, #{Machine.board_millis - t0} ms") if @spr_made != made
  end

  def draw_lines(x0)
    rows = talk_rows
    n = @l_msg.size
    marks = visible_marks(rows)
    me = @link.node.to_s
    i = 0
    while i < rows && @top + i < n
      k = @top + i
      id = @l_msg[k]
      j = id - @m_base
      y = @ans_y + i * LINE_H
      own = @m_node[j] == me
      body = own ? theme_accent : theme_fg
      x = x0
      if k == 0 || @l_msg[k - 1] != id
        @gfx.draw_text(x, y, hhmm(@m_t[j]), theme_border)
        x += TIME_PX
        label = "#{@m_name[j]}#{marks[id]}: "
        @gfx.draw_text(x, y, label, own ? theme_accent : theme_fg)
        x += text_px(label)
      end
      if @m_stamp[j]
        draw_stamp_line(@m_stamp[j], @l_off[k], x0 + 12, y) if @l_off[k] > 0
      elsif @l_len[k] > 0
        @gfx.draw_text(x, y, line_text(k), body)
      end
      i += 1
    end
    if n > rows
      bar_h = @ans_h * rows / n
      bar_h = 6 if bar_h < 6
      mx = n - rows
      bar_y = @ans_y + (@ans_h - bar_h) * (@top > mx ? mx : @top) / mx
      @gfx.fill_rect(@area_w - 3, bar_y, 2, bar_h, theme_border)
    end
  end

  # {id => "(abc)"} for the messages on the screen whose name another node
  # also uses there (the last 3 characters of the node).
  def visible_marks(rows)
    ids = []
    last = nil
    i = 0
    while i < rows && @top + i < @l_msg.size
      id = @l_msg[@top + i]
      ids << id if id != last
      last = id
      i += 1
    end
    marks = {}
    a = 0
    while a < ids.size
      ja = ids[a] - @m_base
      b = 0
      while b < ids.size
        jb = ids[b] - @m_base
        if a != b && @m_name[ja] == @m_name[jb] && @m_node[ja] != @m_node[jb]
          marks[ids[a]] = "(#{tail3(@m_node[ja])})"
          break
        end
        b += 1
      end
      a += 1
    end
    marks
  end

  def tail3(s)
    n = s.bytesize
    n <= 3 ? s : s.byteslice(n - 3, 3).to_s
  end

  def draw_status(show = true)
    x = @wide && @face != :name ? @px : 0
    w = @area_w - x
    @gfx.fill_rect(x, @st_y - 1, w, LINE_H * 2 + 1, theme_bg)
    @gfx.draw_line(x, @st_y - 2, @area_w - 1, @st_y - 2, theme_border)
    sp = wrap(@status, w - 6, w - 6)
    if sp.size > 0
      @gfx.draw_text(x + 3, @st_y, @status.byteslice(sp[0], sp[1]).to_s, theme_fg)
    end
    if sp.size > 2
      l2 = @status.byteslice(sp[2], @status.bytesize - sp[2]).to_s
      l2 = head_fit(l2, w - 6) if sp.size > 4
      @gfx.draw_text(x + 3, @st_y + LINE_H, l2, theme_fg)
    end
    @gfx.present if show
  end

  def set_status(s)
    return if s == @status
    @status = s
    draw_status if @ready
  end

  def send_x
    @area_w - resize_corner - 2 - send_w
  end

  def send_w
    text_px(@face == :name ? "決定" : "送信") + 14
  end

  def draw_send
    label = @face == :name ? "決定" : "送信"
    @gfx.draw_button(send_x, @in_y, send_w, BTN_H, label, theme_accent, theme_fg_light)
    @hits.add(:send, send_x, @in_y, send_w, BTN_H)
  end

  # The stamps' button, left of the send button (a room is open).
  def stamp_on?
    @face != :name && talk_on? && !@cur.nil?
  end

  def stamp_w
    text_px("スタンプ") + 12
  end

  def stamp_x
    send_x - 3 - stamp_w
  end

  def draw_stamp_button
    return unless stamp_on?
    fill = @picking ? theme_accent : theme_bg
    fg = @picking ? theme_fg_light : theme_fg
    @gfx.draw_button(stamp_x, @in_y, stamp_w, BTN_H, "スタンプ", fill, fg)
    @hits.add(:stamps, stamp_x, @in_y, stamp_w, BTN_H)
  end

  def draw_input(show = true)
    return unless input_on?
    x = @px + 1
    w = (stamp_on? ? stamp_x : send_x) - 3 - x
    h = BTN_H
    @gfx.fill_rect(x, @in_y, w, h, theme_bg)
    @gfx.draw_rect(x, @in_y, w, h, theme_border)
    if @input.empty?
      hint = @face == :name ? "表示名を打って Enter" : "ここに打って Enter で送る"
      @gfx.draw_text(x + 3, @in_y + 2, head_fit(hint, w - 6), theme_border)
      @in_cx = x + 3
    else
      t = tail_fit(@input, w - 10)
      @gfx.draw_text(x + 3, @in_y + 2, t, theme_fg)
      @in_cx = x + 3 + text_px(t)
    end
    @gfx.draw_line(@in_cx, @in_y + 2, @in_cx, @in_y + h - 3, theme_fg)
    tell_text_cursor
    @gfx.present if show
  end

  # Where a PC's or a browser's IME composes (FmrbApp.set_text_cursor, as
  # the AI helper does; nothing on the devices): the input line's cursor
  # while there is one. Sent only when it changes; canvas coordinates.
  def tell_text_cursor
    on = input_on?
    k = on ? @in_cx * 4096 + @in_y : -1
    return if k == @tc
    @tc = k
    return FmrbApp.set_text_cursor(0, 0, 0) unless on
    FmrbApp.set_text_cursor(@gfx.origin_x + @in_cx, @gfx.origin_y + @in_y + 1, LINE_H)
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

  # The start of s that fits in w pixels (".." after it when cut).
  def head_fit(s, w)
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

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    if t == :mouse_down
      x = ev[:x]
      y = ev[:y]
      b = @hits.at(x, y)
      return press(b[0], b[5]) if b
      r = room_at(x, y)
      open_room(@rooms[r]) if r
    elsif t == :mouse_wheel
      rows = wheel_rows(ev)
      if rows && talk_on? && @cur
        @picking ? pick_scroll(rows < 0 ? 1 : -1) : scroll(-rows)
      end
    elsif t == :key_down
      key(ev)
    end
  end

  # The room under (x, y) in the list, or nil.
  def room_at(x, y)
    return nil unless list_on?
    return nil if x >= (@wide ? LIST_W : @area_w) || y < @list_y || y >= @list_y + @list_h
    i = (y - @list_y) / ROW_H
    i < @rooms.size ? i : nil
  end

  def press(id, data = nil)
    case id
    when :back then close_room
    when :name then show_face(:name)
    when :send then send_input
    when :stamps then open_picker
    when :cell then pick(data)
    when :tab
      @pick_tab = data
      load_tab
      draw_all
    end
  end

  def leave_name
    return if @name.empty?
    show_face(@cur ? :talk : :list)
    set_status("")
  end

  def key(ev)
    return if ev_ctrl?(ev)
    sc = int(ev[:scancode])
    ch = int(ev[:character])
    if sc == 0x29 || ch == 27
      # Esc: back from the name, or from a room to the list (narrow)
      return leave_name if @face == :name
      return close_picker if @picking
      return close_room if !@wide && @face == :talk
      return
    end
    if @face == :list && !@wide
      return list_key(sc, ch)
    end
    if @picking && talk_on? && @face != :name
      case sc
      when 0x52, 0x4B then return pick_scroll(-1)
      when 0x51, 0x4E then return pick_scroll(1)
      end
    end
    if talk_on? && @face != :name
      case sc
      when 0x52 then return scroll(-1)
      when 0x51 then return scroll(1)
      when 0x4B then return scroll(-talk_rows)
      when 0x4E then return scroll(talk_rows)
      end
    end
    return unless input_on?
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
      erase_char
    elsif ch >= 32 && ch <= 126
      type_char(ch.chr)
    end
  end

  # The list (narrow): Up and Down choose, Enter opens.
  def list_key(sc, ch)
    if sc == 0x52 && @sel > 0
      @sel -= 1
      draw_list
    elsif sc == 0x51 && @sel < @rooms.size - 1
      @sel += 1
      draw_list
    elsif (ch == 10 || ch == 13) && @sel < @rooms.size
      open_room(@rooms[@sel])
    end
  end

  def type_char(s)
    max = @face == :name ? @name_max : @text_max
    if @input.length >= max
      return set_status(@face == :name ? "表示名は #{max} 文字までです。" : "長すぎます (#{max} 文字まで)。")
    end
    @input = @input + s
    draw_input
  end

  # Backspace: the last character off, found by its bytes (as the AI
  # helper does).
  def erase_char
    i = @input.bytesize
    return if i == 0
    i -= 1
    i -= 1 while i > 0 && (@input.getbyte(i) & 0xC0) == 0x80
    @input = "" + @input.byteslice(0, i).to_s
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
