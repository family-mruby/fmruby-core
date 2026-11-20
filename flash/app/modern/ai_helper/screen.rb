# AI helper (doc/ai_coding): the screen, required by ai_helper.app.rb after
# its class body (smaller files keep the compile peak in the VM pool low,
# doc/reference/app_memory.md): the answers (whole messages, wrapped again
# when the window is resized), the menu bar and its Edit pull-down, the
# status and input lines, the bottom row, and the input.
class AiHelperApp < FmrbApp
  # ------------------------------------------------------------ the text

  # Adds str (may hold newlines) under the label, wrapped to the area, and
  # shows its first line.
  def add_text(who, str)
    @msgs << "#{who}: #{str}"
    @msg_colors << (who == "あなた" ? theme_accent : theme_fg)
    first = @l_msg.size
    first -= trim_lines(wrap_msg(@msg_base + @msgs.size - 1))
    first = 0 if first < 0
    rows = answer_rows
    # the start of the new text, or the end when it is short
    @top = @l_msg.size - rows
    @top = first if @top > first
    @top = 0 if @top < 0
    draw_answers if @ready
  end

  # The lines of message m (its number since the start), and an empty line
  # after it.
  def wrap_msg(m)
    s = @msgs[m - @msg_base]
    sp = wrap(s, @wrap_w)
    i = 0
    while i < sp.size
      @l_msg << m
      @l_off << sp[i]
      @l_len << sp[i + 1]
      i += 2
    end
    @l_msg << m
    @l_off << s.bytesize
    @l_len << 0
    m
  end

  # At most MAX_LINES: the oldest messages go (never the newest, m). The
  # number of lines removed.
  def trim_lines(m)
    gone = 0
    while @l_msg.size > MAX_LINES && @msg_base < m
      while @l_msg.size > 0 && @l_msg[0] == @msg_base
        @l_msg.shift
        @l_off.shift
        @l_len.shift
        gone += 1
      end
      @msgs.shift
      @msg_colors.shift
      @msg_base += 1
    end
    gone
  end

  # All the messages wrapped again for the area's width (after a resize);
  # the line at the top of the view stays at the top.
  def rewrap
    keep_m = -1
    keep_off = 0
    if @top < @l_msg.size
      keep_m = @l_msg[@top]
      keep_off = @l_off[@top]
    end
    @wrap_w = @area_w - 8
    @l_msg = []
    @l_off = []
    @l_len = []
    last = @msg_base + @msgs.size - 1
    m = @msg_base
    while m <= last
      wrap_msg(m)
      m += 1
    end
    trim_lines(last) if last >= 0
    @top = 0
    i = 0
    while i < @l_msg.size
      if @l_msg[i] == keep_m && @l_off[i] <= keep_off
        @top = i
      elsif @l_msg[i] > keep_m && keep_m >= 0
        break
      end
      i += 1
    end
    max = @l_msg.size - answer_rows
    max = 0 if max < 0
    @top = max if @top > max
  end

  def line_text(i)
    n = @l_len[i]
    return "" if n == 0
    @msgs[@l_msg[i] - @msg_base].byteslice(@l_off[i], n).to_s
  end

  # s cut into lines of w pixels at its newlines and where it gets too
  # wide, as [offset, length, offset, length, ...] in bytes. Fixed-pitch
  # widths of the 12px Japanese font: ASCII 6, the rest 12.
  def wrap(s, w)
    out = []
    n = s.bytesize
    i = 0
    start = 0
    used = 0
    while i < n
      b = s.getbyte(i)
      if b == 10
        out << start
        out << i - start
        i += 1
        start = i
        used = 0
        next
      end
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : (b >= 0xC0 ? 2 : 1)))
      cw = b < 0x80 ? 6 : 12
      if used + cw > w && i > start
        out << start
        out << i - start
        start = i
        used = 0
      end
      used += cw
      i += len
    end
    if start < n
      out << start
      out << n - start
    end
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

  # The start of s that fits (the status line keeps its beginning).
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

  def set_status(s)
    return if s == @status
    @status = s
    draw_status if @ready
  end

  # ------------------------------------------------------------ drawing

  def layout
    @area_w = @user_area_width
    @area_h = @user_area_height
    @ans_y = MENU_H + 3
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
    n = @l_msg.size
    i = 0
    while i < rows && @top + i < n
      k = @top + i
      @gfx.draw_text(3, @ans_y + i * LINE_H, line_text(k), @msg_colors[@l_msg[k] - @msg_base])
      i += 1
    end
    # where the view is in the whole
    if n > rows
      bar_h = @ans_h * rows / n
      bar_h = 6 if bar_h < 6
      bar_y = @ans_y + (@ans_h - bar_h) * @top / (n - rows)
      @gfx.fill_rect(@area_w - 3, @ans_y, 2, @ans_h, theme_bg)
      @gfx.fill_rect(@area_w - 3, bar_y, 2, bar_h, theme_border)
    end
    # the pull-down stays on top of the answers it covers
    draw_edit_menu if @menu_open
    @gfx.present if show
  end

  def draw_status(show = true)
    @gfx.fill_rect(0, @st_y - 1, @area_w, LINE_H * 2 + 1, theme_bg)
    @gfx.draw_line(0, @st_y - 2, @area_w - 1, @st_y - 2, theme_border)
    sp = wrap(@status, @area_w - 6)
    if sp.size > 0
      @gfx.draw_text(3, @st_y, @status.byteslice(sp[0], sp[1]).to_s, theme_fg)
    end
    if sp.size > 2
      l2 = @status.byteslice(sp[2], @status.bytesize - sp[2]).to_s
      l2 = head_fit(l2, @area_w - 6) if sp.size > 4
      @gfx.draw_text(3, @st_y + LINE_H, l2, theme_fg)
    end
    @gfx.present if show
  end

  def draw_input(show = true)
    h = BTN_H
    @gfx.fill_rect(1, @in_y, @area_w - 2, h, theme_bg)
    @gfx.draw_rect(1, @in_y, @area_w - 2, h, theme_border)
    if @input.empty?
      @gfx.draw_text(4, @in_y + 2, "#{MODE_LABEL[@mode]}: #{PLACEHOLDER[@mode]}", theme_border)
      @in_cx = 4
    else
      t = tail_fit(@input, @area_w - 14)
      @gfx.draw_text(4, @in_y + 2, t, theme_fg)
      @in_cx = 4 + text_px(t)
    end
    @gfx.draw_line(@in_cx, @in_y + 2, @in_cx, @in_y + h - 3, theme_fg)
    tell_text_cursor
    @gfx.present if show
  end

  # Where a PC's or a browser's IME composes (FmrbApp.set_text_cursor, as
  # the editor and the Shell do; nothing on the devices): the input line's
  # cursor while the line takes text, none while the bottom row asks a
  # question or the Edit pull-down is open. Sent only when it changes (the
  # kernel keeps the last one; it follows the focused window by itself).
  # The kernel takes canvas coordinates (from the window's top-left): the
  # user area's origin is added to this app's own.
  def tell_text_cursor
    on = @choices.nil? && !@menu_open
    k = on ? @in_cx * 4096 + @in_y : -1
    return if k == @tc
    @tc = k
    return FmrbApp.set_text_cursor(0, 0, 0) unless on
    FmrbApp.set_text_cursor(@gfx.origin_x + @in_cx, @gfx.origin_y + @in_y + 1, LINE_H)
  end

  def button(id, x, y, w, label, on, enabled = true)
    fill = on ? theme_accent : theme_bg
    fg = on ? theme_fg_light : (enabled ? theme_fg : theme_border)
    @gfx.draw_button(x, y, w, BTN_H, label, fill, fg)
    @gfx.draw_rect(x, y, w, BTN_H, theme_border) unless on
    @hits.add(id, x, y, w, BTN_H)
  end

  def button_w(label)
    text_px(label) + 14
  end

  # The menu bar: words on a band, the use chosen inverted, a rule before
  # the two that act (Fix, and the Edit pull-down with its arrow).
  def draw_menu_bar
    @gfx.fill_rect(0, 0, @area_w, MENU_H, @bar_bg)
    @gfx.fill_rect(0, MENU_H, @area_w, @ans_y - MENU_H, theme_bg)
    x = 2
    i = 0
    while i < MENU_BAR.size
      id = MENU_BAR[i][0]
      label = MENU_BAR[i][1]
      if id == :sep
        @gfx.draw_line(x + 3, 3, x + 3, MENU_H - 4, @bar_dim)
        x += 8
        i += 1
        next
      end
      w = text_px(label) + 10
      w += 8 if id == :edit
      on = (id == :m_ask && @mode == "ask") || (id == :m_hint && @mode == "hint") ||
           (id == :m_make && @mode == "make") || (id == :edit && @menu_open)
      enabled = id != :m_make || @hello.nil? || mode_on?("make")
      fg = enabled ? @bar_fg : @bar_dim
      if on
        @gfx.fill_rect(x, 1, w, MENU_H - 2, @bar_fg)
        fg = @bar_bg
      end
      @gfx.draw_text(x + 5, 2, label, fg)
      if id == :edit
        @edit_x = x
        ax = x + 5 + text_px(label) + 2
        @gfx.fill_triangle(ax, 6, ax + 6, 6, ax + 3, 9, fg)
      end
      @hits.add(id, x, 0, w, MENU_H)
      x += w + 2
      i += 1
    end
  end

  # The Edit pull-down, under its word (kept inside the area).
  def edit_menu_box
    w = 0
    i = 0
    while i < EDIT_ITEMS.size
      tw = text_px(EDIT_ITEMS[i][1])
      w = tw if tw > w
      i += 1
    end
    w += 14
    x = @edit_x
    x = @area_w - w - 1 if x + w > @area_w - 1
    x = 0 if x < 0
    [x, MENU_H, w, ITEM_H * EDIT_ITEMS.size + 2]
  end

  def draw_edit_menu
    b = edit_menu_box
    @gfx.fill_rect(b[0], b[1], b[2], b[3], theme_bg)
    @gfx.draw_rect(b[0], b[1], b[2], b[3], theme_border)
    i = 0
    while i < EDIT_ITEMS.size
      @gfx.draw_text(b[0] + 7, b[1] + 3 + i * ITEM_H, EDIT_ITEMS[i][1], theme_fg)
      if i > 0
        ly = b[1] + 1 + i * ITEM_H
        @gfx.draw_line(b[0] + 3, ly, b[0] + b[2] - 4, ly, theme_border)
      end
      i += 1
    end
  end

  # The item of the pull-down at (x, y), or nil.
  def edit_menu_item(x, y)
    b = edit_menu_box
    return nil if x < b[0] || x >= b[0] + b[2] || y < b[1] + 1 || y >= b[1] + b[3] - 1
    i = (y - b[1] - 1) / ITEM_H
    i < EDIT_ITEMS.size ? EDIT_ITEMS[i][0] : nil
  end

  def open_edit_menu
    @menu_open = true
    draw_buttons(false)
    draw_edit_menu
    @gfx.present
  end

  # Closed by an item, a press outside it, Esc, the wheel or a resize.
  def close_edit_menu
    @menu_open = false
    draw_buttons(false)
    draw_answers
  end

  def draw_buttons(show = true)
    @hits.clear
    draw_menu_bar
    # the bottom row: the choices of a question, or the usual buttons with
    # Reset talk on the right (clear of the resize corner)
    @gfx.fill_rect(0, @btn_y, @area_w, BTN_H, theme_bg)
    x = 1
    if @choices
      i = 0
      while i < @choices.size
        c = @choices[i]
        w = button_w(c[1])
        button(c[0], x, @btn_y, w, c[1], true)
        x += w + 4
        i += 1
      end
    else
      button(:send, x, @btn_y, 40, "送信", true)
      x += 43
      button(:stop, x, @btn_y, 40, "中断", true)
      x += 43
      rw = button_w("会話リセット")
      rx = @area_w - resize_corner - rw - 1
      button(:talk, rx, @btn_y, rw, "会話リセット", true)
      if @mode == "ask"
        label = @attach ? "文書も送る" : "文書は送らない"
        w = button_w(label)
        room = rx - 4 - x
        w = room if w > room
        button(:attach, x, @btn_y, w, label, @attach) if w > 20
      end
    end
    # a question in the bottom row and the pull-down take the cursor away
    tell_text_cursor
    @gfx.present if show
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    if @menu_open && (t == :mouse_down || t == :mouse_wheel || t == :key_down)
      # the pull-down takes the next press, wheel or key: an item acts, the
      # rest only closes it (Esc too); other keys go on as usual
      item = t == :mouse_down ? edit_menu_item(ev[:x], ev[:y]) : nil
      close_edit_menu
      press(item) if item
      return if t != :key_down || ev[:character].to_i == 27 || ev[:scancode].to_i == 0x29
    end
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
    return if id.nil?
    if @choices
      i = 0
      while i < @choices.size
        return choose(id) if @choices[i][0] == id
        i += 1
      end
    end
    case id
    when :send then send_input
    when :stop then cancel_request
    when :talk then new_talk
    when :m_ask then pick_mode("ask")
    when :m_hint then pick_mode("hint")
    when :m_make then pick_mode("make")
    when :fix then press_fix
    when :edit then open_edit_menu
    when :erase then press_erase
    when :restore then press_restore
    when :attach
      @attach = !@attach
      draw_buttons
    end
  end

  def scroll(d)
    rows = answer_rows
    top = @top + d
    max = @l_msg.size - rows
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
    when 0x29 then return cancel_request   # Esc, by its scancode (as the editor reads it)
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
      send_input unless @choices
    elsif ch == 8
      erase_char
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

  # Backspace: the last character off, found by its bytes, into a string of
  # its own ("" + copies it). A slice longer than an embedded string shares
  # the longer text's buffer, and this mruby counts a shared string's
  # characters past its end while the bytes there are not ASCII
  # (mrb_utf8_strlen), so String#length on it was too large and Backspace
  # stopped, or cut into a character, after the first one (32-bit builds
  # from 4 kana, 64-bit ones from 10).
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
