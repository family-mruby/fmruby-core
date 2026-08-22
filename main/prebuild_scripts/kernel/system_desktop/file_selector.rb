# File Selector module for SystemDesktopApp
# Provides file open/save dialog with directory navigation
#
# The dialog answers to the mouse and to the keyboard, and neither is a
# second-class way in: arrows move the selection, Enter takes it (into a
# directory, or out of the dialog with a file), Esc cancels. The kernel already
# points the keyboard at the desktop while this is up (fmrb_kernel.rb,
# "file_select"), which it did so a filename could be typed in save mode; the
# same redirection is what makes the rest of the keys reachable.

module FileSelectorMixin
  # File selector layout
  FSEL_W = 260
  FSEL_H = 200
  FSEL_TITLE_H = 14
  FSEL_ITEM_H = 12
  FSEL_BG = FmrbConst::THEME_WINDOW_BG
  FSEL_TITLE_BG = FmrbConst::THEME_MENU_BG
  FSEL_TEXT = FmrbConst::THEME_TEXT
  FSEL_DIR_COLOR = FmrbConst::THEME_DIR_COLOR
  FSEL_SEL_BG = FmrbConst::THEME_HIGHLIGHT
  FSEL_CANCEL_BG = FmrbConst::THEME_BUTTON

  def open_file_selector(requester_pid, mode = "open")
    @file_selector_open = true
    @file_selector_mode = mode
    @file_selector_requester = requester_pid
    @file_selector_dir = "/"
    @file_selector_scroll = 0
    @file_selector_selected = -1
    @file_selector_filename = ""
    # Double-click state, same shape as the launcher's: one click selects, two
    # activate. Measured in on_update ticks, not wall clock.
    @fsel_click_idx = -1
    @fsel_click_time = 0
    close_launcher
    close_dropdown
    scan_file_selector_dir
    notify_overlay_state(true, @fsel_x, @fsel_y, FSEL_W, FSEL_H)
    update_composite_regions
    draw_foreground
  end

  def close_file_selector(selected_path = nil)
    return unless @file_selector_open
    mode = @file_selector_mode
    @file_selector_open = false

    # Send result back to kernel
    data = {
      "cmd" => "file_select_result",
      "target_pid" => @file_selector_requester,
      "path" => selected_path,
      "mode" => mode
    }
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)

    @file_selector_requester = nil
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def scan_file_selector_dir
    @file_selector_entries = []
    # Add parent directory entry unless at root
    @file_selector_entries << { name: "..", is_dir: true } unless @file_selector_dir == "/"

    begin
      dir = Dir.open(@file_selector_dir)
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
        vpath = @file_selector_dir == "/" ? "/#{name}" : "#{@file_selector_dir}/#{name}"
        is_dir = false
        begin
          d = Dir.open(vpath)
          d.close
          is_dir = true
        rescue
        end
        @file_selector_entries << { name: name, is_dir: is_dir }
      end
    rescue => e
      Log.warn("File selector: cannot scan #{@file_selector_dir}: #{e.message}")
    end

    @file_selector_scroll = 0
    @file_selector_selected = -1
  end

  # ---- Layout ----

  # Where the list sits and how many rows fit. Drawing, clicking and the
  # keyboard all need the same three numbers, and a keyboard selection that
  # scrolls needs max_visible in particular.
  def fsel_list_metrics
    bottom_y = @fsel_y + FSEL_H - 32
    list_y = @fsel_y + FSEL_TITLE_H + 2
    list_h = bottom_y - list_y - 2
    { bottom_y: bottom_y, list_y: list_y, list_h: list_h,
      max_visible: list_h / FSEL_ITEM_H }
  end

  def draw_file_selector
    return unless @file_selector_open

    x = @fsel_x
    y = @fsel_y

    # Window
    @gfx.fill_rect(x, y, FSEL_W, FSEL_H, FSEL_BG)
    @gfx.draw_rect(x, y, FSEL_W, FSEL_H, FmrbConst::THEME_BORDER)

    # Title bar
    @gfx.fill_rect(x + 1, y + 1, FSEL_W - 2, FSEL_TITLE_H - 1, FSEL_TITLE_BG)
    # The dialog does two jobs and looked like one: the title said "Open" while
    # the Save button and the name field sat below it.
    verb = @file_selector_mode == "save" ? "Save" : "Open"
    title = "#{verb}: #{@file_selector_dir}"
    title = title[0, FSEL_W / 6 - 2] if title.length > FSEL_W / 6 - 2
    @gfx.draw_text(x + 4, y + 3, title, FmrbGfx::WHITE, FSEL_TITLE_BG)

    # Bottom area: filename input (save mode) + buttons
    metrics = fsel_list_metrics
    bottom_y = metrics[:bottom_y]

    if @file_selector_mode == "save"
      # Filename label and input field
      @gfx.draw_text(x + 4, bottom_y, "Name:", FSEL_TEXT, FSEL_BG)
      # Input field
      field_x = x + 40
      field_w = FSEL_W - 100
      @gfx.fill_rect(field_x, bottom_y - 1, field_w, 10, FmrbGfx::WHITE)
      @gfx.draw_rect(field_x, bottom_y - 1, field_w, 10, FmrbConst::THEME_BORDER)
      @gfx.draw_text(field_x + 2, bottom_y, @file_selector_filename, FSEL_TEXT, FmrbGfx::WHITE)
      # Cursor
      cursor_x = field_x + 2 + @file_selector_filename.length * 6
      @gfx.draw_line(cursor_x, bottom_y, cursor_x, bottom_y + 7, FSEL_TEXT)

      # Save button
      save_x = x + FSEL_W - 100
      save_y = bottom_y + 14
      @gfx.fill_rect(save_x, save_y, 40, 12, FSEL_TITLE_BG)
      @gfx.draw_text(save_x + 6, save_y + 2, "Save", FmrbGfx::WHITE, FSEL_TITLE_BG)
    end

    # Cancel button
    cancel_x = x + FSEL_W - 50
    cancel_y = bottom_y + 14
    @gfx.fill_rect(cancel_x, cancel_y, 44, 12, FSEL_CANCEL_BG)
    @gfx.draw_text(cancel_x + 6, cancel_y + 2, "Cancel", FmrbGfx::WHITE, FSEL_CANCEL_BG)

    # File list
    list_y = metrics[:list_y]
    list_h = metrics[:list_h]
    max_visible = metrics[:max_visible]
    has_scrollbar = @file_selector_entries.size > max_visible
    text_area_w = has_scrollbar ? FSEL_W - FmrbApp::SCROLLBAR_W - 4 : FSEL_W - 12

    i = 0
    while i < max_visible
      idx = @file_selector_scroll + i
      break if idx >= @file_selector_entries.size

      entry = @file_selector_entries[idx]
      item_y = list_y + i * FSEL_ITEM_H

      if idx == @file_selector_selected
        item_w = has_scrollbar ? FSEL_W - FmrbApp::SCROLLBAR_W - 2 : FSEL_W - 4
        @gfx.fill_rect(x + 2, item_y, item_w, FSEL_ITEM_H, FSEL_SEL_BG)
        text_color = FmrbGfx::WHITE
        text_bg = FSEL_SEL_BG
      else
        text_color = entry[:is_dir] ? FSEL_DIR_COLOR : FSEL_TEXT
        text_bg = FSEL_BG
      end

      prefix = entry[:is_dir] ? "[" : " "
      suffix = entry[:is_dir] ? "]" : ""
      label = "#{prefix}#{entry[:name]}#{suffix}"
      label = label[0, text_area_w / 6] if label.length > text_area_w / 6
      @gfx.draw_text(x + 6, item_y + 2, label, text_color, text_bg)
      i += 1
    end

    # The bar hides itself when everything fits, so has_scrollbar only
    # decides how wide a row may be, not whether to draw one.
    @ui.move(:fsel_sb, x + FSEL_W - FSEL_SB_W, list_y, FSEL_SB_W, list_h)
    @ui.set_range(:fsel_sb, @file_selector_entries.size, max_visible)
    @ui.set_value(:fsel_sb, @file_selector_scroll)
    @ui.set_visible(:fsel_sb, true)
    @ui.invalidate_all
  end

  FSEL_SB_W = 10

  def build_file_selector_widgets
    @ui.scrollbar(:fsel_sb, 0, 0, FSEL_SB_W, 40, 0, 1)
    @ui.set_visible(:fsel_sb, false)
    nil
  end

  def handle_file_selector_widget(id)
    return nil unless id == :fsel_sb
    @file_selector_scroll = @ui.value(:fsel_sb)
    draw_foreground
    nil
  end

  # ---- What a selection is, and what taking it means ----
  #
  # The mouse and the keyboard reach the same four actions below, so a double
  # click and Enter cannot drift apart.

  def fsel_selected_entry
    return nil if @file_selector_selected < 0 ||
                  @file_selector_selected >= @file_selector_entries.size
    @file_selector_entries[@file_selector_selected]
  end

  def fsel_path_of(name)
    @file_selector_dir == "/" ? "/#{name}" : "#{@file_selector_dir}/#{name}"
  end

  def fsel_select(idx)
    return if idx == @file_selector_selected
    @file_selector_selected = idx
    entry = fsel_selected_entry
    # In save mode the name field follows the selection, so a file can be
    # overwritten without retyping its name.
    @file_selector_filename = entry[:name] if entry && !entry[:is_dir]
    draw_foreground
  end

  def fsel_navigate(name)
    if name == ".."
      # Go up. join can come out empty at the first level ("/app" ->
      # ["", "app"] -> [""]), which is not a path the resolver knows.
      parts = @file_selector_dir.split("/")
      parts.pop
      up = parts.join("/")
      @file_selector_dir = up.empty? ? "/" : up
    else
      @file_selector_dir = fsel_path_of(name)
    end
    scan_file_selector_dir
    # A fresh listing means the remembered row is meaningless.
    @fsel_click_idx = -1
    draw_foreground
  end

  # Enter, or a double click: a directory opens, a file answers the dialog. In
  # save mode a file is not the answer -- the name field is -- so Enter saves
  # unless the row under it is a directory to descend into.
  def fsel_activate
    entry = fsel_selected_entry
    if entry && entry[:is_dir]
      fsel_navigate(entry[:name])
    elsif @file_selector_mode == "save"
      fsel_save_typed_name
    elsif entry
      close_file_selector(fsel_path_of(entry[:name]))
    end
  end

  def fsel_save_typed_name
    return if @file_selector_filename.length == 0
    close_file_selector(fsel_path_of(@file_selector_filename))
  end

  def handle_file_selector_click(x, y)
    bottom_y = fsel_list_metrics[:bottom_y]

    # Cancel button
    cancel_x = @fsel_x + FSEL_W - 50
    cancel_y = bottom_y + 14
    if x >= cancel_x && x < cancel_x + 44 && y >= cancel_y && y < cancel_y + 12
      close_file_selector(nil)
      return
    end

    # Save button (save mode only)
    if @file_selector_mode == "save"
      save_x = @fsel_x + FSEL_W - 100
      save_y = bottom_y + 14
      if x >= save_x && x < save_x + 40 && y >= save_y && y < save_y + 12
        fsel_save_typed_name
        return
      end
    end

    # File list
    metrics = fsel_list_metrics
    list_y = metrics[:list_y]
    list_h = metrics[:list_h]
    max_visible = metrics[:max_visible]

    # The bar is a widget and was handled before this; a click that reaches
    # here is on the list itself.
    if y >= list_y && y < list_y + max_visible * FSEL_ITEM_H
      idx = @file_selector_scroll + (y - list_y) / FSEL_ITEM_H
      if idx >= 0 && idx < @file_selector_entries.size
        # One click selects, two activate -- the launcher's rule. Navigating on
        # a single click meant a double-click (the reflex, and what the launcher
        # trains) entered a directory and then immediately acted on whatever row
        # sat under the pointer in the new listing, usually "..".
        now = @counter
        double = (idx == @fsel_click_idx) && ((now - @fsel_click_time) < 5)
        @fsel_click_idx = idx
        @fsel_click_time = now

        fsel_select(idx)
        return unless double

        # In save mode a double click on a file is not the answer -- the click
        # above already put its name in the field -- so activation only
        # descends into directories there. fsel_activate reads the mode.
        entry = @file_selector_entries[idx]
        fsel_activate if entry[:is_dir] || @file_selector_mode != "save"
      end
    end
  end

  # ---- Keyboard ----
  #
  # Scancodes, not keycodes: a scancode is the HID usage ID on the device and
  # the SDL scancode in the simulator, and those are the same numbers. The
  # keycode is a character on one and an SDL keysym on the other, so `R` and
  # KEY_UP (0x52) would be the same key there.
  #
  # Returns true when the key was the dialog's, so the caller knows whether to
  # pass it on to the filename field.
  def handle_file_selector_key(scancode)
    case scancode
    when FmrbConst::KEY_UP    then fsel_move_selection(-1)
    when FmrbConst::KEY_DOWN  then fsel_move_selection(1)
    when FmrbConst::KEY_PGUP  then fsel_move_selection(-fsel_list_metrics[:max_visible])
    when FmrbConst::KEY_PGDN  then fsel_move_selection(fsel_list_metrics[:max_visible])
    when FmrbConst::KEY_HOME  then fsel_move_to(0)
    when FmrbConst::KEY_END   then fsel_move_to(@file_selector_entries.size - 1)
    when FmrbConst::KEY_ENTER then fsel_activate
    when FmrbConst::KEY_ESC   then close_file_selector(nil)
    when FmrbConst::KEY_RIGHT
      entry = fsel_selected_entry
      fsel_navigate(entry[:name]) if entry && entry[:is_dir]
    when FmrbConst::KEY_LEFT then fsel_go_up
    when FmrbConst::KEY_BACKSPACE
      # In save mode backspace belongs to the name being typed.
      return false if @file_selector_mode == "save"
      fsel_go_up
    else
      return false
    end
    true
  end

  def fsel_go_up
    fsel_navigate("..") unless @file_selector_dir == "/"
  end

  def fsel_move_selection(delta)
    return if @file_selector_entries.size == 0
    # Nothing selected yet (the dialog opens that way for the mouse): the first
    # key press lands on the top row rather than moving from nowhere.
    from = @file_selector_selected
    if from < 0
      from = delta > 0 ? -1 : 0
    end
    fsel_move_to(from + delta)
  end

  def fsel_move_to(idx)
    size = @file_selector_entries.size
    return if size == 0
    idx = 0 if idx < 0
    idx = size - 1 if idx >= size
    fsel_reveal(idx)
    fsel_select(idx)
  end

  # Scroll just enough to bring a row into view. Called before the selection
  # changes so one redraw shows both.
  def fsel_reveal(idx)
    max_visible = fsel_list_metrics[:max_visible]
    scroll = @file_selector_scroll
    if idx < scroll
      scroll = idx
    elsif idx >= scroll + max_visible
      scroll = idx - max_visible + 1
    end
    scroll = 0 if scroll < 0
    return if scroll == @file_selector_scroll
    @file_selector_scroll = scroll
    draw_foreground
  end

  def hit_file_selector?(x, y)
    x >= @fsel_x && x < @fsel_x + FSEL_W &&
      y >= @fsel_y && y < @fsel_y + FSEL_H
  end
end
