# Keyboard input for the editor: character entry / UTF-8 assembly / indent /
# enter / backspace / delete, and the arrow/page/home/end navigation.
# Split out of editor.app.rb (doc/editor_refactor). self.class::ASCII_PRINTABLE and
# self.class::TAB_SIZE are the editor body's; reached as self.class::NAME.
module EditorInput

  # ---- Key handling ----

  def handle_key(ch)
    if ch >= 0x80
      s = utf8_feed(ch)
      insert_char(s) if s
      return
    end
    utf8_reset
    case ch
    when 10, 13  # Enter
      handle_enter
    when 8       # Backspace
      handle_backspace
    when 127     # Delete (some terminals)
      handle_delete
    when 9       # Tab
      insert_indent
    when 32..126 # Printable
      insert_char(printable_char(ch))
      # "(" and "," are where the answer to "what goes here" changes, so they
      # are the only keys that ask for it.
      show_signature_help if ch == 40 || ch == 44
    end
  end

  # Feed one byte of a possibly multi-byte character. Returns the finished
  # character as a String, or nil while more bytes are needed. Used by both
  # the document and the Find field, so Japanese can be typed into either.
  def utf8_feed(byte)
    if @u8_need == 0
      if byte >= 0xF0
        need = 3
      elsif byte >= 0xE0
        need = 2
      elsif byte >= 0xC0
        need = 1
      else
        return nil  # continuation byte with nothing in front of it
      end
      @u8_buf = one_byte_string(byte)
      @u8_need = need
      return nil
    end
    # A byte that is not a continuation means the sequence was cut short.
    # Drop what we had and start over with this byte.
    if byte < 0x80 || byte >= 0xC0
      utf8_reset
      return utf8_feed(byte)
    end
    @u8_buf += one_byte_string(byte)
    @u8_need -= 1
    return nil if @u8_need > 0
    s = @u8_buf
    utf8_reset
    s
  end

  def utf8_reset
    @u8_buf = ""
    @u8_need = 0
  end

  # One-character String holding +byte+ (no Array#pack in picoruby).
  def one_byte_string(byte)
    s = " ".dup
    s.setbyte(0, byte)
    s
  end

  # One-character String for a printable ASCII code ("" outside 32..126).
  def printable_char(code)
    return "" if code < 32 || code > 126
    self.class::ASCII_PRINTABLE[code - 32, 1]
  end

  def insert_indent
    ti = 0
    while ti < self.class::TAB_SIZE
      insert_char(' ')
      ti += 1
    end
  end

  def insert_char(c)
    delete_selection if has_selection?
    nx = EditorCore.insert_text(@cy, @cx, c)
    if nx < 0
      doc_full
      return
    end
    @cx = nx
    mark_edited
    ensure_cursor_visible
    mark_dirty_line(@cy)
  end

  def handle_enter
    delete_selection if has_selection?
    if EditorCore.split_line(@cy, @cx) < 0
      doc_full
      return
    end
    # Everything below the split shifted down one row.
    mark_dirty_from(@cy)
    @cy += 1
    @cx = 0
    mark_edited
    ensure_cursor_visible
  end

  def handle_backspace
    return if delete_selection
    if @cx > 0
      EditorCore.delete_char(@cy, @cx - 1)
      @cx -= 1
      mark_edited
      ensure_cursor_visible
      mark_dirty_line(@cy)
    elsif @cy > 0
      # Merge with previous line; join_line returns where the cursor belongs.
      prev_len = EditorCore.join_line(@cy - 1)
      return if prev_len < 0
      @cy -= 1
      @cx = prev_len
      mark_edited
      ensure_cursor_visible
      # The merged line and everything that shifted up.
      mark_dirty_from(@cy)
    end
  end

  def handle_delete
    return if delete_selection
    if @cx < EditorCore.line_length(@cy)
      EditorCore.delete_char(@cy, @cx)
      mark_edited
      mark_dirty_line(@cy)
    elsif @cy < EditorCore.line_count - 1
      # Merge next line
      EditorCore.join_line(@cy)
      mark_edited
      mark_dirty_from(@cy)
    end
  end

  # ---- Navigation (arrow keys, page up/down, home/end) ----

  def execute_key_action(keycode)
    case keycode
    when 79 then move_right
    when 80 then move_left
    when 81 then move_down
    when 82 then move_up
    when 75 then page_up
    when 78 then page_down
    when 74 then move_home
    when 77 then move_end
    when 76 then handle_delete  # Delete forward key
    end
  end

  # Cursor moves repaint the line the cursor left and the one it arrived on
  # (a selection being extended covers the lines in between, hence the range).
  # A move that scrolls is upgraded to a full repaint inside
  # ensure_cursor_visible.

  # Up and down move by screen row, not by logical line: inside a folded line
  # that means the previous or next segment, and the column is kept as a cell
  # position so the cursor tracks down the page the way it looks like it should.
  def move_up
    if @wrap_on
      seg = segment_of(@cy, @cx)
      if seg > 0
        move_cursor_to_segment(@cy, seg - 1)
        return
      end
      if @cy > 0
        move_cursor_to_segment(@cy - 1, line_segments(@cy - 1) - 1)
      end
      return
    end
    if @cy > 0
      @cy -= 1
      clamp_cx
      ensure_cursor_visible
      mark_dirty_range(@cy, @cy + 1)
    end
  end

  def move_down
    if @wrap_on
      seg = segment_of(@cy, @cx)
      if seg + 1 < line_segments(@cy)
        move_cursor_to_segment(@cy, seg + 1)
        return
      end
      if @cy < EditorCore.line_count - 1
        move_cursor_to_segment(@cy + 1, 0)
      end
      return
    end
    if @cy < EditorCore.line_count - 1
      @cy += 1
      clamp_cx
      ensure_cursor_visible
      mark_dirty_range(@cy - 1, @cy)
    end
  end

  def move_left
    if @cx > 0
      @cx -= 1
      ensure_cursor_visible
      mark_dirty_line(@cy)
    elsif @cy > 0
      @cy -= 1
      @cx = EditorCore.line_length(@cy)
      ensure_cursor_visible
      mark_dirty_range(@cy, @cy + 1)
    end
  end

  def move_right
    if @cx < EditorCore.line_length(@cy)
      @cx += 1
      ensure_cursor_visible
      mark_dirty_line(@cy)
    elsif @cy < EditorCore.line_count - 1
      @cy += 1
      @cx = 0
      ensure_cursor_visible
      mark_dirty_range(@cy - 1, @cy)
    end
  end

  def page_up
    if @wrap_on
      move_cursor_rows(-@edit_rows)
      return
    end
    prev_cy = @cy
    @cy -= @edit_rows
    @cy = 0 if @cy < 0
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  def page_down
    if @wrap_on
      move_cursor_rows(@edit_rows)
      return
    end
    prev_cy = @cy
    @cy += @edit_rows
    max = EditorCore.line_count - 1
    @cy = max if @cy > max
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  def move_home
    @cx = 0
    ensure_cursor_visible
    mark_dirty_line(@cy)
  end

  def move_end
    @cx = EditorCore.line_length(@cy)
    ensure_cursor_visible
    mark_dirty_line(@cy)
  end

  # Cell column of the cursor within its own segment. Vertical movement aims
  # for the same one on the target segment, which is what makes the cursor look
  # like it goes straight down.
  def cursor_cell_column
    seg = segment_of(@cy, @cx)
    col0 = segment_start(@cy, seg)
    widths = EditorCore.render_width(@cy, col0, @edit_cols)
    cell_offset(widths, @cx - col0)
  end

  # Character index in (y, seg) nearest to cell column +want+.
  def char_at_cell(y, seg, want)
    col0 = segment_start(y, seg)
    limit = segment_chars(y, seg)
    widths = EditorCore.render_width(y, col0, @edit_cols)
    n = widths.bytesize
    n = limit if limit < n
    cells = 0
    i = 0
    while i < n
      w = widths.getbyte(i)
      break if cells + w > want
      cells += w
      i += 1
    end
    col0 + i
  end

  def move_cursor_to_segment(y, seg)
    want = cursor_cell_column
    prev_cy = @cy
    @cy = y
    @cx = char_at_cell(y, seg, want)
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  # Move the cursor +rows+ screen rows, following segments across lines.
  def move_cursor_rows(rows)
    want = cursor_cell_column
    prev_cy = @cy
    y = @cy
    seg = segment_of(@cy, @cx)
    step = rows > 0 ? 1 : -1
    n = rows > 0 ? rows : -rows
    i = 0
    while i < n
      if step > 0
        if seg + 1 < line_segments(y)
          seg += 1
        else
          break if y + 1 >= EditorCore.line_count
          y += 1
          seg = 0
        end
      else
        if seg > 0
          seg -= 1
        else
          break if y == 0
          y -= 1
          seg = line_segments(y) - 1
        end
      end
      i += 1
    end
    @cy = y
    @cx = char_at_cell(y, seg, want)
    clamp_cx
    ensure_cursor_visible
    mark_dirty_range(prev_cy, @cy)
  end

  def clamp_cx
    len = EditorCore.line_length(@cy)
    @cx = len if @cx > len
  end

end
