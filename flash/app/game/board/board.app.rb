# Drawing board (doc/ruby_asterism, GS6): the screen of a shared drawing
# board run by a game host (the asterism-games repository, README "The
# drawing board") on the same Zenoh network. Everybody at the board draws
# on one picture; nobody wins.
#
# - The host has one or more boards (objects <host>/games/board*) and puts
#   each board's view on asterism/<host>/games/<board>/state after every
#   change. This app lists them and joins one (join(name) -> a token; the
#   first to join is the owner).
# - The picture is strokes: a colour (one of 16), a width step (one of 4)
#   and points in board coordinates (640x360). Every change goes out on
#   asterism/<host>/games/<board>/strokes: points of a stroke ("o" "a",
#   "k" 1 when they go on with a stroke, starting with its last point so
#   far), a stroke taken back ("u") and the board cleared ("c"). This app
#   draws them as they come, and keeps the strokes (2 bytes a coordinate)
#   to draw the picture again after one is taken back.
# - On joining (and after the connection or the host comes back, or R) it
#   reads the whole picture in pages (picture(id, at)).
# - A recording of the strokes key played back by a console puts the same
#   messages: they are drawn the same way. Their version ("v") is older
#   than the board's, so the screen says a recording is playing; R reads
#   the board's own picture again.
# - Drawing: press, move and release the left button in the picture for
#   one stroke. The points go to the host while the stroke is drawn
#   (line, then more), so the others see it grow.
# - The connection is the machine's (FmrbAsterism.settings, as the other
#   table apps): the router, the ID, a credentials .tar, the room's seat
#   in the browser build. The member's name is the profile's.
#   Staying connected, the list, joining, leaving and the view are the
#   table client's (FmrbAsterism::TableClient, doc/app_refactor R4); this
#   file has the board: the strokes, the picture, the screen.
# - Keys. The list: up / down choose, Enter joins. At the board: 1-4 the
#   width, U takes back your last stroke, C clears (the owner; y / n),
#   R reads the picture again, Esc leaves. A click picks a colour or a
#   button.

# A stroke on its way to the host: its points not sent yet, the id the
# host gave it, how many points it has there.
class BoardOutStroke
  attr_accessor :c, :w, :pts, :id, :sent, :asked, :done

  def initialize(c, w)
    @c = c
    @w = w
    @pts = []
    @id = nil
    @sent = 0
    @asked = false
    @done = false
  end
end

class DrawingBoardApp < FmrbApp
  MEM_EVERY_MS = 30000
  # The board (the host's lim says the same).
  BW = 640
  BH = 360
  PART_POINTS = 256
  LINE_POINTS = 1024
  MAX_STROKES = 1000
  MAX_POINTS = 40000
  # A part goes to the host once this many points wait, or this long after the last.
  SEND_POINTS = 48
  SEND_MS = 200
  # Messages kept while the picture is read.
  QUEUE_MAX = 64
  PANEL_W = 62
  # The board's own colours (the picture is the app's work).
  C_PAPER = 0xFF
  C_EDGE = 0x92
  PALETTE = [0x00, 0x92, 0xDB, 0xFF, 0xE0, 0xF0, 0xFC, 0x88,
             0x14, 0x5C, 0x1F, 0x03, 0x02, 0x82, 0xE3, 0xF2]
  WIDTH_PX = [1, 2, 4, 8]

  REASONS = {
    "token" => "ボードの人ではない", "not owner" => "主だけができる", "nothing" => "取り消す線が無い",
    "colour" => "色が違う", "width" => "太さが違う", "points" => "点が違う", "full" => "ボードが満員",
    "identity" => "この部屋での名乗りが合わない (部屋のリンクを開き直す)"
  }

  def on_create
    @gfx.set_font(:ja, 12)
    # The boards ("<host>/games/board*", tables[p] = [strokes, members]
    # from its last put), the board this app is at, its token: the table
    # client's. A newer version is asked no sooner than 1 s after the last
    # view (the strokes show the changes meanwhile).
    @tc = ::FmrbAsterism::TableClient.new(self, "board", tag: "board")
    @tc.view_min_ms = 1000
    @id = @tc.link.node
    @sub = nil            # the strokes of every board
    @list_sel = 0
    @cmd_f = nil
    @pic_f = nil
    @send_f = nil
    @view = nil
    @owner = false
    @confirm = false
    @replay = false
    @note = ""
    # The picture: parallel arrays, oldest first.
    @s_id = []
    @s_c = []
    @s_w = []
    @s_p = []             # String: 4 bytes a point (x high, x low, y high, y low)
    @s_box = []           # [x0, y0, x1, y1] of each stroke, board coordinates
    @npoints = 0
    @loading = false
    @pic_v = 0
    @pic_q = []
    @color = 0
    @width = 1
    @drawing = false
    @last_sx = 0
    @last_sy = 0
    @last_bx = -1
    @last_by = -1
    @out = []             # BoardOutStroke, oldest first
    @sent_at = 0
    @keys = []
    @moves = []
    @hits = FmrbUI::HitAreas.new
    @mem_at = 0
    @lat_n = 0
    @lat_sum = 0
    @lat_max = 0
    @redraw_ms = 0
    @shown_n = 0
    @dirty = true
    @panel_dirty = true
    layout
    draw_screen
  end

  def on_resize(w, h)
    layout
    @dirty = true
  end

  def my_name
    pr = ::Asterism.profile
    n = pr ? pr["name"] : nil
    n && !n.to_s.empty? ? n.to_s : @id
  rescue
    @id
  end

  # ------------------------------------------------------------ network
  # The connection, the list, joining and the view are @tc's
  # (FmrbAsterism::TableClient); these are its calls, the strokes and the
  # board's own calls.

  # A put of a board: [strokes, members] for the list.
  def table_state(p, pv)
    @dirty = true if @tc.table.nil?
    [pv["n"].to_i, pv["mb"].is_a?(Array) ? pv["mb"].size : 0]
  end

  def tables_changed(added)
    @list_sel = 0 if !added && @list_sel >= @tc.table_list.size
    @dirty = true if added || @tc.table.nil?
  end

  def table_link_up(link)
    subscribe_strokes
    @note = ""
    # Back at a board: read the picture again.
    start_picture if @tc.token
    @dirty = true if @tc.table.nil?
    @panel_dirty = true
  end

  def table_link_down(link)
    @sub = nil
    @cmd_f = nil
    @pic_f = nil
    @send_f = nil
    @loading = false
    @out = []
    @dirty = true if @tc.table.nil?
    @note = "網から切れた: つなぎ直し中" if @tc.table
    @panel_dirty = true
  end

  def table_host_gone
    @note = "ホストとのつながりが切れた"
    @panel_dirty = true
  end

  def table_host_back
    @note = ""
    start_picture
    @panel_dirty = true
  end

  # The strokes before the states: a state put follows the strokes of its
  # version, so a stroke older than the last state seen is a recording
  # played back.
  def table_watch
    subscribe_strokes if @sub.nil?
    watch_strokes if @tc.table
  end

  def subscribe_strokes
    @sub = ::Asterism.session.subscribe("asterism/*/games/*/strokes", depth: 32)
  rescue ::Asterism::Error => e
    Log.info("board: subscribe: #{e.message}")
    @sub = nil
  end

  # "asterism/<host>/games/<board>/<last>" -> "<host>/games/<board>", or nil.
  def path_of(k, last)
    return nil unless k.start_with?("asterism/") && k.end_with?(last)
    k.byteslice(9, k.bytesize - 9 - last.bytesize)
  end

  # The strokes of this board as they come: drawn now (or kept while the
  # picture is read). The time from taking them to the screen is measured.
  def watch_strokes
    return unless @sub
    t0 = Machine.board_millis
    drew = false
    table = @tc.table
    @sub.each_pending do |key, payload, att|
      begin
        next unless table && @tc.token && path_of(key.to_s, "/strokes") == table
        m = ::Asterism::Codec.unpack(payload)
        next unless m.is_a?(Hash)
        if @loading
          @pic_q << m if @pic_q.size < QUEUE_MAX
          next
        end
        v = m["v"].to_i
        if v < @tc.want_v && !@replay
          @replay = true
          @panel_dirty = true
        end
        drew = true if apply(m)
      rescue => e
        Log.info("board: strokes: #{e.message}")
      end
    end
    return unless drew
    @gfx.present
    @panel_dirty = true if @shown_n != @s_id.size
    dt = Machine.board_millis - t0
    @lat_n += 1
    @lat_sum += dt
    @lat_max = dt if dt > @lat_max
  end

  # One strokes message onto the picture. true when it drew.
  def apply(m)
    o = m["o"]
    if o == "a"
      pts = m["p"]
      return false unless pts.is_a?(Array) && pts.size >= 2
      id = m["id"].to_i
      c = m["c"].to_i
      w = m["w"].to_i
      i = stroke_index(id)
      if i
        append_points(i, pts, 2)
      elsif m["k"].to_i == 1 && @s_id.size > 0 && id < @s_id[0]
        return false
      else
        add_stroke(id, c, w, pts)
      end
      draw_pts(pts, c, w)
      true
    elsif o == "u"
      i = stroke_index(m["id"].to_i)
      return false unless i
      bx = @s_box[i]
      remove_at(i)
      # Where it was, and the width of the widest pen around it.
      redraw_region(bx[0] - 5, bx[1] - 5, bx[2] + 5, bx[3] + 5)
      true
    elsif o == "c"
      clear_picture
      paint_paper
      true
    else
      false
    end
  end

  def join(p)
    return if @tc.joining? || p.nil?
    err = @tc.join(p, my_name)
    @note = err || ""
    @dirty = true
  end

  def table_joined(r)
    @owner = r["owner"] ? true : false
    @view = nil
    @confirm = false
    @dirty = false
    draw_screen
    start_picture
  end

  def table_refused(why)
    @note = why ? reason(why) : "ホストが答えない"
    @dirty = true
  end

  def leave_table
    @tc.leave
    @view = nil
    @cmd_f = nil
    @pic_f = nil
    @send_f = nil
    @loading = false
    @out = []
    @drawing = false
    @confirm = false
    @replay = false
    @note = ""
    clear_picture
    @dirty = true
  end

  def table_dropped
    leave_table
    @note = "ボードから外れた"
  end

  def table_view(v)
    @view = v
    @owner = v["own"] ? true : false
    @panel_dirty = true
  end

  def table_view_failed
    @note = "ホストが答えない: 待っています"
    @panel_dirty = true
  end

  def send_cmd(what)
    tok = @tc.token
    return if @cmd_f || tok.nil?
    px = @tc.proxy.async
    @cmd_f = what == :undo ? px.undo(tok) : px.clear(tok)
    @confirm = false
    @panel_dirty = true
  rescue ::Asterism::Error => e
    @cmd_f = nil
    @note = e.message.to_s
  end

  def collect_cmd
    f = @cmd_f
    return unless f && f.done?
    @cmd_f = nil
    begin
      r = f.value
      @note = r == "ok" ? "" : reason(r)
    rescue ::Asterism::Error => e
      @note = "ホストが答えない"
    end
    @tc.refresh_view
    @panel_dirty = true
  end

  def reason(r)
    s = r.to_s
    REASONS[s] || s
  end

  # ------------------------------------------------------------ the picture (reading it)

  # Reads the whole picture again, in pages; messages that come meanwhile
  # wait in @pic_q.
  def start_picture
    return unless @tc.token
    clear_picture
    paint_paper
    @gfx.present
    @replay = false
    @loading = true
    @pic_q = []
    @pic_v = -1
    @pic_t0 = Machine.board_millis
    ask_picture(0, 0)
    @panel_dirty = true
  end

  def ask_picture(id, at)
    @pic_f = @tc.proxy.async.picture(id, at)
  rescue ::Asterism::Error => e
    @pic_f = nil
    @loading = false
    @note = e.message.to_s
  end

  def collect_picture
    f = @pic_f
    return unless f && f.done?
    @pic_f = nil
    pg = nil
    begin
      pg = f.value
    rescue ::Asterism::Error => e
      @loading = false
      @note = "絵を読めない: R で読み直す"
      @panel_dirty = true
      return
    end
    return unless pg.is_a?(Hash)
    @pic_v = pg["v"].to_i if @pic_v < 0
    items = pg["p"].is_a?(Array) ? pg["p"] : []
    i = 0
    while i < items.size
      it = items[i]
      id = it[0].to_i
      pts = it[3]
      if pts.is_a?(Array) && pts.size >= 2
        j = stroke_index(id)
        if it[4].to_i == 1 && j
          append_points(j, pts, 2)
        elsif j.nil?
          add_stroke(id, it[1].to_i, it[2].to_i, pts)
        end
        draw_pts(pts, it[1].to_i, it[2].to_i)
      end
      i += 1
    end
    @gfx.present
    nx = pg["nx"]
    if nx.is_a?(Array) && nx.size == 2
      ask_picture(nx[0], nx[1])
      return
    end
    # Done: the messages that came while reading, newer than its first page.
    @loading = false
    q = @pic_q
    @pic_q = []
    i = 0
    while i < q.size
      m = q[i]
      if m["v"].to_i > @pic_v
        if m["o"] == "a" && m["k"].to_i == 0 && stroke_index(m["id"].to_i)
          # Read with the picture already.
        else
          apply(m)
        end
      end
      i += 1
    end
    @gfx.present
    @redraw_ms = Machine.board_millis - @pic_t0
    Log.info("board: picture read: #{@s_id.size} strokes, #{@npoints} points in #{@redraw_ms} ms")
    @panel_dirty = true
  end

  # ------------------------------------------------------------ the picture (kept)

  def stroke_index(id)
    # Newest first: the strokes that go on are the newest.
    i = @s_id.size - 1
    while i >= 0
      return i if @s_id[i] == id
      i -= 1
    end
    nil
  end

  # [x, y, ...] from index `from` on as 4 bytes a point.
  def pack_points(pts, from)
    n = (pts.size - from) / 2
    s = "\x00" * (n * 4)
    i = 0
    while i < n
      x = pts[from + i * 2].to_i
      y = pts[from + i * 2 + 1].to_i
      s.setbyte(i * 4, (x >> 8) & 255)
      s.setbyte(i * 4 + 1, x & 255)
      s.setbyte(i * 4 + 2, (y >> 8) & 255)
      s.setbyte(i * 4 + 3, y & 255)
      i += 1
    end
    s
  end

  def add_stroke(id, c, w, pts)
    @s_id << id
    @s_c << c
    @s_w << w
    @s_p << pack_points(pts, 0)
    x = pts[0].to_i
    y = pts[1].to_i
    box = [x, y, x, y]
    grow_box(box, pts, 2)
    @s_box << box
    @npoints += pts.size / 2
    trim
  end

  def append_points(i, pts, from)
    return if pts.size <= from
    @s_p[i] << pack_points(pts, from)
    grow_box(@s_box[i], pts, from)
    @npoints += (pts.size - from) / 2
    trim
  end

  def grow_box(box, pts, from)
    i = from
    while i + 1 < pts.size
      x = pts[i].to_i
      y = pts[i + 1].to_i
      box[0] = x if x < box[0]
      box[1] = y if y < box[1]
      box[2] = x if x > box[2]
      box[3] = y if y > box[3]
      i += 2
    end
  end

  def remove_at(i)
    @npoints -= @s_p[i].bytesize / 4
    @s_id.delete_at(i)
    @s_c.delete_at(i)
    @s_w.delete_at(i)
    @s_p.delete_at(i)
    @s_box.delete_at(i)
  end

  # The host's limits: the oldest strokes go (they stay on the screen
  # until the picture is drawn again).
  def trim
    while @s_id.size > 1 && (@s_id.size > MAX_STROKES || @npoints > MAX_POINTS)
      remove_at(0)
    end
  end

  def clear_picture
    @s_id = []
    @s_c = []
    @s_w = []
    @s_p = []
    @s_box = []
    @npoints = 0
  end

  # ------------------------------------------------------------ drawing

  def sx(x)
    @cx + x * @sk / 1024
  end

  def sy(y)
    @cy + y * @sk / 1024
  end

  def pen_px(w)
    px = WIDTH_PX[w & 3] * @sk / 1024
    px < 1 ? 1 : px
  end

  def paint_paper
    @gfx.fill_rect(@cx, @cy, @cw, @ch, C_PAPER)
    @gfx.draw_rect(@cx - 1, @cy - 1, @cw + 2, @ch + 2, C_EDGE)
  end

  # A segment in screen coordinates, kept inside the picture.
  def seg(x0, y0, x1, y1, px, color)
    if px <= 1
      @gfx.draw_line(x0, y0, x1, y1, color)
      return
    end
    h = px / 2
    lo_x = @cx + h
    hi_x = @cx + @cw - px + h
    lo_y = @cy + h
    hi_y = @cy + @ch - px + h
    x0 = lo_x if x0 < lo_x
    x0 = hi_x if x0 > hi_x
    x1 = lo_x if x1 < lo_x
    x1 = hi_x if x1 > hi_x
    y0 = lo_y if y0 < lo_y
    y0 = hi_y if y0 > hi_y
    y1 = lo_y if y1 < lo_y
    y1 = hi_y if y1 > hi_y
    if x0 == x1 && y0 == y1
      @gfx.fill_rect(x0 - h, y0 - h, px, px, color)
    else
      @gfx.draw_thick_line(x0, y0, x1, y1, px, color)
    end
  end

  # [x, y, ...] in board coordinates as one line.
  def draw_pts(pts, c, w)
    color = PALETTE[c & 15]
    px = pen_px(w)
    x0 = sx(pts[0].to_i)
    y0 = sy(pts[1].to_i)
    if pts.size == 2
      seg(x0, y0, x0, y0, px, color)
      return
    end
    i = 2
    while i + 1 < pts.size
      x1 = sx(pts[i].to_i)
      y1 = sy(pts[i + 1].to_i)
      # Points closer than a screen pixel draw nothing new.
      if x1 != x0 || y1 != y0
        seg(x0, y0, x1, y1, px, color)
        x0 = x1
        y0 = y1
      end
      i += 2
    end
  end

  # A kept stroke (4 bytes a point), the segments that cross the board
  # rectangle @rl..@rr x @rt..@rb only, cut at it.
  def draw_packed(s, c, w)
    color = PALETTE[c & 15]
    px = pen_px(w)
    n = s.bytesize / 4
    bx0 = s.getbyte(0) * 256 + s.getbyte(1)
    by0 = s.getbyte(2) * 256 + s.getbyte(3)
    if n == 1
      if bx0 >= @rl && bx0 <= @rr && by0 >= @rt && by0 <= @rb
        seg(sx(bx0), sy(by0), sx(bx0), sy(by0), px, color)
      end
      return
    end
    i = 1
    while i < n
      b = i * 4
      bx1 = s.getbyte(b) * 256 + s.getbyte(b + 1)
      by1 = s.getbyte(b + 2) * 256 + s.getbyte(b + 3)
      if clip_seg(bx0, by0, bx1, by1)
        x0 = sx(@k0)
        y0 = sy(@k1)
        x1 = sx(@k2)
        y1 = sy(@k3)
        seg(x0, y0, x1, y1, px, color) if x0 != x1 || y0 != y1 || i == 1
      end
      bx0 = bx1
      by0 = by1
      i += 1
    end
  end

  def outcode(x, y)
    c = 0
    c |= 1 if x < @rl
    c |= 2 if x > @rr
    c |= 4 if y < @rt
    c |= 8 if y > @rb
    c
  end

  # Cohen-Sutherland: the segment cut at the rectangle into @k0..@k3;
  # false when it misses it.
  def clip_seg(x0, y0, x1, y1)
    c0 = outcode(x0, y0)
    c1 = outcode(x1, y1)
    n = 0
    while n < 6
      if c0 | c1 == 0
        @k0 = x0
        @k1 = y0
        @k2 = x1
        @k3 = y1
        return true
      end
      return false if c0 & c1 != 0
      c = c0 != 0 ? c0 : c1
      if c & 8 != 0
        x = x0 + (x1 - x0) * (@rb - y0) / (y1 - y0)
        y = @rb
      elsif c & 4 != 0
        x = x0 + (x1 - x0) * (@rt - y0) / (y1 - y0)
        y = @rt
      elsif c & 2 != 0
        y = y0 + (y1 - y0) * (@rr - x0) / (x1 - x0)
        x = @rr
      else
        y = y0 + (y1 - y0) * (@rl - x0) / (x1 - x0)
        x = @rl
      end
      if c == c0
        x0 = x
        y0 = y
        c0 = outcode(x0, y0)
      else
        x1 = x
        y1 = y
        c1 = outcode(x1, y1)
      end
      n += 1
    end
    false
  end

  # The whole picture again (a new size, joining).
  def redraw_picture
    redraw_region(0, 0, BW - 1, BH - 1)
  end

  # A part of the picture again (a stroke taken back): the paper over the
  # rectangle (board coordinates), then the strokes that cross it, cut at
  # it, oldest first. Only what is in the rectangle is drawn.
  def redraw_region(l, t, r, b)
    t0 = Machine.board_millis
    l = 0 if l < 0
    t = 0 if t < 0
    r = BW - 1 if r > BW - 1
    b = BH - 1 if b > BH - 1
    @rl = l
    @rt = t
    @rr = r
    @rb = b
    if l == 0 && t == 0 && r == BW - 1 && b == BH - 1
      paint_paper
    else
      x0 = sx(l)
      y0 = sy(t)
      @gfx.fill_rect(x0, y0, sx(r) - x0 + 1, sy(b) - y0 + 1, C_PAPER)
    end
    i = 0
    while i < @s_id.size
      bx = @s_box[i]
      if bx[2] >= l && bx[0] <= r && bx[3] >= t && bx[1] <= b
        draw_packed(@s_p[i], @s_c[i], @s_w[i])
      end
      i += 1
    end
    @redraw_ms = Machine.board_millis - t0
  end

  # ------------------------------------------------------------ the pen (this member's strokes)

  def in_picture?(x, y)
    x >= @cx && y >= @cy && x < @cx + @cw && y < @cy + @ch
  end

  def board_xy(x, y)
    bx = (x - @cx) * 1024 / @sk
    by = (y - @cy) * 1024 / @sk
    bx = 0 if bx < 0
    bx = BW - 1 if bx >= BW
    by = 0 if by < 0
    by = BH - 1 if by >= BH
    [bx, by]
  end

  def pen_down(x, y)
    return unless @tc.token && !@loading && in_picture?(x, y)
    @drawing = true
    st = BoardOutStroke.new(@color, @width)
    @out << st
    b = board_xy(x, y)
    st.pts << b[0]
    st.pts << b[1]
    @last_bx = b[0]
    @last_by = b[1]
    @last_sx = sx(b[0])
    @last_sy = sy(b[1])
    seg(@last_sx, @last_sy, @last_sx, @last_sy, pen_px(@width), PALETTE[@color])
    @sent_at = Machine.board_millis
  end

  def pen_move(x, y)
    return unless @drawing
    st = @out[-1]
    return unless st
    b = board_xy(x, y)
    return if b[0] == @last_bx && b[1] == @last_by
    nx = sx(b[0])
    ny = sy(b[1])
    seg(@last_sx, @last_sy, nx, ny, pen_px(st.w), PALETTE[st.c])
    st.pts << b[0]
    st.pts << b[1]
    @last_bx = b[0]
    @last_by = b[1]
    @last_sx = nx
    @last_sy = ny
  end

  def pen_up
    return unless @drawing
    @drawing = false
    st = @out[-1]
    st.done = true if st
  end

  # Sends the oldest stroke's waiting points: the first part with line,
  # the next ones with more (once the host gave the id); past LINE_POINTS
  # a new stroke goes on from the last point.
  def pump_send(now)
    collect_send
    tok = @tc.token
    return if @send_f || @out.empty? || tok.nil?
    st = @out[0]
    if st.pts.empty?
      @out.shift if st.done && (st.asked == false || st.id || st.sent == 0)
      return
    end
    return if st.asked && st.id.nil?
    waiting = st.pts.size / 2
    return unless st.done || waiting >= SEND_POINTS || now - @sent_at >= SEND_MS
    n = waiting < PART_POINTS ? waiting : PART_POINTS
    if st.asked && st.sent + n > LINE_POINTS
      # A new stroke from the last point sent.
      fresh = BoardOutStroke.new(st.c, st.w)
      fresh.pts = [@sent_x, @sent_y] + st.pts
      fresh.done = st.done
      @out[0] = fresh
      st = fresh
      waiting = st.pts.size / 2
      n = waiting < PART_POINTS ? waiting : PART_POINTS
    end
    part = st.pts[0, n * 2]
    st.pts = st.pts[n * 2, st.pts.size - n * 2] || []
    @sent_x = part[part.size - 2]
    @sent_y = part[part.size - 1]
    px = @tc.proxy.async
    @send_f = st.asked ? px.more(tok, st.id, part) : px.line(tok, st.c, st.w, part)
    st.asked = true
    st.sent += n
    @sent_at = now
  rescue ::Asterism::Error => e
    @send_f = nil
    @note = e.message.to_s
    @panel_dirty = true
  end

  def collect_send
    f = @send_f
    return unless f && f.done?
    @send_f = nil
    st = @out[0]
    begin
      r = f.value
      if r.is_a?(Hash) && r["id"]
        st.id = r["id"].to_i if st
      elsif r != "ok"
        # Refused ("long", "not yours" ...): what is left starts a new stroke.
        @note = reason(r)
        if st
          st.asked = false
          st.id = nil
          st.sent = 0
        end
        @panel_dirty = true
      end
    rescue ::Asterism::Error => e
      @note = "ホストが答えない"
      @out.shift
      @panel_dirty = true
    end
    @out.shift if st && st.done && st.pts.empty? && (st.id || !st.asked)
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    if t == :key_down
      @keys << [ev[:scancode], ev[:character] || 0]
    elsif t == :mouse_down && ev[:button] == 1
      @moves << [0, ev[:x], ev[:y]]
    elsif t == :mouse_move
      @moves << [1, ev[:x], ev[:y]] if @drawing || @moves.size > 0
    elsif t == :mouse_up && ev[:button] == 1
      @moves << [2, ev[:x], ev[:y]]
    end
  end

  def run_moves
    drew = false
    while @moves.size > 0
      m = @moves.shift
      kind = m[0]
      x = m[1]
      y = m[2]
      if kind == 0
        if @tc.token && in_picture?(x, y)
          pen_down(x, y)
          drew = true
        end
      elsif kind == 1
        if @drawing
          pen_move(x, y)
          drew = true
        end
      else
        if @drawing
          pen_move(x, y)
          pen_up
          drew = true
        else
          # In order with the strokes: a colour picked just before a stroke is its colour.
          run_click([x, y])
        end
      end
    end
    @gfx.present if drew
  end

  def run_key(k)
    sc = k[0]
    ch = k[1]
    enter = sc == FmrbConst::KEY_ENTER || ch == 13 || ch == 10
    esc = sc == FmrbConst::KEY_ESC || ch == 27
    if @tc.table.nil?
      if sc == FmrbConst::KEY_UP
        @list_sel -= 1 if @list_sel > 0
      elsif sc == FmrbConst::KEY_DOWN
        @list_sel += 1 if @list_sel < @tc.table_list.size - 1
      elsif enter
        join(@tc.table_list[@list_sel])
      end
      @dirty = true
      return
    end
    @panel_dirty = true
    if @confirm
      if enter || ch == 121 || ch == 89
        send_cmd(:clear)
      elsif esc || ch == 110 || ch == 78
        @confirm = false
      end
      return
    end
    if esc
      leave_table
    elsif ch >= 49 && ch <= 52 # 1-4
      @width = ch - 49
    elsif ch == 117 || ch == 85 # u
      send_cmd(:undo)
    elsif ch == 99 || ch == 67  # c
      @confirm = true if @owner
    elsif ch == 114 || ch == 82 # r
      start_picture
    end
  end

  def run_click(c)
    x = c[0]
    y = c[1]
    b = @hits.id_at(x, y)
    if b
      run_button(b)
      return
    end
    if @tc.table.nil?
      row = (y - @list_y) / 15
      if y >= @list_y && row >= 0 && row < @tc.table_list.size
        if row == @list_sel
          join(@tc.table_list[row])
        else
          @list_sel = row
        end
        @dirty = true
      end
      return
    end
    # The palette and the widths.
    if x >= @px && x < @px + 56 && y >= @pal_y && y < @pal_y + 56
      @color = ((y - @pal_y) / 14) * 4 + (x - @px) / 14
      @panel_dirty = true
    elsif x >= @px && x < @px + 56 && y >= @wid_y && y < @wid_y + 14
      @width = (x - @px) / 14
      @panel_dirty = true
    end
  end

  def run_button(id)
    @panel_dirty = true
    case id
    when :join then join(@tc.table_list[@list_sel])
    when :undo then send_cmd(:undo)
    when :clear then @confirm = true if @owner
    when :yes then send_cmd(:clear)
    when :no then @confirm = false
    when :reload then start_picture
    when :leave then leave_table
    end
  end

  # ------------------------------------------------------------ the screen

  def layout
    @x0 = @user_area_x0
    @y0 = @user_area_y0
    uw = @user_area_width
    uh = @user_area_height
    aw = uw - PANEL_W - 6
    ah = uh - 4
    # 16:9: scale (1024 = 1:1) by the tighter side.
    k = aw * 1024 / BW
    k2 = ah * 1024 / BH
    k = k2 if k2 < k
    k = 64 if k < 64
    @sk = k
    @cw = BW * k / 1024
    @ch = BH * k / 1024
    @cx = @x0 + 2 + (aw - @cw) / 2
    @cy = @y0 + 2 + (ah - @ch) / 2
    @px = @x0 + uw - PANEL_W + 2
    @pw = PANEL_W - 4
    @pal_y = @y0 + 30
    @wid_y = @pal_y + 60
    @list_y = @y0 + 34
  end

  # A button of the panel and the list (theme colours, 15 px high).
  def button(id, x, y, w, label)
    @gfx.draw_button(x, y, w, 15, label, theme_accent, theme_fg_light)
    @hits.add(id, x, y, w, 15)
  end

  def text(x, y, s, color = nil)
    @gfx.draw_text(x, y, s, color || theme_fg)
  end

  def draw_screen
    @hits.clear
    clear_user_area
    if @tc.table
      # The strokes kept so far (while the picture is read, what came of it).
      redraw_picture
      draw_panel
    else
      draw_list
    end
    @gfx.present
    @panel_dirty = false
  end

  def net_line
    net = @tc.net
    return "つないでいます: #{@tc.link.router}" if net == :connecting
    return "網から切れた: つなぎ直し中" if net == :down || @tc.reconnecting?
    "#{@id} (#{@tc.link.router})"
  end

  def draw_list
    x = @x0 + 4
    y = @y0 + 3
    w = @user_area_width - 8
    text(x, y, @gfx.fit_text("お絵かき: ボードを選ぶ (Enter で入る)", w))
    text(x, y + 15, @gfx.fit_text(net_line, w), theme_border)
    if @tc.table_list.empty?
      text(x, @list_y, @gfx.fit_text(@tc.net == :up ? "ゲームホストを探しています (games/board*)" : "-", w), theme_border)
    end
    i = 0
    while i < @tc.table_list.size && i < 8
      p = @tc.table_list[i]
      t = @tc.tables[p]
      line = p
      line = "#{p}  線 #{t[0]}  #{t[1]} 人" if t
      yy = @list_y + i * 15
      if i == @list_sel
        @gfx.fill_rect(@x0 + 2, yy - 1, @user_area_width - 4, 14, theme_accent)
        text(x, yy, @gfx.fit_text(line, w), theme_fg_light)
      else
        text(x, yy, @gfx.fit_text(line, w))
      end
      i += 1
    end
    by = @y0 + @user_area_height - 40
    text(x, by - 2, @gfx.fit_text(@note, w), theme_border)
    button(:join, x, by + 16, 90, "入る[Enter]") unless @tc.table_list.empty?
  end

  # The panel at the right: the board, the palette, the widths, the buttons.
  def draw_panel
    @hits.clear
    x = @px
    @gfx.fill_rect(x - 1, @y0, @pw + 3, @user_area_height, theme_bg)
    v = @view
    text(x, @y0 + 1, @gfx.fit_text(@tc.table.split("/")[2].to_s, @pw))
    @shown_n = @s_id.size
    info = v ? "線#{@s_id.size} #{v['mb'].is_a?(Array) ? v['mb'].size : 0}人" : "..."
    info = "読込中" if @loading
    text(x, @y0 + 14, @gfx.fit_text(info, @pw), theme_border)
    i = 0
    while i < 16
      cx = x + (i % 4) * 14
      cy = @pal_y + (i / 4) * 14
      @gfx.fill_rect(cx, cy, 13, 13, PALETTE[i])
      @gfx.draw_rect(cx, cy, 13, 13, C_EDGE)
      if i == @color
        @gfx.draw_rect(cx - 1, cy - 1, 15, 15, theme_fg)
        @gfx.draw_rect(cx + 1, cy + 1, 11, 11, i == 0 ? 0xFF : 0x00)
      end
      i += 1
    end
    i = 0
    while i < 4
      wx = x + i * 14
      @gfx.fill_rect(wx, @wid_y, 13, 13, i == @width ? theme_accent : C_PAPER)
      @gfx.draw_rect(wx, @wid_y, 13, 13, C_EDGE)
      d = WIDTH_PX[i]
      d = 9 if d > 9
      @gfx.fill_rect(wx + 6 - d / 2, @wid_y + 6 - d / 2, d, d, i == @width ? theme_fg_light : 0x00)
      i += 1
    end
    by = @wid_y + 18
    if @confirm
      text(x, by, @gfx.fit_text("全部消す?", @pw), theme_accent)
      button(:yes, x, by + 15, 27, "はい")
      button(:no, x + 29, by + 15, 27, "いや")
    else
      button(:undo, x, by, 27, "戻")
      button(:clear, x + 29, by, 27, "消") if @owner
      button(:reload, x, by + 17, 27, "読")
      button(:leave, x + 29, by + 17, 27, "出")
    end
    note = @note
    note = "再生中" if @replay && note.empty?
    note = "主" if note.empty? && @owner
    text(x, by + 36, @gfx.fit_text(note, @pw), theme_border) unless note.empty?
  end

  # ------------------------------------------------------------ the loop

  def on_update
    now = Machine.board_millis
    run_moves
    # The strokes come first (table_watch), then the states, the list, the join.
    if @tc.update
      collect_cmd if @tc.table
      @tc.update_view(now)
      if @tc.table
        collect_picture
        pump_send(now)
      end
    end
    run_key(@keys.shift) until @keys.empty?
    if now >= @mem_at
      # The VM pool and the drawing times, now and then (doc/ruby_asterism/report/gs6.md).
      @mem_at = now + MEM_EVERY_MS
      avg = @lat_n > 0 ? @lat_sum * 10 / @lat_n : 0
      Log.info("board: pool #{FmrbApp.pool_used / 1024} KB (#{FmrbApp.pool_usage}%), #{@s_id.size} strokes " \
               "#{@npoints} points, drawn #{@lat_n} times avg #{avg / 10}.#{avg % 10} ms max #{@lat_max} ms, " \
               "last full drawing #{@redraw_ms} ms")
    end
    if @dirty
      @dirty = false
      draw_screen
    elsif @panel_dirty && @tc.table
      @panel_dirty = false
      draw_panel
      @gfx.present
    end
    @drawing || @loading ? 20 : 40
  end

  def on_destroy
    leave_table if @tc.token
    @tc.close
    Log.info("board: closed")
  rescue => e
    Log.info("board: close: #{e.message}")
  end
end

begin
  app = DrawingBoardApp.new
  app.start
rescue => e
  puts "board: #{e.message}"
end
