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

  LINE_H   = 14
  RTT_MAX  = 10   # how many command round trips to log

  # How often think() is asked. Also the floor between any two sends: the
  # round trip is 0-1 ms, so an unthrottled brain would run hundreds of turns
  # a second and nobody would see the robot solve anything.
  THINK_MS = 200

  def on_create
    @gfx.set_font(:ja, 12)
    @state = nil
    @result_text = ""
    @result_ok = true
    @sent_at = nil
    @rtt_n = 0
    @last_send = 0
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
      draw_screen
    elsif topic == TOPIC_RESULT
      note_result(data)
      draw_screen
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
      return "進んだ" if op == "move"
      return "回った" if op == "turn"
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
    return if @state.nil?
    cmd = call_brain { @brain.on_key(ev[:scancode], @state) }
    send_cmd(cmd)
  end

  # think() runs on the update tick: steady, visible pacing, and no feedback
  # loop through the broker (a think that always answers would otherwise
  # drive a turn per round trip, hundreds per second).
  def on_update
    now = Machine.board_millis
    if @state && !@state["done"] && now - @last_send >= THINK_MS
      cmd = call_brain { @brain.think(@state) }
      send_cmd(cmd)
    end
    THINK_MS
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
    y = @user_area_y0 + 3
    st = @state
    if st.nil?
      @gfx.draw_text(x, y, "待機中", COL_DIM)
    else
      @gfx.draw_text(x, y, "場所 #{st["x"]},#{st["y"]}", COL_TEXT)
      y += LINE_H
      @gfx.draw_text(x, y, "向き #{dir_ja(st["dir"])}", COL_TEXT)
      y += LINE_H
      @gfx.draw_text(x, y, "前 #{front_ja(st["front"])}", COL_TEXT)
      y += LINE_H
      @gfx.draw_text(x, y, "鍵 #{st["keys"]}", COL_KEY)
      y += LINE_H
      @gfx.draw_text(x, y, "ゴール #{dir_ja(st["goal"])}", COL_GOAL)
      y += LINE_H
      @gfx.draw_text(x, y, "手数 #{st["steps"]}", COL_TEXT)
      y += LINE_H
      if st["done"]
        @gfx.draw_text(x, y, "クリア!", COL_GOAL)
      end
    end
    y = @user_area_y1 - LINE_H * 3 - 4
    unless @result_text.empty?
      @gfx.draw_text(x, y, @result_text, @result_ok ? COL_GOAL : COL_NG)
    end
    y += LINE_H
    @gfx.draw_text(x, y, "上:進む", COL_DIM)
    y += LINE_H
    @gfx.draw_text(x, y, "左/右:回る", COL_DIM)
    draw_window_frame
    @gfx.present
  end
end

begin
  app = RoboPilotApp.new
  app.start
rescue => e
  Log.error("RoboPilot: #{e}")
end
