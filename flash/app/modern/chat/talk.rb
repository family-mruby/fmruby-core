# Chat (doc/asterism_chat): the messages of the open room, required by
# chat.app.rb after its class body. Only the open room's messages are held
# (at most HOLD): whole messages in five arrays, and the lines they make on
# the screen as (message id, byte offset, byte length) -- a message's id is
# its index plus @m_base, so lines stay valid when older messages are put
# in front or the oldest go.
#
# A message is shown as "HH:MM name: text". Its first line leaves room for
# the time, the name and a node mark "(abc)", which is drawn only when the
# same name comes from another node among the messages on the screen
# (screen.rb), so the wrapping never depends on what is scrolled into view.
class ChatApp < FmrbApp
  TIME_PX = 36           # "HH:MM "
  MARK_PX = 30           # "(abc)"

  def talk_reset
    @m_n = []
    @m_t = []
    @m_node = []
    @m_name = []
    @m_text = []
    @m_base = 0
    @l_msg = []
    @l_off = []
    @l_len = []
    @top = 0
    @t_open = false       # the newest of the room to fetch (it was opened)
    @t_after = false      # what came after the newest held to fetch
    @t_before = false     # older ones to fetch (scrolled back)
    @t_more = false       # the host has older ones than the oldest held
    @t_cut = false        # newer ones were dropped (scrolled far back)
  end

  def open_room(id)
    return unless @titles.key?(id)
    if id != @cur
      @cur = id
      talk_reset
      @t_open = true
    end
    @sel = @rooms.index(id) || 0
    show_face(:talk)
  end

  # Back to the list (narrow): the room's messages are let go.
  def close_room
    @cur = nil
    talk_reset
    show_face(:list)
  end

  def newest_n
    @m_n.empty? ? 0 : @m_n[@m_n.size - 1]
  end

  # ------------------------------------------------------------ answers

  def took_history(kind, room, v)
    if kind == :open
      @t_open = false
    elsif kind == :after
      @t_after = false
    else
      @t_before = false
    end
    return if room != @cur
    if error_of(v) || !v.is_a?(Hash)
      @need_rooms = true if error_of(v) == "no_room"
      set_status(message_of(v))
      return
    end
    set_last(room, int(v["last"]))
    list = v["msgs"]
    list = [] unless list.is_a?(Array)
    more = v["more"] ? true : false
    if kind == :open
      add_after(list)
      @t_more = more
      @top = max_top
      @t_after = true if newest_n < int(@last[room])
    elsif kind == :after
      bottom = at_bottom?
      add_after(list)
      @top = max_top if bottom
      if more
        bottom ? (@t_after = true) : (@t_cut = true)
      else
        @t_cut = false
      end
    else
      add_before(list)
      @t_more = more
      set_status("") if @status == "前の発言を読んでいます…"
    end
    mark_seen(room, newest_n) unless @t_cut || @m_n.empty?
    @list_dirty = true
    Log.info("chat: #{kind} #{room} +#{list.size} held #{@m_n.size} pool #{FmrbApp.pool_used}")
    draw_talk
  end

  # One message of the host -> [n, t, node, name, text], or nil.
  def msg_of(m)
    return nil unless m.is_a?(Hash)
    text = m["text"].to_s
    # a stamp (C3) is not shown yet: a word in its place
    text = "(スタンプ)" if text.empty? && m.key?("stamp")
    text = " " if text.empty?
    [int(m["n"]), int(m["t"]), m["node"].to_s, m["name"].to_s, text]
  end

  # Newer messages at the end (the ones held already are skipped); the
  # oldest go past HOLD.
  def add_after(list)
    i = 0
    while i < list.size
      m = msg_of(list[i])
      i += 1
      next if m.nil? || m[0] <= newest_n
      @m_n << m[0]
      @m_t << m[1]
      @m_node << m[2]
      @m_name << m[3]
      @m_text << m[4]
      wrap_msg(@m_base + @m_n.size - 1, @l_msg, @l_off, @l_len)
    end
    while @m_n.size > HOLD
      gone = 0
      while @l_msg.size > 0 && @l_msg[0] == @m_base
        @l_msg.shift
        @l_off.shift
        @l_len.shift
        gone += 1
      end
      @m_n.shift
      @m_t.shift
      @m_node.shift
      @m_name.shift
      @m_text.shift
      @m_base += 1
      @t_more = true
      @top -= gone
    end
    @top = 0 if @top < 0
  end

  # Older messages in front; the newest go past HOLD (fetched again when
  # the view comes back down). The view stays on what it showed, one line
  # of the older ones above it.
  def add_before(list)
    oldest = @m_n.empty? ? 0 : @m_n[0]
    keep = []
    i = 0
    while i < list.size
      m = msg_of(list[i])
      i += 1
      keep << m if m && (oldest == 0 || m[0] < oldest)
    end
    return if keep.empty?
    pm = []
    po = []
    pl = []
    base = @m_base - keep.size
    j = keep.size - 1
    while j >= 0
      m = keep[j]
      @m_n.unshift(m[0])
      @m_t.unshift(m[1])
      @m_node.unshift(m[2])
      @m_name.unshift(m[3])
      @m_text.unshift(m[4])
      j -= 1
    end
    @m_base = base
    k = 0
    while k < keep.size
      wrap_msg(base + k, pm, po, pl)
      k += 1
    end
    added = pm.size
    @l_msg = pm + @l_msg
    @l_off = po + @l_off
    @l_len = pl + @l_len
    @top += added - 1
    @top = 0 if @top < 0
    while @m_n.size > HOLD
      last = @m_base + @m_n.size - 1
      while @l_msg.size > 0 && @l_msg[@l_msg.size - 1] == last
        @l_msg.pop
        @l_off.pop
        @l_len.pop
      end
      @m_n.pop
      @m_t.pop
      @m_node.pop
      @m_name.pop
      @m_text.pop
      @t_cut = true
    end
    mx = max_top
    @top = mx if @top > mx
  end

  # ------------------------------------------------------------ lines

  # The lines of message id into the three arrays.
  def wrap_msg(id, lm, lo, ll)
    k = id - @m_base
    s = @m_text[k]
    first = @talk_w - TIME_PX - text_px(@m_name[k]) - 12 - MARK_PX
    if first < 48
      # a narrow pane: the text starts under the name
      lm << id
      lo << 0
      ll << 0
      first = @talk_w
    end
    sp = wrap(s, first, @talk_w)
    i = 0
    while i < sp.size
      lm << id
      lo << sp[i]
      ll << sp[i + 1]
      i += 2
    end
    id
  end

  # Every message wrapped again (the width changed); the message at the top
  # stays at the top.
  def rewrap
    keep = @top < @l_msg.size ? @l_msg[@top] : -1
    @l_msg = []
    @l_off = []
    @l_len = []
    i = 0
    while i < @m_n.size
      wrap_msg(@m_base + i, @l_msg, @l_off, @l_len)
      i += 1
    end
    @top = 0
    if keep >= 0
      j = 0
      while j < @l_msg.size
        if @l_msg[j] == keep
          @top = j
          break
        end
        j += 1
      end
    end
    mx = max_top
    @top = mx if @top > mx
  end

  # s cut into lines (the first first_w pixels wide, the rest w) at its
  # newlines and where it gets too wide, as [offset, length, ...] in bytes.
  # Fixed-pitch widths of the 12px Japanese font: ASCII 6, the rest 12.
  def wrap(s, first_w, w)
    out = []
    n = s.bytesize
    i = 0
    start = 0
    used = 0
    lim = first_w
    while i < n
      b = s.getbyte(i)
      if b == 10
        out << start
        out << i - start
        i += 1
        start = i
        used = 0
        lim = w
        next
      end
      len = b < 0x80 ? 1 : (b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : (b >= 0xC0 ? 2 : 1)))
      cw = b < 0x80 ? 6 : 12
      if used + cw > lim && i > start
        out << start
        out << i - start
        start = i
        used = 0
        lim = w
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

  def line_text(i)
    n = @l_len[i]
    return "" if n == 0
    @m_text[@l_msg[i] - @m_base].byteslice(@l_off[i], n).to_s
  end

  def max_top
    m = @l_msg.size - talk_rows
    m < 0 ? 0 : m
  end

  def at_bottom?
    @top >= max_top
  end

  # "HH:MM" of a host time, in the host's zone (hello's tz).
  def hhmm(t)
    s = (t + @tz) % 86400
    h = s / 3600
    m = s % 3600 / 60
    "#{h < 10 ? '0' : ''}#{h}:#{m < 10 ? '0' : ''}#{m}"
  end

  # ------------------------------------------------------------ moving

  # d lines down (up when negative). Past the top the older ones are asked
  # for; at the bottom, the newer ones dropped before.
  def scroll(d)
    if d < 0 && @top == 0
      return older
    end
    top = @top + d
    mx = max_top
    top = mx if top > mx
    top = 0 if top < 0
    if top == @top
      @t_after = true if d > 0 && @t_cut
      return
    end
    @top = top
    @t_after = true if @t_cut && @top >= mx
    set_status("") if d > 0 && @status == "これより前の発言はありません。"
    draw_talk
  end

  def older
    return if @cur.nil? || @t_open || @m_n.empty?
    if @t_more
      return if @t_before
      @t_before = true
      set_status("前の発言を読んでいます…")
    else
      set_status("これより前の発言はありません。")
    end
  end
end
