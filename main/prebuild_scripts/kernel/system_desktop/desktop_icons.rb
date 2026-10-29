# Desktop icons module for SystemDesktopApp
#
# A column of app icons down the left of the wallpaper, like a PC desktop.
# Double-click (or double-tap) one to start the app. They live on the
# background canvas (@bg_gfx, z=0), so every window covers them, and a click
# only reaches them where no window is -- the kernel hands the desktop the
# clicks nothing else is under (input_router, find_window_at).
#
# Which apps: /home/desktop.toml when the user has one, otherwise the default
# set shipped in /usr/share/desktop/desktop.toml (/home ships empty). The
# launcher's icon menu (right click, or the D key) adds and removes apps and
# writes /home/desktop.toml.
# A file that cannot be read or parsed counts as absent, so a broken user
# file falls back to the default set instead of an empty desktop.
#
# Both files hold one key, an array of the app paths the launcher uses:
#   apps = ["default/editor", "/app/tool/nsf_player.app.rb"]
# An entry whose app is not on this machine is skipped (the default set names
# apps that one board ships and another does not, such as the mahjong table).
# Two entries are not apps: "desktop/launcher" opens the launcher (the menu's
# Launcher, shortcut l), shown with a grid of tiles, and "desktop/file_manager"
# opens the desktop's own File Manager (the menu's File Manager), shown with a
# folder icon. Neither is in the launcher's list: the launcher's own icon is
# added and removed from the menu a right click on the launcher's empty space
# opens (or D with nothing selected); the File Manager's only by editing the
# file.
#
# Linux applications (doc/linux_native L15): an entry "linux:<id>" is the
# PC's application with that desktop file id, started as the launcher's
# "Linux" group starts it, with the icon the group shows (Breeze's, made by
# native/sys/linux_apps.c). "linux:a|b" names alternatives (the same program
# packaged twice: firefox, or the snap's firefox_firefox): the first one
# installed is shown. One that is not installed is left out; on every other
# machine (the devices, the simulation, the browser) "linux:" entries are
# left out silently. The PC's list is read on a thread of its own, so the
# entries wait for it: on_update asks again until it is ready, then the
# icons are resolved once more and the wallpaper repainted.
#
# Drawing: the icons are stamped onto the background canvas with draw_tile
# from the launcher's icon SpriteImages (the same bitmaps, shared by icon
# file, so no extra graphics-side memory). They are drawn in draw_background,
# right after the wallpaper that covers them. Selecting one only repaints the
# name plates of the two icons involved.

module DesktopIconsMixin
  DICON_USER_PATH    = "/home/desktop.toml"
  DICON_DEFAULT_PATH = "/usr/share/desktop/desktop.toml"
  DICON_MAX_BYTES    = 4096
  DICON_KEY          = "apps"
  DICON_LAUNCHER     = "desktop/launcher"
  DICON_FILE_MANAGER = "desktop/file_manager"
  DICON_LAUNCHER_ICON = "usr/share/icon/launcher.icon"
  DICON_FOLDER_ICON  = "usr/share/icon/folder.icon"

  # Layout. A cell is the icon and up to two lines of name under it.
  DICON_MARGIN_X = 4
  DICON_MARGIN_Y = 4
  DICON_CELL_W   = 68   # "Breakout.py" (11 Font0 characters, 66 px) on one line
  DICON_CELL_H   = 44
  DICON_COL_GAP  = 4
  DICON_ICON_Y   = 2     # icon top within the cell
  DICON_LABEL_Y  = 28    # first name line within the cell
  DICON_LINE_H   = 9
  DICON_PLATE    = FmrbConst::THEME_WINDOW_BG
  DICON_PLATE_SEL = FmrbConst::THEME_HIGHLIGHT
  DICON_TEXT     = FmrbConst::THEME_TEXT
  # Clicks on the same icon within this many desktop ticks (on_update runs
  # every 500 ms) are a double-click: the launcher and the file manager count
  # the same way, so a double-tap is equally forgiving everywhere.
  DICON_DOUBLE_TICKS = 5

  DI_QUOTE = '"'
  DI_CLOSE = "]"
  DI_OPEN  = "["
  DI_HASH  = "#"
  DI_EQ    = "="
  DI_NL    = "\n"
  DI_SLASH = "/"
  DI_DOT   = "."
  DI_ALT   = "|"
  DI_TAB   = "\t"
  DI_DESKTOP = ".desktop"

  # ---- Which apps ----

  # Read the list and resolve it into icons. Boot calls this once the launcher
  # list exists.
  def build_desktop_icons
    mark = ::FmrbApp.pool_used
    @dicon_paths = load_desktop_icon_paths
    resolve_desktop_icons
    grew = (::FmrbApp.pool_used - mark) / 1024
    Log.info("Desktop icons: #{@dicon_items.size}/#{@dicon_paths.size} shown, pool +#{grew}KB (#{::FmrbApp.pool_usage}%)")
    nil
  end

  def resolve_desktop_icons
    @dicon_items = []
    @dicon_missing = false
    @dicon_selected = -1
    @dicon_last_idx = -1
    @dicon_last_time = 0
    @dicon_linux_lines = nil
    @dicon_linux_wait = false
    @dicon_paths.each do |path|
      if path.start_with?(LauncherMixin::LNX_PREFIX)
        entry = desktop_linux_entry(path)
        if entry
          @dicon_items << { app: path, label: entry[:label], icon_file: entry[:icon_file],
                            icon_char: "L", lnx: entry[:lnx] }
        end
        next
      end
      entry = desktop_icon_entry(path)
      if entry
        @dicon_items << { app: path, label: entry[:label],
                          icon_file: entry[:icon_file], icon_char: entry[:icon_char] }
      else
        @dicon_missing = true
      end
    end
    @dicon_linux_lines = nil
    layout_desktop_icons
    ensure_desktop_icon_images
    nil
  end

  def desktop_icon_listed?(path)
    return false unless @dicon_paths
    @dicon_paths.index(path) ? true : false
  end

  # The launcher's icon menu. Adds the app at the bottom or takes it off, keeps
  # the choice in /home and repaints the background: taking an icon off has
  # to give the wallpaper under it back, and that is only had by redrawing it.
  def toggle_desktop_icon(path)
    @dicon_paths ||= []
    idx = @dicon_paths.index(path)
    if idx
      @dicon_paths.delete_at(idx)
      Log.info("Desktop icons: removed #{path}")
    else
      @dicon_paths << path
      Log.info("Desktop icons: added #{path}")
    end
    save_desktop_icon_paths
    resolve_desktop_icons
    draw_background
    nil
  end

  # After a launcher rescan: an app the list names but which was not found may
  # have been installed since. Nothing to do (and no wallpaper redraw) when
  # every entry already resolved.
  def refresh_desktop_icons_after_rescan
    return nil unless @dicon_missing
    resolve_desktop_icons
    draw_background
    nil
  end

  def load_desktop_icon_paths
    list = read_desktop_icon_file(DICON_USER_PATH)
    return list if list
    read_desktop_icon_file(DICON_DEFAULT_PATH) || []
  end

  # The list in one file, or nil when the file is absent or broken.
  def read_desktop_icon_file(path)
    return nil unless File.exist?(path)
    f = File.open(path, "r")
    text = f.read(DICON_MAX_BYTES)
    f.close
    list = parse_desktop_icon_list(text)
    Log.warn("Desktop icons: #{path} is not readable, ignored") unless list
    list
  rescue => e
    Log.warn("Desktop icons: cannot read #{path}: #{e.message}")
    nil
  end

  # `apps = [ "a", "b", ... ]`, on one line or spread over several, with
  # comments. No Regexp on this machine, so this walks the lines. nil when
  # the key is missing, the bracket never closes or a string is unterminated.
  def parse_desktop_icon_list(text)
    return nil unless text
    lines = text.split(DI_NL)
    out = nil
    i = 0
    while i < lines.size
      line = lines[i].strip
      i += 1
      next if line.empty? || line.start_with?(DI_HASH)
      if out.nil?
        eq = line.index(DI_EQ)
        next unless eq
        next unless line[0, eq].strip == DICON_KEY
        rest = line[eq + 1, line.length - eq - 1].to_s.strip
        return nil unless rest.start_with?(DI_OPEN)
        out = []
        line = rest[1, rest.length - 1].to_s
      end
      st = collect_desktop_icon_strings(line, out)
      return nil if st < 0
      return out if st > 0
    end
    nil
  end

  # Append the quoted strings of one line to out. 1 when the closing bracket
  # was reached, 0 to go on with the next line, -1 for a broken line.
  def collect_desktop_icon_strings(line, out)
    pos = 0
    n = line.length
    while pos < n
      q = line.index(DI_QUOTE, pos)
      close = line.index(DI_CLOSE, pos)
      hash = line.index(DI_HASH, pos)
      # A comment before the next string ends the line.
      return 0 if hash && (q.nil? || hash < q) && (close.nil? || hash < close)
      return 1 if close && (q.nil? || close < q)
      return 0 unless q
      e = line.index(DI_QUOTE, q + 1)
      return -1 unless e
      # Interpolated: a fresh String, not a slice sharing the file's buffer.
      s = "#{line[q + 1, e - q - 1]}"
      out << s if s.length > 0 && out.index(s).nil?
      pos = e + 1
    end
    0
  end

  def save_desktop_icon_paths
    buf = "# Apps shown as icons on the desktop, top to bottom. The launcher's\n"
    buf << "# icon menu (right click) writes this file; delete it to get the default set back.\n"
    buf << "apps = [\n"
    @dicon_paths.each { |p| buf << "  \"#{p}\",\n" }
    buf << "]\n"
    f = File.open(DICON_USER_PATH, "w")
    f.write(buf)
    f.close
    true
  rescue => e
    Log.warn("Desktop icons: cannot write #{DICON_USER_PATH}: #{e.message}")
    false
  end

  # The launcher's entry for the app, or one read from the app's own .toml
  # when the launcher does not list it (its category is excluded, or the
  # launcher cache predates it). nil when the app is not on this machine.
  def desktop_icon_entry(path)
    if path == DICON_LAUNCHER
      return { label: FmrbI18n.t(:launcher), app: path, icon_file: DICON_LAUNCHER_ICON, icon_char: "L" }
    end
    if path == DICON_FILE_MANAGER
      return { label: FmrbI18n.t(:file_manager), app: path, icon_file: DICON_FOLDER_ICON, icon_char: "F" }
    end
    i = 0
    n = @launcher_apps.size
    while i < n
      a = @launcher_apps[i]
      return a if a[:app] == path
      i += 1
    end
    return nil unless path.start_with?(DI_SLASH)
    slash = path.rindex(DI_SLASH)
    dot = path.rindex(DI_DOT)
    return nil unless slash && dot && dot > slash
    toml = "#{path[0, dot]}.toml"
    return nil unless File.exist?(toml) && File.exist?(path)
    dir = path[0, slash]
    name = path[slash + 1, path.length - slash - 1].to_s
    parse_app_toml(toml, dir, [name])
  end

  # The PC's application a "linux:" entry names (the first of its
  # alternatives installed), or nil: none installed, not a PC (the list
  # answers ""), or the list still being read (@dicon_linux_wait, see
  # tick_desktop_icons_linux).
  def desktop_linux_entry(path)
    lines = @dicon_linux_lines
    if lines.nil?
      text = FmrbApp.linux_apps(LauncherMixin::LNX_MODE_LIST, FmrbI18n.lang)
      lines = text.empty? ? [] : text.split(DI_NL)
      @dicon_linux_lines = lines
    end
    return nil if lines.empty?
    if lines[0].to_s != LauncherMixin::LNX_READY
      @dicon_linux_wait = true
      return nil
    end
    pre = LauncherMixin::LNX_PREFIX.length
    alts = path[pre, path.length - pre].to_s.split(DI_ALT)
    a = 0
    while a < alts.size
      want = alts[a].strip
      a += 1
      next if want.empty?
      with = want.end_with?(DI_DESKTOP) ? want : "#{want}#{DI_DESKTOP}"
      i = 1
      n = lines.size
      while i < n
        line = lines[i]
        i += 1
        next unless line.index(want)
        f = line.split(DI_TAB)
        next unless f[0] && f[1]
        next unless f[1] == want || f[1] == with
        icon = f[2]
        icon = LauncherMixin::LNX_APP_ICON if icon.nil? || icon.empty?
        return { label: f[0], icon_file: icon, lnx: f[1] }
      end
    end
    nil
  end

  # From on_update: the PC's list was still being read when the icons were
  # resolved; once it is ready, resolve them again and repaint.
  def tick_desktop_icons_linux
    return nil unless @dicon_linux_wait
    head = FmrbApp.linux_apps(LauncherMixin::LNX_MODE_HEADER, FmrbI18n.lang)
    return nil unless head.start_with?(LauncherMixin::LNX_READY)
    resolve_desktop_icons
    return nil if @dicon_linux_wait
    Log.info("Desktop icons: the Linux applications resolved, #{@dicon_items.size}/#{@dicon_paths.size} shown")
    draw_background
    nil
  end

  # Column by column from the top left; a column holds as many cells as the
  # screen is tall. Cells that would run off the right half are dropped.
  def layout_desktop_icons
    x = DICON_MARGIN_X
    y = SystemDesktopApp::MENU_BAR_HEIGHT + DICON_MARGIN_Y
    i = 0
    n = @dicon_items.size
    while i < n
      if y + DICON_CELL_H > @window_height
        x += DICON_CELL_W + DICON_COL_GAP
        y = SystemDesktopApp::MENU_BAR_HEIGHT + DICON_MARGIN_Y
      end
      it = @dicon_items[i]
      if x + DICON_CELL_W > @window_width / 2
        it[:x] = -1
      else
        it[:x] = x
        it[:y] = y
        it[:lay] = icon_label_layout(it[:label], DICON_CELL_W)
      end
      y += DICON_CELL_H
      i += 1
    end
    nil
  end

  # Bitmaps come from the launcher's cache (keyed by icon file); an icon the
  # launcher has not loaded -- an app it does not list -- is loaded into the
  # same cache, so it is shared and released with the rest.
  def ensure_desktop_icon_images
    @icon_sprite_images ||= {}
    @dicon_items.each do |it|
      f = it[:icon_file]
      next unless f
      next if @icon_sprite_images[f]
      img = build_icon_sprite_image(f)
      @icon_sprite_images[f] = img if img
    end
    nil
  end

  # ---- Drawing (background canvas) ----

  # Called by draw_background after the wallpaper, before its present.
  def draw_desktop_icons
    return nil unless @dicon_items
    i = 0
    n = @dicon_items.size
    while i < n
      draw_desktop_icon(i)
      i += 1
    end
    nil
  end

  def draw_desktop_icon(i)
    it = @dicon_items[i]
    x = it[:x]
    return nil if x < 0
    y = it[:y]
    ix = x + (DICON_CELL_W - LauncherMixin::ICON_SPRITE_W) / 2
    iy = y + DICON_ICON_Y
    f = it[:icon_file]
    img = f ? @icon_sprite_images[f] : nil
    if img
      @bg_gfx.draw_tile(img.id, 0, 0, LauncherMixin::ICON_SPRITE_W,
                        LauncherMixin::ICON_SPRITE_H, dst_x: ix, dst_y: iy)
    else
      # No artwork: a plate with the VM letter, like the launcher's fallback.
      @bg_gfx.fill_rect(ix, iy, LauncherMixin::ICON_SPRITE_W, LauncherMixin::ICON_SPRITE_H, DICON_PLATE)
      @bg_gfx.draw_rect(ix, iy, LauncherMixin::ICON_SPRITE_W, LauncherMixin::ICON_SPRITE_H, FmrbConst::THEME_BORDER)
      @bg_gfx.draw_text(ix + 9, iy + 8, it[:icon_char] || "?", DICON_TEXT, DICON_PLATE)
    end
    draw_desktop_icon_label(i)
    nil
  end

  # The name on a plate of its own width, so it reads on any wallpaper. The
  # plate changes colour with the selection; it is the same rectangle either
  # way, so repainting it needs nothing from the wallpaper.
  def draw_desktop_icon_label(i)
    it = @dicon_items[i]
    x = it[:x]
    return nil if x < 0
    lay = it[:lay]
    bg = (i == @dicon_selected) ? DICON_PLATE_SEL : DICON_PLATE
    ly = it[:y] + DICON_LABEL_Y
    draw_desktop_icon_line(x + lay[1], ly, lay[0], bg)
    draw_desktop_icon_line(x + lay[3], ly + DICON_LINE_H, lay[2], bg) if lay.size > 2
    nil
  end

  def draw_desktop_icon_line(x, y, text, bg)
    return nil if text.length == 0
    w = FmrbI18n.text_width(text)
    @bg_gfx.fill_rect(x - 1, y - 1, w + 2, DICON_LINE_H, bg)
    @bg_gfx.draw_text_mixed(x, y, text, DICON_TEXT)
    nil
  end

  # ---- Input ----

  def desktop_icon_at(x, y)
    return -1 unless @dicon_items
    i = 0
    n = @dicon_items.size
    while i < n
      it = @dicon_items[i]
      ix = it[:x]
      if ix >= 0 && x >= ix && x < ix + DICON_CELL_W &&
         y >= it[:y] && y < it[:y] + DICON_CELL_H
        return i
      end
      i += 1
    end
    -1
  end

  # A click on the bare desktop (no window, no overlay, below the menu bar).
  # Selects the icon under it, starts it on the second click, or clears the
  # selection when it missed every icon.
  def handle_desktop_icon_click(x, y)
    idx = desktop_icon_at(x, y)
    if idx < 0
      select_desktop_icon(-1)
      return nil
    end
    now = @counter
    if idx == @dicon_last_idx && (now - @dicon_last_time) < DICON_DOUBLE_TICKS
      @dicon_last_idx = -1
      select_desktop_icon(-1)
      app = @dicon_items[idx][:app]
      lnx = @dicon_items[idx][:lnx]
      if app == DICON_LAUNCHER
        open_launcher
      elsif app == DICON_FILE_MANAGER
        open_file_manager
      elsif lnx
        # As the launcher's "Linux" group starts it.
        if FmrbApp.linux_launch(lnx) == 0
          Log.info("Desktop icons: Linux app #{lnx} asked to start")
        else
          Log.warn("Desktop icons: Linux app #{lnx} could not start")
        end
      else
        spawn_app(app)
      end
    else
      @dicon_last_idx = idx
      @dicon_last_time = now
      select_desktop_icon(idx)
    end
    nil
  end

  def select_desktop_icon(idx)
    prev = @dicon_selected
    return nil if prev == idx
    @dicon_selected = idx
    draw_desktop_icon_label(prev) if prev >= 0
    draw_desktop_icon_label(idx) if idx >= 0
    @bg_gfx.present
    nil
  end
end
