# Text Editor Application
# MS-DOS style text editor with menu bar and status line

class EditorApp < FmrbApp
  # Colors (RGB332)
  BG_COLOR      = 0x00  # Dark blue background
  TEXT_COLOR    = 0xFF  # White text
  MENU_BG       = 0x01  # Black menu bar
  MENU_TEXT     = 0xFF  # White menu text
  MENU_KEY      = 0xFC  # Yellow highlight for hotkey letter
  STATUS_BG     = 0x00  # Black status line
  STATUS_TEXT   = 0x1F  # Cyan status text
  CURSOR_COLOR  = 0xFC  # Yellow cursor

  CHAR_W = 6
  CHAR_H = 8
  TAB_SIZE = 2

  def initialize
    super()
    @lines = [""]       # Document lines
    @cx = 0             # Cursor column in current line
    @cy = 0             # Cursor line index
    @scroll_y = 0       # First visible line index
    @need_redraw = true
    @input_buffer = []
    @frame_ms = 33
    @modified = false
  end

  def on_create
    # Calculate layout
    @menu_y = @user_area_y0
    @edit_y = @menu_y + CHAR_H + 1
    @status_y = @user_area_y0 + @user_area_height - CHAR_H
    @edit_height = @status_y - @edit_y
    @edit_cols = (@user_area_width - 2) / CHAR_W
    @edit_rows = @edit_height / CHAR_H

    @need_redraw = true

    app_self = self
    @editor_task = Task.new(name: "editor_task", priority: 100) do
      app_self.editor_loop
    end
  end

  def editor_loop
    loop do
      break unless @running
      ch = getch
      break if ch.nil?
      handle_key(ch)
    end
  end

  def getch
    while @input_buffer.empty? && @running
      sleep_ms @frame_ms
    end
    return nil unless @running
    @input_buffer.shift
  end

  # ---- Drawing ----

  def draw_menu_bar
    y = @menu_y
    @gfx.fill_rect(@user_area_x0, y, @user_area_width, CHAR_H, MENU_BG)

    x = @user_area_x0 + 2
    # File
    draw_menu_item(x, y, "F", "ile")
    x += 6 * CHAR_W
    # Edit
    draw_menu_item(x, y, "E", "dit")
    x += 6 * CHAR_W
    # Search
    draw_menu_item(x, y, "S", "earch")
  end

  def draw_menu_item(x, y, key_char, rest)
    @gfx.draw_text(x, y, key_char, MENU_KEY, MENU_BG)
    @gfx.draw_text(x + CHAR_W, y, rest, MENU_TEXT, MENU_BG)
  end

  def draw_status_line
    y = @status_y
    @gfx.fill_rect(@user_area_x0, y, @user_area_width, CHAR_H, STATUS_BG)

    line_num = @cy + 1
    col_num = @cx + 1
    total = @lines.length

    status = " Ln #{line_num}, Col #{col_num}  Lines: #{total}"
    status += "  [Modified]" if @modified

    @gfx.draw_text(@user_area_x0 + 2, y, status, STATUS_TEXT, STATUS_BG)
  end

  def draw_edit_area
    @gfx.fill_rect(@user_area_x0, @edit_y,
                    @user_area_width, @edit_height, BG_COLOR)

    @edit_rows.times do |row|
      line_idx = @scroll_y + row
      break if line_idx >= @lines.length

      text = @lines[line_idx]
      # Truncate to visible width
      visible = text.length > @edit_cols ? text[0, @edit_cols] : text

      x = @user_area_x0 + 1
      y = @edit_y + row * CHAR_H
      @gfx.draw_text(x, y, visible, TEXT_COLOR, BG_COLOR) unless visible.empty?
    end

    draw_cursor
  end

  def draw_cursor
    # Cursor position relative to scroll
    screen_row = @cy - @scroll_y
    return if screen_row < 0 || screen_row >= @edit_rows

    x = @user_area_x0 + 1 + @cx * CHAR_W
    y = @edit_y + screen_row * CHAR_H

    # Draw block cursor
    @gfx.fill_rect(x, y, CHAR_W, CHAR_H, CURSOR_COLOR)

    # Draw character under cursor in contrasting color
    line = @lines[@cy] || ""
    if @cx < line.length
      @gfx.draw_text(x, y, line[@cx], BG_COLOR, CURSOR_COLOR)
    end
  end

  def redraw_all
    draw_menu_bar
    draw_edit_area
    draw_status_line
    @gfx.present
  end

  # ---- Scrolling ----

  def ensure_cursor_visible
    if @cy < @scroll_y
      @scroll_y = @cy
    elsif @cy >= @scroll_y + @edit_rows
      @scroll_y = @cy - @edit_rows + 1
    end
  end

  # ---- Key handling ----

  def handle_key(ch)
    case ch
    when 10, 13  # Enter
      handle_enter
    when 8       # Backspace
      handle_backspace
    when 127     # Delete (some terminals)
      handle_delete
    when 9       # Tab
      TAB_SIZE.times { insert_char(' ') }
    when 32..126 # Printable
      insert_char(ch.chr)
    end
  end

  def insert_char(c)
    line = @lines[@cy] || ""
    @lines[@cy] = line[0, @cx].to_s + c + line[@cx..-1].to_s
    @cx += 1
    @modified = true
    @need_redraw = true
  end

  def handle_enter
    line = @lines[@cy] || ""
    # Split line at cursor
    left = line[0, @cx].to_s
    right = line[@cx..-1].to_s
    @lines[@cy] = left
    @lines.insert(@cy + 1, right)
    @cy += 1
    @cx = 0
    @modified = true
    ensure_cursor_visible
    @need_redraw = true
  end

  def handle_backspace
    if @cx > 0
      line = @lines[@cy] || ""
      @lines[@cy] = line[0, @cx - 1].to_s + line[@cx..-1].to_s
      @cx -= 1
      @modified = true
      @need_redraw = true
    elsif @cy > 0
      # Merge with previous line
      prev_len = @lines[@cy - 1].length
      @lines[@cy - 1] += @lines[@cy]
      @lines.delete_at(@cy)
      @cy -= 1
      @cx = prev_len
      @modified = true
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def handle_delete
    line = @lines[@cy] || ""
    if @cx < line.length
      @lines[@cy] = line[0, @cx].to_s + line[@cx + 1..-1].to_s
      @modified = true
      @need_redraw = true
    elsif @cy < @lines.length - 1
      # Merge next line
      @lines[@cy] += @lines[@cy + 1]
      @lines.delete_at(@cy + 1)
      @modified = true
      @need_redraw = true
    end
  end

  # ---- Arrow keys (via keycode) ----

  def move_up
    if @cy > 0
      @cy -= 1
      clamp_cx
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def move_down
    if @cy < @lines.length - 1
      @cy += 1
      clamp_cx
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def move_left
    if @cx > 0
      @cx -= 1
      @need_redraw = true
    elsif @cy > 0
      @cy -= 1
      @cx = (@lines[@cy] || "").length
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def move_right
    line = @lines[@cy] || ""
    if @cx < line.length
      @cx += 1
      @need_redraw = true
    elsif @cy < @lines.length - 1
      @cy += 1
      @cx = 0
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def clamp_cx
    line = @lines[@cy] || ""
    @cx = line.length if @cx > line.length
  end

  # ---- Events ----

  def on_event(ev)
    super(ev)

    if ev[:type] == :key_down
      keycode = ev[:keycode] || 0
      character = ev[:character] || 0

      # Arrow keys (USB HID keycodes)
      case keycode
      when 79  # Right
        move_right
        return
      when 80  # Left
        move_left
        return
      when 81  # Down
        move_down
        return
      when 82  # Up
        move_up
        return
      end

      # Modifier keys - ignore
      return if keycode >= 225 && keycode <= 229

      # Printable / control characters
      if character > 0
        @input_buffer << character
      end
    end
  end

  def on_update
    if @need_redraw
      redraw_all
      @need_redraw = false
    end
    @frame_ms
  end

  def on_destroy
    Log.info("Editor destroyed")
  end
end

Log.info("EditorApp.new")
begin
  app = EditorApp.new
  Log.info("EditorApp created successfully")
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
