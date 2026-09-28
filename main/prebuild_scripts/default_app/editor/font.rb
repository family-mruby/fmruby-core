# The edit area's font: which one, what it does to the grid, and where the
# choice is kept (View > Font, /home/editor.toml).
#
# The edit area is a grid of cells. A half-width character is one cell and a
# full-width one two, in every font offered here, so changing the font changes
# three numbers and nothing else: the size handed to set_font, the width of a
# cell and the height of a row. They live in @edit_font_size / @cell_w /
# @line_h, and every piece of edit-area geometry is computed from those.
#
# Only fonts the display already carries are offered (no new font data: the
# Modern flash is nearly full). The chrome -- menu bar, status line, dialogs --
# stays on the 6x8 default font whatever is chosen here.
#
# The choice is one setting for the window and the fullscreen alike, kept in
# its own file rather than in /home/colors.toml, which is a colour table.
#
# Written into EditorMenu (reopened here) rather than a module of its own, and
# with the sizes in a method rather than in constant arrays. In the Spinel
# build every module costs a pool counter pair and every non-integer constant
# a pointer, all in internal RAM's small-data section, and the budget there is
# "not one byte more" (doc/fullscreen_hires/report/h3.md).
module EditorMenu
  include EditorConst

  # The font sizes offered, in menu order (small to large): misaki_8,
  # efontJA_12, efontJA_16. Each is one set_font(:ja, n) the display carries.
  def edit_font_size_at(idx)
    case idx
    when 0 then 8
    when 2 then 16
    else 12
    end
  end

  # Font size -> menu index, or -1 for a size that is not offered.
  def edit_font_index(size)
    i = 0
    while i < EDIT_FONT_COUNT
      return i if edit_font_size_at(i) == size
      i += 1
    end
    -1
  end

  # Take a font size into the three numbers the layout uses. A size that is not
  # offered falls back to the default, so a hand-edited file cannot leave the
  # grid on a font the display does not have. All three fonts are exactly 1:2
  # (half-width : full-width) and as tall as their size, so the cell is half
  # the size and the row is the size.
  def apply_edit_font(size)
    s = edit_font_index(size) < 0 ? EDIT_FONT_DEFAULT : size
    @edit_font_size = s
    @cell_w = s / 2
    @line_h = s
  end

  # ---- /home/editor.toml ----
  #
  #   [editor]
  #   font = 12
  #
  # Read once at start. A missing or unreadable file, a missing key or a size
  # that is not offered all mean the default. Parsed by hand, like the colour
  # table: no TOML library on the Spinel side, and one key does not need one.

  def read_editor_conf
    text = nil
    begin
      f = File.open("/home/editor.toml", "r")
      text = f.read
      f.close
    rescue => err
      return ""
    end
    text.to_s
  end

  # The integer value of +key+ under [editor], or -1 when there is none (an
  # Integer either way, so the caller's type is settled in both engines).
  def editor_conf_int(text, key)
    return -1 if text.length == 0
    inside = false
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i].to_s.strip
      i += 1
      next if line.length == 0
      next if line.start_with?("#")
      if line.start_with?("[")
        inside = (line == "[editor]")
        next
      end
      next unless inside
      eq = line.index("=")
      next if eq.nil?
      k = line[0, eq].to_s.strip
      next if k != key
      v = line[eq + 1, line.length - eq - 1].to_s.strip
      cut = v.index("#")
      v = v[0, cut].to_s.strip unless cut.nil?
      return -1 if v.length == 0
      return -1 unless digits_only?(v)
      return v.to_i
    end
    -1
  end

  def digits_only?(s)
    i = 0
    n = s.bytesize
    while i < n
      b = s.getbyte(i)
      return false if b < 48 || b > 57
      i += 1
    end
    n > 0
  end

  def load_editor_conf
    text = read_editor_conf
    apply_edit_font(editor_conf_int(text, "font"))
    Log.info("Editor font: #{@edit_font_size}")
  end

  # Write the setting back. The rest of the file is kept as it is -- lines of
  # other sections, comments, keys this version does not know -- and only the
  # font line under [editor] is replaced, or added when there is none.
  def save_editor_conf
    old = read_editor_conf
    out = []
    lines = old.length > 0 ? old.split("\n") : []
    inside = false
    seen_section = false
    written = false
    font_line = "font = #{@edit_font_size}"
    i = 0
    while i < lines.size
      raw = lines[i].to_s
      line = raw.strip
      i += 1
      if line.start_with?("[")
        # Leaving [editor] without having met the key: put it at the end of
        # the section, before the next header.
        if inside && !written
          out << font_line
          written = true
        end
        inside = (line == "[editor]")
        seen_section = true if inside
        out << raw
        next
      end
      if inside && !written
        # Locals rather than one condition: a negated call inside an if is
        # one of the shapes the Spinel generator emits broken C for.
        comment = line.start_with?("#")
        eq = line.index("=")
        is_font = false
        is_font = (line[0, eq].to_s.strip == "font") unless comment || eq.nil?
        if is_font
          out << font_line
          written = true
          next
        end
      end
      out << raw
    end
    unless written
      out << "[editor]" unless seen_section
      out << font_line
    end
    body = ""
    i = 0
    while i < out.size
      body = body + out[i].to_s + "\n"
      i += 1
    end
    begin
      f = File.open("/home/editor.toml", "w")
      f.write(body)
      f.close
    rescue => err
      Log.error("Cannot write /home/editor.toml: #{err.message}")
      return false
    end
    true
  end

  # ---- View > Font ----

  def menu_font_items
    items = []
    i = 0
    while i < EDIT_FONT_COUNT
      mark = edit_font_size_at(i) == @edit_font_size ? "(*) " : "( ) "
      items << mark + font_label(i)
      i += 1
    end
    items
  end

  def font_label(idx)
    case idx
    when 0 then FmrbI18n.t(:font_small).to_s
    when 1 then FmrbI18n.t(:font_standard).to_s
    else FmrbI18n.t(:font_large).to_s
    end
  end

  # Menu action: switch the edit area to font +idx+ now and remember it.
  def select_edit_font(idx)
    return if idx < 0 || idx >= EDIT_FONT_COUNT
    size = edit_font_size_at(idx)
    if size != @edit_font_size
      apply_edit_font(size)
      relayout_for_font
      Log.info("Editor font -> #{@edit_font_size}")
    end
    ok = save_editor_conf
    flash_status(FmrbI18n.t(:b_font_save_failed).to_s) unless ok
  end

  # Everything the grid decides, again: columns, rows, where wrapped lines
  # break and so which segment the view starts on. The anchor keeps its line;
  # its segment is clamped, since a wider grid folds a line into fewer of them.
  def relayout_for_font
    recompute_layout
    if @wrap_on
      segs = line_segments(@scroll_y)
      @anchor_seg = segs - 1 if @anchor_seg >= segs
      @cur_line_segs = line_segments(@cy)
    end
    ensure_cursor_visible
    fill_view_bottom
    @need_redraw = true
  end

  # A smaller font leaves rows below the end of the document empty while lines
  # above the anchor are hidden. Pull the anchor up by the empty rows: the
  # cursor stays on screen (it only moves down), and the window shows as much
  # of the file as it can.
  def fill_view_bottom
    rows = 0
    ly = @scroll_y
    ls = @anchor_seg
    n = EditorCore.line_count
    while rows < @edit_rows && ly < n
      rows += 1
      ls += 1
      if ls >= line_segments(ly)
        ly += 1
        ls = 0
      end
    end
    move_anchor(rows - @edit_rows) if rows < @edit_rows
  end
end
