# File Selector module for SystemDesktopApp
# Provides file open/save dialog with directory navigation

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
    close_launcher
    close_dropdown
    scan_file_selector_dir
    notify_overlay_state(true, @fsel_x, @fsel_y, FSEL_W, FSEL_H)
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
    draw_foreground
  end

  def scan_file_selector_dir
    @file_selector_entries = []
    # Add parent directory entry unless at root
    @file_selector_entries << { name: "..", is_dir: true } unless @file_selector_dir == "/"

    begin
      dir = Dir.open(@file_selector_dir)
      names = []
      while (e = dir.read)
        names << e unless e == "." || e == ".."
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


  def draw_file_selector
    return unless @file_selector_open

    x = @fsel_x
    y = @fsel_y

    # Window
    @gfx.fill_rect(x, y, FSEL_W, FSEL_H, FSEL_BG)
    @gfx.draw_rect(x, y, FSEL_W, FSEL_H, FmrbConst::THEME_BORDER)

    # Title bar
    @gfx.fill_rect(x + 1, y + 1, FSEL_W - 2, FSEL_TITLE_H - 1, FSEL_TITLE_BG)
    title = "Open: #{@file_selector_dir}"
    title = title[0, FSEL_W / 6 - 2] if title.length > FSEL_W / 6 - 2
    @gfx.draw_text(x + 4, y + 3, title, FmrbGfx::WHITE, FSEL_TITLE_BG)

    # Bottom area: filename input (save mode) + buttons
    bottom_y = y + FSEL_H - 32

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
    list_y = y + FSEL_TITLE_H + 2
    list_h = bottom_y - list_y - 2
    max_visible = list_h / FSEL_ITEM_H
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

    # Draw scrollbar if needed
    if has_scrollbar
      draw_scrollbar(@file_selector_scroll, @file_selector_entries.size,
                     max_visible, x, list_y, FSEL_W, list_h)
    end
  end

  def handle_file_selector_click(x, y)
    bottom_y = @fsel_y + FSEL_H - 32

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
        if @file_selector_filename.length > 0
          path = if @file_selector_dir == "/"
                   "/#{@file_selector_filename}"
                 else
                   "#{@file_selector_dir}/#{@file_selector_filename}"
                 end
          close_file_selector(path)
        end
        return
      end
    end

    # File list
    list_y = @fsel_y + FSEL_TITLE_H + 2
    list_h = bottom_y - list_y - 2
    max_visible = list_h / FSEL_ITEM_H

    # Scrollbar click
    sb = scrollbar_hit(x, y, @fsel_x, list_y, FSEL_W, list_h)
    if sb
      if sb == :up
        @file_selector_scroll -= 1 if @file_selector_scroll > 0
      else
        max_scroll = @file_selector_entries.size - max_visible
        @file_selector_scroll += 1 if @file_selector_scroll < max_scroll
      end
      draw_foreground
      return
    end

    if y >= list_y && y < list_y + max_visible * FSEL_ITEM_H
      idx = @file_selector_scroll + (y - list_y) / FSEL_ITEM_H
      if idx >= 0 && idx < @file_selector_entries.size
        entry = @file_selector_entries[idx]
        if entry[:is_dir]
          # Navigate into directory
          if entry[:name] == ".."
            # Go up
            parts = @file_selector_dir.split("/")
            parts.pop
            @file_selector_dir = parts.empty? ? "/" : parts.join("/")
          else
            if @file_selector_dir == "/"
              @file_selector_dir = "/#{entry[:name]}"
            else
              @file_selector_dir = "#{@file_selector_dir}/#{entry[:name]}"
            end
          end
          scan_file_selector_dir
          draw_foreground
        else
          if @file_selector_mode == "save"
            # In save mode, clicking a file sets the filename
            @file_selector_filename = entry[:name]
            draw_foreground
          else
            # In open mode, select the file
            path = if @file_selector_dir == "/"
                     "/#{entry[:name]}"
                   else
                     "#{@file_selector_dir}/#{entry[:name]}"
                   end
            close_file_selector(path)
          end
        end
      end
    end
  end

  def hit_file_selector?(x, y)
    x >= @fsel_x && x < @fsel_x + FSEL_W &&
      y >= @fsel_y && y < @fsel_y + FSEL_H
  end
end
