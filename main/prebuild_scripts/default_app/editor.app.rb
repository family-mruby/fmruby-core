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

  # Syntax highlight category colors (RGB332)
  HL_COLORS = [
    0xFF,  # 0: default    - white
    0xE0,  # 1: keyword    - red
    0x1C,  # 2: string     - green
    0x92,  # 3: comment    - gray
    0xFC,  # 4: number     - yellow
    0xE3,  # 5: symbol     - magenta
    0x1F,  # 6: constant   - cyan
    0xFE,  # 7: variable   - light pink
    0x9F,  # 8: method     - light blue
  ]

  CHAR_W = 6
  CHAR_H = 8
  TAB_SIZE = 2

  # File dropdown
  DROPDOWN_BG = 0xFF
  DROPDOWN_TEXT = 0x00
  DROPDOWN_SEL_BG = 0xC5
  DROPDOWN_SEL_TEXT = 0xFF
  DROPDOWN_ITEMS = ["Open", "Save", "Save as"]
  DROPDOWN_W = 54
  DROPDOWN_ITEM_H = 10

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
    @current_file = nil
    @file_dropdown_open = false
    @pending_file_op = nil  # :open or :save
    @highlight_map = nil
    @highlight_dirty = true
    @line_offsets = nil
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

    fname = @current_file ? @current_file.split("/").last : "[New]"
    status = " #{fname}  Ln #{line_num}, Col #{col_num}"
    status += " *" if @modified

    @gfx.draw_text(@user_area_x0 + 2, y, status, STATUS_TEXT, STATUS_BG)
  end

  def update_highlight
    return unless @highlight_dirty
    @highlight_dirty = false

    source = @lines.join("\n")
    @highlight_map = SyntaxHighlight.tokenize(source)

    # Build line offset table
    @line_offsets = []
    offset = 0
    @lines.each do |line|
      @line_offsets << offset
      offset += line.length + 1  # +1 for newline
    end
  end

  def draw_edit_area
    @gfx.fill_rect(@user_area_x0, @edit_y,
                    @user_area_width, @edit_height, BG_COLOR)

    update_highlight

    @edit_rows.times do |row|
      line_idx = @scroll_y + row
      break if line_idx >= @lines.length

      text = @lines[line_idx]
      visible_len = text.length > @edit_cols ? @edit_cols : text.length
      next if visible_len == 0

      x = @user_area_x0 + 1
      y = @edit_y + row * CHAR_H

      if @highlight_map && @line_offsets
        draw_highlighted_line(x, y, text, visible_len, @line_offsets[line_idx])
      else
        @gfx.draw_text(x, y, text[0, visible_len], TEXT_COLOR, BG_COLOR)
      end
    end

    draw_cursor
  end

  def draw_highlighted_line(x, y, text, visible_len, offset)
    i = 0
    while i < visible_len
      cat = @highlight_map.getbyte(offset + i) || 0
      color = HL_COLORS[cat] || TEXT_COLOR

      # Gather consecutive characters with the same category
      j = i + 1
      while j < visible_len
        next_cat = @highlight_map.getbyte(offset + j) || 0
        break if next_cat != cat
        j += 1
      end

      chunk = text[i, j - i]
      @gfx.draw_text(x + i * CHAR_W, y, chunk, color, BG_COLOR)
      i = j
    end
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
    @highlight_dirty = true
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
    @highlight_dirty = true
    ensure_cursor_visible
    @need_redraw = true
  end

  def handle_backspace
    if @cx > 0
      line = @lines[@cy] || ""
      @lines[@cy] = line[0, @cx - 1].to_s + line[@cx..-1].to_s
      @cx -= 1
      @modified = true
      @highlight_dirty = true
      @need_redraw = true
    elsif @cy > 0
      # Merge with previous line
      prev_len = @lines[@cy - 1].length
      @lines[@cy - 1] += @lines[@cy]
      @lines.delete_at(@cy)
      @cy -= 1
      @cx = prev_len
      @modified = true
      @highlight_dirty = true
      ensure_cursor_visible
      @need_redraw = true
    end
  end

  def handle_delete
    line = @lines[@cy] || ""
    if @cx < line.length
      @lines[@cy] = line[0, @cx].to_s + line[@cx + 1..-1].to_s
      @modified = true
      @highlight_dirty = true
      @need_redraw = true
    elsif @cy < @lines.length - 1
      # Merge next line
      @lines[@cy] += @lines[@cy + 1]
      @lines.delete_at(@cy + 1)
      @modified = true
      @highlight_dirty = true
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

  # ---- File dropdown ----

  def draw_file_dropdown
    return unless @file_dropdown_open

    x = @user_area_x0 + 2
    y = @menu_y + CHAR_H
    h = DROPDOWN_ITEM_H * DROPDOWN_ITEMS.size + 2

    @gfx.fill_rect(x, y, DROPDOWN_W, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, DROPDOWN_W, h, 0x60)

    DROPDOWN_ITEMS.each_with_index do |item, i|
      item_y = y + 1 + i * DROPDOWN_ITEM_H
      @gfx.draw_text(x + 4, item_y + 1, item, DROPDOWN_TEXT, DROPDOWN_BG)
    end
  end

  def open_file_dropdown
    @file_dropdown_open = true
    redraw_all
    draw_file_dropdown
    @gfx.present
  end

  def close_file_dropdown
    @file_dropdown_open = false
    @need_redraw = true
  end

  def handle_file_dropdown_click(x, y)
    dx = @user_area_x0 + 2
    dy = @menu_y + CHAR_H

    if x >= dx && x < dx + DROPDOWN_W && y >= dy
      idx = (y - dy - 1) / DROPDOWN_ITEM_H
      close_file_dropdown

      case idx
      when 0  # Open
        @pending_file_op = :open
        request_file_select("open")
      when 1  # Save
        save_file
      when 2  # Save as
        @pending_file_op = :save
        request_file_select("save")
      end
      return
    end

    close_file_dropdown
  end

  # ---- File operations ----

  def load_file(path)
    begin
      file = File.open(path, "r")
      content = file.read
      file.close

      @lines = content.split("\n")
      @lines = [""] if @lines.empty?
      @cx = 0
      @cy = 0
      @scroll_y = 0
      @modified = false
      @highlight_dirty = true
      @current_file = path
      @need_redraw = true
      Log.info("Loaded file: #{path} (#{@lines.length} lines)")
    rescue => e
      Log.error("Failed to load file: #{e.message}")
    end
  end

  def save_file
    unless @current_file
      Log.warn("No file to save (use Save as)")
      return
    end

    begin
      file = File.open(@current_file, "w")
      file.write(@lines.join("\n"))
      file.close
      @modified = false
      @need_redraw = true
      Log.info("Saved file: #{@current_file}")
    rescue => e
      Log.error("Failed to save file: #{e.message}")
    end
  end

  # ---- Events ----

  def on_control(msg)
    if msg["cmd"] == "file_selected" && msg["path"]
      if msg["mode"] == "save" || @pending_file_op == :save
        save_file_as(msg["path"])
      else
        load_file(msg["path"])
      end
      @pending_file_op = nil
    end
  end

  def save_file_as(path)
    @current_file = path
    save_file
  end

  def on_event(ev)
    super(ev)

    if ev[:type] == :mouse_up
      # File dropdown click handling
      if @file_dropdown_open
        handle_file_dropdown_click(ev[:x], ev[:y])
        return
      end

      # Menu bar click
      if ev[:y] >= @menu_y && ev[:y] < @menu_y + CHAR_H
        if ev[:x] >= @user_area_x0 + 2 && ev[:x] < @user_area_x0 + 2 + 4 * CHAR_W
          open_file_dropdown
          return
        end
      end
    end

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
