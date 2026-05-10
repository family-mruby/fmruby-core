# Tetris - Classic falling block puzzle game
# Controls: Arrow keys (Left/Right/Down), Up=Rotate, Space=Hard drop

class TetrisApp < FmrbApp
  # Board dimensions
  COLS = 10
  ROWS = 20
  CELL = 8  # Cell size in pixels

  # Colors (RGB332)
  BG_COLOR     = 0x00  # Playfield interior (kept black for piece contrast)
  PANEL_COLOR  = 0x29  # Area outside the playfield (dark blue-gray)
  BORDER_OUTER = 0x92  # Bright outer border line
  BORDER_INNER = 0x49  # Dark inner border line
  GHOST_COLOR  = 0x6D  # Dim gray for ghost-piece preview
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
    @next_piece_type = nil

    @bg_block = nil
    @piece_block = nil
    @next_block = nil
  end

  def on_create
    Log.info("Tetris on_create")
    build_blocks
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
    @next_piece_type = nil
  end

  def spawn_piece
    if @next_piece_type
      @piece_type = @next_piece_type
    else
      @piece_type = RNG.random_int % PIECES.size
    end
    @next_piece_type = RNG.random_int % PIECES.size
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
    cells = PIECES[@piece_type][rot]
    ci = 0
    cn = cells.length
    while ci < cn
      cell = cells[ci]
      r = py + cell[0]
      c = px + cell[1]
      return true if c < 0 || c >= COLS || r >= ROWS
      return true if r >= 0 && @board[r][c] != 0
      ci += 1
    end
    false
  end

  def lock_piece
    cells = current_cells
    ci = 0
    cn = cells.length
    while ci < cn
      cell = cells[ci]
      r = @piece_y + cell[0]
      c = @piece_x + cell[1]
      if r >= 0 && r < ROWS && c >= 0 && c < COLS
        @board[r][c] = @piece_type + 1
      end
      ci += 1
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

  def calc_ghost_y(px, py, rot)
    gy = py
    while !collides?(px, gy + 1, rot)
      gy += 1
    end
    gy
  end

  # ---- Layout helpers ----

  def board_x0
    @user_area_x0 + 2
  end

  def board_y0
    @user_area_y0 + 2
  end

  def info_x
    board_x0 + COLS * CELL + 6
  end

  def info_y
    board_y0 + 2
  end

  def next_box_x
    info_x
  end

  def next_box_y
    info_y + 74
  end

  # ---- GfxBlock setup ----
  #
  # Three reusable bytecode blocks are uploaded once to WROVER and re-executed
  # with changing register values to avoid resending static command sequences.

  def build_blocks
    pf_w  = COLS * CELL
    pf_h  = ROWS * CELL
    pf_x  = board_x0
    pf_y  = board_y0
    nb_x  = next_box_x
    nb_y  = next_box_y
    nb_sz = 4 * CELL
    ix    = info_x
    iy    = info_y

    # @bg_block: panel background, playfield interior, double border, info
    # labels, and the empty NEXT preview frame. Drawn at start and after every
    # board update (lock_piece) to refresh the static layout. No kwargs because
    # nothing about it changes per draw.
    @bg_block = GfxBlock.new(@gfx) do |r|
      # Panel area (covers the whole user area beneath the title bar).
      r.fill_rect @user_area_x0, @user_area_y0,
                  @user_area_width, @user_area_height, PANEL_COLOR
      # Playfield interior.
      r.fill_rect pf_x, pf_y, pf_w, pf_h, BG_COLOR
      # Double border around the playfield (outer bright, inner dark).
      r.draw_rect pf_x - 2, pf_y - 2, pf_w + 4, pf_h + 4, BORDER_OUTER
      r.draw_rect pf_x - 1, pf_y - 1, pf_w + 2, pf_h + 2, BORDER_INNER
      # Info labels (values are drawn separately because their text changes).
      r.draw_text ix, iy,      "SCORE", TEXT_COLOR
      r.draw_text ix, iy + 24, "LINES", TEXT_COLOR
      r.draw_text ix, iy + 48, "LV",    TEXT_COLOR
      r.draw_text ix, iy + 64, "NEXT",  TEXT_COLOR
      # NEXT preview frame.
      r.draw_rect nb_x - 1, nb_y - 1, nb_sz + 2, nb_sz + 2, BORDER_INNER
    end

    # @piece_block: four colored cells. Reused for the active piece and the
    # ghost piece (different color and positions).
    @piece_block = GfxBlock.new(@gfx,
                                x0: 0, y0: 0, x1: 0, y1: 0,
                                x2: 0, y2: 0, x3: 0, y3: 0,
                                color: 0) do |r,
                                                x0:, y0:, x1:, y1:,
                                                x2:, y2:, x3:, y3:, color:|
      r.fill_rect x0, y0, CELL - 1, CELL - 1, color
      r.fill_rect x1, y1, CELL - 1, CELL - 1, color
      r.fill_rect x2, y2, CELL - 1, CELL - 1, color
      r.fill_rect x3, y3, CELL - 1, CELL - 1, color
    end

    # @next_block: clear the NEXT preview area, then draw the four cells of
    # the upcoming piece. Separated from @piece_block because it has the
    # extra clear command (Block command sequences must match across calls).
    @next_block = GfxBlock.new(@gfx,
                               x0: 0, y0: 0, x1: 0, y1: 0,
                               x2: 0, y2: 0, x3: 0, y3: 0,
                               color: 0) do |r,
                                              x0:, y0:, x1:, y1:,
                                              x2:, y2:, x3:, y3:, color:|
      r.fill_rect nb_x, nb_y, nb_sz, nb_sz, BG_COLOR
      r.fill_rect x0, y0, CELL - 1, CELL - 1, color
      r.fill_rect x1, y1, CELL - 1, CELL - 1, color
      r.fill_rect x2, y2, CELL - 1, CELL - 1, color
      r.fill_rect x3, y3, CELL - 1, CELL - 1, color
    end
  end

  # ---- Drawing ----

  def draw_all
    @bg_block.draw
    draw_window_frame
    draw_board
    draw_ghost
    draw_piece
    draw_next
    draw_info
    @gfx.present
  end

  def draw_board
    # Border and empty-cell background are part of @bg_block, so just paint
    # the occupied cells here.
    r = 0
    while r < ROWS
      c = 0
      while c < COLS
        val = @board[r][c]
        if val != 0
          draw_cell(c, r, val)
        end
        c += 1
      end
      r += 1
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

  def draw_4cells_at(px, py, rot, color)
    cells = PIECES[@piece_type][rot]
    @piece_block.draw(
      x0: board_x0 + (px + cells[0][1]) * CELL,
      y0: board_y0 + (py + cells[0][0]) * CELL,
      x1: board_x0 + (px + cells[1][1]) * CELL,
      y1: board_y0 + (py + cells[1][0]) * CELL,
      x2: board_x0 + (px + cells[2][1]) * CELL,
      y2: board_y0 + (py + cells[2][0]) * CELL,
      x3: board_x0 + (px + cells[3][1]) * CELL,
      y3: board_y0 + (py + cells[3][0]) * CELL,
      color: color
    )
  end

  def draw_piece
    return if @game_over
    draw_4cells_at(@piece_x, @piece_y, @piece_rot, PIECE_COLORS[@piece_type])
  end

  def draw_ghost
    return if @game_over
    gy = calc_ghost_y(@piece_x, @piece_y, @piece_rot)
    return if gy == @piece_y  # piece already at landing position
    draw_4cells_at(@piece_x, gy, @piece_rot, GHOST_COLOR)
  end

  def erase_piece
    cells = current_cells
    ci = 0
    cn = cells.length
    while ci < cn
      cell = cells[ci]
      r = @piece_y + cell[0]
      c = @piece_x + cell[1]
      if r >= 0 && r < ROWS && c >= 0 && c < COLS
        draw_cell(c, r, @board[r][c])
      end
      ci += 1
    end
  end

  def erase_ghost
    return if @game_over
    gy = calc_ghost_y(@piece_x, @piece_y, @piece_rot)
    return if gy == @piece_y
    cells = current_cells
    ci = 0
    cn = cells.length
    while ci < cn
      cell = cells[ci]
      r = gy + cell[0]
      c = @piece_x + cell[1]
      if r >= 0 && r < ROWS && c >= 0 && c < COLS
        draw_cell(c, r, @board[r][c])
      end
      ci += 1
    end
  end

  def draw_next
    return if @next_piece_type.nil?
    cells = PIECES[@next_piece_type][0]
    color = PIECE_COLORS[@next_piece_type]
    @next_block.draw(
      x0: next_box_x + cells[0][1] * CELL,
      y0: next_box_y + cells[0][0] * CELL,
      x1: next_box_x + cells[1][1] * CELL,
      y1: next_box_y + cells[1][0] * CELL,
      x2: next_box_x + cells[2][1] * CELL,
      y2: next_box_y + cells[2][0] * CELL,
      x3: next_box_x + cells[3][1] * CELL,
      y3: next_box_y + cells[3][0] * CELL,
      color: color
    )
  end

  def draw_info
    ix = info_x
    iy = info_y
    # Clear value rows (panel background) before rewriting numbers.
    @gfx.fill_rect(ix, iy + 8,  38, 8, PANEL_COLOR)
    @gfx.fill_rect(ix, iy + 32, 38, 8, PANEL_COLOR)
    @gfx.fill_rect(ix, iy + 56, 38, 8, PANEL_COLOR)

    @gfx.draw_text(ix, iy + 8,  @score.to_s, TEXT_COLOR, PANEL_COLOR)
    @gfx.draw_text(ix, iy + 32, @lines.to_s, TEXT_COLOR, PANEL_COLOR)
    @gfx.draw_text(ix, iy + 56, @level.to_s, TEXT_COLOR, PANEL_COLOR)
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
    erase_ghost
    erase_piece
    if !collides?(@piece_x + dx, @piece_y + dy, @piece_rot)
      @piece_x += dx
      @piece_y += dy
    end
    draw_ghost
    draw_piece
    @gfx.present
  end

  def try_rotate
    return if @game_over
    erase_ghost
    erase_piece
    new_rot = (@piece_rot + 1) % 4
    if !collides?(@piece_x, @piece_y, new_rot)
      @piece_rot = new_rot
    end
    draw_ghost
    draw_piece
    @gfx.present
  end

  def move_down
    return if @game_over
    erase_ghost
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
      draw_ghost
      draw_piece
      @gfx.present
    end
  end

  def hard_drop
    return if @game_over
    erase_ghost
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
    @bg_block&.destroy
    @piece_block&.destroy
    @next_block&.destroy
    @bg_block = nil
    @piece_block = nil
    @next_block = nil
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
