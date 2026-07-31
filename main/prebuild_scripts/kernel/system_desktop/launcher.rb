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

  # Built-in app entries. Labels are resolved through FmrbI18n at scan time so
  # they pick up the active language ("ja" / "en") from system_conf.toml.
  def builtin_apps
    [
      { label: FmrbI18n.t(:shell),  app: "default/shell",  icon_file: "usr/share/icon/shell.icon" },
      { label: FmrbI18n.t(:editor), app: "default/editor", icon_file: "usr/share/icon/editor.icon" },
    ]
  end

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
    "py" => "P",
  }

  # ---- Icon sprite lifecycle ----

  # Icon artwork is shipped as a BMP next to its .icon source (see
  # tool/gen_icon_bmp.rb, "rake icons"). The launcher used to parse the .icon
  # text here and push the bitmap to the graphics side one GFX command per
  # pixel, which cost about 2 s of boot for five icons. graphics-audio decodes
  # a BMP itself, so the file is transferred there once and loaded with
  # SpriteImage#load_bmp instead - the same route flappy.rb and rpg_demo use
  # for their sprites.
  #
  # The generator bakes in the 2x scale the old code applied, so a 12x12 source
  # still lands as a 24x24 sprite and the launcher layout is unchanged.
  ICON_SPRITE_W = 24
  ICON_SPRITE_H = 24
  ICON_CACHE_DIR = "sprites/launcher"
  S_ICON_EXT = ".icon"
  S_BMP_EXT  = ".bmp"

  # App entries still carry the .icon path (that is what the .toml files and
  # VM_ICON_FILES name); the BMP beside it is what actually gets loaded.
  def icon_bmp_source(icon_file)
    return icon_file unless icon_file.end_with?(S_ICON_EXT)
    "#{icon_file[0, icon_file.length - S_ICON_EXT.length]}#{S_BMP_EXT}"
  end

  # Push the BMP to graphics-audio once and build the sprite image from it.
  # Returns nil when the artwork is missing so the caller just skips the icon.
  def build_icon_sprite_image(icon_file)
    src = icon_bmp_source(icon_file)
    dest = "#{ICON_CACHE_DIR}/#{src.split(S_SLASH).last}"
    status = @gfx.file_status(dest)
    unless status && status[:exists]
      @gfx.transfer_file(src, dest: dest)
    end
    img = SpriteImage.new(@gfx, width: ICON_SPRITE_W, height: ICON_SPRITE_H,
                          transparent_color: 0, use_transparent: true)
    img.load_bmp(dest)
    img
  rescue => e
    Log.warn("Icon load failed for #{icon_file}: #{e.message}")
    nil
  end

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

      img = @icon_sprite_images[icon_file]
      unless img
        img = build_icon_sprite_image(icon_file)
        next unless img
        @icon_sprite_images[icon_file] = img
      end

      inst = SpriteInstance.new(@gfx, img, x: 0, y: 0, z: 1)
      inst.visible = false
      @icon_sprite_instances[idx] = inst
      @icon_sprite_metrics[idx] = { bmp_w: ICON_SPRITE_W, bmp_h: ICON_SPRITE_H }
      # Yield so status_led gets to toggle while sprites are being built.
      Machine.delay_ms(1)
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

  # ---- App scanning ----

  # String literals used by the boot-scan hot paths, hoisted out of the loops.
  #
  # Passing a literal to a method is far more expensive than passing a constant
  # on this build: `key == "literal"` measured ~40x the cost of
  # `key == CONSTANT`, reproduced across boots. Hoisting every literal out of
  # the per-line and per-entry loops took the parse phase from 5.0 s to 3.0 s
  # over the same 43 files, which is the number to trust - short micro-loops
  # are not reproducible here (a GC cycle landing inside one swings it by 100x),
  # so treat any single per-call figure with suspicion and measure aggregates.
  #
  # See doc/boot_performance.md.
  KEY_APP_SCREEN_NAME  = "app_screen_name"
  KEY_LAUNCHER_VISIBLE = "launcher_visible"
  KEY_ICON             = "icon"
  S_DOT     = "."
  S_DOTDOT  = ".."
  S_SLASH   = "/"
  S_HASH    = "#"
  S_EQ      = "="
  S_QUOTE   = '"'
  S_NEWLINE = "\n"
  S_TOML    = ".toml"
  S_FALSE   = "false"
  S_ZERO    = "0"
  S_TAB     = "\t"
  S_EMPTY   = ""
  SCRIPT_EXTS = ["rb", "lua", "bas", "py"]

  # ---- App index cache ----
  #
  # Scanning /app costs seconds: 43 .toml files to open, read and parse plus
  # the directory walk. The result almost never changes between boots, so it is
  # written to a flat file and replayed instead. Boot trusts the cache without
  # revalidating it - checking would mean walking the directories again, which
  # is most of what the cache exists to avoid. The launcher already has an
  # explicit rescan on right-click, and that is what picks up added or removed
  # apps; it rewrites the cache afterwards.
  #
  # Format: a version+language header line, then one app per line, tab
  # separated: label, app path, icon char, icon file. No quoting - the scan
  # drops any entry whose fields contain a tab (see save_launcher_cache), which
  # in practice never happens. The language is part of the header because app
  # labels are resolved per language at scan time, so a language change must
  # invalidate the cache.
  CACHE_PATH    = "/data/launcher_index"
  CACHE_VERSION = "1"
  CACHE_MAX_BYTES = 16384

  # Load the cached app list, or nil when there is nothing usable. Built-ins
  # are not cached: their labels are translated at scan time, so they are
  # rebuilt every boot and only the scanned entries come from here.
  def load_launcher_cache
    file = File.open(CACHE_PATH, "r")
    content = file.read(CACHE_MAX_BYTES)
    file.close
    return nil unless content

    lines = content.split(S_NEWLINE)
    header = lines[0]
    return nil unless header
    h = header.split(S_TAB)
    return nil unless h[1] && h[0] == CACHE_VERSION && h[1] == FmrbI18n.lang

    apps = []
    i = 1
    n = lines.size
    while i < n
      f = lines[i].split(S_TAB)
      i += 1
      next unless f[3]
      icon = f[3]
      icon = nil if icon == S_EMPTY
      apps << { label: f[0], app: f[1], icon_char: f[2], icon_file: icon }
    end
    apps.empty? ? nil : apps
  rescue
    nil
  end

  def save_launcher_cache(apps)
    buf = "#{CACHE_VERSION}#{S_TAB}#{FmrbI18n.lang}\n"
    apps.each do |a|
      label = a[:label]
      path  = a[:app]
      icon  = a[:icon_file] || S_EMPTY
      # A tab in any field would corrupt the record on read-back. Skip the
      # entry rather than write something that cannot be parsed; the app is
      # still reachable, it just misses the cache until the fields change.
      next if label.include?(S_TAB) || path.include?(S_TAB) || icon.include?(S_TAB)
      buf += "#{label}#{S_TAB}#{path}#{S_TAB}#{a[:icon_char]}#{S_TAB}#{icon}\n"
    end
    file = File.open(CACHE_PATH, "w")
    file.write(buf)
    file.close
    true
  rescue => e
    Log.warn("Launcher cache write failed: #{e.message}")
    false
  end

  # force: skip the cache and walk /app. Used by the right-click rescan, which
  # is the only thing that picks up added or removed apps.
  def scan_apps(force = false)
    @launcher_apps = builtin_apps
    builtin_count = @launcher_apps.size

    unless force
      cached = load_launcher_cache
      if cached
        @launcher_apps = @launcher_apps + cached
        Log.info("Launcher: #{@launcher_apps.size} apps from cache")
        return
      end
    end

    # Single virtual path - the HAL resolver maps "/app" to LittleFS on ESP32
    # and to the local "flash/app" directory on Linux.
    scan_app_dir("/app")
    # Keep BUILTIN_APPS fixed at the front; sort the scanned apps by label
    # so launcher order is stable regardless of filesystem enumeration order.
    scanned = @launcher_apps[builtin_count..-1] || []
    scanned.sort! { |a, b| a[:label] <=> b[:label] }
    @launcher_apps = @launcher_apps[0, builtin_count] + scanned
    save_launcher_cache(scanned)
    Log.info("Launcher: #{@launcher_apps.size} apps found")
  end

  # Read a directory into an array of entry names, or nil when the path is not
  # a directory / cannot be opened.
  def read_dir_entries(path)
    dir = Dir.open(path)
    entries = []
    while (e = dir.read)
      entries << e unless e == S_DOT || e == S_DOTDOT
    end
    dir.close
    entries
  rescue
    nil
  end

  # Whether a name can be a directory in the app layout. Every app file
  # carries an extension and bundle directories (e.g. /app/game/rpg_demo/) do
  # not, so this stands in for opening each entry as a directory and rescuing
  # the failure. That probe ran on every non-.toml entry: 42 failed opendir
  # calls, and 42 mruby exceptions, per boot scan.
  def dir_candidate?(name)
    !name.include?(S_DOT)
  end

  # Pick the script extension for "base" out of a directory listing already in
  # memory. Previously this opened "<base>.rb", then ".lua", then ".bas" until
  # one succeeded - up to three filesystem opens per app, for information the
  # enumeration had already produced.
  def find_script_ext(entry_names, base)
    SCRIPT_EXTS.each do |ext|
      return ext if entry_names.include?("#{base}.#{ext}")
    end
    nil
  end

  def scan_app_dir(base_path)
    entries = read_dir_entries(base_path)
    unless entries
      Log.warn("Cannot scan #{base_path}")
      return
    end

    entries.each do |entry|
      next unless dir_candidate?(entry)
      path = "#{base_path}/#{entry}"
      sub_entries = read_dir_entries(path)
      next unless sub_entries

      sub_entries.each do |f|
        if f.end_with?(S_TOML)
          app_entry = parse_app_toml("#{path}/#{f}", path, sub_entries)
          if app_entry
            @launcher_apps << app_entry
            Log.info("Found app: #{app_entry[:label]} (#{app_entry[:app]})")
          end
        elsif dir_candidate?(f)
          # 3rd-level scan: a subdirectory under a category may itself
          # contain a .toml + script, so a self-contained app bundle
          # (assets co-located with .rb / .toml, e.g. /app/game/rpg_demo/)
          # also shows up in the launcher.
          full = "#{path}/#{f}"
          dd_entries = read_dir_entries(full)
          next unless dd_entries
          dd_entries.each do |df|
            next unless df.end_with?(S_TOML)
            app_entry = parse_app_toml("#{full}/#{df}", full, dd_entries)
            if app_entry
              @launcher_apps << app_entry
              Log.info("Found app: #{app_entry[:label]} (#{app_entry[:app]})")
            end
          end
        end
      end

      # Yield to FreeRTOS so lower-priority tasks on Core 1 (status_led
      # prio 2) get scheduled during the filesystem scan. Each top-level
      # entry may issue many hw_proxy round-trips that keep this task in
      # ready state continuously otherwise.
      Machine.delay_ms(1)
    end
  end

  # entry_names is the listing of dir_path, used to resolve the script file
  # without touching the filesystem again.
  def parse_app_toml(toml_path, dir_path, entry_names)
    label = nil
    label_lang = nil   # Localized label from app_screen_name_<lang>, takes precedence
    icon_field = nil
    launcher_visible = true
    lang_key = "app_screen_name_#{FmrbI18n.lang}"
    begin
      file = File.open(toml_path, "r")
      content = file.read
      file.close

      # Hot loop: runs for every line of every app's .toml at boot, so it is
      # written around what was measured on device.
      #
      # String#gsub is implemented in Ruby in picoruby and dominated this loop
      # until it was removed (scan 13.0 s -> 7.8 s); the surrounding quotes are
      # stripped by slicing instead. Allocating calls are expensive in general,
      # so the key is compared against constants before the value is touched at
      # all: a line that is not one of the four keys we care about does no work
      # beyond its own strip.
      content.split(S_NEWLINE).each do |line|
        line = line.strip
        next if line.empty? || line.start_with?(S_HASH)
        eq = line.index(S_EQ)
        next unless eq
        key = line[0, eq].strip
        next unless key == lang_key || key == KEY_APP_SCREEN_NAME ||
                    key == KEY_LAUNCHER_VISIBLE || key == KEY_ICON
        val = line[eq + 1, line.length - eq - 1].strip
        vlen = val.length
        if vlen >= 2 && val[0] == S_QUOTE && val[vlen - 1] == S_QUOTE
          val = val[1, vlen - 2]
        end
        case key
        when lang_key
          label_lang = val
        when KEY_APP_SCREEN_NAME
          label = val
        when KEY_LAUNCHER_VISIBLE
          v = val.downcase
          launcher_visible = !(v == S_FALSE || v == S_ZERO)
        when KEY_ICON
          icon_field = val
        end
      end
    rescue => e
      Log.warn("Cannot read #{toml_path}: #{e.message}")
      return nil
    end

    return nil unless launcher_visible

    # Derive script filename from toml filename
    toml_name = toml_path.split(S_SLASH).last
    base = toml_name.sub(S_TOML, "")

    ext = find_script_ext(entry_names, base)
    return nil unless ext

    icon_char = VM_ICONS[ext] || "?"
    icon_file = icon_field || VM_ICON_FILES[ext]
    label = label_lang || label || base
    app_path = "#{dir_path}/#{base}.#{ext}"

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
    @gfx.draw_text(x + 4, y + 3, FmrbI18n.t(:launcher), FmrbGfx::WHITE, LAUNCHER_TITLE_BG, mixed: true)
    bar_y = y + LAUNCHER_TITLE_H
    bar_h = LAUNCHER_H - LAUNCHER_TITLE_H
    draw_scrollbar(@launcher_scroll, launcher_total_rows, launcher_visible_rows,
                   x, bar_y, LAUNCHER_W-1, bar_h)
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
      idx = 0
      isn = @icon_sprite_instances.length
      while idx < isn
        inst = @icon_sprite_instances[idx]
        if inst && (idx < start_idx || idx >= vis_end)
          inst.visible = false
        end
        idx += 1
      end
    end

    vrow = 0
    while vrow < vis_rows
      col = -1
      while (col += 1) < LAUNCHER_ICON_COLS
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

        # Label below icon. Uses mixed:true so Japanese (UTF-8) labels render
        # with misaki_8 (8px) while ASCII stays on Font0 (6px). Width helpers
        # in FmrbI18n keep centering and ellipsis correct for either case.
        label = app[:label]
        max_px = LAUNCHER_ICON_W
        full_w = FmrbI18n.text_width(label)
        if full_w <= max_px
          label_x = icon_x + (LAUNCHER_ICON_W - full_w) / 2
          @gfx.draw_text(label_x, icon_y + LAUNCHER_ICON_H - 8, label, LAUNCHER_TEXT, mixed: true)
        else
          line1 = FmrbI18n.truncate_to(label, max_px, "")
          consumed = line1.bytesize
          line2 = (consumed < label.bytesize) ? label.byteslice(consumed, label.bytesize - consumed).to_s : ""
          line2 = FmrbI18n.truncate_to(line2, max_px) if line2.length > 0
          w1 = FmrbI18n.text_width(line1)
          w2 = FmrbI18n.text_width(line2)
          l1x = icon_x + (LAUNCHER_ICON_W - w1) / 2
          l2x = icon_x + (LAUNCHER_ICON_W - w2) / 2
          @gfx.draw_text(l1x, icon_y + LAUNCHER_ICON_H - 16, line1, LAUNCHER_TEXT, mixed: true)
          @gfx.draw_text(l2x, icon_y + LAUNCHER_ICON_H - 8,  line2, LAUNCHER_TEXT, mixed: true)
        end
      end
      vrow += 1
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
    update_composite_regions
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
    @gfx.draw_text(x + 4, y + 3, text, color, LAUNCHER_TITLE_BG, mixed: true)
    @gfx.present
  end

  # Re-scan /app/ for apps and rebuild icon sprites if the app list
  # changed. Called from handle_launcher_right_click. This is the only path
  # that walks the filesystem after the first boot: scan_apps replays the
  # index cache otherwise, so adding or removing an app needs a rescan here.
  # It rewrites the cache on the way out.
  def rescan_launcher
    # Immediate feedback: change the title bar before the slow work starts.
    draw_launcher_status("Rescanning...")

    prev_handles = @launcher_apps.map { |a| a[:app] }
    scan_apps(true)
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
    update_composite_regions
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
    nil  # spawn_app branch is void-typed (Spinel) -> pin a concrete return
  end

  def find_icon_at(x, y, content_y)
    start_idx = @launcher_scroll * LAUNCHER_ICON_COLS
    vis_rows = launcher_visible_rows

    vrow = 0
    while vrow < vis_rows
      col = 0
      while col < LAUNCHER_ICON_COLS
        i = start_idx + vrow * LAUNCHER_ICON_COLS + col
        return -1 if i >= @launcher_apps.size

        icon_x = @launcher_x + LAUNCHER_ICON_PAD_X + col * (LAUNCHER_ICON_W + LAUNCHER_ICON_PAD_X)
        icon_y = content_y + vrow * (LAUNCHER_ICON_H + LAUNCHER_ICON_PAD_Y)

        if x >= icon_x && x < icon_x + LAUNCHER_ICON_W &&
           y >= icon_y && y < icon_y + LAUNCHER_ICON_H
          return i
        end
        col += 1
      end
      vrow += 1
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
