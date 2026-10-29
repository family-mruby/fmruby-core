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
    "Run" => "Ent", "Edit" => "E", "Copy" => "C", "Delete" => "Del", "Paste" => "V", "Linux" => "L"
  }

  # "Open in a Linux app" (L11): the menu item, on the machines that have
  # Linux apps (the PC's compositor) and only there. It opens a second menu,
  # the apps that open the file, the default first; FMGR_LX_PAGE of them at
  # a time, then "Other..." for the next ones.
  FMGR_CTX_LINUX = "Linux"
  FMGR_LX_PAGE = 6
  FMGR_LX_MIN_W = 100


  # The panel's size. The mouse desktop shows a window of FMGR_W x FMGR_H;
  # a desktop that wants another size (the keys desktop fills the screen
  # under its bar) defines these two itself.
  def fmgr_w; FMGR_W; end
  def fmgr_h; FMGR_H; end
  def open_file_manager
    @file_manager_open = true
    @file_manager_dir = fmgr_remembered_dir
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
    @fmgr_lx_open = false
    # Asked once per opening: "" on every machine without Linux apps (the
    # devices, the simulation, the browser, a native machine that is not the
    # compositor), so the item and the Linux defaults are never offered there.
    @fmgr_linux = FmrbApp.linux_apps(2, FmrbI18n.lang) != ""
    close_launcher
    close_dropdown
    scan_file_manager_dir
    notify_overlay_state(true, @fmgr_x, @fmgr_y, fmgr_w, fmgr_h)
    update_composite_regions
    draw_foreground
  end

  def close_file_manager
    return unless @file_manager_open
    @file_manager_open = false
    @fmgr_ctx_open = false
    @fmgr_lx_open = false
    save_app_state({ "dir" => @file_manager_dir }, "file_manager")
    @ui.set_visible(:fmgr_sb, false)
    @ui.flush
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  # The folder the File Manager was showing when it was last closed
  # (app_state, /tmp/file_manager.state.toml: shared by the mouse and the
  # keys desktop, forgotten when the machine restarts), or "/" when nothing
  # is remembered or the folder can no longer be opened.
  def fmgr_remembered_dir
    st = app_state("file_manager")
    dir = st["dir"]
    return "/" unless dir.is_a?(String)
    path = dir.to_s
    return "/" unless path.start_with?("/")
    begin
      d = Dir.open(path)
      d.close
    rescue => e
      return "/"
    end
    path
  end

  def scan_file_manager_dir
    @file_manager_entries = []
    @file_manager_entries << { name: "..", is_dir: true, size: 0, special: false } unless @file_manager_dir == "/"

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
        special = false
        file_size = 0
        begin
          d = Dir.open(vpath)
          d.close
          is_dir = true
        rescue
          begin
            file_size = File.size(vpath)
          rescue
            # Listed but neither a directory nor a file that stats: on the
            # PC, a device, a FIFO or a socket under /mnt (the file HAL does
            # not open those), or a link to nothing. Shown, marked, not opened.
            special = true
          end
        end
        @file_manager_entries << { name: name, is_dir: is_dir, size: file_size, special: special }
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

  # Fits the size column (at most four digits and the unit) whatever the size:
  # /proc/kcore on the PC says 128 TB. Divided step by step rather than by
  # 1024 ** n, so no constant needs more than the engine's 32-bit integers.
  FMGR_SIZE_UNITS = ["B", "KB", "MB", "GB", "TB", "PB"]

  def format_size(bytes)
    n = bytes
    unit = 0
    while n >= 1024 && unit < FileManagerMixin::FMGR_SIZE_UNITS.size - 1
      n = n / 1024
      unit += 1
    end
    "#{n}#{FileManagerMixin::FMGR_SIZE_UNITS[unit]}"
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
    @gfx.fill_rect(x, y, fmgr_w, fmgr_h, FMGR_BG)
    @gfx.draw_rect(x, y, fmgr_w, fmgr_h, FmrbConst::THEME_BORDER)

    # Title bar
    @gfx.fill_rect(x + 1, y + 1, fmgr_w - 2, FMGR_TITLE_H - 1, FMGR_TITLE_BG)
    title = "File: #{@file_manager_dir}"
    max_title_len = (fmgr_w - 60) / 6
    title = title[0, max_title_len] if title.length > max_title_len
    @gfx.draw_text(x + 4, y + 3, title, FmrbConst::THEME_TEXT_LIGHT, FMGR_TITLE_BG)

    # Close button on title bar (small filled circle, matches app windows).
    close_cx = x + fmgr_w - 7
    close_cy = y + FMGR_TITLE_H / 2
    @gfx.fill_circle(close_cx, close_cy, 3, FmrbConst::THEME_TEXT_LIGHT)

    # Column header
    header_y = y + FMGR_TITLE_H + 1
    header_bg = FmrbConst::THEME_HIGHLIGHT
    @gfx.fill_rect(x + 1, header_y, fmgr_w - 2, FMGR_ITEM_H, header_bg)
    @gfx.draw_text(x + 6, header_y + 2, "Name", FMGR_TEXT, header_bg)
    size_col_x = x + fmgr_w - 60
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
        @gfx.fill_rect(x + 2, item_y, fmgr_w - 4, FMGR_ITEM_H, FMGR_SEL_BG)
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
      max_name_len = (fmgr_w - 72) / 6
      label = label[0, max_name_len] if label.length > max_name_len
      @gfx.draw_text(x + 6, item_y + 2, label, text_color, text_bg)

      # Size column (files only). A file that cannot be opened says so there
      # instead of a size (fmgr_cannot_open).
      unless entry[:is_dir]
        size_str = entry[:special] ? "special" : format_size(entry[:size])
        @gfx.draw_text(size_col_x, item_y + 2, size_str, size_color, text_bg)
      end
      i += 1
    end

    # One pixel inside, clear of the panel's border (see file_selector).
    @ui.move(:fmgr_sb, x + fmgr_w - 1 - FMGR_SB_W, list_y, FMGR_SB_W, m[:list_h])
    @ui.set_range(:fmgr_sb, @file_manager_entries.size, max_visible)
    @ui.set_value(:fmgr_sb, @file_manager_scroll)
    @ui.set_visible(:fmgr_sb, true)
    @ui.invalidate_all

    # Context menu (drawn on top)
    draw_fmgr_context_menu if @fmgr_ctx_open
    draw_fmgr_linux_menu if @fmgr_lx_open
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
    cw = fmgr_ctx_w(items)

    @gfx.fill_rect(cx, cy, cw, ch, FMGR_CTX_BG)
    @gfx.draw_rect(cx, cy, cw, ch, FMGR_CTX_BORDER)

    items.each_with_index do |label, i|
      iy = cy + 1 + i * FMGR_CTX_ITEM_H
      if label == FMGR_CTX_LINUX
        # The one item in the user's language (Japanese needs the mixed font).
        @gfx.draw_text_mixed(cx + 6, iy + 3, FmrbI18n.t(:fmgr_open_linux), FMGR_CTX_TEXT)
      else
        @gfx.draw_text(cx + 6, iy + 3, label, FMGR_CTX_TEXT, FMGR_CTX_BG)
      end
      key = FMGR_CTX_KEYS[label]
      # Right-aligned, at 6 pixels per character (the system font's advance).
      @gfx.draw_text(cx + cw - 4 - key.length * 6, iy + 3, key,
                     FMGR_SIZE_COLOR, FMGR_CTX_BG) if key
    end
  end

  # The menu's width: FMGR_CTX_W, wider when the Linux item's label needs it.
  def fmgr_ctx_w(items)
    return FMGR_CTX_W unless items.include?(FMGR_CTX_LINUX)
    w = FmrbI18n.text_width(FmrbI18n.t(:fmgr_open_linux)) + 6 + 12 + 6 + 4
    w > FMGR_CTX_W ? w : FMGR_CTX_W
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
        items << FMGR_CTX_LINUX if @fmgr_linux && !entry[:special]
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
    list_h = fmgr_h - FMGR_TITLE_H - FMGR_ITEM_H - 4
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

    # The Linux apps' menu, then the context menu, take priority
    if @fmgr_lx_open
      if hit_fmgr_linux_menu?(x, y)
        fmgr_linux_choose((y - @fmgr_lx_y - 1) / FMGR_CTX_ITEM_H)
      else
        @fmgr_lx_open = false
        draw_foreground
      end
      return
    end
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
    close_cx = fx + fmgr_w - 7
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
    @fmgr_lx_open = false

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
    ctx_w = fmgr_ctx_w(items)
    if @fmgr_ctx_x + ctx_w > @fmgr_x + fmgr_w
      @fmgr_ctx_x = @fmgr_x + fmgr_w - ctx_w
    end
    if @fmgr_ctx_y + ctx_h > @fmgr_y + fmgr_h
      @fmgr_ctx_y = @fmgr_y + fmgr_h - ctx_h
    end
    @fmgr_ctx_open = true
    draw_foreground
  end

  def hit_fmgr_context_menu?(x, y)
    return false unless @fmgr_ctx_open
    items = fmgr_ctx_items
    ch = FMGR_CTX_ITEM_H * items.size + 2
    x >= @fmgr_ctx_x && x < @fmgr_ctx_x + fmgr_ctx_w(items) &&
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
    when FMGR_CTX_LINUX
      fmgr_open_linux_menu(@fmgr_ctx_idx, true)
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
    if @fmgr_lx_open
      # The Linux apps' menu is the one menu with a selection of its own:
      # arrows move it, Enter takes it, Esc and Tab take the menu down. Any
      # other key takes it down and acts on the row underneath.
      case scancode
      when FmrbConst::KEY_UP
        fmgr_linux_move(-1)
        return nil
      when FmrbConst::KEY_DOWN
        fmgr_linux_move(1)
        return nil
      when FmrbConst::KEY_ENTER
        fmgr_linux_choose(@fmgr_lx_sel)
        return nil
      end
      @fmgr_lx_open = false
      draw_foreground
      return nil if scancode == FmrbConst::KEY_ESC || scancode == FmrbConst::KEY_TAB
    end
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
      elsif (character == 108 || character == 76) && @fmgr_linux  # l / L
        fmgr_open_linux_menu(@file_manager_selected, false)
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
    @fmgr_ctx_x = @fmgr_x + fmgr_w / 3
    @fmgr_ctx_y = m[:list_y] + (row + 1) * FMGR_ITEM_H
    ctx_h = FMGR_CTX_ITEM_H * items.size + 2
    if @fmgr_ctx_y + ctx_h > @fmgr_y + fmgr_h
      @fmgr_ctx_y = @fmgr_y + fmgr_h - ctx_h
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
    return fmgr_cannot_open(entry) if entry[:special]
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
    return fmgr_cannot_open(entry) if entry[:special]

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
    return fmgr_cannot_open(entry) if entry[:special]
    vpath = fmgr_selected_virtual_path
    return unless vpath

    # A kind of file the table does not list goes to Linux's default app
    # where there are Linux apps (L11), and to the editor as before where
    # there are none or Linux has no app for it.
    action = fmgr_assoc_listed(vpath)
    unless action
      return nil if @fmgr_linux && fmgr_linux_open("", vpath, entry[:name], false)
      action = FmrbAssoc::EDIT
    end
    if fmgr_assoc_linux?(action)
      fmgr_open_by_table(action, vpath, idx)
      return nil
    end
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

  # ---- Linux apps (L11) ----
  #
  # The association table's answer for this file, or nil when the table
  # does not list its kind (FmrbAssoc.resolve would answer "edit" then).
  def fmgr_assoc_listed(vpath)
    ext = FmrbAssoc.extension_of(vpath)
    return nil if ext.empty?
    FmrbAssoc.load_table[ext]
  end

  # A table answer that names a Linux app: "linux" (Linux's default for the
  # file's kind) or "linux:<desktop file id>".
  def fmgr_assoc_linux?(action)
    action == "linux" || action.start_with?("linux:")
  end

  # The table says Linux. Where there are no Linux apps the line means
  # nothing, and the file opens the way an unlisted one does: the editor.
  def fmgr_open_by_table(action, vpath, idx)
    unless @fmgr_linux
      Log.info("File manager: #{vpath}: \"#{action}\" needs Linux apps; the editor instead")
      fmgr_edit_file(idx)
      return nil
    end
    id = action == "linux" ? "" : action[6, action.length - 6]
    entry = @file_manager_entries[idx]
    fmgr_linux_open(id, vpath, entry[:name], true)
    nil
  end

  # Ask for vpath to be opened in the Linux app id ("": Linux's default for
  # its kind). The File Manager closes, as it does for an app of ours. When
  # nothing starts: with tell, a notice says so; without, the caller goes on
  # (false).
  def fmgr_linux_open(id, vpath, name, tell)
    app = FmrbApp.linux_open(id, vpath)
    if app.empty?
      Log.info("File manager: no Linux app opened #{vpath}#{id.empty? ? '' : ' (' + id + ')'}")
      if tell
        what = id.empty? ? "no Linux app opens this kind of file" : "#{id} could not start"
        fmgr_notice("Cannot open #{name}:\n#{what}.")
      end
      return false
    end
    Log.info("File manager: open #{vpath} in the Linux app #{app}")
    close_file_manager
    true
  end

  # The menu of the Linux apps that open the file at idx: where the context
  # menu was (at_menu), else under the row (the L key).
  def fmgr_open_linux_menu(idx, at_menu)
    return nil if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    return nil if entry[:is_dir] || entry[:special] || entry[:name] == ".."
    vpath = @file_manager_dir == "/" ? "/#{entry[:name]}" : "#{@file_manager_dir}/#{entry[:name]}"
    text = FmrbApp.linux_handlers(vpath)
    apps = []
    lines = text.split("\n")
    type = ""
    i = 0
    while i < lines.size
      parts = lines[i].split("\t")
      if i == 0
        type = parts.size > 1 ? parts[1] : ""
      elsif parts.size >= 2
        apps << [parts[0], parts[1]]
      end
      i += 1
    end
    if apps.empty?
      Log.info("File manager: no Linux app opens #{vpath} (#{type.empty? ? 'kind not known' : type})")
      fmgr_notice("No Linux app opens #{entry[:name]}#{type.empty? ? '' : "\n(#{type})"}.")
      return nil
    end
    Log.info("File manager: Linux apps for #{vpath} (#{type}): #{apps.size}")
    @fmgr_lx_apps = apps
    @fmgr_lx_path = vpath
    @fmgr_lx_name = entry[:name]
    @fmgr_lx_page = 0
    @fmgr_lx_sel = 0
    @fmgr_ctx_open = false
    unless at_menu
      m = fmgr_list_metrics
      row = idx - @file_manager_scroll
      row = 0 if row < 0
      @fmgr_ctx_x = @fmgr_x + fmgr_w / 3
      @fmgr_ctx_y = m[:list_y] + (row + 1) * FMGR_ITEM_H
    end
    @fmgr_lx_open = true
    fmgr_place_linux_menu
    draw_foreground
    nil
  end

  # The labels of the page shown: the apps of this page, then "Other..."
  # when more follow. The default (the first app) says so.
  def fmgr_linux_labels
    labels = []
    apps = @fmgr_lx_apps
    i = @fmgr_lx_page
    last = @fmgr_lx_page + FMGR_LX_PAGE
    while i < last && i < apps.size
      label = apps[i][0]
      label = "#{label} #{FmrbI18n.t(:fmgr_linux_default)}" if i == 0
      labels << label
      i += 1
    end
    labels << FmrbI18n.t(:fmgr_linux_other) if apps.size > last
    labels
  end

  def fmgr_linux_menu_w
    w = FMGR_LX_MIN_W
    labels = fmgr_linux_labels
    labels.each do |label|
      lw = FmrbI18n.text_width(label) + 12
      w = lw if lw > w
    end
    max = fmgr_w - 8
    w > max ? max : w
  end

  # Inside the File Manager's rectangle (the overlay the kernel composites).
  def fmgr_place_linux_menu
    h = FMGR_CTX_ITEM_H * fmgr_linux_labels.size + 2
    w = fmgr_linux_menu_w
    x = @fmgr_ctx_x
    y = @fmgr_ctx_y
    x = @fmgr_x + fmgr_w - w - 2 if x + w > @fmgr_x + fmgr_w - 2
    x = @fmgr_x + 2 if x < @fmgr_x + 2
    y = @fmgr_y + fmgr_h - h - 2 if y + h > @fmgr_y + fmgr_h - 2
    y = @fmgr_y + FMGR_TITLE_H if y < @fmgr_y + FMGR_TITLE_H
    @fmgr_lx_x = x
    @fmgr_lx_y = y
    nil
  end

  def draw_fmgr_linux_menu
    labels = fmgr_linux_labels
    w = fmgr_linux_menu_w
    h = FMGR_CTX_ITEM_H * labels.size + 2
    x = @fmgr_lx_x
    y = @fmgr_lx_y
    @gfx.fill_rect(x, y, w, h, FMGR_CTX_BG)
    @gfx.draw_rect(x, y, w, h, FMGR_CTX_BORDER)
    i = 0
    while i < labels.size
      iy = y + 1 + i * FMGR_CTX_ITEM_H
      label = FmrbI18n.truncate_to(labels[i], w - 10)
      if i == @fmgr_lx_sel
        @gfx.fill_rect(x + 1, iy, w - 2, FMGR_CTX_ITEM_H, FMGR_CTX_HL)
        @gfx.draw_text_mixed(x + 5, iy + 3, label, FmrbConst::THEME_TEXT_LIGHT)
      else
        @gfx.draw_text_mixed(x + 5, iy + 3, label, FMGR_CTX_TEXT)
      end
      i += 1
    end
    nil
  end

  def hit_fmgr_linux_menu?(x, y)
    return false unless @fmgr_lx_open
    h = FMGR_CTX_ITEM_H * fmgr_linux_labels.size + 2
    x >= @fmgr_lx_x && x < @fmgr_lx_x + fmgr_linux_menu_w && y >= @fmgr_lx_y && y < @fmgr_lx_y + h
  end

  def fmgr_linux_move(delta)
    n = fmgr_linux_labels.size
    sel = @fmgr_lx_sel + delta
    sel = 0 if sel < 0
    sel = n - 1 if sel >= n
    return nil if sel == @fmgr_lx_sel
    @fmgr_lx_sel = sel
    draw_foreground
    nil
  end

  # Row i of the menu taken: an app opens the file, "Other..." shows the
  # next page.
  def fmgr_linux_choose(i)
    n = fmgr_linux_labels.size
    return nil if i < 0 || i >= n
    at = @fmgr_lx_page + i
    if at >= @fmgr_lx_page + FMGR_LX_PAGE
      @fmgr_lx_page += FMGR_LX_PAGE
      @fmgr_lx_sel = 0
      fmgr_place_linux_menu
      draw_foreground
      return nil
    end
    app = @fmgr_lx_apps[at]
    @fmgr_lx_open = false
    Log.info("File manager: chose the Linux app #{app[1]} for #{@fmgr_lx_path}")
    draw_foreground unless fmgr_linux_open(app[1], @fmgr_lx_path, @fmgr_lx_name, true)
    nil
  end

  def fmgr_copy_file(idx)
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    return fmgr_cannot_open(entry) if entry[:special]
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

  # Delete asks first (L7c): /mnt/host shows the user's own files, so one
  # stray key must not lose one. This only remembers the path and puts the
  # question up; the desktop answers through fmgr_delete_confirmed (Yes) or
  # fmgr_delete_cancelled (No). How the question is shown is the desktop's
  # (fmgr_ask_delete): the mouse desktop's confirm dialog, the keys desktop's
  # line at the bottom.
  def fmgr_delete_file(idx)
    return if idx < 0 || idx >= @file_manager_entries.size
    entry = @file_manager_entries[idx]
    return if entry[:name] == ".."

    vpath = if @file_manager_dir == "/"
              "/#{entry[:name]}"
            else
              "#{@file_manager_dir}/#{entry[:name]}"
            end
    # A place that cannot be changed (/mnt/root, the PC's distribution tree)
    # says so before the question rather than after it (L7d).
    if FmrbApp.path_read_only?(vpath)
      Log.info("File manager: #{vpath} is read-only")
      fmgr_notice("Cannot delete #{entry[:name]}:\n#{@file_manager_dir} is read-only.")
      return nil
    end
    @fmgr_delete_path = vpath
    fmgr_ask_delete(fmgr_delete_question(entry[:name]))
    nil
  end

  # "Delete <name>?" in the user's language. The table's "%s" is replaced by
  # hand: sprintf with a String argument is not something the kernel Ruby
  # uses anywhere else, and the slicing below works the same on both engines.
  def fmgr_delete_question(name)
    t = FmrbI18n.t(:fmgr_delete_confirm)
    at = t.index("%s")
    return "#{t} #{name}" unless at
    t[0, at] + name + t[at + 2, t.length - at - 2]
  end

  def fmgr_delete_cancelled
    @fmgr_delete_path = nil
    nil
  end

  def fmgr_delete_confirmed
    file_path = @fmgr_delete_path
    @fmgr_delete_path = nil
    return nil unless file_path

    begin
      File.unlink(file_path)
      Log.info("File manager: deleted #{file_path}")
      scan_file_manager_dir
      draw_foreground
    rescue => e
      Log.error("File manager: delete failed: #{e.message}")
      # On screen too (L7d): the log alone left a Yes that did nothing.
      fmgr_notice("Could not delete #{file_path}.")
    end
    nil  # begin arm ends in a void-typed call (Spinel) -> pin a concrete return
  end

  # ---- Notices ----
  #
  # What the File Manager cannot do, on the error dialog (both desktops have
  # it). It sits over the File Manager, which stays open behind it.
  def fmgr_notice(message)
    # The dialog draws in the system font only, so its title stays English.
    open_error_dialog("File Manager", message)
    nil
  end

  # A listed entry the file HAL will not open (scan_file_manager_dir).
  def fmgr_cannot_open(entry)
    fmgr_notice("Cannot open #{entry[:name]}:\nnot a regular file (a device, a FIFO,\na socket or a link to nothing).")
    nil
  end

  # A dialog over the File Manager closed. The kernel keeps one overlay
  # rectangle for its hit testing, and opening the dialog replaced the File
  # Manager's with the dialog's: give it back while the File Manager is still
  # open, or clicks on its list would fall through to the window beneath.
  # With nothing left open this is the old "no overlay".
  def notify_overlay_after_dialog
    if @file_manager_open
      notify_overlay_state(true, @fmgr_x, @fmgr_y, fmgr_w, fmgr_h)
    else
      notify_overlay_state(false, 0, 0, 0, 0)
    end
    nil
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
    return true if @fmgr_lx_open && hit_fmgr_linux_menu?(x, y)
    x >= @fmgr_x && x < @fmgr_x + fmgr_w &&
      y >= @fmgr_y && y < @fmgr_y + fmgr_h
  end
end
