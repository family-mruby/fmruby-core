# Robot Explorer -- the world side.
#
# A maze puzzle you cannot play on this app: the robot takes no key input
# here. Its state is published on "robo/state" and it obeys commands that
# arrive on "robo/cmd"; every command is answered on "robo/result". Write a
# second app (see robo_pilot.app.rb) to drive it.
#
# Protocol: doc/robo_explorer/plan.md chapter 4. The wire vocabulary stays
# English on purpose -- the display is Japanese, the messages are for programs.

class RoboExplorerApp < FmrbApp
  TOPIC_CMD    = "robo/cmd"
  TOPIC_STATE  = "robo/state"
  TOPIC_RESULT = "robo/result"

  LEVEL = 1

  # Cell kinds. Stored as small integers in a flat array (12x12).
  FLOOR = 0
  WALL  = 1
  KEY   = 2
  DOOR  = 3
  GOAL  = 4

  BOARD_W = 12
  BOARD_H = 12
  TILE    = 14

  # Fixed level 1. The door is the only way into the goal cell, so the key
  # must be collected first.
  MAP = [
    "############",
    "#S.........#",
    "#.####.###.#",
    "#....#.#...#",
    "###.#..#.###",
    "#...#.##...#",
    "#.###....#.#",
    "#.#...##.#.#",
    "#.#.#K...#.#",
    "#...####D#.#",
    "#.#....#G#.#",
    "############"
  ]

  # Direction index: 0=N 1=E 2=S 3=W (clockwise, so turn R is +1).
  START_DIR = 1
  DIR_CODE  = ["N", "E", "S", "W"]
  DIR_JA    = ["北", "東", "南", "西"]
  DIR_DX    = [0, 1, 0, -1]
  DIR_DY    = [-1, 0, 1, 0]

  CELL_CODE = ["floor", "wall", "key", "door", "goal"]

  # RGB332 colors. Literals rather than FmrbGfx constants so the table can
  # live in the class body.
  COL_FLOOR  = 0x00  # black
  COL_WALL   = 0x6D  # gray
  COL_KEY    = 0xFC  # yellow
  COL_DOOR   = 0xA8  # brown
  COL_GOAL   = 0x1C  # green
  COL_ROBOT  = 0xFF  # white
  COL_NOSE   = 0xE0  # red
  COL_TEXT   = 0xFF
  COL_DIM    = 0x6D
  COL_OK     = 0x1C
  COL_GRID   = 0x24  # dark green-gray, board background

  BOARD_X = 3
  BOARD_Y = 13
  PANEL_X = 176
  PANEL_Y = 14
  LINE_H  = 15

  STATE_PERIOD_MS = 1000

  def on_create
    @gfx.set_font(:ja, 12)
    subscribe(TOPIC_CMD)
    load_map
    reset_world
    @next_state_at = Machine.board_millis + STATE_PERIOD_MS
    draw_all
    Log.info("RoboExplorer: world ready")
  end

  def on_destroy
    unsubscribe(TOPIC_CMD)
  end

  # ---- world data ----

  def load_map
    @cells = Array.new(BOARD_W * BOARD_H, FLOOR)
    @start_x = 1
    @start_y = 1
    @goal_x  = 1
    @goal_y  = 1
    @map_keys = 0
    y = 0
    while y < BOARD_H
      row = MAP[y]
      x = 0
      while x < BOARD_W
        c = row.getbyte(x)
        kind = FLOOR
        if c == 35        # '#'
          kind = WALL
        elsif c == 75     # 'K'
          kind = KEY
          @map_keys += 1
        elsif c == 68     # 'D'
          kind = DOOR
        elsif c == 71     # 'G'
          kind = GOAL
          @goal_x = x
          @goal_y = y
        elsif c == 83     # 'S'
          @start_x = x
          @start_y = y
        end
        @cells[y * BOARD_W + x] = kind
        x += 1
      end
      y += 1
    end
  end

  def reset_world
    load_map
    @x = @start_x
    @y = @start_y
    @dir = START_DIR
    @keys = 0
    @keys_left = @map_keys
    @turn = 0
    @steps = 0
    @done = false
    @last_op = nil
    @last_ok = true
    @last_reason = nil
  end

  def cell_at(x, y)
    @cells[y * BOARD_W + x]
  end

  def set_cell(x, y, kind)
    @cells[y * BOARD_W + x] = kind
  end

  def front_cell
    nx = @x + DIR_DX[@dir]
    ny = @y + DIR_DY[@dir]
    return WALL if nx < 0 || ny < 0 || nx >= BOARD_W || ny >= BOARD_H
    cell_at(nx, ny)
  end

  # Eight-point compass from the robot to the goal. A pure sign test would
  # call a nearly-north goal "NE", so a shallow axis is dropped.
  def goal_dir
    dx = @goal_x - @x
    dy = @goal_y - @y
    return "-" if dx == 0 && dy == 0
    adx = dx < 0 ? -dx : dx
    ady = dy < 0 ? -dy : dy
    ns = ""
    ew = ""
    ns = (dy < 0) ? "N" : "S" if dy != 0
    ew = (dx > 0) ? "E" : "W" if dx != 0
    if adx > ady * 2
      ns = ""
    elsif ady > adx * 2
      ew = ""
    end
    ns + ew
  end

  def goal_dir_ja(code)
    case code
    when "N"  then "北"          # north
    when "S"  then "南"          # south
    when "E"  then "東"          # east
    when "W"  then "西"          # west
    when "NE" then "北東"
    when "NW" then "北西"
    when "SE" then "南東"
    when "SW" then "南西"
    else "ここ"              # koko (here)
    end
  end

  def cell_ja(kind)
    case kind
    when WALL then "壁"
    when KEY  then "鍵"
    when DOOR then "扉"
    when GOAL then "ゴール"
    else "床"
    end
  end

  # ---- protocol ----

  def on_control(msg)
    return unless msg["cmd"] == "topic_data"
    return unless msg["topic"] == TOPIC_CMD
    data = msg["data"]
    return unless data
    handle_cmd(data)
  end

  def handle_cmd(data)
    op = data["op"]
    @turn += 1
    @last_op = op
    if @done
      @last_ok = false
      @last_reason = "done"
    else
      apply_op(op, data)
    end
    publish_result
    draw_status
    redraw_robot_area
    @gfx.present
    publish_state
  end

  def apply_op(op, data)
    @last_ok = false
    @last_reason = nil
    if op == "move"
      do_move
    elsif op == "turn"
      to = data["to"]
      if to == "L"
        @dir = (@dir + 3) % 4
        @last_ok = true
      elsif to == "R"
        @dir = (@dir + 1) % 4
        @last_ok = true
      else
        @last_reason = "bad_cmd"
      end
    elsif op == "wait"
      @last_ok = true
    else
      @last_reason = "bad_cmd"
    end
  end

  def do_move
    nx = @x + DIR_DX[@dir]
    ny = @y + DIR_DY[@dir]
    if nx < 0 || ny < 0 || nx >= BOARD_W || ny >= BOARD_H
      @last_reason = "edge"
      return
    end
    kind = cell_at(nx, ny)
    if kind == WALL
      @last_reason = "wall"
      return
    elsif kind == DOOR
      if @keys <= 0
        @last_reason = "locked"
        return
      end
      @keys -= 1
      set_cell(nx, ny, FLOOR)
      @board_dirty = true
    elsif kind == KEY
      @keys += 1
      @keys_left -= 1
      set_cell(nx, ny, FLOOR)
      @board_dirty = true
    end
    @prev_x = @x
    @prev_y = @y
    @x = nx
    @y = ny
    @steps += 1
    @done = true if cell_at(nx, ny) == GOAL
    @last_ok = true
  end

  def publish_result
    publish(TOPIC_RESULT,
            { "turn" => @turn, "op" => @last_op, "ok" => @last_ok,
              "reason" => @last_reason })
  end

  def publish_state
    publish(TOPIC_STATE,
            { "turn" => @turn, "x" => @x, "y" => @y, "dir" => DIR_CODE[@dir],
              "front" => CELL_CODE[front_cell], "goal" => goal_dir,
              "keys" => @keys, "keys_left" => @keys_left,
              "done" => @done, "steps" => @steps, "level" => LEVEL })
  end

  def on_update
    now = Machine.board_millis
    if now >= @next_state_at
      @next_state_at = now + STATE_PERIOD_MS
      publish_state
    end
    200
  end

  # ---- input (the world takes no robot commands from the keyboard) ----

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :key_down
    if ev[:scancode] == FmrbConst::KEY_R
      reset_world
      draw_all
      publish_state
      Log.info("RoboExplorer: reset")
    end
  end

  # ---- drawing ----

  def draw_all
    clear_user_area
    draw_board
    draw_status
    draw_window_frame
    @gfx.present
  end

  def draw_board
    @gfx.fill_rect(BOARD_X - 1, BOARD_Y - 1,
                   BOARD_W * TILE + 2, BOARD_H * TILE + 2, COL_GRID)
    y = 0
    while y < BOARD_H
      x = 0
      while x < BOARD_W
        draw_tile(x, y)
        x += 1
      end
      y += 1
    end
    draw_robot
    @board_dirty = false
  end

  def draw_tile(x, y)
    kind = cell_at(x, y)
    color = COL_FLOOR
    if kind == WALL
      color = COL_WALL
    elsif kind == KEY
      color = COL_KEY
    elsif kind == DOOR
      color = COL_DOOR
    elsif kind == GOAL
      color = COL_GOAL
    end
    @gfx.fill_rect(BOARD_X + x * TILE, BOARD_Y + y * TILE,
                   TILE - 1, TILE - 1, color)
  end

  def draw_robot
    px = BOARD_X + @x * TILE
    py = BOARD_Y + @y * TILE
    @gfx.fill_rect(px + 3, py + 3, TILE - 7, TILE - 7, COL_ROBOT)
    # A red mark on the side the robot faces.
    nx = px + 5
    ny = py + 5
    if @dir == 0
      ny = py + 1
    elsif @dir == 1
      nx = px + TILE - 5
    elsif @dir == 2
      ny = py + TILE - 5
    else
      nx = px + 1
    end
    @gfx.fill_rect(nx, ny, 3, 3, COL_NOSE)
  end

  # Only the two cells the robot left and entered need repainting -- a full
  # 144-tile redraw per turn would put 144 draw calls on the wire for nothing.
  def redraw_robot_area
    if @board_dirty
      draw_board
      return
    end
    if @prev_x
      draw_tile(@prev_x, @prev_y)
      @prev_x = nil
    end
    draw_tile(@x, @y)
    draw_robot
  end

  def draw_status
    @gfx.fill_rect(PANEL_X, PANEL_Y - 2, @user_area_x1 - PANEL_X - 1,
                   @user_area_y1 - PANEL_Y - 1, COL_FLOOR)
    y = PANEL_Y
    @gfx.draw_text(PANEL_X, y, "場所 #{@x},#{@y}", COL_TEXT)
    y += LINE_H
    @gfx.draw_text(PANEL_X, y, "向き #{DIR_JA[@dir]}", COL_TEXT)
    y += LINE_H
    @gfx.draw_text(PANEL_X, y, "前 #{cell_ja(front_cell)}", COL_TEXT)
    y += LINE_H
    @gfx.draw_text(PANEL_X, y, "鍵 #{@keys}", COL_KEY)
    y += LINE_H
    @gfx.draw_text(PANEL_X, y, "ゴール #{goal_dir_ja(goal_dir)}",
                   COL_GOAL)
    y += LINE_H
    @gfx.draw_text(PANEL_X, y, "手数 #{@steps}", COL_TEXT)
    y += LINE_H + 4
    if @done
      @gfx.draw_text(PANEL_X, y, "クリア!", COL_OK)
    end
    @gfx.draw_text(PANEL_X, @user_area_y1 - 16, "R:リセット",
                   COL_DIM)
  end
end

begin
  app = RoboExplorerApp.new
  app.start
rescue => e
  Log.error("RoboExplorer: #{e}")
end
