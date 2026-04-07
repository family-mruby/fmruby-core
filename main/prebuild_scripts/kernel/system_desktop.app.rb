# System Desktop Application
# Manages two canvas layers:
#   @gfx    (z=254): Menu bar, dropdown, launcher window (foreground)
#   @bg_gfx (z=0):   Wallpaper, memory stats (background)

class SystemDesktopApp < FmrbApp
  MENU_BAR_HEIGHT = 13
  MENU_BG = 0xC5
  DROPDOWN_BG = 0x60
  DROPDOWN_TEXT = 0xFF
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
  LAUNCHER_BG = 0x60
  LAUNCHER_TITLE_BG = 0xC5
  LAUNCHER_ICON_BG = 0x49
  LAUNCHER_ICON_SEL = 0xC5
  LAUNCHER_TEXT = 0xFF

  # Built-in app entries
  BUILTIN_APPS = [
    { label: "Shell",  app: "default/shell",  icon_file: "icon/shell.icon" },
    { label: "Editor", app: "default/editor", icon_file: "icon/editor.icon" },
  ]

  # Default icon files per VM type
  VM_ICON_FILES = {
    "rb"  => "icon/ruby.icon",
    "lua" => "icon/lua.icon",
    "bas" => "icon/basic.icon",
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
    @icon_cache = {}  # icon_file => [[row_string, ...], color]

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
    @gfx.present
  end

  def draw_menu_bar
    @gfx.clear(0x00)  # Transparent (clear foreground)
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
    @gfx.draw_rect(x, y, LAUNCHER_W, LAUNCHER_H, 0x00)

    # Title bar
    @gfx.fill_rect(x, y, LAUNCHER_W, LAUNCHER_TITLE_H, LAUNCHER_TITLE_BG)
    @gfx.draw_text(x + 4, y + 3, "Launcher", FmrbGfx::WHITE, LAUNCHER_TITLE_BG)

    # Close button
    close_x = x + LAUNCHER_W - 12
    close_y = y + 3
    @gfx.fill_rect(close_x, close_y, 8, 8, 0xE0)
    @gfx.draw_line(close_x + 2, close_y + 2, close_x + 5, close_y + 5, FmrbGfx::WHITE)
    @gfx.draw_line(close_x + 5, close_y + 2, close_x + 2, close_y + 5, FmrbGfx::WHITE)

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
      @gfx.draw_rect(icon_x, icon_y, LAUNCHER_ICON_W, LAUNCHER_ICON_H - 10, 0x00)

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
        @gfx.draw_text(char_x, char_y, app[:icon_char] || "?", FmrbGfx::WHITE, bg)
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

  def on_update()
    draw_memory_stats
    @counter += 1
    330
  end

  # ---- Event handling ----

  def on_event(ev)
    if ev[:type] == :mouse_up
      handle_click(ev[:x], ev[:y])
    end
  end

  def handle_click(x, y)
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
    # Close button
    close_x = @launcher_x + LAUNCHER_W - 12
    close_y = @launcher_y + 3
    if x >= close_x && x < close_x + 8 && y >= close_y && y < close_y + 8
      close_launcher
      return
    end

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
