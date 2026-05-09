# Launcher module for SystemDesktopApp
# Provides app launcher window with icon grid and double-click launch

module LauncherMixin
  # Launcher window layout
  LAUNCHER_W = 300
  LAUNCHER_H = 190
  LAUNCHER_TITLE_H = 14
  LAUNCHER_ICON_W = 56
  LAUNCHER_ICON_H = 48
  LAUNCHER_ICON_COLS = 4
  LAUNCHER_ICON_PAD_X = 12
  LAUNCHER_ICON_PAD_Y = 8
  LAUNCHER_BG = FmrbConst::THEME_WINDOW_BG
  LAUNCHER_TITLE_BG = FmrbConst::THEME_MENU_BG
  LAUNCHER_ICON_BG = FmrbConst::THEME_WINDOW_BG
  LAUNCHER_ICON_SEL = FmrbGfx.rgb_to_332(200, 180, 180)
  LAUNCHER_TEXT = FmrbConst::THEME_TEXT

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

  # ---- Icon sprite lifecycle ----

  # Build SpriteImage + SpriteInstance for every app with an icon file.
  # Idempotent: instances already created are skipped. SpriteImages are shared
  # across apps that reference the same icon file.
  def ensure_icon_sprites
    @icon_sprite_images    ||= {}
    @icon_sprite_instances ||= []
    @icon_sprite_metrics   ||= []

    @launcher_apps.each_with_index do |app, idx|
      next if @icon_sprite_instances[idx]
      icon_file = app[:icon_file]
      next unless icon_file
      icon_data = load_icon(icon_file)
      next unless icon_data

      rows = icon_data[:rows]
      ih = rows.size
      iw = rows[0] ? rows[0].length : 0
      next if iw == 0 || ih == 0

      scale = [(LAUNCHER_ICON_W - 4) / iw, (LAUNCHER_ICON_H - 22) / ih].min
      scale = 1 if scale < 1
      spr_w = iw * scale
      spr_h = ih * scale

      img = @icon_sprite_images[icon_file]
      unless img
        color = icon_data[:color]
        img = SpriteImage.new(@gfx, width: spr_w, height: spr_h,
                              transparent_color: 0, use_transparent: true)
        img.draw do |g|
          g.fill_rect(0, 0, spr_w, spr_h, 0)
          rows.each_with_index do |row, iy|
            row.length.times do |ix|
              next unless row[ix] == '1'
              if scale > 1
                g.fill_rect(ix * scale, iy * scale, scale, scale, color)
              else
                g.set_pixel(ix, iy, color)
              end
            end
          end
        end
        @icon_sprite_images[icon_file] = img
      end

      inst = SpriteInstance.new(@gfx, img, x: 0, y: 0, z: 1)
      inst.visible = false
      @icon_sprite_instances[idx] = inst
      @icon_sprite_metrics[idx] = { bmp_w: spr_w, bmp_h: spr_h }
    end
  end

  # Hide every icon sprite. Called from close_launcher to keep the cache alive
  # but invisible.
  def hide_all_icon_sprites
    return unless @icon_sprite_instances
    @icon_sprite_instances.each { |inst| inst.visible = false if inst }
  end

  # Release every sprite/image the launcher created. Called from on_destroy.
  def destroy_icon_sprites
    if @icon_sprite_instances
      @icon_sprite_instances.each { |inst| inst.destroy if inst }
    end
    if @icon_sprite_images
      @icon_sprite_images.each_value { |img| img.destroy }
    end
    @icon_sprite_instances = []
    @icon_sprite_images = {}
    @icon_sprite_metrics = []
  end

  # Destroy only sprite instances; keep the SpriteImage cache (keyed by
  # icon_file) intact. Used by rescan: re-uploading icon bitmaps to WROVER
  # is expensive (~300ms each), but instances are cheap (just position +
  # image reference).
  def destroy_icon_instances_only
    if @icon_sprite_instances
      @icon_sprite_instances.each { |inst| inst.destroy if inst }
    end
    @icon_sprite_instances = []
    @icon_sprite_metrics = []
    # @icon_sprite_images is kept (cached by icon_file path).
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

  # ---- App scanning ----

  def scan_apps
    @launcher_apps = BUILTIN_APPS.dup
    builtin_count = BUILTIN_APPS.size
    # Try both ESP32 (LittleFS at /flash) and Linux (relative flash/) paths
    ["/flash/app", "flash/app"].each do |path|
      scan_app_dir(path)
    end
    # Keep BUILTIN_APPS fixed at the front; sort the scanned apps by label
    # so launcher order is stable regardless of filesystem enumeration order.
    scanned = @launcher_apps[builtin_count..-1] || []
    scanned.sort! { |a, b| a[:label] <=> b[:label] }
    @launcher_apps = @launcher_apps[0, builtin_count] + scanned
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
      path  # absolute path, keep as-is
    else
      path
    end
  end

  def parse_app_toml(toml_path, dir_path)
    label = nil
    icon_field = nil
    launcher_visible = true
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
        elsif line.start_with?("launcher_visible")
          m = line.split("=", 2)
          if m[1]
            v = m[1].strip.gsub('"', '').downcase
            launcher_visible = !(v == "false" || v == "0")
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

    return nil unless launcher_visible

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

  # ---- Launcher metrics ----

  def launcher_content_h
    LAUNCHER_H - LAUNCHER_TITLE_H - LAUNCHER_ICON_PAD_Y
  end

  def launcher_row_h
    LAUNCHER_ICON_H + LAUNCHER_ICON_PAD_Y
  end

  def launcher_visible_rows
    launcher_content_h / launcher_row_h
  end

  def launcher_total_rows
    (@launcher_apps.size + LAUNCHER_ICON_COLS - 1) / LAUNCHER_ICON_COLS
  end

  # ---- Launcher drawing ----

  def draw_launcher
    draw_launcher_frame
    draw_launcher_cells
  end

  # Repaint launcher only (skip menu bar / clock / taskbar). Used on scroll so
  # we do not send redraw commands for UI that has not changed.
  def redraw_launcher_only
    draw_launcher_frame
    draw_launcher_cells
    @gfx.present
  end

  # Window frame, title bar, scrollbar. Also paints the full launcher rect with
  # LAUNCHER_BG which overwrites the previous frame's cell content.
  def draw_launcher_frame
    x = @launcher_x
    y = @launcher_y
    @gfx.fill_rect(x, y, LAUNCHER_W, LAUNCHER_H, LAUNCHER_BG)
    @gfx.draw_rect(x, y, LAUNCHER_W, LAUNCHER_H, FmrbConst::THEME_BORDER)
    @gfx.fill_rect(x + 1, y + 1, LAUNCHER_W - 2, LAUNCHER_TITLE_H - 1, LAUNCHER_TITLE_BG)
    @gfx.draw_text(x + 4, y + 3, "Launcher", FmrbGfx::WHITE, LAUNCHER_TITLE_BG)
    bar_y = y + LAUNCHER_TITLE_H
    bar_h = LAUNCHER_H - LAUNCHER_TITLE_H
    draw_scrollbar(@launcher_scroll, launcher_total_rows, launcher_visible_rows,
                   x, bar_y, LAUNCHER_W, bar_h)
  end

  # Icon cell backgrounds, labels, and sprite placement/visibility.
  def draw_launcher_cells
    x = @launcher_x
    y = @launcher_y
    vis_rows = launcher_visible_rows
    content_y = y + LAUNCHER_TITLE_H + LAUNCHER_ICON_PAD_Y
    start_idx = @launcher_scroll * LAUNCHER_ICON_COLS
    vis_end = start_idx + vis_rows * LAUNCHER_ICON_COLS

    # Hide sprites that are not in the visible range
    if @icon_sprite_instances
      @icon_sprite_instances.each_with_index do |inst, idx|
        next unless inst
        inst.visible = false if idx < start_idx || idx >= vis_end
      end
    end

    vis_rows.times do |vrow|
      LAUNCHER_ICON_COLS.times do |col|
        i = start_idx + vrow * LAUNCHER_ICON_COLS + col
        break if i >= @launcher_apps.size

        app = @launcher_apps[i]
        icon_x = x + LAUNCHER_ICON_PAD_X + col * (LAUNCHER_ICON_W + LAUNCHER_ICON_PAD_X)
        icon_y = content_y + vrow * (LAUNCHER_ICON_H + LAUNCHER_ICON_PAD_Y)

        if icon_y + LAUNCHER_ICON_H > y + LAUNCHER_H
          inst = @icon_sprite_instances ? @icon_sprite_instances[i] : nil
          inst.visible = false if inst
          next
        end

        bg = (i == @launcher_selected) ? LAUNCHER_ICON_SEL : LAUNCHER_ICON_BG
        @gfx.fill_rect(icon_x, icon_y, LAUNCHER_ICON_W, LAUNCHER_ICON_H - 18, bg)

        inst = @icon_sprite_instances ? @icon_sprite_instances[i] : nil
        metrics = @icon_sprite_metrics ? @icon_sprite_metrics[i] : nil
        if inst && metrics
          bmp_x = icon_x + (LAUNCHER_ICON_W - metrics[:bmp_w]) / 2
          bmp_y = icon_y + (LAUNCHER_ICON_H - 18 - metrics[:bmp_h]) / 2
          inst.move(bmp_x, bmp_y)
          inst.visible = true
        else
          char_x = icon_x + (LAUNCHER_ICON_W - 6) / 2
          char_y = icon_y + (LAUNCHER_ICON_H - 18 - 8) / 2
          @gfx.draw_text(char_x, char_y, app[:icon_char] || "?", 0x00, bg)
        end

        # Label below icon (2-line with truncation)
        label = app[:label]
        max_chars = LAUNCHER_ICON_W / 6
        if label.length <= max_chars
          label_x = icon_x + (LAUNCHER_ICON_W - label.length * 6) / 2
          @gfx.draw_text(label_x, icon_y + LAUNCHER_ICON_H - 8, label, LAUNCHER_TEXT)
        else
          line1 = label[0, max_chars]
          line2 = label[max_chars..]
          if line2.length > max_chars
            line2 = line2[0, max_chars - 2] + ".."
          end
          l1x = icon_x + (LAUNCHER_ICON_W - line1.length * 6) / 2
          l2x = icon_x + (LAUNCHER_ICON_W - line2.length * 6) / 2
          @gfx.draw_text(l1x, icon_y + LAUNCHER_ICON_H - 16, line1, LAUNCHER_TEXT)
          @gfx.draw_text(l2x, icon_y + LAUNCHER_ICON_H - 8, line2, LAUNCHER_TEXT)
        end
      end
    end
  end

  # Redraw a single icon cell background. The sprite on top of this cell stays
  # in place and is composited by WROVER, so we only repaint the bg rect. For
  # fallback-character apps (no sprite) we redraw the character.
  def redraw_launcher_icon(idx, bg)
    return if idx < 0 || idx >= @launcher_apps.size
    start_idx = @launcher_scroll * LAUNCHER_ICON_COLS
    vis_end = start_idx + launcher_visible_rows * LAUNCHER_ICON_COLS
    return if idx < start_idx || idx >= vis_end

    vrow = (idx - start_idx) / LAUNCHER_ICON_COLS
    col  = (idx - start_idx) % LAUNCHER_ICON_COLS
    content_y = @launcher_y + LAUNCHER_TITLE_H + LAUNCHER_ICON_PAD_Y
    icon_x = @launcher_x + LAUNCHER_ICON_PAD_X + col * (LAUNCHER_ICON_W + LAUNCHER_ICON_PAD_X)
    icon_y = content_y + vrow * (LAUNCHER_ICON_H + LAUNCHER_ICON_PAD_Y)

    @gfx.fill_rect(icon_x, icon_y, LAUNCHER_ICON_W, LAUNCHER_ICON_H - 18, bg)

    inst = @icon_sprite_instances ? @icon_sprite_instances[idx] : nil
    unless inst
      app = @launcher_apps[idx]
      char_x = icon_x + (LAUNCHER_ICON_W - 6) / 2
      char_y = icon_y + (LAUNCHER_ICON_H - 18 - 8) / 2
      @gfx.draw_text(char_x, char_y, app[:icon_char] || "?", 0x00, bg)
    end
  end

  # ---- Launcher state ----

  def open_launcher
    @launcher_open = true
    @launcher_selected = -1
    @launcher_scroll = 0
    @last_click_time = 0
    @last_click_idx = -1

    # Notify kernel: overlay covers launcher area
    notify_overlay_state(true, @launcher_x, @launcher_y, LAUNCHER_W, LAUNCHER_H)
    draw_foreground
  end

  # Right-click anywhere inside the launcher rescans the app directories so
  # newly added/removed apps (e.g. via `create_app` or BLE upload) appear
  # without rebooting.
  def handle_launcher_right_click(x, y)
    rescan_launcher
  end

  # Show immediate visual feedback in the title bar so the user knows the
  # rescan was accepted. The actual work (filesystem scan + sprite uploads)
  # may take 1-2 seconds before the new icons are visible.
  def draw_launcher_status(text, color = FmrbGfx::YELLOW)
    x = @launcher_x
    y = @launcher_y
    @gfx.fill_rect(x + 1, y + 1, LAUNCHER_W - 2, LAUNCHER_TITLE_H - 1, LAUNCHER_TITLE_BG)
    @gfx.draw_text(x + 4, y + 3, text, color, LAUNCHER_TITLE_BG)
    @gfx.present
  end

  # Re-scan /flash/app/ for apps and rebuild icon sprites if the app list
  # changed. Called from handle_launcher_right_click.
  def rescan_launcher
    # Immediate feedback: change the title bar before the slow work starts.
    draw_launcher_status("Rescanning...")

    prev_handles = @launcher_apps.map { |a| a[:app] }
    scan_apps
    new_handles = @launcher_apps.map { |a| a[:app] }

    if prev_handles != new_handles
      # App list changed: existing instance indexes may no longer match the
      # new @launcher_apps order, so rebuild instances. The SpriteImage cache
      # (icon bitmaps already uploaded to WROVER) is kept intact.
      destroy_icon_instances_only
      Log.info("Launcher: rescan rebuilt instances (#{prev_handles.size} -> #{new_handles.size})")
    else
      Log.info("Launcher: rescan no change (#{new_handles.size} apps)")
    end
    ensure_icon_sprites

    # Reset selection/scroll because indexes may have shifted.
    @launcher_selected = -1
    @launcher_scroll = 0
    draw_foreground
  end

  def close_launcher
    return unless @launcher_open
    @launcher_open = false
    @launcher_selected = -1
    hide_all_icon_sprites
    notify_overlay_state(false, 0, 0, 0, 0)
    draw_foreground
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

  # ---- Launcher hit testing ----

  def hit_launcher?(x, y)
    x >= @launcher_x && x < @launcher_x + LAUNCHER_W &&
      y >= @launcher_y && y < @launcher_y + LAUNCHER_H
  end

  def handle_launcher_click(x, y)
    # Scroll bar hit test
    bar_y = @launcher_y + LAUNCHER_TITLE_H
    bar_h = LAUNCHER_H - LAUNCHER_TITLE_H
    sb = scrollbar_hit(x, y, @launcher_x, bar_y, LAUNCHER_W, bar_h)
    if sb
      sb == :up ? launcher_scroll_up : launcher_scroll_down
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
        # Single click - partial redraw for selection change
        prev = @launcher_selected
        redraw_launcher_icon(prev, LAUNCHER_ICON_BG) if prev >= 0
        if icon_idx == prev
          @launcher_selected = -1
        else
          @launcher_selected = icon_idx
          redraw_launcher_icon(icon_idx, LAUNCHER_ICON_SEL)
        end
        @last_click_idx = icon_idx
        @last_click_time = now
        @gfx.present
      end
    else
      if @launcher_selected >= 0
        redraw_launcher_icon(@launcher_selected, LAUNCHER_ICON_BG)
        @launcher_selected = -1
        @gfx.present
      end
    end
  end

  def find_icon_at(x, y, content_y)
    start_idx = @launcher_scroll * LAUNCHER_ICON_COLS
    vis_rows = launcher_visible_rows

    vis_rows.times do |vrow|
      LAUNCHER_ICON_COLS.times do |col|
        i = start_idx + vrow * LAUNCHER_ICON_COLS + col
        return -1 if i >= @launcher_apps.size

        icon_x = @launcher_x + LAUNCHER_ICON_PAD_X + col * (LAUNCHER_ICON_W + LAUNCHER_ICON_PAD_X)
        icon_y = content_y + vrow * (LAUNCHER_ICON_H + LAUNCHER_ICON_PAD_Y)

        if x >= icon_x && x < icon_x + LAUNCHER_ICON_W &&
           y >= icon_y && y < icon_y + LAUNCHER_ICON_H
          return i
        end
      end
    end
    -1
  end

  def launcher_scroll_up
    if @launcher_scroll > 0
      @launcher_scroll -= 1
      redraw_launcher_only
    end
  end

  def launcher_scroll_down
    max_scroll = launcher_total_rows - launcher_visible_rows
    if max_scroll > 0 && @launcher_scroll < max_scroll
      @launcher_scroll += 1
      redraw_launcher_only
    end
  end
end
