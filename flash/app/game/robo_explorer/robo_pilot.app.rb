# Robot Explorer -- the pilot frame.
#
# The part a player writes lives in my_pilot.rb (required below): MyPilot
# gets the keys, a think() call five times a second, and every result. This
# file is the plumbing around it -- subscribe, draw, send -- and is not meant
# to be edited to make the robot smarter.
#
# Everything the pilot knows arrives over Pub/Sub. There is no other channel
# to the world app.

require "/app/game/robo_explorer/my_pilot"

class RoboPilotApp < FmrbApp
  TOPIC_CMD    = "robo/cmd"
  TOPIC_STATE  = "robo/state"
  TOPIC_RESULT = "robo/result"

  COL_TEXT = 0xFF  # white
  COL_KEY  = 0xFC  # yellow
  COL_GOAL = 0x1C  # green
  COL_NG   = 0xE0  # red
  COL_DIM  = 0x6D  # gray
  COL_BG   = 0x00

  LINE_H   = 13
  RTT_MAX  = 10   # how many command round trips to log

  # First-person view box (Wizardry-style wireframe), drawn from state["view"].
  # Frame rectangles per depth, precomputed as [x0, y0, x1, y1] around the
  # box center; each step shrinks by roughly a third, which reads as a
  # corridor without any real projection math.
  VB_X = 3
  VB_Y = 3
  VB_W = 160
  VB_H = 80
  # Inset one pixel from the box on every side: the outer frame IS the first
  # cell's near edge, and drawing it at the box size put lines on top of --
  # and one pixel past -- the border.
  VB_FRAMES = [
    [1,   1,   158, 78],
    [28,  14,  132, 66],
    [46,  23,  114, 57],
    [58,  29,  102, 51],
    [66,  33,  94,  47],
    [71,  36,  89,  44],
  ]

  COL_WIRE = 0xFF  # white
  COL_DOOR = 0xA8  # brown
  COL_KEYM = 0xFC  # yellow

  # How often think() is asked. Also the floor between any two sends: the
  # round trip is 0-1 ms, so an unthrottled brain would run hundreds of turns
  # a second and nobody would see the robot solve anything. 1/2/3 pick a
  # preset at runtime; 200 ms is the default.
  THINK_PRESETS = [500, 200, 100]
  THINK_MS = 200

  def on_create
    @gfx.set_font(:ja, 12)
    @state = nil
    @result_text = ""
    @result_ok = true
    @sent_at = nil
    @rtt_n = 0
    @last_send = 0
    # The autopilot is armed, not running: S starts it (and stops it again).
    # A brain that drives the moment the app opens leaves no room to watch,
    # to drive by hand first, or to start the run deliberately.
    @auto = false
    @think_ms = THINK_MS
    @brain = MyPilot.new
    subscribe(TOPIC_STATE)
    subscribe(TOPIC_RESULT)
    draw_screen
    Log.info("RoboPilot: ready")
  end

  def on_destroy
    unsubscribe(TOPIC_STATE)
    unsubscribe(TOPIC_RESULT)
  end

  # ---- receiving ----

  def on_control(msg)
    return unless msg["cmd"] == "topic_data"
    topic = msg["topic"]
    data = msg["data"]
    return unless data
    if topic == TOPIC_STATE
      # Turn 0 means the world was reset; the answer to the last command
      # belongs to the run that just ended, so drop it.
      @result_text = "" if data["turn"] == 0
      @state = data
      @dirty = true
    elsif topic == TOPIC_RESULT
      note_result(data)
      @dirty = true
    end
  end

  def note_result(data)
    @result_ok = data["ok"] ? true : false
    @result_text = result_ja(data["op"], @result_ok, data["reason"])
    if @sent_at && @rtt_n < RTT_MAX
      @rtt_n += 1
      Log.info("RoboPilot: rtt #{@rtt_n} = #{Machine.board_millis - @sent_at} ms")
    end
    @sent_at = nil
    call_brain { @brain.on_result(data, @state) }
  end

  def result_ja(op, ok, reason)
    if ok
      return "進んだ"     if op == "move"
      return "回った"     if op == "turn"
      return "リセットした" if op == "reset"
      return "待った"
    end
    case reason
    when "wall"   then "前は壁"
    when "locked" then "鍵がない"
    when "edge"   then "外には出られない"
    when "done"   then "ゴール済み"
    else "不明な命令"
    end
  end

  # ---- the brain ----

  # A broken MyPilot (step 2 is the player's first own code) must not take
  # the frame down with it: the error goes to the panel and the log, and the
  # world keeps running so a fixed brain can be relaunched into the same run.
  def call_brain
    yield
  rescue => e
    @result_ok = false
    @result_text = "頭脳でエラー"
    Log.error("MyPilot: #{e.message}")
    nil
  end

  def send_cmd(cmd)
    return unless cmd
    @sent_at = Machine.board_millis
    @last_send = @sent_at
    publish(TOPIC_CMD, cmd)
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :key_down
    # S belongs to the frame, not the brain: every brain gets the same
    # start/stop control without having to implement it.
    if ev[:scancode] == FmrbConst::KEY_S
      @auto = !@auto
      @dirty = true
      return
    end
    # 1/2/3 set the pace (slow / normal / fast); the frame's key, like S,
    # so every brain drives at the speed the player chose.
    if ev[:scancode] >= FmrbConst::KEY_1 && ev[:scancode] <= FmrbConst::KEY_3
      @think_ms = THINK_PRESETS[ev[:scancode] - FmrbConst::KEY_1]
      @dirty = true
      return
    end
    return if @state.nil?
    cmd = call_brain { @brain.on_key(ev[:scancode], @state) }
    send_cmd(cmd)
  end

  # think() runs on the update tick: steady, visible pacing, and no feedback
  # loop through the broker (a think that always answers would otherwise
  # drive a turn per round trip, hundreds per second).
  #
  # Drawing happens here too, once per tick at most, and never from the
  # message handlers. Drawing the wireframe on every arriving message could
  # not keep up with the message rate; the queue then grew without bound and
  # think() was fed states that were seconds old -- the autopilot span in
  # place on stale walls. Messages are cheap to absorb, the redraw is
  # batched, and the brain always reads the freshest state.
  def on_update
    now = Machine.board_millis
    if @auto && @state && !@state["done"] && now - @last_send >= @think_ms
      cmd = call_brain { @brain.think(@state) }
      send_cmd(cmd)
    end
    if @dirty
      @dirty = false
      draw_screen
    end
    @think_ms
  end

  # ---- drawing ----

  def dir_ja(code)
    case code
    when "N"  then "北"
    when "E"  then "東"
    when "S"  then "南"
    when "W"  then "西"
    when "NE" then "北東"
    when "NW" then "北西"
    when "SE" then "南東"
    when "SW" then "南西"
    else "ここ"
    end
  end

  def front_ja(code)
    case code
    when "wall" then "壁"
    when "key"  then "鍵"
    when "door" then "扉"
    when "goal" then "ゴール"
    else "床"
    end
  end

  def draw_screen
    clear_user_area
    x = @user_area_x0 + 3
    st = @state
    if st.nil?
      @gfx.draw_text(x, @user_area_y0 + 3, "待機中", COL_DIM)
      draw_window_frame
      @gfx.present
      return
    end

    draw_view(st)

    y = @user_area_y0 + VB_Y + VB_H + 5
    @gfx.draw_text(x, y, "場所 #{st["x"]},#{st["y"]}  #{dir_ja(st["dir"])}", COL_TEXT)
    y += LINE_H
    @gfx.draw_text(x, y, "前 #{front_ja(st["front"])}", COL_TEXT)
    y += LINE_H
    @gfx.draw_text(x, y, "鍵 #{st["keys"]}  ゴール #{dir_ja(st["goal"])}", COL_KEY)
    y += LINE_H
    @gfx.draw_text(x, y, "手数 #{st["steps"]}", COL_TEXT)
    y += LINE_H
    unless @result_text.empty?
      @gfx.draw_text(x, y, @result_text, @result_ok ? COL_GOAL : COL_NG)
    end
    y = @user_area_y1 - LINE_H * 2 - 4
    @gfx.draw_text(x, y, (@auto ? "自動:ON" : "自動:OFF") +
                   "  速度:#{@think_ms}ms",
                   @auto ? COL_GOAL : COL_DIM)
    y += LINE_H
    @gfx.draw_text(x, y, "S:自動 1-3:速度 矢印:手動", COL_DIM)
    draw_window_frame
    @gfx.present
  end

  # ---- first-person view ----
  #
  # One [left_wall, right_wall, kind] per corridor cell (see the world's
  # view_scan). Nested frames give the depth; each cell draws its two sides
  # between its frame and the next: a wall is two diagonals, an opening is a
  # doorway (near vertical edge and two shelves). The list ending on floor
  # means a wall dead ahead, which is drawn as the facing rectangle.
  def draw_view(st)
    bx = @user_area_x0 + VB_X
    by = @user_area_y0 + VB_Y
    @gfx.fill_rect(bx, by, VB_W, VB_H, COL_BG)
    @gfx.draw_rect(bx, by, VB_W, VB_H, COL_DIM)

    view = st["view"]
    unless view.is_a?(Array) && view.size > 0
      @gfx.draw_text(bx + 8, by + VB_H / 2 - 6, "(視界なし)", COL_DIM)
      return
    end

    ended = nil
    n = view.size
    n = VB_FRAMES.size - 1 if n > VB_FRAMES.size - 1
    i = 0
    while i < n
      cell = view[i]
      nf = VB_FRAMES[i]
      ff = VB_FRAMES[i + 1]
      draw_view_side(bx, by, nf, ff, cell[0], true)
      draw_view_side(bx, by, nf, ff, cell[1], false)
      # The corner: where a side flips between wall and opening, the edge
      # between the two cells is a vertical line at the boundary frame.
      if i + 1 < n
        nxt = view[i + 1]
        draw_view_corner(bx, by, ff, true)  if cell[0] != nxt[0]
        draw_view_corner(bx, by, ff, false) if cell[1] != nxt[1]
      end
      kind = cell[2]
      if kind == "door" || kind == "goal"
        ended = kind
        draw_view_face(bx, by, nf, kind)
        break
      elsif kind == "key"
        # The key lies on the floor of this cell: a small marker at its depth.
        mx = bx + (nf[0] + nf[2]) / 2 - 3
        my = by + (nf[3] + ff[3]) / 2 - 1
        @gfx.fill_rect(mx, my, 6, 3, COL_KEYM)
      end
      i += 1
    end
    # Ran out of corridor without a door or the goal: a wall (or the sight
    # limit) faces the robot at the last frame.
    draw_view_face(bx, by, VB_FRAMES[i], "wall") if ended.nil?

    if st["done"]
      @gfx.draw_text(bx + VB_W / 2 - 21, by + VB_H / 2 - 6, "クリア!", COL_GOAL)
    end
  end

  def draw_view_corner(bx, by, f, left)
    x = bx + (left ? f[0] : f[2])
    @gfx.draw_line(x, by + f[1], x, by + f[3], COL_WIRE)
  end

  # One side of one corridor cell. wall==1 draws the slanted wall (top and
  # bottom diagonals); wall==0 draws an opening (near vertical edge plus the
  # ceiling and floor shelves of the side passage).
  def draw_view_side(bx, by, nf, ff, wall, left)
    if left
      nx = bx + nf[0]
      fx = bx + ff[0]
    else
      nx = bx + nf[2]
      fx = bx + ff[2]
    end
    nty = by + nf[1]
    nby = by + nf[3]
    fty = by + ff[1]
    fby = by + ff[3]
    if wall == 1
      @gfx.draw_line(nx, nty, fx, fty, COL_WIRE)
      @gfx.draw_line(nx, nby, fx, fby, COL_WIRE)
    else
      @gfx.draw_line(nx, fty, nx, fby, COL_WIRE)
      @gfx.draw_line(nx, fty, fx, fty, COL_WIRE)
      @gfx.draw_line(nx, fby, fx, fby, COL_WIRE)
    end
  end

  # The facing surface at a frame: a plain rectangle for a wall, with a door
  # leaf inside it for a door, green with a mark for the goal.
  def draw_view_face(bx, by, f, kind)
    fx = bx + f[0]
    fy = by + f[1]
    fw = f[2] - f[0]
    fh = f[3] - f[1]
    @gfx.draw_rect(fx, fy, fw, fh, COL_WIRE)
    if kind == "door"
      dw = fw * 2 / 5
      dh = fh * 7 / 10
      dx = fx + (fw - dw) / 2
      dy = fy + fh - dh
      @gfx.draw_rect(dx, dy, dw, dh, COL_DOOR)
      @gfx.fill_rect(dx + dw - 3, dy + dh / 2, 2, 2, COL_DOOR)
    elsif kind == "goal"
      @gfx.draw_rect(fx + 2, fy + 2, fw - 4, fh - 4, COL_GOAL)
      @gfx.draw_text(fx + fw / 2 - 3, fy + fh / 2 - 6, "G", COL_GOAL) if fh >= 16
    end
  end
end

begin
  app = RoboPilotApp.new
  app.start
rescue => e
  Log.error("RoboPilot: #{e}")
end
