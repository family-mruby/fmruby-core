# File Manager module for SystemDesktopApp
# Provides file browsing, delete, copy, execute, and open-in-editor

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
      while (e = dir.read)
        names << e unless e == "." || e == ".."
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
    name.end_with?(".rb") || name.end_with?(".lua") || name.end_with?(".bas")
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
    @gfx.draw_text(x + 4, y + 3, title, FmrbGfx::WHITE, FMGR_TITLE_BG)

    # Close button on title bar (small filled circle, matches app windows).
    close_cx = x + FMGR_W - 7
    close_cy = y + FMGR_TITLE_H / 2
    @gfx.fill_circle(close_cx, close_cy, 3, FmrbGfx::WHITE)

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
        text_color = FmrbGfx::WHITE
        text_bg = FMGR_SEL_BG
        size_color = FmrbGfx::WHITE
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

    # Scroll bar
    draw_scrollbar(@file_manager_scroll, @file_manager_entries.size, max_visible,
                   x, list_y, FMGR_W, m[:list_h])

    # Context menu (drawn on top)
    draw_fmgr_context_menu if @fmgr_ctx_open
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
    return -1 if scrollbar_hit(x, y, @fmgr_x, list_y, FMGR_W, list_h)
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

    draw_foreground if had_pressed

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

    # Scroll bar hit test
    sb = scrollbar_hit(x, y, fx, list_y, FMGR_W, m[:list_h])
    if sb
      handle_file_manager_scroll(sb == :up ? -1 : 1)
      return
    end

    # File list area
    if y >= list_y && y < list_y + max_visible * FMGR_ITEM_H
      idx = @file_manager_scroll + (y - list_y) / FMGR_ITEM_H
      if idx >= 0 && idx < total
        entry = @file_manager_entries[idx]
        if entry[:is_dir]
          # Navigate into directory
          fmgr_navigate_dir(entry[:name])
        else
          # Double-click detection
          now = @counter
          if idx == @fmgr_last_click_idx && (now - @fmgr_last_click_time) < 5
            # Double click - run if runnable
            if fmgr_runnable?(entry[:name])
              fmgr_run_file(idx)
            end
            @fmgr_last_click_idx = -1
          else
            # Single click - select
            @file_manager_selected = idx
            @fmgr_last_click_idx = idx
            @fmgr_last_click_time = now
            draw_foreground
          end
        end
      end
    end
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

  # ---- File operations ----

  def fmgr_navigate_dir(name)
    if name == ".."
      parts = @file_manager_dir.split("/")
      parts.pop
      @file_manager_dir = parts.empty? ? "/" : parts.join("/")
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

    # Spawn editor then send file_selected message after it starts
    spawn_app("default/editor")
    @fmgr_pending_edit_path = file_path
    @fmgr_pending_edit_counter = 3  # Wait a few update cycles for editor to init
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
