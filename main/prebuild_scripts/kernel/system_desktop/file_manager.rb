# File Manager module for SystemDesktopApp
# Provides file browsing, delete, copy, execute, and open-in-editor
#
# Everything here is reachable from the keyboard as well as the mouse: arrows
# move the selection, Enter opens what is selected, and the actions that hide
# behind a right click have their own keys. The context menu prints those keys
# next to its items, since that menu is where they are discovered.

module FileManagerMixin
  # File manager layout
  FMGR_W = 300
  FMGR_H = 206
  FMGR_TITLE_H = 14
  FMGR_ITEM_H = 12
  FMGR_BG = FmrbConst::THEME_WINDOW_BG
  FMGR_TITLE_BG = FmrbConst::THEME_MENU_BG
  FMGR_TEXT = FmrbConst::THEME_TEXT
  FMGR_DIR_COLOR = FmrbConst::THEME_DIR_COLOR
  FMGR_SEL_BG = FmrbConst::THEME_HIGHLIGHT
  FMGR_SIZE_COLOR = 0x49

  # Context menu
  FMGR_CTX_W = 90
  FMGR_CTX_ITEM_H = 14
  FMGR_CTX_BG = FmrbConst::THEME_WINDOW_BG
  FMGR_CTX_BORDER = FmrbConst::THEME_BORDER
  FMGR_CTX_TEXT = FmrbConst::THEME_TEXT
  FMGR_CTX_HL = FmrbConst::THEME_HIGHLIGHT

  FMGR_CTX_ITEMS_FILE = ["Run", "Edit", "Copy", "Delete"]
  FMGR_CTX_ITEMS_DIR = ["Copy", "Delete"]

  # The key that does the same thing without the menu, printed beside each
  # item. Enter runs (and opens a directory), so Run shows that instead of a
  # letter, and Delete stays on the Delete key rather than a letter that could
  # be hit while reaching for something else.
  FMGR_CTX_KEYS = {
    "Run" => "Ent", "Edit" => "E", "Copy" => "C", "Delete" => "Del", "Paste" => "V"
  }

  def open_file_manager
    @file_manager_open = true
    @file_manager_dir = "/"
    @file_manager_entries = []
    @file_manager_scroll = 0
    @file_manager_selected = -1
    @fmgr_pressed_idx = -1
    @fmgr_last_click_idx = -1
    @fmgr_last_click_time = 0
    @fmgr_ctx_open = false
    @fmgr_ctx_x = 0
    @fmgr_ctx_y = 0
    @fmgr_ctx_idx = -1
    @fmgr_copy_path = nil
    close_launcher
    close_dropdown
    scan_file_manager_dir
    notify_overlay_state(true, @fmgr_x, @fmgr_y, FMGR_W, FMGR_H)
    update_composite_regions
    draw_foreground
  end

  def close_file_manager
    return unless @file_manager_open
    @file_manager_open = false
    @fmgr_ctx_open = false
    @ui.set_visible(:fmgr_sb, false)
    @ui.flush
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def scan_file_manager_dir
    @file_manager_entries = []
    @file_manager_entries << { name: "..", is_dir: true, size: 0 } unless @file_manager_dir == "/"

    begin
      dir = Dir.open(@file_manager_dir)
      names = []
      # "ent", not "e": this method also has "rescue => e", and Spinel types a
      # local by name across the whole method -- the loop variable became an
      # Exception, the names array a poly array of exceptions, and sort
      # crashed on the first open (ruby_writing_constraints B).
      while (ent = dir.read)
        names << ent unless ent == "." || ent == ".."
      end
      dir.close

      names.sort.each do |name|
        vpath = @file_manager_dir == "/" ? "/#{name}" : "#{@file_manager_dir}/#{name}"
        is_dir = false
        file_size = 0
        begin
          d = Dir.open(vpath)
          d.close
          is_dir = true
        rescue
          begin
            file_size = File.size(vpath)
          rescue
            file_size = 0
          end
        end
        @file_manager_entries << { name: name, is_dir: is_dir, size: file_size }
      end
    rescue => e
      Log.warn("File manager: cannot scan #{@file_manager_dir}: #{e.message}")
    end

    @file_manager_scroll = 0
    @file_manager_selected = -1
    @fmgr_pressed_idx = -1
    @fmgr_last_click_idx = -1
    @fmgr_last_click_time = 0
  end

  def format_size(bytes)
    if bytes >= 1024 * 1024
      "#{bytes / (1024 * 1024)}MB"
    elsif bytes >= 1024
      "#{bytes / 1024}KB"
    else
      "#{bytes}B"
    end
  end

  def fmgr_selected_virtual_path
    return nil if @file_manager_selected < 0 || @file_manager_selected >= @file_manager_entries.size
    entry = @file_manager_entries[@file_manager_selected]
    return nil if entry[:name] == ".."
    if @file_manager_dir == "/"
      "/#{entry[:name]}"
    else
      "#{@file_manager_dir}/#{entry[:name]}"
    end
  end

  def fmgr_runnable?(name)
    name.end_with?(".rb") || name.end_with?(".lua") ||
      name.end_with?(".bas") || name.end_with?(".py")
  end

  # ---- Drawing ----

  def draw_file_manager
    return unless @file_manager_open

    x = @fmgr_x
    y = @fmgr_y

    # Window frame
    @gfx.fill_rect(x, y, FMGR_W, FMGR_H, FMGR_BG)
    @gfx.draw_rect(x, y, FMGR_W, FMGR_H, FmrbConst::THEME_BORDER)

    # Title bar
    @gfx.fill_rect(x + 1, y + 1, FMGR_W - 2, FMGR_TITLE_H - 1, FMGR_TITLE_BG)
    title = "File: #{@file_manager_dir}"
    max_title_len = (FMGR_W - 60) / 6
    title = title[0, max_title_len] if title.length > max_title_len
    @gfx.draw_text(x + 4, y + 3, title, FmrbConst::THEME_TEXT_LIGHT, FMGR_TITLE_BG)

    # Close button on title bar (small filled circle, matches app windows).
    close_cx = x + FMGR_W - 7
    close_cy = y + FMGR_TITLE_H / 2
    @gfx.fill_circle(close_cx, close_cy, 3, FmrbConst::THEME_TEXT_LIGHT)

    # Column header
    header_y = y + FMGR_TITLE_H + 1
    header_bg = FmrbConst::THEME_HIGHLIGHT
    @gfx.fill_rect(x + 1, header_y, FMGR_W - 2, FMGR_ITEM_H, header_bg)
    @gfx.draw_text(x + 6, header_y + 2, "Name", FMGR_TEXT, header_bg)
    size_col_x = x + FMGR_W - 60
    @gfx.draw_text(size_col_x, header_y + 2, "Size", FMGR_TEXT, header_bg)

    # Copy indicator — themed accent so it pops against the header band.
    if @fmgr_copy_path
      @gfx.draw_text(x + 40, header_y + 2, "[Copy]", FmrbConst::THEME_DIR_COLOR, header_bg)
    end

    # File list
    m = fmgr_list_metrics
    list_y = m[:list_y]
    max_visible = m[:max_visible]

    i = 0
    while i < max_visible
      idx = @file_manager_scroll + i
      break if idx >= @file_manager_entries.size

      entry = @file_manager_entries[idx]
      item_y = list_y + i * FMGR_ITEM_H

      if idx == @file_manager_selected || idx == @fmgr_pressed_idx
        @gfx.fill_rect(x + 2, item_y, FMGR_W - 4, FMGR_ITEM_H, FMGR_SEL_BG)
        text_color = FmrbConst::THEME_TEXT_LIGHT
        text_bg = FMGR_SEL_BG
        size_color = FmrbConst::THEME_TEXT_LIGHT
      else
        text_color = entry[:is_dir] ? FMGR_DIR_COLOR : FMGR_TEXT
        text_bg = FMGR_BG
        size_color = FMGR_SIZE_COLOR
      end

      # Name column
      prefix = entry[:is_dir] ? "[" : " "
      suffix = entry[:is_dir] ? "]" : ""
      label = "#{prefix}#{entry[:name]}#{suffix}"
      max_name_len = (FMGR_W - 72) / 6
      label = label[0, max_name_len] if label.length > max_name_len
      @gfx.draw_text(x + 6, item_y + 2, label, text_color, text_bg)

      # Size column (files only)
      unless entry[:is_dir]
        size_str = format_size(entry[:size])
        @gfx.draw_text(size_col_x, item_y + 2, size_str, size_color, text_bg)
      end
      i += 1
    end

    # One pixel inside, clear of the panel's border (see file_selector).
    @ui.move(:fmgr_sb, x + FMGR_W - 1 - FMGR_SB_W, list_y, FMGR_SB_W, m[:list_h])
    @ui.set_range(:fmgr_sb, @file_manager_entries.size, max_visible)
    @ui.set_value(:fmgr_sb, @file_manager_scroll)
    @ui.set_visible(:fmgr_sb, true)
    @ui.invalidate_all

    # Context menu (drawn on top)
    draw_fmgr_context_menu if @fmgr_ctx_open
  end

  FMGR_SB_W = 10

  def build_file_manager_widgets
    @ui.scrollbar(:fmgr_sb, 0, 0, FMGR_SB_W, 40, 0, 1)
    @ui.set_visible(:fmgr_sb, false)
    nil
  end

  # The file manager moves one entry per press, which is what the bar does
  # too, so its own value can be taken straight.
  def handle_file_manager_widget(id)
    return nil unless id == :fmgr_sb
    handle_file_manager_scroll(@ui.value(:fmgr_sb) - @file_manager_scroll)
    nil
  end

  def draw_fmgr_context_menu
    return unless @fmgr_ctx_open
    items = fmgr_ctx_items
    return if items.empty?

    cx = @fmgr_ctx_x
    cy = @fmgr_ctx_y
    ch = FMGR_CTX_ITEM_H * items.size + 2

    @gfx.fill_rect(cx, cy, FMGR_CTX_W, ch, FMGR_CTX_BG)
    @gfx.draw_rect(cx, cy, FMGR_CTX_W, ch, FMGR_CTX_BORDER)

    items.each_with_index do |label, i|
      iy = cy + 1 + i * FMGR_CTX_ITEM_H
      @gfx.draw_text(cx + 6, iy + 3, label, FMGR_CTX_TEXT, FMGR_CTX_BG)
      key = FMGR_CTX_KEYS[label]
      # Right-aligned, at 6 pixels per character (the system font's advance).
      @gfx.draw_text(cx + FMGR_CTX_W - 4 - key.length * 6, iy + 3, key,
                     FMGR_SIZE_COLOR, FMGR_CTX_BG) if key
    end
  end

  def fmgr_ctx_items
    items = []

    if @fmgr_ctx_idx >= 0 && @fmgr_ctx_idx < @file_manager_entries.size
      entry = @file_manager_entries[@fmgr_ctx_idx]
      return [] if entry[:name] == ".."

      if entry[:is_dir]
        items = FMGR_CTX_ITEMS_DIR.dup
      else
        items = FMGR_CTX_ITEMS_FILE.dup
        items.delete("Run") unless fmgr_runnable?(entry[:name])
      end
    end

    # Paste is available when copy buffer is set (on any right-click)
    items << "Paste" if @fmgr_copy_path
    items
  end

  # ---- Metrics ----

  def fmgr_list_metrics
    header_y = @fmgr_y + FMGR_TITLE_H + 1
    list_y = header_y + FMGR_ITEM_H + 1
    list_h = FMGR_H - FMGR_TITLE_H - FMGR_ITEM_H - 4
    max_visible = list_h / FMGR_ITEM_H
    { list_y: list_y, list_h: list_h, max_visible: max_visible }
  end

  # ---- Click handling ----

  # Index of the list entry under (x, y), or -1 if outside the rows.
  # Excludes scrollbar column and "no entry" gaps so press feedback only
  # highlights real entries.
  def fmgr_entry_at(x, y)
    m = fmgr_list_metrics
    list_y = m[:list_y]
    list_h = m[:list_h]
    max_visible = m[:max_visible]
    return -1 if @ui.find(:fmgr_sb).hit?(x, y)
    return -1 unless y >= list_y && y < list_y + max_visible * FMGR_ITEM_H
    idx = @file_manager_scroll + (y - list_y) / FMGR_ITEM_H
    return -1 if idx < 0 || idx >= @file_manager_entries.size
    idx
  end

  # Called on mouse_down inside the file manager. Highlights the pressed
  # entry until mouse_up so the user gets visual feedback that the click
  # was registered — directories navigate immediately on release, and
  # without this they had no pre-navigation feedback.
  def handle_file_manager_press(x, y)
    return unless @file_manager_open
    return if @fmgr_ctx_open

    new_pressed = fmgr_entry_at(x, y)
    if new_pressed != @fmgr_pressed_idx
      @fmgr_pressed_idx = new_pressed
      draw_foreground
    end
  end

  def handle_file_manager_click(x, y)
    # Mouse released — drop the press highlight regardless of where the
    # release landed. Redraw if needed; later branches may redraw again
    # but the extra draw is cheap relative to the user-visible feedback.
    had_pressed = @fmgr_pressed_idx >= 0
    @fmgr_pressed_idx = -1

    # Context menu takes priority
    if @fmgr_ctx_open
      if hit_fmgr_context_menu?(x, y)
        handle_fmgr_context_click(x, y)
      else
        @fmgr_ctx_open = false
        draw_foreground
      end
      return
    end

    # Do NOT clear the press highlight with its own redraw here: selecting a row
    # below sets @file_manager_selected to the same row, so it stays highlighted
    # (pressed -> selected) with no intermediate un-highlighted frame. Drawing
    # that intermediate frame made the selection highlight blink on a tap. The
    # fall-through at the end of this method clears a stray press highlight when
    # the click did not land on a row.

    fx = @fmgr_x
    fy = @fmgr_y

    # Close button hit zone — square around the circle drawn in
    # draw_file_manager. Slightly larger than the circle for tap tolerance.
    close_cx = fx + FMGR_W - 7
    close_cy = fy + FMGR_TITLE_H / 2
    if x >= close_cx - 5 && x <= close_cx + 5 &&
       y >= close_cy - 5 && y <= close_cy + 5
      close_file_manager
      return
    end

    m = fmgr_list_metrics
    list_y = m[:list_y]
    max_visible = m[:max_visible]
    total = @file_manager_entries.size

    # File list area
    if y >= list_y && y < list_y + max_visible * FMGR_ITEM_H
      idx = @file_manager_scroll + (y - list_y) / FMGR_ITEM_H
      if idx >= 0 && idx < total
        # One click selects, two activate -- the same rule as the file selector
        # (file_selector.rb). Navigating a directory on a single click meant a
        # reflex double-click entered the directory and then acted on whatever
        # row happened to sit under the pointer in the fresh listing.
        now = @counter
        double = (idx == @fmgr_last_click_idx) && ((now - @fmgr_last_click_time) < 5)
        @fmgr_last_click_idx = idx
        @fmgr_last_click_time = now
        fmgr_select(idx)
        return unless double

        fmgr_activate
      end
    end
    # The click did not select or activate a row (empty space, etc.). Clear a
    # stray press highlight left from mouse-down. (Row selection above keeps the
    # row highlighted and returns, so it never reaches here.)
    draw_foreground if had_pressed
  end

  def handle_file_manager_right_click(x, y)
    # Close existing context menu first
    @fmgr_ctx_open = false

    m = fmgr_list_metrics
    list_y = m[:list_y]
    max_visible = m[:max_visible]

    # Check if right-click is on a file entry
    idx = -1
    if y >= list_y && y < list_y + max_visible * FMGR_ITEM_H
      tmp = @file_manager_scroll + (y - list_y) / FMGR_ITEM_H
      if tmp >= 0 && tmp < @file_manager_entries.size
        entry = @file_manager_entries[tmp]
        idx = tmp unless entry[:name] == ".."
      end
    end

    @fmgr_ctx_idx = idx
    @file_manager_selected = idx if idx >= 0

    items = fmgr_ctx_items
    return if items.empty?

    @fmgr_ctx_x = x
    @fmgr_ctx_y = y
    # Clamp context menu within file manager window
    ctx_h = FMGR_CTX_ITEM_H * items.size + 2
    if @fmgr_ctx_x + FMGR_CTX_W > @fmgr_x + FMGR_W
      @fmgr_ctx_x = @fmgr_x + FMGR_W - FMGR_CTX_W
    end
    if @fmgr_ctx_y + ctx_h > @fmgr_y + FMGR_H
      @fmgr_ctx_y = @fmgr_y + FMGR_H - ctx_h
    end
    @fmgr_ctx_open = true
    draw_foreground
  end

  def hit_fmgr_context_menu?(x, y)
    return false unless @fmgr_ctx_open
    items = fmgr_ctx_items
    ch = FMGR_CTX_ITEM_H * items.size + 2
    x >= @fmgr_ctx_x && x < @fmgr_ctx_x + FMGR_CTX_W &&
      y >= @fmgr_ctx_y && y < @fmgr_ctx_y + ch
  end

  def handle_fmgr_context_click(x, y)
    items = fmgr_ctx_items
    item_idx = (y - @fmgr_ctx_y - 1) / FMGR_CTX_ITEM_H
    @fmgr_ctx_open = false

    return if item_idx < 0 || item_idx >= items.size

    action = items[item_idx]
    case action
    when "Run"
      fmgr_run_file(@fmgr_ctx_idx)
    when "Edit"
      fmgr_edit_file(@fmgr_ctx_idx)
    when "Copy"
      fmgr_copy_file(@fmgr_ctx_idx)
    when "Delete"
      fmgr_delete_file(@fmgr_ctx_idx)
    when "Paste"
      fmgr_paste_file
    end
  end

  # ---- Selection, and what taking it means ----
  #
  # The mouse and the keyboard both come through here, so a double click and
  # Enter cannot drift apart.

  def fmgr_selected_entry
    return nil if @file_manager_selected < 0 ||
                  @file_manager_selected >= @file_manager_entries.size
    @file_manager_entries[@file_manager_selected]
  end

  def fmgr_select(idx)
    return if idx == @file_manager_selected
    @file_manager_selected = idx
    draw_foreground
  end

  # A directory opens, a runnable file runs. Anything else has no obvious
  # single meaning, so it stays selected and waits for a named action.
  def fmgr_activate
    entry = fmgr_selected_entry
    return unless entry
    # A fresh listing means the remembered row is meaningless.
    @fmgr_last_click_idx = -1
    if entry[:is_dir]
      fmgr_navigate_dir(entry[:name])
    else
      fmgr_open_file(@file_manager_selected)
    end
    # Explicit: the branches end in a FmrbGfx (present) and a void leaf, which
    # Spinel cannot give one return type (ruby_writing_constraints B).
    nil
  end

  def fmgr_go_up
    fmgr_navigate_dir("..") unless @file_manager_dir == "/"
  end

  def fmgr_move_selection(delta)
    return if @file_manager_entries.size == 0
    from = @file_manager_selected
    if from < 0
      from = delta > 0 ? -1 : 0
    end
    fmgr_move_to(from + delta)
  end

  def fmgr_move_to(idx)
    size = @file_manager_entries.size
    return if size == 0
    idx = 0 if idx < 0
    idx = size - 1 if idx >= size
    fmgr_reveal(idx)
    fmgr_select(idx)
  end

  # Scroll just enough to bring a row into view.
  def fmgr_reveal(idx)
    max_visible = fmgr_list_metrics[:max_visible]
    scroll = @file_manager_scroll
    if idx < scroll
      scroll = idx
    elsif idx >= scroll + max_visible
      scroll = idx - max_visible + 1
    end
    scroll = 0 if scroll < 0
    return if scroll == @file_manager_scroll
    @file_manager_scroll = scroll
    draw_foreground
  end

  # ---- Keyboard ----
  #
  # Scancodes, not keycodes: a scancode is the HID usage ID on the device and
  # the SDL scancode in the simulator, and those are the same numbers, while a
  # keycode is a character on one and an SDL keysym on the other. The letters
  # below do come from the character, since that is what a letter is.
  def handle_file_manager_key(scancode, character)
    if @fmgr_ctx_open
      # The menu is in the way of everything: the first key takes it down, and
      # only Esc is spent doing that -- the rest act on the row underneath.
      # Tab is spent too, so the key that opens the menu also closes it.
      @fmgr_ctx_open = false
      draw_foreground
      return if scancode == FmrbConst::KEY_ESC || scancode == FmrbConst::KEY_TAB
    end

    case scancode
    when FmrbConst::KEY_UP    then fmgr_move_selection(-1)
    when FmrbConst::KEY_DOWN  then fmgr_move_selection(1)
    when FmrbConst::KEY_PGUP  then fmgr_move_selection(-fmgr_list_metrics[:max_visible])
    when FmrbConst::KEY_PGDN  then fmgr_move_selection(fmgr_list_metrics[:max_visible])
    when FmrbConst::KEY_HOME  then fmgr_move_to(0)
    when FmrbConst::KEY_END   then fmgr_move_to(@file_manager_entries.size - 1)
    when FmrbConst::KEY_ENTER then fmgr_activate
    when FmrbConst::KEY_TAB   then fmgr_open_context_for_selection
    when FmrbConst::KEY_ESC   then close_file_manager
    when FmrbConst::KEY_RIGHT
      entry = fmgr_selected_entry
      fmgr_activate if entry && entry[:is_dir]
    when FmrbConst::KEY_LEFT, FmrbConst::KEY_BACKSPACE then fmgr_go_up
    when FmrbConst::KEY_DELETE then fmgr_delete_file(@file_manager_selected)
    else
      # The context menu's actions, on the initials it prints beside them.
      # Upper and lower case both, since Shift should not matter here.
      if character == 101 || character == 69        # e / E
        fmgr_edit_file(@file_manager_selected)
      elsif character == 99 || character == 67      # c / C
        fmgr_copy_file(@file_manager_selected)
      elsif character == 118 || character == 86     # v / V
        fmgr_paste_file
      end
    end
    nil  # same reason as fmgr_activate
  end

  # Open the actions menu on the selected row, the one a right click reaches.
  # Keyboard only has to get here: the menu prints the key that does each
  # action beside it, so it is read and then acted on with those keys. A touch
  # screen has no right button at all, which makes this the only way to Copy
  # and Paste on a Tab5.
  def fmgr_open_context_for_selection
    idx = @file_manager_selected
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    return if entry[:name] == ".."

    @fmgr_ctx_idx = idx
    items = fmgr_ctx_items
    return if items.empty?

    # Anchored under the selected row rather than at the pointer, which is
    # wherever the mouse was last left.
    m = fmgr_list_metrics
    row = idx - @file_manager_scroll
    row = 0 if row < 0
    @fmgr_ctx_x = @fmgr_x + FMGR_W / 3
    @fmgr_ctx_y = m[:list_y] + (row + 1) * FMGR_ITEM_H
    ctx_h = FMGR_CTX_ITEM_H * items.size + 2
    if @fmgr_ctx_y + ctx_h > @fmgr_y + FMGR_H
      @fmgr_ctx_y = @fmgr_y + FMGR_H - ctx_h
    end
    @fmgr_ctx_open = true
    draw_foreground
  end

  # ---- File operations ----

  def fmgr_navigate_dir(name)
    if name == ".."
      # join can come out empty at the first level ("/bin" -> ["", "bin"] ->
      # [""]), and [""] is not empty -- so the test has to be on the joined
      # string, not on the array. Getting that wrong left the directory as ""
      # rather than "/", which listed the root with a "[..]" row above it.
      parts = @file_manager_dir.split("/")
      parts.pop
      up = parts.join("/")
      @file_manager_dir = up.empty? ? "/" : up
    else
      if @file_manager_dir == "/"
        @file_manager_dir = "/#{name}"
      else
        @file_manager_dir = "#{@file_manager_dir}/#{name}"
      end
    end
    scan_file_manager_dir
    draw_foreground
  end

  def fmgr_run_file(idx)
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    vpath = fmgr_selected_virtual_path
    return unless vpath

    file_path = vpath
    Log.info("File manager: run #{file_path}")
    close_file_manager
    spawn_app(file_path)
  end

  def fmgr_edit_file(idx)
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    return if entry[:is_dir]

    vpath = fmgr_selected_virtual_path
    return unless vpath
    file_path = vpath

    Log.info("File manager: edit #{file_path}")
    close_file_manager
    # The kernel hands the path over once the editor can receive it. This used
    # to spawn and then count update cycles here before sending the message
    # itself, which was the same job done worse: the count was a guess, and it
    # was the only caller in the machine still doing it.
    spawn_app("default/editor", file_path)
    nil
  end

  # Double click, or Enter: open the file the way the association table says
  # to (FmrbAssoc). "run" spawns the file itself, "edit" opens the editor, and
  # an app path spawns that app with the file handed to it. The right-click
  # menu keeps its own Run and Edit -- those are the user saying which, and
  # the table is only consulted when they have not.
  def fmgr_open_file(idx)
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    return if entry[:is_dir]
    vpath = fmgr_selected_virtual_path
    return unless vpath

    action = FmrbAssoc.resolve(vpath)
    if action == FmrbAssoc::RUN
      fmgr_run_file(idx)
      return nil
    end
    if action == FmrbAssoc::EDIT
      fmgr_edit_file(idx)
      return nil
    end
    Log.info("File manager: open #{vpath} with #{action}")
    close_file_manager
    spawn_app(action, vpath)
    nil
  end

  def fmgr_copy_file(idx)
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    vpath = if @file_manager_dir == "/"
              "/#{entry[:name]}"
            else
              "#{@file_manager_dir}/#{entry[:name]}"
            end
    @fmgr_copy_path = vpath
    @fmgr_copy_is_dir = entry[:is_dir]
    Log.info("File manager: copied #{vpath}")
    draw_foreground
  end

  def fmgr_paste_file
    return unless @fmgr_copy_path
    return if @fmgr_copy_is_dir  # Directory copy not supported yet

    src_name = @fmgr_copy_path.split("/").last
    dest_vpath = if @file_manager_dir == "/"
                   "/#{src_name}"
                 else
                   "#{@file_manager_dir}/#{src_name}"
                 end

    src_file = @fmgr_copy_path
    dest_file = dest_vpath

    if src_file == dest_file
      Log.warn("File manager: cannot copy to same location")
      return
    end

    begin
      f = File.open(src_file, "r")
      data = f.read
      f.close

      f = File.open(dest_file, "w")
      f.write(data)
      f.close

      Log.info("File manager: pasted #{src_file} -> #{dest_file}")
      @fmgr_copy_path = nil
      scan_file_manager_dir
      draw_foreground
    rescue => e
      Log.error("File manager: paste failed: #{e.message}")
    end
    nil  # begin arm ends in a void-typed call (Spinel) -> pin a concrete return
  end

  def fmgr_delete_file(idx)
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    return if entry[:name] == ".."

    vpath = if @file_manager_dir == "/"
              "/#{entry[:name]}"
            else
              "#{@file_manager_dir}/#{entry[:name]}"
            end
    file_path = vpath

    begin
      File.unlink(file_path)
      Log.info("File manager: deleted #{file_path}")
      scan_file_manager_dir
      draw_foreground
    rescue => e
      Log.error("File manager: delete failed: #{e.message}")
    end
    nil  # begin arm ends in a void-typed call (Spinel) -> pin a concrete return
  end

  # ---- Scroll ----

  def handle_file_manager_scroll(direction)
    return unless @file_manager_open
    m = fmgr_list_metrics
    total = @file_manager_entries.size

    if direction > 0 && @file_manager_scroll + m[:max_visible] < total
      @file_manager_scroll += 1
      draw_foreground
    elsif direction < 0 && @file_manager_scroll > 0
      @file_manager_scroll -= 1
      draw_foreground
    end
  end

  # ---- Hit test ----

  def hit_file_manager?(x, y)
    return false unless @file_manager_open
    # Include context menu area
    if @fmgr_ctx_open && hit_fmgr_context_menu?(x, y)
      return true
    end
    x >= @fmgr_x && x < @fmgr_x + FMGR_W &&
      y >= @fmgr_y && y < @fmgr_y + FMGR_H
  end
end
