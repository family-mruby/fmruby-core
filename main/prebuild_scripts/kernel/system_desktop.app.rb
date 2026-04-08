# System Desktop Application
# Manages two canvas layers:
#   @gfx    (z=254): Menu bar, dropdown, launcher window (foreground)
#   @bg_gfx (z=0):   Wallpaper, memory stats (background)

class SystemDesktopApp < FmrbApp
  MENU_BAR_HEIGHT = 13
  MENU_BG = 0xC5
  DROPDOWN_BG = 0xFF
  DROPDOWN_TEXT = 0x00
  DROPDOWN_HIGHLIGHT = 0xC5
  BG_COLOR = 0xF6

  # Dropdown menu items
  DROPDOWN_ITEMS = [
    { label: "Launcher" },
    { label: "Config", app: "default/config" },
  ]

  DROPDOWN_X = 0
  DROPDOWN_Y = MENU_BAR_HEIGHT
  DROPDOWN_W = 80
  DROPDOWN_ITEM_H = 12

  # Launcher window layout
  LAUNCHER_W = 280
  LAUNCHER_H = 180
  LAUNCHER_TITLE_H = 14
  LAUNCHER_ICON_W = 56
  LAUNCHER_ICON_H = 48
  LAUNCHER_ICON_COLS = 4
  LAUNCHER_ICON_PAD_X = 12
  LAUNCHER_ICON_PAD_Y = 8
  LAUNCHER_BG = 0xFF
  LAUNCHER_TITLE_BG = 0xC5
  LAUNCHER_ICON_BG = 0xFF
  LAUNCHER_ICON_SEL = 0xC5
  LAUNCHER_TEXT = 0x00

  # File selector layout
  FSEL_W = 260
  FSEL_H = 200
  FSEL_TITLE_H = 14
  FSEL_ITEM_H = 12
  FSEL_BG = 0xFF
  FSEL_TITLE_BG = 0xC5
  FSEL_TEXT = 0x00
  FSEL_DIR_COLOR = 0x03   # Blue for directories
  FSEL_SEL_BG = 0xC5
  FSEL_CANCEL_BG = 0x60

  # Built-in app entries
  BUILTIN_APPS = [
    { label: "Shell",  app: "default/shell",  icon_file: "usr/share/icon/shell.icon" },
    { label: "Editor", app: "default/editor", icon_file: "usr/share/icon/editor.icon" },
  ]

  # Default icon files per VM type
  VM_ICON_FILES = {
    "rb"  => "usr/share/icon/ruby.icon",
    "lua" => "usr/share/icon/lua.icon",
    "bas" => "usr/share/icon/basic.icon",
  }

  # Fallback icon characters (when icon file is not available)
  VM_ICONS = {
    "rb" => "R",
    "lua" => "L",
    "bas" => "B",
  }

  def initialize
    super()
    @counter = 0
    @mem_update_interval = 30

    @dropdown_open = false
    @launcher_open = false
    @launcher_selected = -1
    @last_click_time = 0
    @last_click_idx = -1
    @launcher_apps = []
    @icon_cache = {}

    # File selector state
    @file_selector_open = false
    @file_selector_mode = "open"
    @file_selector_requester = nil
    @file_selector_dir = "/"
    @file_selector_entries = []
    @file_selector_scroll = 0
    @file_selector_selected = -1
    @file_selector_filename = ""
    @fsel_x = 0
    @fsel_y = 0

    # Launcher window position (centered)
    @launcher_x = 0
    @launcher_y = 0
  end

  def on_create()
    Log.info("on_create called")

    # Center launcher window
    @launcher_x = (@window_width - LAUNCHER_W) / 2
    @launcher_y = MENU_BAR_HEIGHT + (@window_height - MENU_BAR_HEIGHT - LAUNCHER_H) / 2

    # Build app list
    scan_apps

    # Center file selector
    @fsel_x = (@window_width - FSEL_W) / 2
    @fsel_y = MENU_BAR_HEIGHT + (@window_height - MENU_BAR_HEIGHT - FSEL_H) / 2

    # Start background music
    @audio = FmrbAudio.new(self)
    @audio.play("/data/test.nsf")

    draw_background
    draw_foreground
  end

  # ---- Icon loading ----

  def load_icon(icon_file)
    return @icon_cache[icon_file] if @icon_cache.key?(icon_file)

    rows = []
    color = 0xFF  # default white
    begin
      file = File.open(icon_file, "r")
      content = file.read
      file.close

      content.split("\n").each do |line|
        line = line.strip
        if line.start_with?("#")
          # Parse color from comment: "# ... color=0xFF"
          if line.include?("color=")
            c = line.split("color=")[1]
            if c
              c = c.strip.split(" ")[0]
              color = c.start_with?("0x") ? c[2..-1].to_i(16) : c.to_i
            end
          end
        elsif line.length > 0
          rows << line
        end
      end
    rescue => e
      Log.warn("Cannot load icon #{icon_file}: #{e.message}")
      @icon_cache[icon_file] = nil
      return nil
    end

    icon_data = { rows: rows, color: color }
    @icon_cache[icon_file] = icon_data
    icon_data
  end

  def draw_icon_bitmap(x, y, icon_data, scale, bg_color)
    return unless icon_data
    rows = icon_data[:rows]
    color = icon_data[:color]

    rows.each_with_index do |row, iy|
      row.length.times do |ix|
        if row[ix] == '1'
          if scale > 1
            @gfx.fill_rect(x + ix * scale, y + iy * scale, scale, scale, color)
          else
            @gfx.set_pixel(x + ix, y + iy, color)
          end
        end
      end
    end
  end

  # ---- App scanning ----

  def scan_apps
    @launcher_apps = BUILTIN_APPS.dup
    # Try both ESP32 (LittleFS at /flash) and Linux (relative flash/) paths
    ["/flash/app", "flash/app"].each do |path|
      scan_app_dir(path)
    end
    Log.info("Launcher: #{@launcher_apps.size} apps found")
  end

  def scan_app_dir(base_path)
    begin
      dir = Dir.open(base_path)
      entries = []
      while (entry = dir.read)
        entries << entry unless entry == "." || entry == ".."
      end
      dir.close

      entries.each do |entry|
        path = "#{base_path}/#{entry}"
        # Check for subdirectories containing .toml files
        begin
          sub_dir = Dir.open(path)
          sub_entries = []
          while (e = sub_dir.read)
            sub_entries << e
          end
          sub_dir.close

          sub_entries.each do |f|
            next unless f.end_with?(".toml")
            toml_path = "#{path}/#{f}"
            app_entry = parse_app_toml(toml_path, path)
            if app_entry
              @launcher_apps << app_entry
              Log.info("Found app: #{app_entry[:label]} (#{app_entry[:app]})")
            end
          end
        rescue
          # Not a directory, skip
        end
      end
    rescue => e
      Log.warn("Cannot scan #{base_path}: #{e.message}")
    end
  end

  # Strip "flash/" or "flash" prefix for File.open (HAL adds it automatically)
  def strip_flash_prefix(path)
    if path.start_with?("flash/")
      path[6..-1]
    elsif path.start_with?("/flash/")
      path  # absolute path, keep as-is (HAL prepends "flash" to make "flash/flash/...")
    else
      path
    end
  end

  def parse_app_toml(toml_path, dir_path)
    label = nil
    icon_field = nil
    begin
      file = File.open(strip_flash_prefix(toml_path), "r")
      content = file.read
      file.close

      content.split("\n").each do |line|
        line = line.strip
        if line.start_with?("app_screen_name")
          m = line.split("=", 2)
          if m[1]
            label = m[1].strip.gsub('"', '')
          end
        elsif line.start_with?("icon")
          m = line.split("=", 2)
          if m[1]
            icon_field = m[1].strip.gsub('"', '')
          end
        end
      end
    rescue => e
      Log.warn("Cannot read #{toml_path}: #{e.message}")
      return nil
    end

    # Derive script filename from toml filename
    toml_name = toml_path.split("/").last
    base = toml_name.sub(".toml", "")

    ext = nil
    ["rb", "lua", "bas"].each do |try_ext|
      begin
        f = File.open(strip_flash_prefix("#{dir_path}/#{base}.#{try_ext}"), "r")
        f.close
        ext = try_ext
        break
      rescue
      end
    end

    return nil unless ext

    icon_char = VM_ICONS[ext] || "?"
    icon_file = icon_field || VM_ICON_FILES[ext]
    label ||= base
    app_path = strip_flash_prefix("#{dir_path}/#{base}.#{ext}")

    { label: label, app: app_path, icon_char: icon_char, icon_file: icon_file }
  end

  # ---- Background layer (@bg_gfx) ----

  def draw_background
    return unless @bg_gfx
    @bg_gfx.clear(BG_COLOR)
    @bg_gfx.present
  end

  def draw_memory_stats
    return unless @bg_gfx
    if @counter % @mem_update_interval == 0
      begin
        processes = FmrbApp.ps
        heap_info = FmrbApp.heap_info
        sys_pool_info = FmrbApp.sys_pool_info
        return if processes.nil?

        stats_area_width = 150
        y_offset = @window_height - 50
        x_offset = 2
        line_height = 8

        @bg_gfx.fill_rect(x_offset, @window_height/2, x_offset + stats_area_width, y_offset + line_height, BG_COLOR)

        if heap_info && heap_info[:total] > 0
          heap_used_kb = (heap_info[:total] - heap_info[:free]) / 1024
          heap_total_kb = heap_info[:total] / 1024
          text = "Heap: #{heap_used_kb}KB/#{heap_total_kb}KB"
          @bg_gfx.draw_text(x_offset, y_offset, text, FmrbGfx::BLUE)
          y_offset -= line_height
        end

        if sys_pool_info && sys_pool_info[:total] > 0
          sys_used_kb = sys_pool_info[:used] / 1024
          sys_total_kb = sys_pool_info[:total] / 1024
          text = "SysPool: #{sys_used_kb}KB/#{sys_total_kb}KB"
          @bg_gfx.draw_text(x_offset, y_offset, text, FmrbGfx::BLUE)
          y_offset -= line_height
        end

        active_procs = processes.select { |p|
          p[:state] == FmrbConst::PROC_STATE_RUNNING ||
          p[:state] == FmrbConst::PROC_STATE_SUSPENDED
        }

        active_procs.each do |proc|
          name = proc[:name]
          mem_used_kb = proc[:mem_used] / 1024
          mem_total_kb = proc[:mem_total] / 1024
          text = "#{name}: #{mem_used_kb}KB/#{mem_total_kb}KB"
          @bg_gfx.draw_text(x_offset, y_offset, text, FmrbGfx::BLUE)
          y_offset -= line_height
        end

        @bg_gfx.present
      rescue => e
        Log.error("Error getting memory stats: #{e.message}")
      end
    end
  end

  # ---- Foreground layer (@gfx) ----

  def draw_foreground
    draw_menu_bar
    draw_dropdown if @dropdown_open
    draw_launcher if @launcher_open
    draw_file_selector if @file_selector_open
    @gfx.present
  end

  def draw_menu_bar
    @gfx.clear(0x01)  # Transparent (clear foreground, 0x01 = transparent color key)
    @gfx.fill_rect(0, 0, @window_width, MENU_BAR_HEIGHT, MENU_BG)
    @gfx.draw_text(2, 2, "Family mruby", FmrbGfx::WHITE)
    @gfx.draw_line(0, MENU_BAR_HEIGHT - 1, @window_width, MENU_BAR_HEIGHT - 1, 0x60)
  end

  def draw_dropdown
    x = DROPDOWN_X
    y = DROPDOWN_Y
    h = DROPDOWN_ITEM_H * DROPDOWN_ITEMS.size + 2

    @gfx.fill_rect(x, y, DROPDOWN_W, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, DROPDOWN_W, h, 0x00)

    DROPDOWN_ITEMS.each_with_index do |item, i|
      item_y = y + 1 + i * DROPDOWN_ITEM_H
      @gfx.draw_text(x + 6, item_y + 2, item[:label], DROPDOWN_TEXT, DROPDOWN_BG)
    end
  end

  def draw_launcher
    x = @launcher_x
    y = @launcher_y

    # Window frame
    @gfx.fill_rect(x, y, LAUNCHER_W, LAUNCHER_H, LAUNCHER_BG)
    @gfx.draw_rect(x, y, LAUNCHER_W, LAUNCHER_H, 0x60)

    # Title bar
    @gfx.fill_rect(x + 1, y + 1, LAUNCHER_W - 2, LAUNCHER_TITLE_H - 1, LAUNCHER_TITLE_BG)
    @gfx.draw_text(x + 4, y + 3, "Launcher", FmrbGfx::WHITE, LAUNCHER_TITLE_BG)

    # Icon grid
    content_y = y + LAUNCHER_TITLE_H + LAUNCHER_ICON_PAD_Y

    @launcher_apps.each_with_index do |app, i|
      col = i % LAUNCHER_ICON_COLS
      row = i / LAUNCHER_ICON_COLS

      icon_x = x + LAUNCHER_ICON_PAD_X + col * (LAUNCHER_ICON_W + LAUNCHER_ICON_PAD_X)
      icon_y = content_y + row * (LAUNCHER_ICON_H + LAUNCHER_ICON_PAD_Y)

      # Icon background
      bg = (i == @launcher_selected) ? LAUNCHER_ICON_SEL : LAUNCHER_ICON_BG
      @gfx.fill_rect(icon_x, icon_y, LAUNCHER_ICON_W, LAUNCHER_ICON_H - 10, bg)
      #@gfx.draw_rect(icon_x, icon_y, LAUNCHER_ICON_W, LAUNCHER_ICON_H - 10, 0x00)

      # Draw icon bitmap or fallback character
      icon_data = app[:icon_file] ? load_icon(app[:icon_file]) : nil
      if icon_data
        icon_rows = icon_data[:rows]
        icon_h = icon_rows.size
        icon_w = icon_rows[0] ? icon_rows[0].length : 0
        # Scale to fit icon area (2x for 12x12 icons)
        scale = [(LAUNCHER_ICON_W - 4) / icon_w, (LAUNCHER_ICON_H - 14) / icon_h].min
        scale = 1 if scale < 1
        bmp_x = icon_x + (LAUNCHER_ICON_W - icon_w * scale) / 2
        bmp_y = icon_y + (LAUNCHER_ICON_H - 10 - icon_h * scale) / 2
        draw_icon_bitmap(bmp_x, bmp_y, icon_data, scale, bg)
      else
        char_x = icon_x + (LAUNCHER_ICON_W - 6) / 2
        char_y = icon_y + (LAUNCHER_ICON_H - 10 - 8) / 2
        @gfx.draw_text(char_x, char_y, app[:icon_char] || "?", 0x00, bg)
      end

      # Label below icon
      label = app[:label]
      label_x = icon_x + (LAUNCHER_ICON_W - label.length * 6) / 2
      label_y = icon_y + LAUNCHER_ICON_H - 8
      @gfx.draw_text(label_x, label_y, label, LAUNCHER_TEXT)
    end
  end

  # ---- State management ----

  def open_dropdown
    return if @dropdown_open
    @dropdown_open = true
    close_launcher

    dropdown_h = DROPDOWN_ITEM_H * DROPDOWN_ITEMS.size + 2
    notify_overlay_state(true, DROPDOWN_X, DROPDOWN_Y, DROPDOWN_W, dropdown_h)
    draw_foreground
  end

  def close_dropdown
    return unless @dropdown_open
    @dropdown_open = false
    unless @launcher_open
      notify_overlay_state(false, 0, 0, 0, 0)
    end
    draw_foreground
  end

  def open_launcher
    @launcher_open = true
    @launcher_selected = -1
    @last_click_time = 0
    @last_click_idx = -1

    # Notify kernel: overlay covers launcher area
    notify_overlay_state(true, @launcher_x, @launcher_y, LAUNCHER_W, LAUNCHER_H)
    draw_foreground
  end

  def close_launcher
    return unless @launcher_open
    @launcher_open = false
    @launcher_selected = -1
    notify_overlay_state(false, 0, 0, 0, 0)
    draw_foreground
  end

  def notify_overlay_state(active, x, y, w, h)
    data = {
      "cmd" => "overlay_state",
      "active" => active,
      "rect_x" => x, "rect_y" => y,
      "rect_w" => w, "rect_h" => h
    }
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
  end

  def spawn_app(app_name)
    Log.info("Requesting spawn: #{app_name}")
    data = { "cmd" => "spawn", "app_name" => app_name }
    success = send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    if success
      Log.info("Spawn request sent successfully")
    else
      Log.error("Failed to send spawn request")
    end
  end

  # ---- Update loop ----

  # ---- File selector ----

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

    os_path = to_os_dir_path(@file_selector_dir)
    begin
      dir = Dir.open(os_path)
      entries = []
      while (e = dir.read)
        entries << e unless e == "." || e == ".."
      end
      dir.close

      entries.sort.each do |name|
        # Try to open as directory
        full = "#{os_path}/#{name}"
        full = "#{os_path}#{name}" if os_path.end_with?("/")
        is_dir = false
        begin
          d = Dir.open(full)
          d.close
          is_dir = true
        rescue
        end
        @file_selector_entries << { name: name, is_dir: is_dir }
      end
    rescue => e
      Log.warn("File selector: cannot scan #{os_path}: #{e.message}")
    end

    @file_selector_scroll = 0
    @file_selector_selected = -1
  end

  def to_os_dir_path(virtual_path)
    fs_root = @platform == :linux ? "flash" : "/flash"
    if virtual_path == "/"
      fs_root
    else
      "#{fs_root}#{virtual_path}"
    end
  end

  def to_file_path(virtual_path)
    virtual_path.start_with?("/") ? virtual_path[1..-1] : virtual_path
  end

  def draw_file_selector
    return unless @file_selector_open

    x = @fsel_x
    y = @fsel_y

    # Window
    @gfx.fill_rect(x, y, FSEL_W, FSEL_H, FSEL_BG)
    @gfx.draw_rect(x, y, FSEL_W, FSEL_H, 0x60)

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
      @gfx.draw_rect(field_x, bottom_y - 1, field_w, 10, 0x60)
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

    max_visible.times do |i|
      idx = @file_selector_scroll + i
      break if idx >= @file_selector_entries.size

      entry = @file_selector_entries[idx]
      item_y = list_y + i * FSEL_ITEM_H

      if idx == @file_selector_selected
        @gfx.fill_rect(x + 2, item_y, FSEL_W - 4, FSEL_ITEM_H, FSEL_SEL_BG)
        text_color = FmrbGfx::WHITE
        text_bg = FSEL_SEL_BG
      else
        text_color = entry[:is_dir] ? FSEL_DIR_COLOR : FSEL_TEXT
        text_bg = FSEL_BG
      end

      prefix = entry[:is_dir] ? "[" : " "
      suffix = entry[:is_dir] ? "]" : ""
      label = "#{prefix}#{entry[:name]}#{suffix}"
      label = label[0, (FSEL_W - 12) / 6] if label.length > (FSEL_W - 12) / 6
      @gfx.draw_text(x + 6, item_y + 2, label, text_color, text_bg)
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
          close_file_selector(to_file_path(path))
        end
        return
      end
    end

    # File list
    list_y = @fsel_y + FSEL_TITLE_H + 2
    list_h = bottom_y - list_y - 2
    max_visible = list_h / FSEL_ITEM_H

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
            close_file_selector(to_file_path(path))
          end
        end
      end
    end
  end

  def hit_file_selector?(x, y)
    x >= @fsel_x && x < @fsel_x + FSEL_W &&
      y >= @fsel_y && y < @fsel_y + FSEL_H
  end

  def on_update()
    draw_memory_stats
    @counter += 1
    330
  end

  # ---- Event handling ----

  def on_control(msg)
    if msg["cmd"] == "file_select"
      open_file_selector(msg["requester_pid"], msg["mode"] || "open")
    end
  end

  def on_event(ev)
    if ev[:type] == :mouse_up
      handle_click(ev[:x], ev[:y])
    end

    # Key input for file selector filename (save mode)
    if @file_selector_open && @file_selector_mode == "save" && ev[:type] == :key_down
      character = ev[:character] || 0
      if character == 10 || character == 13  # Enter
        if @file_selector_filename.length > 0
          path = if @file_selector_dir == "/"
                   "/#{@file_selector_filename}"
                 else
                   "#{@file_selector_dir}/#{@file_selector_filename}"
                 end
          close_file_selector(to_file_path(path))
        end
      elsif character == 8  # Backspace
        if @file_selector_filename.length > 0
          @file_selector_filename = @file_selector_filename[0...-1]
          draw_foreground
        end
      elsif character >= 32 && character <= 126  # Printable
        @file_selector_filename += character.chr
        draw_foreground
      end
    end
  end

  def handle_click(x, y)
    # File selector has priority
    if @file_selector_open
      if hit_file_selector?(x, y)
        handle_file_selector_click(x, y)
        return
      end
      close_file_selector(nil)
      return
    end

    # Launcher window has priority when open
    if @launcher_open
      if hit_launcher?(x, y)
        handle_launcher_click(x, y)
        return
      end
      # Click outside launcher - close it
      close_launcher
      return
    end

    if @dropdown_open
      if hit_dropdown?(x, y)
        handle_dropdown_click(x, y)
        return
      end
      close_dropdown
      return
    end

    # Menu bar
    if y < MENU_BAR_HEIGHT && x < 80
      open_dropdown
    end
  end

  def hit_dropdown?(x, y)
    dropdown_h = DROPDOWN_ITEM_H * DROPDOWN_ITEMS.size + 2
    x >= DROPDOWN_X && x < DROPDOWN_X + DROPDOWN_W &&
      y >= DROPDOWN_Y && y < DROPDOWN_Y + dropdown_h
  end

  def handle_dropdown_click(x, y)
    item_idx = (y - DROPDOWN_Y - 1) / DROPDOWN_ITEM_H
    return if item_idx < 0 || item_idx >= DROPDOWN_ITEMS.size

    item = DROPDOWN_ITEMS[item_idx]
    close_dropdown

    if item[:label] == "Launcher"
      open_launcher
    elsif item[:app]
      spawn_app(item[:app])
    end
  end

  def hit_launcher?(x, y)
    x >= @launcher_x && x < @launcher_x + LAUNCHER_W &&
      y >= @launcher_y && y < @launcher_y + LAUNCHER_H
  end

  def handle_launcher_click(x, y)
    # Icon hit test
    content_y = @launcher_y + LAUNCHER_TITLE_H + LAUNCHER_ICON_PAD_Y
    icon_idx = find_icon_at(x, y, content_y)

    if icon_idx >= 0 && icon_idx < @launcher_apps.size
      # Double-click detection
      now = @counter
      if icon_idx == @last_click_idx && (now - @last_click_time) < 5
        # Double click - launch app
        app_name = @launcher_apps[icon_idx][:app]
        close_launcher
        spawn_app(app_name)
      else
        # Single click - select
        @launcher_selected = icon_idx
        @last_click_idx = icon_idx
        @last_click_time = now
        draw_foreground
      end
    else
      @launcher_selected = -1
      draw_foreground
    end
  end

  def find_icon_at(x, y, content_y)
    @launcher_apps.each_with_index do |app, i|
      col = i % LAUNCHER_ICON_COLS
      row = i / LAUNCHER_ICON_COLS

      icon_x = @launcher_x + LAUNCHER_ICON_PAD_X + col * (LAUNCHER_ICON_W + LAUNCHER_ICON_PAD_X)
      icon_y = content_y + row * (LAUNCHER_ICON_H + LAUNCHER_ICON_PAD_Y)

      if x >= icon_x && x < icon_x + LAUNCHER_ICON_W &&
         y >= icon_y && y < icon_y + LAUNCHER_ICON_H
        return i
      end
    end
    -1
  end

  def on_suspend
    # Hide foreground canvas (clear to transparent)
    @gfx.clear(0x01)
    @gfx.present
    Log.info("Desktop suspended")
  end

  def on_resume
    draw_foreground
    draw_background
    Log.info("Desktop resumed")
  end

  def on_destroy
    Log.info("Destroyed")
  end
end

# Create and start the system desktop app instance
Log.info("SystemDesktopApp.new")
begin
  app = SystemDesktopApp.new
  Log.info("SystemDesktopApp created successfully")
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
