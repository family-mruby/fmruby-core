# Tetris - Classic falling block puzzle game
# Controls: Arrow keys (Left/Right/Down), Up=Rotate, Space=Hard drop

class TetrisApp < FmrbApp
  # Board dimensions
  COLS = 10
  ROWS = 20
  CELL = 8  # Cell size in pixels

  # Colors (RGB332)
  BG_COLOR    = 0x00
  GRID_COLOR  = 0x24
  BORDER_COLOR = 0x60
  TEXT_COLOR   = 0xFF
  GAMEOVER_BG  = 0xE0

  # Piece colors
  PIECE_COLORS = [
    0x1F,  # I - cyan
    0x03,  # J - blue
    0xFC,  # L - orange (yellow-ish in RGB332)
    0xED,  # O - yellow
    0x1C,  # S - green
    0xE3,  # T - magenta
    0xE0,  # Z - red
  ]

  # Piece shapes (each is array of 4 rotations, each rotation is [row, col] offsets)
  PIECES = [
    # I
    [[[0,0],[0,1],[0,2],[0,3]], [[0,0],[1,0],[2,0],[3,0]],
     [[0,0],[0,1],[0,2],[0,3]], [[0,0],[1,0],[2,0],[3,0]]],
    # J
    [[[0,0],[1,0],[1,1],[1,2]], [[0,0],[0,1],[1,0],[2,0]],
     [[0,0],[0,1],[0,2],[1,2]], [[0,1],[1,1],[2,0],[2,1]]],
    # L
    [[[0,2],[1,0],[1,1],[1,2]], [[0,0],[1,0],[2,0],[2,1]],
     [[0,0],[0,1],[0,2],[1,0]], [[0,0],[0,1],[1,1],[2,1]]],
    # O
    [[[0,0],[0,1],[1,0],[1,1]], [[0,0],[0,1],[1,0],[1,1]],
     [[0,0],[0,1],[1,0],[1,1]], [[0,0],[0,1],[1,0],[1,1]]],
    # S
    [[[0,1],[0,2],[1,0],[1,1]], [[0,0],[1,0],[1,1],[2,1]],
     [[0,1],[0,2],[1,0],[1,1]], [[0,0],[1,0],[1,1],[2,1]]],
    # T
    [[[0,1],[1,0],[1,1],[1,2]], [[0,0],[1,0],[1,1],[2,0]],
     [[0,0],[0,1],[0,2],[1,1]], [[0,1],[1,0],[1,1],[2,1]]],
    # Z
    [[[0,0],[0,1],[1,1],[1,2]], [[0,1],[1,0],[1,1],[2,0]],
     [[0,0],[0,1],[1,1],[1,2]], [[0,1],[1,0],[1,1],[2,0]]],
  ]

  def initialize
    super()
    @board = []
    @score = 0
    @lines = 0
    @level = 1
    @game_over = false
    @drop_counter = 0
    @input_buffer = []
    @frame_ms = 33
  end

  def on_create
    Log.info("Tetris on_create")
    reset_board
    spawn_piece
    draw_all
  end

  def reset_board
    @board = Array.new(ROWS) { Array.new(COLS, 0) }
    @score = 0
    @lines = 0
    @level = 1
    @game_over = false
    @drop_counter = 0
  end

  def spawn_piece
    @piece_type = RNG.random_int % PIECES.size
    @piece_rot = 0
    @piece_x = COLS / 2 - 1
    @piece_y = 0

    if collides?(@piece_x, @piece_y, @piece_rot)
      @game_over = true
    end
  end

  def current_cells
    PIECES[@piece_type][@piece_rot]
  end

  def collides?(px, py, rot)
    PIECES[@piece_type][rot].each do |cell|
      r = py + cell[0]
      c = px + cell[1]
      return true if c < 0 || c >= COLS || r >= ROWS
      return true if r >= 0 && @board[r][c] != 0
    end
    false
  end

  def lock_piece
    current_cells.each do |cell|
      r = @piece_y + cell[0]
      c = @piece_x + cell[1]
      if r >= 0 && r < ROWS && c >= 0 && c < COLS
        @board[r][c] = @piece_type + 1
      end
    end
    clear_lines
    spawn_piece
  end

  def clear_lines
    cleared = 0
    row = ROWS - 1
    while row >= 0
      if @board[row].all? { |c| c != 0 }
        @board.delete_at(row)
        @board.unshift(Array.new(COLS, 0))
        cleared += 1
      else
        row -= 1
      end
    end

    if cleared > 0
      @lines += cleared
      # Scoring: 1=100, 2=300, 3=500, 4=800
      points = [0, 100, 300, 500, 800]
      @score += (points[cleared] || 800) * @level
      @level = @lines / 10 + 1
    end
  end

  def drop_speed
    # Frames per drop (faster at higher levels)
    speed = 15 - @level
    speed = 1 if speed < 1
    speed
  end

  # ---- Drawing ----

  def board_x0
    @user_area_x0 + 2
  end

  def board_y0
    @user_area_y0 + 2
  end

  def draw_all
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                    @user_area_width, @user_area_height, BG_COLOR)
    draw_window_frame
    draw_board
    draw_piece
    draw_info
    @gfx.present
  end

  def draw_board
    # Draw border
    bx = board_x0 - 1
    by = board_y0 - 1
    bw = COLS * CELL + 2
    bh = ROWS * CELL + 2
    @gfx.draw_rect(bx, by, bw, bh, BORDER_COLOR)

    # Draw cells
    ROWS.times do |r|
      COLS.times do |c|
        draw_cell(c, r, @board[r][c])
      end
    end
  end

  def draw_cell(col, row, val)
    x = board_x0 + col * CELL
    y = board_y0 + row * CELL
    if val != 0
      color = PIECE_COLORS[val - 1] || TEXT_COLOR
      @gfx.fill_rect(x, y, CELL - 1, CELL - 1, color)
    else
      @gfx.fill_rect(x, y, CELL - 1, CELL - 1, BG_COLOR)
    end
  end

  def draw_piece
    return if @game_over
    color = PIECE_COLORS[@piece_type]
    current_cells.each do |cell|
      r = @piece_y + cell[0]
      c = @piece_x + cell[1]
      if r >= 0 && r < ROWS && c >= 0 && c < COLS
        x = board_x0 + c * CELL
        y = board_y0 + r * CELL
        @gfx.fill_rect(x, y, CELL - 1, CELL - 1, color)
      end
    end
  end

  def erase_piece
    current_cells.each do |cell|
      r = @piece_y + cell[0]
      c = @piece_x + cell[1]
      if r >= 0 && r < ROWS && c >= 0 && c < COLS
        draw_cell(c, r, @board[r][c])
      end
    end
  end

  def draw_info
    ix = board_x0 + COLS * CELL + 6
    iy = board_y0 + 2

    # Clear info area
    @gfx.fill_rect(ix, iy, 50, 60, BG_COLOR)

    @gfx.draw_text(ix, iy, "SCORE", TEXT_COLOR)
    @gfx.draw_text(ix, iy + 10, @score.to_s, TEXT_COLOR)

    @gfx.draw_text(ix, iy + 24, "LINES", TEXT_COLOR)
    @gfx.draw_text(ix, iy + 34, @lines.to_s, TEXT_COLOR)

    @gfx.draw_text(ix, iy + 48, "LV #{@level}", TEXT_COLOR)
  end

  def draw_game_over
    cx = board_x0 + (COLS * CELL) / 2
    cy = board_y0 + (ROWS * CELL) / 2
    @gfx.fill_rect(cx - 30, cy - 14, 60, 30, GAMEOVER_BG)
    @gfx.draw_text(cx - 27, cy - 10, "GAME OVER", TEXT_COLOR, GAMEOVER_BG)
    @gfx.draw_text(cx - 27, cy + 2, "Enter:Retry", TEXT_COLOR, GAMEOVER_BG)
    @gfx.present
  end

  # ---- Input ----

  def on_event(ev)
    super(ev)

    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0

      case keycode
      when 80  # Left
        @input_buffer << :left
      when 79  # Right
        @input_buffer << :right
      when 81  # Down
        @input_buffer << :down
      when 82  # Up (rotate)
        @input_buffer << :rotate
      end

      if character == 32  # Space (hard drop)
        @input_buffer << :drop
      end

      # Restart on Enter when game over
      if @game_over && (character == 10 || character == 13)
        @input_buffer << :restart
      end
    end
  end

  def process_input
    while !@input_buffer.empty?
      cmd = @input_buffer.shift
      case cmd
      when :left
        try_move(-1, 0)
      when :right
        try_move(1, 0)
      when :down
        move_down
      when :rotate
        try_rotate
      when :drop
        hard_drop
      when :restart
        reset_board
        spawn_piece
        draw_all
      end
    end
  end

  def try_move(dx, dy)
    return if @game_over
    erase_piece
    if !collides?(@piece_x + dx, @piece_y + dy, @piece_rot)
      @piece_x += dx
      @piece_y += dy
    end
    draw_piece
    @gfx.present
  end

  def try_rotate
    return if @game_over
    erase_piece
    new_rot = (@piece_rot + 1) % 4
    if !collides?(@piece_x, @piece_y, new_rot)
      @piece_rot = new_rot
    end
    draw_piece
    @gfx.present
  end

  def move_down
    return if @game_over
    erase_piece
    if collides?(@piece_x, @piece_y + 1, @piece_rot)
      draw_piece
      lock_piece
      if @game_over
        draw_game_over
      else
        draw_all
      end
    else
      @piece_y += 1
      draw_piece
      @gfx.present
    end
  end

  def hard_drop
    return if @game_over
    erase_piece
    while !collides?(@piece_x, @piece_y + 1, @piece_rot)
      @piece_y += 1
    end
    draw_piece
    lock_piece
    if @game_over
      draw_game_over
    else
      draw_all
    end
  end

  # ---- Game loop ----

  def on_update
    process_input

    if @game_over
      return 100
    end

    @drop_counter += 1
    if @drop_counter >= drop_speed
      @drop_counter = 0
      move_down
    end

    @frame_ms
  end

  def on_destroy
    Log.info("Tetris destroyed")
  end
end

Log.info("TetrisApp.new")
begin
  app = TetrisApp.new
  Log.info("TetrisApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
