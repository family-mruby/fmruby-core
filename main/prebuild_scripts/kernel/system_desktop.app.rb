# System Desktop Application
# Manages two canvas layers:
#   @gfx    (z=254): Menu bar, dropdown, launcher window (foreground)
#   @bg_gfx (z=0):   Wallpaper, memory stats (background)
#
# Modules (loaded from system_desktop/ subdirectory):
#   LauncherMixin     - App launcher with icon grid
#   FileSelectorMixin - File open/save dialog

class SystemDesktopApp < FmrbApp
  include LauncherMixin
  include FileSelectorMixin
  include FileManagerMixin
  include ConfirmDialogMixin
  include ConfigDialogMixin
  include StorageDialogMixin
  include NetworkDialogMixin
  include ErrorDialogMixin
  include ClockSettingMixin
  include TaskbarMixin
  include AboutDialogMixin
  include ShortcutsDialogMixin
  include TbdDialogMixin

  MENU_BAR_HEIGHT = 13
  # Max cursor travel (virtual px) between mouse_down and mouse_up for the
  # release to still count as a click; larger travel is treated as a drag.
  CLICK_MOVE_THRESHOLD = 3
  MENU_BG = FmrbConst::THEME_MENU_BG
  DROPDOWN_BG = FmrbConst::THEME_WINDOW_BG
  DROPDOWN_TEXT = FmrbConst::THEME_TEXT
  DROPDOWN_HIGHLIGHT = FmrbConst::THEME_HIGHLIGHT
  BG_COLOR = FmrbConst::THEME_DESKTOP_BG
  # What every panel on this canvas is filled with (LAUNCHER_BG, CDLG_BG,
  # FSEL_BG ... all resolve to it). paint_bg_rect needs it to fill a hole
  # left inside a panel that is still open.
  PANEL_BG = FmrbConst::THEME_WINDOW_BG
  # GRAPHICS-SIDE path, not a local file: create_image sends it verbatim to
  # the graphics processor, which resolves it on its own filesystem (with a
  # /flash prefix on the device side).
  # - Modern (ESP32-P4): the graphics side shares this chip's storage, so it
  #   reads the shipped asset directly. No sync entry exists on P4 -- the
  #   old /data path silently blanked when the asset moved out of /data.
  # - Retro / Linux sim: the graphics side is a separate processor/process;
  #   the kernel file-sync (system_conf.toml, Linux) pushes the asset to its
  #   /flash/data, which that side sees as /data.
  # The browser build. It shares the Linux port, so PLATFORM says "linux" for
  # both and only BOARD tells them apart. Used where a cell or a menu entry
  # describes hardware a page does not have.
  ON_WEB = (FmrbConst::BOARD == "wasm")

  # Wallpaper. The theme decides which of the shipped pictures is used, and a
  # path in system_conf.toml's `wallpaper` beats that -- one line, so a
  # machine can wear anything the user drops in /home.
  #
  # Modern reads the file itself; on Retro and the simulation the graphics
  # side has its own filesystem and reads one fixed name out of it, so the
  # chosen picture is transferred over that name first (which is what the
  # boot-time [[sync_files]] entry used to be for on its own).
  BG_DIR = "/usr/share/backgrounds/"
  BG_LOCAL = (FmrbConst::PLATFORM == "esp32" && FmrbConst::CHIP_MODEL == "ESP32-P4")
  BG_SHARED_NAME = "bg_426x240.png"
  BG_IMAGE_PATH = BG_LOCAL ? "#{BG_DIR}#{BG_SHARED_NAME}" : "/data/#{BG_SHARED_NAME}"
  BOOT_IMAGE_PATH = "/boot/boot.png"
  # Iris reveal: a diamond-shaped window opens from the center, one punched
  # rect per horizontal band per frame. With the diamond's half-width scaled
  # by width/height, the corners are reached exactly at radius = height, on
  # any screen size.
  BOOT_IRIS_BAND_H = 8
  BOOT_IRIS_FRAMES = 28   # ~1.7s at BOOT_FRAME_MS (same length as the jingle)
  BOOT_FRAME_MS = 60
  BOOT_HOLD_MS = 1000  # Wait time after full reveal, before swapping to desktop

  # Dropdown menu items. `:key` drives both the dispatch in handle_dropdown_click
  # and the localized label resolved via FmrbI18n.t(:key) at draw time, so the
  # menu picks up the system language ("ja" / "en") from system_conf.toml.
  # Network is Modern-only (ESP32-P4 has WiFi via the C6 coprocessor).
  # Reset is ESP32-only (esp_restart); on Linux the host process just exits.
  DROPDOWN_ITEMS = [
    { key: :launcher },
    # The editor is a built-in, so it never appears in the launcher (that list is
    # a scan of /app). Without an entry here it could only be started from the
    # shell, which is no use with a mouse or a touch screen.
    { key: :editor },
    { key: :file_manager },
    { key: :log_viewer },
    { key: :monitor },
  ] +
    # Setting the clock writes the system clock and the RTC chip, neither of
    # which a page has: off-device apply_clock_setting only logs what it was
    # given. A dialog that accepts a time and does nothing with it is worse
    # than no dialog -- and the browser is already showing the right time,
    # because it takes it from the machine it runs on.
    (ON_WEB ? [] : [{ key: :set_clock }]) + [
    { key: :config },
  ] +
    # Storage clears the device's own caches, which the browser build has no
    # equivalent of -- what a visitor there wants to manage is the page's
    # stored /home, and that lives on the page around the screen.
    (ON_WEB ? [] : [{ key: :storage }]) +
    (FmrbConst::HAS_WIFI ? [{ key: :network }] : []) +
    # Retro only: manual BLE start for the ble_auto_start=false configuration
    # (on Modern the C6 radio path manages itself). Harmless when BLE is
    # already up -- the C side is idempotent.
    (FmrbConst::PLATFORM == "esp32" && FmrbConst::CHIP_MODEL != "ESP32-P4" ? [{ key: :ble_start }] : []) + [
    { key: :shortcuts },
    { key: :about },
  ] + (FmrbConst::PLATFORM == "esp32" ? [{ key: :reset }] : [])

  DROPDOWN_X = 0
  DROPDOWN_Y = MENU_BAR_HEIGHT
  DROPDOWN_W = 80
  DROPDOWN_ITEM_H = 12

  def initialize
    super()
    @counter = 0
    @mem_update_interval = 30

    @resize_preview_active = false
    @resize_preview_x = 0
    @resize_preview_y = 0
    @resize_preview_w = 0
    @resize_preview_h = 0

    @dropdown_open = false
    @dropdown_hover_idx = -1
    @about_open = false
    @skey_open = false
    @tbd_open = false
    @tbd_title = "TBD"
    @about_x = 0
    @about_y = 0
    @launcher_open = false
    @launcher_selected = -1
    @launcher_scroll = 0
    @last_click_time = 0
    @last_click_idx = -1
    @launcher_apps = []

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

    # File manager state
    @file_manager_open = false
    @file_manager_dir = "/"
    @file_manager_entries = []
    @file_manager_scroll = 0
    @file_manager_selected = -1
    @fmgr_x = 0
    @fmgr_y = 0
    @fmgr_last_click_idx = -1
    @fmgr_last_click_time = 0
    @fmgr_ctx_open = false
    @fmgr_ctx_x = 0
    @fmgr_ctx_y = 0
    @fmgr_ctx_idx = -1
    @fmgr_copy_path = nil
    @fmgr_copy_is_dir = false

    # Confirm dialog state
    @cdlg_open = false
    @cdlg_message = nil
    @cdlg_on_yes_cmd = nil
    @cdlg_on_yes_data = nil
    @cdlg_x = 0
    @cdlg_y = 0

    # Clock setting state
    @clk_open = false
    @clk_values = nil
    @clk_selected = 0
    @clk_x = 0
    @clk_y = 0

    # Config dialog state
    @cfg_open = false
    @cfg_selected = -1
    @cfg_status = nil
    @cfg_status_until = 0

    # Network dialog state (Modern only)
    @net_open = false
    @net_info = nil

    # Kana input mode as last reported by the host. Shown from boot, at "A",
    # rather than appearing once kana input is first used: the indicator is
    # clickable, and on a keyboard with no half/full-width key (US) or no
    # keyboard at all (Tab5) that click is the way in. An indicator that only
    # appears after you have already found another way in is no use.
    @kana_mode = 0

    # Boot animation state
    @boot_anim_state = :init  # :init -> :revealing -> :wait_to_finish -> :done
    @boot_iris_t = 0
    @boot_img = nil
    @boot_audio = nil
    @boot_audio_tick = 0
    @boot_audio_finished = false

    # Composite regions opt-in. While false the desktop main canvas is
    # composited as the full transparent area (boot screen path). Switched
    # on after finish_boot_animation so the compositor can skip the ~95%
    # of the canvas that is empty (= color-key 0x01) and only push the
    # menu bar + currently open overlays.
    @composite_regions_enabled = false
  end

  def on_create()
    Log.info("on_create called")

    # The desktop allocates every second redrawing the clock, and its GC
    # runs at priority 8 on the apps' core -- a synchronous collection here
    # stalls every app below for 100-200ms (measured as the residual MIDI
    # playback stalls after P7 removed the apps' own GC). Collect in this
    # app's own idle time instead; the clock wait gives it wide windows.
    self.idle_gc = true

    # The desktop owns the screen: its top-right corner is the menu bar,
    # never a close button. Without this a click there stops the desktop
    # "normally" and the system loses its window manager.
    self.closable = false

    # Keep idle_gc's step_limit (512). A larger limit was tried (2000) and
    # backfired on the Tab5: big steps inflate the running max-step
    # estimate, which stops idle stepping from fitting into the ~300ms
    # gaps between clicks (spin_idle_gc only starts a step that fits), so
    # collection starved exactly when the user was active. Small steps
    # keep the between-click recovery alive.

    # One FmrbUI for the whole desktop. The dialogs are never open two at a
    # time, so their widgets can share a container and take turns being
    # visible; hit testing skips the invisible ones. The dialogs move, so
    # each one places its widgets with move() when it opens.
    #
    # set_origin(0, 0) because every dialog here computes canvas-absolute
    # coordinates (@cdlg_x, @str_y ...) and hands them straight to move().
    # The desktop is NOT fullscreen -- it is a window with the frame
    # suppressed -- so its user area starts at (1, TITLE_BAR_H), and letting
    # FmrbUI add that put every dialog button eleven pixels below its panel.
    #
    # bg_painter: self because the ground here is not one colour -- see
    # paint_bg_rect. Without it, hiding a widget painted a window-coloured
    # rectangle over the wallpaper.
    @ui = FmrbUI.new(self, bg_painter: self)
    @ui.set_origin(0, 0)
    build_confirm_widgets
    build_config_widgets
    build_storage_widgets
    build_network_widgets
    build_clock_widgets
    build_file_selector_widgets
    build_file_manager_widgets
    build_launcher_widgets

    # Load keyboard shortcuts from config
    @shortcuts = load_shortcuts

    # Center the launcher horizontally (fixed size, below the menu bar).
    # On Retro (320 wide) this lands at x=10, same as the old left margin;
    # on Modern (426 wide) it no longer hugs the left edge.
    lx = (@window_width - LauncherMixin::LAUNCHER_W) / 2
    @launcher_x = lx > 0 ? lx : 0
    @launcher_y = MENU_BAR_HEIGHT + 8

    # Build app list and pre-create launcher icon sprites so the first
    # open_launcher is cheap (all WROVER-side sprite uploads happen here).
    scan_apps
    ensure_icon_sprites

    # Center file selector
    @fsel_x = (@window_width - FSEL_W) / 2
    @fsel_y = MENU_BAR_HEIGHT + (@window_height - MENU_BAR_HEIGHT - FSEL_H) / 2

    # Center file manager (nudge 2px down so the title bar clears the menu
    # bar boundary visually).
    @fmgr_x = (@window_width - FMGR_W) / 2
    @fmgr_y = MENU_BAR_HEIGHT + (@window_height - MENU_BAR_HEIGHT - FMGR_H) / 2 + 2

    init_taskbar
    @taskbar_focused_pid = nil

    start_boot_animation
  end

  # ---- Boot animation ----

  # Famicom-style chord progression with a sweep flourish on the
  # final tonic. Each event: [tick, ch, freq, vol, duty, sweep].
  # ch: 0=P1, 1=P2, 2=Triangle, 3=Noise. Tick uses BOOT_FRAME_MS (60ms).
  # freq=0 means note_off on that channel.
  # Sweep byte: 0x80 | (period<<4) | negate(0x08=up) | shift.
  BOOT_SWEEP_UP = 0x80 | (5 << 4) | 0x08 | 2  # slow rising sweep on tonic
  BOOT_EVENTS = [
    # Beat 1 (~0ms): C major + noise kick
    [0, 0, 523,  9, 2, 0],   # P1 C5
    [0, 1, 659,  6, 1, 0],   # P2 E5  (third, slight detune by harmonic)
    [0, 2, 131, 12, 0, 0],   # Tri C3 bass
    [0, 3,   6,  8, 0, 0],   # Noise short kick
    [1, 3,   0,  0, 0, 0],   # Noise off (~60ms)
    # Beat 2 (~180ms): F major
    [3, 0, 698,  9, 2, 0],   # P1 F5
    [3, 1, 880,  6, 1, 0],   # P2 A5
    [3, 2, 175, 12, 0, 0],   # Tri F3
    # Beat 3 (~360ms): G major
    [6, 0, 784,  9, 2, 0],   # P1 G5
    [6, 1, 988,  6, 1, 0],   # P2 B5
    [6, 2, 196, 12, 0, 0],   # Tri G3
    # Beat 4 (~540ms): C major resolution + up-sweep flourish on top
    [9, 0, 1047, 10, 2, BOOT_SWEEP_UP],  # P1 C6 with up sweep
    [9, 1, 659,  6, 1, 0],               # P2 E5
    [9, 2, 262, 12, 0, 0],               # Tri C4
    # End (~960ms): silence everything
    [16, 0, 0, 0, 0, 0],
    [16, 1, 0, 0, 0, 0],
    [16, 2, 0, 0, 0, 0],
  ]

  def start_boot_animation
    return unless @gfx && @bg_gfx

    # Cover both canvases: white background, black foreground overlay.
    @bg_gfx.clear(0xFF)
    @bg_gfx.present
    @gfx.clear(0x00)
    @gfx.present

    # Load logo onto background layer, centered (hidden under black @gfx).
    @boot_img = @bg_gfx.create_image(BOOT_IMAGE_PATH)
    if @boot_img
      cx = (@window_width  - @boot_img[:width])  / 2
      cy = (@window_height - @boot_img[:height]) / 2
      @bg_gfx.draw_image(@boot_img[:id], x: cx, y: cy)
      @bg_gfx.present
    else
      Log.warn("Boot logo not found: #{BOOT_IMAGE_PATH}")
    end

    @boot_iris_t = 0
    @boot_anim_state = :revealing

    @boot_audio = FmrbAudio.new(self)
    @boot_audio_tick = 0
    @boot_audio_finished = false
  end

  def tick_boot_animation
    case @boot_anim_state
    when :revealing
      tick_boot_jingle
      @boot_iris_t += 1
      # Ease-in: radius grows with t^2, so the window opens slowly at first
      # and accelerates, reaching the full screen at BOOT_IRIS_FRAMES.
      r = @window_height * @boot_iris_t * @boot_iris_t /
          (BOOT_IRIS_FRAMES * BOOT_IRIS_FRAMES)
      cx = @window_width / 2
      cy = @window_height / 2
      y = 0
      while y < @window_height
        dy = y + BOOT_IRIS_BAND_H / 2 - cy
        dy = -dy if dy < 0
        # Diamond half-width at this band; integer math only (no Float).
        hw = (r - dy) * @window_width / @window_height
        if hw > 0
          x0 = cx - hw
          x0 = 0 if x0 < 0
          x1 = cx + hw
          x1 = @window_width if x1 > @window_width
          # 0x01 is the foreground canvas' color key -> pixel becomes transparent.
          @gfx.fill_rect(x0, y, x1 - x0, BOOT_IRIS_BAND_H, 0x01)
        end
        y += BOOT_IRIS_BAND_H
      end
      @gfx.present
      if @boot_iris_t >= BOOT_IRIS_FRAMES
        @gfx.clear(0x01)  # Ensure full transparency
        @gfx.present
        @boot_anim_state = :wait_to_finish
      end
    when :wait_to_finish
      finish_boot_animation
    end
  end

  # Fire any BOOT_EVENTS scheduled for the current animation tick.
  def tick_boot_jingle
    return unless @boot_audio
    return if @boot_audio_finished
    BOOT_EVENTS.each do |ev|
      next unless ev[0] == @boot_audio_tick
      ch = ev[1]
      if ev[2] == 0
        @boot_audio.note_off(ch)
      else
        @boot_audio.note_on(ch, ev[2], ev[3], ev[4], ev[5])
      end
    end
    last_tick = BOOT_EVENTS.last[0]
    @boot_audio_finished = true if @boot_audio_tick >= last_tick
    @boot_audio_tick += 1
  end

  def finish_boot_animation
    if @boot_img
      @bg_gfx.delete_image(@boot_img[:id])
      @boot_img = nil
    end
    if @boot_audio && !@boot_audio_finished
      @boot_audio.note_off(0)
      @boot_audio.note_off(1)
      @boot_audio.note_off(2)
      @boot_audio.note_off(3)
      @boot_audio_finished = true
    end
    @boot_audio = nil
    draw_background
    draw_foreground
    FmrbApp.enable_cursor
    @boot_anim_state = :done

    # Status LED switches from boot fast-blink to heartbeat once the desktop
    # is fully interactive. After this point, a missing heartbeat is the
    # primary signal of a real system hang (WDT IDLE monitoring is off).
    FmrbKernel.boot_complete!

    # From here on the desktop main canvas only ever has the menu bar +
    # whatever overlays are open. Switch to region-based compositing so
    # the graphics-audio side stops walking the ~71k transparent pixels
    # in the rest of the canvas every frame.
    @composite_regions_enabled = true
    update_composite_regions

    spawn_startup_app
  end

  # One app opened as soon as the desktop is up, named by `startup_app` in
  # system_conf. A machine that exists to run one thing boots into it, and the
  # browser turns ?app= in its URL into this key
  # (wasm/backend/page_settings_wasm.c).
  #
  # The file is read here rather than through FmrbApp.config, which returns
  # the tables of a section ([[launcher_exclude]] and the like) and has no way
  # to hand back one top-level string.
  def spawn_startup_app
    name = read_conf_string("startup_app")
    return if name.nil? || name.empty?
    Log.info("startup_app: #{name}")
    spawn_app(name)
  rescue => e
    Log.error("startup_app failed: #{e.message}")
  end

  # One top-level `key = "value"` out of system_conf. No Regexp on this
  # machine, so this walks the lines, the way the launcher reads a .app.toml.
  def read_conf_string(key)
    text = File.open("/etc/system_conf.toml", "r") { |f| f.read }
    return nil unless text
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i].strip
      i += 1
      next if line.empty?
      next if line.start_with?("#")
      # A section header means the top-level keys are behind us.
      break if line.start_with?("[")
      eq = line.index("=")
      next unless eq
      next unless line[0, eq].strip == key
      v = line[eq + 1, line.length - eq - 1].strip
      # Trim a trailing comment before the quotes are taken off.
      hash = v.index("#")
      v = v[0, hash].strip if hash && hash > 0
      len = v.length
      v = v[1, len - 2] if len >= 2 && v[0] == "\"" && v[len - 1] == "\""
      return v
    end
    nil
  rescue => e
    Log.warn("could not read #{key} from system_conf: #{e.message}")
    nil
  end

  # The key is folded to lower case here, once, rather than on every key press:
  # handle_shortcut runs on the input path and String#downcase allocates.
  def load_shortcuts
    entries = FmrbApp.config("shortcuts")
    return [] unless entries
    list = []
    # each, not while + [i]: an element taken out of this array by index does
    # not carry its type under Spinel, so e["key"] does not compile that way,
    # while a block parameter does. This runs once at boot, so the block call
    # costs nothing that matters (launcher_exclude_dirs reads config the same way).
    entries.each do |e|
      key = e["key"]
      list << { key: key ? key.downcase : nil, app: e["app"] }
    end
    list
  rescue => e
    Log.error("Failed to load shortcuts: #{e.message}")
    []
  end

  # Is something on top that owns the screen? The menu bar key and the letter
  # shortcuts both stay out of the way of one. (The list used to be written out
  # in handle_shortcut and stopped at the dialogs that read the keyboard, so a
  # letter typed over the About or Config dialog started an app behind it.)
  def desktop_overlay_open?
    @dropdown_open || @launcher_open || @file_selector_open || @file_manager_open ||
      @cdlg_open || @error_dlg_open || @clk_open || @cfg_open || @str_open ||
      @net_open || @about_open || @tbd_open || @skey_open
  end

  def handle_shortcut(character)
    return false if desktop_overlay_open?
    # Integer#chr has no Spinel runtime backing (sp_str_chr); build the 1-byte
    # String with setbyte instead (dual-build safe).
    ch = nil
    if character && character >= 0 && character <= 255
      # Build the 1-byte String on a *concrete* local (a nil-initialised var is
      # poly on Spinel, and String#setbyte does not dispatch on a poly receiver).
      c = "\x00"
      c.setbyte(0, character)
      ch = c
    end
    return false unless ch
    # while rather than each: this is the key press path, and passing a block
    # costs about 0.4 ms per call here whether or not a shortcut matches.
    lower = ch.downcase
    i = 0
    while i < @shortcuts.size
      sc = @shortcuts[i]
      if sc[:key] && sc[:key] == lower
        case sc[:app]
        when "launcher" then open_launcher
        when "file_manager" then open_file_manager
        when "log_viewer" then spawn_app("default/logviewer")
        else spawn_app(sc[:app])
        end
        return true
      end
      i += 1
    end
    false
  end

  # ---- Background layer (@bg_gfx) ----

  def draw_background
    return unless @bg_gfx
    @bg_gfx.clear(BG_COLOR)
    path = wallpaper_path
    img = path ? @bg_gfx.create_image(path) : nil
    if img
      @bg_gfx.draw_image(img[:id], x: 0, y: 0)
      @bg_gfx.delete_image(img[:id])
    end
    @bg_gfx.present
  end

  # ---- Which picture, and getting it where the graphics side can see it ----

  # The file to draw, or nil for none (a plain BG_COLOR desktop). Worked out
  # once and remembered: draw_background also runs on every resize, and the
  # transfer below must not run with it.
  def wallpaper_path
    return @wallpaper_path if @wallpaper_resolved
    @wallpaper_resolved = true
    @wallpaper_path = resolve_wallpaper
  end

  def resolve_wallpaper
    chosen = wallpaper_setting
    return nil if chosen == "none"
    src = (chosen && chosen.length > 0) ? chosen : theme_wallpaper
    return nil unless src
    return src if BG_LOCAL
    # The graphics side reads its own /data by one fixed name. sync_file
    # compares size and CRC32, so the usual case (nothing changed) costs a
    # comparison and no transfer.
    @bg_gfx.sync_file(src, dest: "/flash/data/#{BG_SHARED_NAME}")
    BG_IMAGE_PATH
  end

  # The shipped picture that goes with the theme in force: the neon one for
  # cyberpunk, the western one otherwise. One file per size, so the name
  # carries the screen; a size we ship nothing for falls back to the smallest,
  # which is what every machine has.
  def theme_wallpaper
    stem = cfg_current_preset == "cyberpunk" ? "bg_cyber_" : "bg_"
    sized = "#{BG_DIR}#{stem}#{@window_width}x#{@window_height}.png"
    # File.exist? asks this side, which is the one holding the source.
    # gfx.file_status would ask the graphics side about a file it never has.
    return sized if File.exist?(sized)
    "#{BG_DIR}#{stem}426x240.png"
  end

  # The `wallpaper` line of system_conf.toml, or nil when it is not set. Read
  # here rather than through FmrbApp.config, which answers with sections; this
  # is one top-level key and the file is small.
  def wallpaper_setting
    begin
      f = File.open("/etc/system_conf.toml", "r")
      text = f.read
      f.close
    rescue => e
      Log.warn("wallpaper: cannot read system_conf.toml: #{e.message}")
      return nil
    end
    return nil if text.nil?
    value = nil
    text.split("\n").each do |raw|
      line = raw.strip
      break if line.start_with?("[")      # top-level keys only
      next unless line.start_with?("wallpaper")
      idx = line.index("=")
      next unless idx
      v = line[idx + 1, line.length - idx - 1].to_s.strip
      hash_idx = v.index("#")
      v = v[0, hash_idx].to_s.strip if hash_idx
      if v.length >= 2 && v.start_with?("\"") && v.end_with?("\"")
        v = v[1, v.length - 2].to_s
      end
      value = v
    end
    value
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

        # One pass, no intermediate array: select + each would be two blocks
        # (~0.4 ms each) and a throwaway Array every time this redraws.
        pi = 0
        while pi < processes.size
          p = processes[pi]
          pi += 1
          next unless p[:state] == FmrbConst::PROC_STATE_RUNNING ||
                      p[:state] == FmrbConst::PROC_STATE_SUSPENDED
          mem_used_kb = p[:mem_used] / 1024
          mem_total_kb = p[:mem_total] / 1024
          text = "#{p[:name]}: #{mem_used_kb}KB/#{mem_total_kb}KB"
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
    draw_file_manager if @file_manager_open
    draw_confirm_dialog if @cdlg_open
    draw_clock_setting if @clk_open
    draw_config_dialog if @cfg_open
    draw_storage_dialog if @str_open
    draw_network_dialog if @net_open
    draw_error_dialog if @error_dlg_open
    draw_about_dialog if @about_open
    draw_shortcuts_dialog if @skey_open
    draw_starting if @starting_name
    draw_tbd_dialog if @tbd_open
    draw_resize_preview if @resize_preview_active
    # flush presents when a widget was drawn; otherwise the present is ours.
    @gfx.present unless @ui.flush
  end

  # Outline overlay drawn while the user is dragging the resize handle of a
  # window. The kernel input router sends start/update/end messages so the
  # underlying app does not repaint until mouse_up.
  #
  # A single-color frame disappears against same-color backgrounds (e.g. a
  # white window). Use a black-outside / white-inside double frame so at
  # least one of the two strokes contrasts with whatever is underneath.
  def draw_resize_preview
    return unless @resize_preview_w && @resize_preview_h
    return if @resize_preview_w <= 0 || @resize_preview_h <= 0
    x = @resize_preview_x
    y = @resize_preview_y
    w = @resize_preview_w
    h = @resize_preview_h
    @gfx.draw_rect(x, y, w, h, FmrbGfx::BLACK)
    return if w < 4 || h < 4
    @gfx.draw_rect(x + 1, y + 1, w - 2, h - 2, FmrbGfx::WHITE)
  end

  def draw_menu_bar
    @gfx.clear(0x01)  # Transparent (clear foreground, 0x01 = transparent color key)
    @gfx.fill_rect(0, 0, @window_width, MENU_BAR_HEIGHT, MENU_BG)
    @gfx.draw_text(2, 2, "Family mruby", FmrbGfx::WHITE)
    draw_taskbar
    draw_clock
    draw_wifi_icon
    draw_meminfo
    draw_ble_icon
    draw_kana_icon
    @gfx.draw_line(0, MENU_BAR_HEIGHT - 1, @window_width, MENU_BAR_HEIGHT - 1, FmrbConst::THEME_BORDER)
  end

  def draw_clock
    # Entirely in C (one text command formatted on the C stack): the 1Hz
    # tick is the desktop's steady state and must create no Ruby object.
    # The Ruby route cost a wallclock Hash + a sprintf String per second.
    @gfx.draw_wallclock(@window_width - 90, 2, FmrbGfx::WHITE, MENU_BG)
  end

  # Network status icon just left of the clock. Shown on Modern (WiFi) and
  # Linux (host network); wifi_info is nil on Retro and the icon is hidden.
  # Signal-bars pictogram; gray bars with a red slash while disconnected.
  # Clicking the icon opens the network dialog (see handle_click).
  WIFI_ICON_W = 10

  # Free internal RAM readout, leftmost of the status cells, always shown --
  # internal RAM is the scarce resource here (one running app costs ~25KB),
  # so this answers "can I open another app" at a glance. Fetch, format and
  # draw all happen in C (allocation-free); the Linux sim shows "---KB".
  # Order left-to-right: RAM, kana, BLE, wifi, clock -- the readout sits
  # apart so the three icon cells line up.
  MEMINFO_W = 30  # 5 chars x 6px, fixed width

  # ---- Start indicator ----
  #
  # Raised by the kernel when a spawn begins and taken down when the app has
  # its canvas ("app_started"), when it fails ("show_error") or when it dies
  # (the kernel clears it). This timeout is the backstop for a clear that
  # never arrives: it must never be possible to leave the plate on screen.
  STARTING_TIMEOUT_MS = 25000
  STARTING_H = 20
  STARTING_PAD = 8
  STARTING_Y = MENU_BAR_HEIGHT + 6

  def clear_starting
    return unless @starting_name
    @starting_name = nil
    @starting_at = nil
    update_composite_regions
    draw_foreground if @boot_anim_state == :done
  end

  def tick_starting
    return unless @starting_at
    return if Machine.board_millis - @starting_at < STARTING_TIMEOUT_MS
    Log.warn("Start indicator timed out: #{@starting_name}")
    clear_starting
  end

  # A plate under the menu bar rather than a dialog: it is information, it
  # must not take the focus, and the foreground canvas is cleared and redrawn
  # whole, so taking it away is just not drawing it.
  def draw_starting
    label = "#{@starting_name} #{FmrbI18n.t(:starting)}"
    # Measured and drawn the way every localized label here is: mixed: true
    # puts ASCII on Font0 and the Japanese on misaki_8, and FmrbI18n.text_width
    # is the matching measurement.
    w = FmrbI18n.text_width(label) + STARTING_PAD * 2
    w = @window_width - 8 if w > @window_width - 8
    x = (@window_width - w) / 2
    y = STARTING_Y
    @gfx.fill_rect(x, y, w, STARTING_H, FmrbConst::THEME_WINDOW_BG)
    @gfx.draw_rect(x, y, w, STARTING_H, FmrbConst::THEME_BORDER)
    @gfx.draw_text(x + STARTING_PAD, y + 6, label,
                   FmrbConst::THEME_TEXT, FmrbConst::THEME_WINDOW_BG, mixed: true)
  end

  def draw_meminfo
    # Nothing to say in a browser: there is no internal RAM figure to fetch,
    # so the cell would sit there reading "---KB" for ever. An empty slot is
    # better than a broken-looking one.
    return if ON_WEB
    x = @window_width - 90 - WIFI_ICON_W - 7 - BLE_CELL_W - 1 - 4 - KANA_CELL_W - 4 - MEMINFO_W
    @gfx.draw_free_iram(x, 2, FmrbGfx::WHITE, MENU_BG)
  end

  # BLE indicator, left of the free-RAM readout, drawn as a filled box so
  # it cannot be misread as part of the neighboring RAM digits. Three
  # states: off/unavailable = empty gray box (no letter, so it reads as an
  # inactive slot and keeps the three icon cells from leaving a gap where
  # BLE is off -- e.g. the Linux sim, or Retro before a manual start),
  # gray box + white B = enabled and waiting for a central, white box +
  # bar-colored B (inverted) = client connected. A letterless box is what
  # tells "off" apart from "waiting", which also uses the gray box. The
  # state read is a bare fixnum, the label a constant -- the 1Hz repaint
  # stays allocation-free.
  BLE_LABEL = "B"
  BLE_CELL_W = 10

  def draw_ble_icon
    # Right of the RAM readout, next to the wifi icon (7px gap to wifi)
    x = @window_width - 90 - WIFI_ICON_W - 7 - BLE_CELL_W - 1
    state = FmrbApp.ble_state
    if state == 0
      @gfx.fill_rect(x, 1, BLE_CELL_W, 10, FmrbGfx::GRAY)
      return
    end
    box = (state == 2) ? FmrbGfx::WHITE : FmrbGfx::GRAY
    fg  = (state == 2) ? MENU_BG : FmrbGfx::WHITE
    @gfx.fill_rect(x, 1, BLE_CELL_W, 10, box)
    @gfx.draw_text(x + 2, 2, BLE_LABEL, fg, box)
  end

  # Kana input mode, between the free-RAM readout and the BLE cell so the
  # three icon cells (kana, BLE, wifi) sit together. Drawn like the BLE cell
  # so it reads as an indicator and not as a stray letter: gray box + white
  # "A" for ASCII, white box + inverted kana while kana input is on.
  KANA_CELL_W = 10
  KANA_LABELS = ["A", "あ", "ア"]

  def draw_kana_icon
    if @kana_mode.nil?
      @kana_icon_x = nil
      return
    end
    x = @window_width - 90 - WIFI_ICON_W - 7 - BLE_CELL_W - 1 - 4 - KANA_CELL_W
    @kana_icon_x = x
    # One fixed style for every mode: white box, menu-colored glyph. The old
    # gray direct-input cell sat next to the BLE icon's disconnected gray and
    # read as "this feature is off/unavailable", when the indicator is a live
    # clickable control in either mode. The glyph alone carries the mode.
    @gfx.fill_rect(x, 1, KANA_CELL_W, 10, FmrbGfx::WHITE)
    @gfx.draw_text(x + 1, 2, KANA_LABELS[@kana_mode], MENU_BG, FmrbGfx::WHITE, mixed: true)
  end

  # Click the indicator to step off -> hiragana -> katakana -> off. The host
  # owns the mode, so this only asks; the indicator redraws when the answer
  # comes back as a kana_mode event.
  def cycle_kana_mode
    FmrbApp.set_kana_mode(((@kana_mode || 0) + 1) % 3)
  end

  def draw_wifi_icon
    # Capability probe once (wifi_info allocates a Hash + 3 Strings); the
    # 1Hz redraw then only needs the allocation-free connected? bool.
    @wifi_supported = FmrbApp.wifi_info ? true : false if @wifi_supported.nil?
    unless @wifi_supported
      # No WiFi on this build: an empty gray box, like the BLE cell, so the
      # three status icons keep a uniform row instead of leaving a gap.
      # @wifi_icon_x stays nil -- with no radio there is no network dialog
      # to open, so the placeholder is not clickable.
      @wifi_icon_x = nil
      x = @window_width - 90 - WIFI_ICON_W - 4
      @gfx.fill_rect(x, 1, WIFI_ICON_W, 10, FmrbGfx::GRAY)
      return
    end
    x = @window_width - 90 - WIFI_ICON_W - 4
    @wifi_icon_x = x
    # Clear the icon cell first: this draw must be self-contained now that
    # the 1Hz tick repaints it without a full menu-bar repaint underneath
    # (stale disconnect slashes would linger otherwise).
    @gfx.fill_rect(x - 1, 2, WIFI_ICON_W + 2, 10, MENU_BG)
    connected = FmrbApp.wifi_connected?
    if connected && FmrbApp.rd_stream_state > 0
      # Remote desktop video (MJPEG or H.264) is going out: a small screen
      # pictogram instead of the signal bars, so "someone is watching this
      # display over the network" is visible at a glance.
      @gfx.draw_rect(x, 2, 9, 6, FmrbGfx::WHITE)   # screen outline
      @gfx.fill_rect(x + 2, 4, 5, 2, FmrbGfx::WHITE) # lit panel
      @gfx.fill_rect(x + 2, 9, 5, 1, FmrbGfx::WHITE) # stand base
      return
    end
    color = connected ? FmrbGfx::WHITE : FmrbGfx::GRAY
    @gfx.fill_rect(x,     7, 2, 3, color)   # bars grow up from y=10
    @gfx.fill_rect(x + 3, 5, 2, 5, color)
    @gfx.fill_rect(x + 6, 3, 2, 7, color)
    unless connected
      @gfx.draw_line(x, 10, x + 8, 2, FmrbGfx::RED)
    end
  end

  def draw_dropdown
    x = DROPDOWN_X
    y = DROPDOWN_Y
    h = DROPDOWN_ITEM_H * DROPDOWN_ITEMS.size + 2

    @gfx.fill_rect(x, y, DROPDOWN_W, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, DROPDOWN_W, h, 0x00)

    DROPDOWN_ITEMS.each_with_index do |item, i|
      item_y = y + 1 + i * DROPDOWN_ITEM_H
      label = FmrbI18n.t(item[:key])
      if i == @dropdown_hover_idx
        @gfx.fill_rect(x + 1, item_y, DROPDOWN_W - 2, DROPDOWN_ITEM_H, DROPDOWN_HIGHLIGHT)
        @gfx.draw_text(x + 6, item_y + 2, label, DROPDOWN_TEXT, DROPDOWN_HIGHLIGHT, mixed: true)
      else
        @gfx.draw_text(x + 6, item_y + 2, label, DROPDOWN_TEXT, DROPDOWN_BG, mixed: true)
      end
    end
  end

  def dropdown_item_at(x, y)
    return -1 unless hit_dropdown?(x, y)
    idx = (y - DROPDOWN_Y - 1) / DROPDOWN_ITEM_H
    return -1 if idx < 0 || idx >= DROPDOWN_ITEMS.size
    idx
  end

  # ---- State management ----

  def open_dropdown
    return if @dropdown_open
    @dropdown_open = true
    close_launcher

    dropdown_h = DROPDOWN_ITEM_H * DROPDOWN_ITEMS.size + 2
    notify_overlay_state(true, DROPDOWN_X, DROPDOWN_Y, DROPDOWN_W, dropdown_h)
    update_composite_regions
    draw_foreground
  end

  # The menu bar from the keyboard: F10 opens it with the first entry picked,
  # arrows move, Enter runs, Esc or F10 again closes. The highlight is the same
  # @dropdown_hover_idx the mouse moves, so the two ways of driving the menu
  # cannot disagree about what is selected.
  def open_dropdown_from_key
    open_dropdown
    @dropdown_hover_idx = 0
    draw_foreground
  end

  # Returns true when the key was spent on the menu.
  def handle_dropdown_key(scancode)
    case scancode
    when FmrbConst::KEY_UP    then dropdown_move(-1)
    when FmrbConst::KEY_DOWN  then dropdown_move(1)
    when FmrbConst::KEY_HOME  then dropdown_move_to(0)
    when FmrbConst::KEY_END   then dropdown_move_to(DROPDOWN_ITEMS.size - 1)
    when FmrbConst::KEY_ENTER
      idx = @dropdown_hover_idx
      close_dropdown
      run_dropdown_item(idx)
    when FmrbConst::KEY_ESC, FmrbConst::KEY_F10
      close_dropdown
    else
      return false
    end
    true
  end

  # Wraps at both ends: the menu is short, and a list that stops dead at the
  # bottom makes the last entries the awkward ones to reach.
  def dropdown_move(delta)
    n = DROPDOWN_ITEMS.size
    return if n == 0
    idx = @dropdown_hover_idx
    idx = (delta > 0 ? -1 : 0) if idx < 0
    dropdown_move_to((idx + delta + n) % n)
  end

  # Hover moved: repaint only the two rows that changed, not the whole
  # foreground. A full draw_foreground here (clear + menu bar + every status
  # cell + all rows) cost more than the 33ms between mouse moves, so the
  # event queue backed up and the highlight froze, then jumped. The dropdown
  # is an opaque box, so repainting single rows is safe.
  def dropdown_move_to(idx)
    old = @dropdown_hover_idx
    return if idx == old
    @dropdown_hover_idx = idx
    draw_dropdown_row(old) if old >= 0
    draw_dropdown_row(idx) if idx >= 0
    @gfx.present
  end

  # One dropdown row, highlighted or not according to @dropdown_hover_idx.
  def draw_dropdown_row(i)
    return if i < 0 || i >= DROPDOWN_ITEMS.size
    item_y = DROPDOWN_Y + 1 + i * DROPDOWN_ITEM_H
    label = FmrbI18n.t(DROPDOWN_ITEMS[i][:key])
    if i == @dropdown_hover_idx
      @gfx.fill_rect(DROPDOWN_X + 1, item_y, DROPDOWN_W - 2, DROPDOWN_ITEM_H, DROPDOWN_HIGHLIGHT)
      @gfx.draw_text(DROPDOWN_X + 6, item_y + 2, label, DROPDOWN_TEXT, DROPDOWN_HIGHLIGHT, mixed: true)
    else
      @gfx.fill_rect(DROPDOWN_X + 1, item_y, DROPDOWN_W - 2, DROPDOWN_ITEM_H, DROPDOWN_BG)
      @gfx.draw_text(DROPDOWN_X + 6, item_y + 2, label, DROPDOWN_TEXT, DROPDOWN_BG, mixed: true)
    end
  end

  def close_dropdown
    return unless @dropdown_open
    @dropdown_open = false
    @dropdown_hover_idx = -1
    unless @launcher_open
      notify_overlay_state(false, 0, 0, 0, 0)
    end
    update_composite_regions
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

  # Rebuild the desktop canvas composite region list from the current UI
  # state. Called after every open_*/close_* (paired with notify_overlay_state).
  # Sends one opaque region per visible UI element; everything else on the
  # canvas is left as color-key 0x01 and skipped by the compositor.
  #
  # The fmgr right-click context menu is positioned inside the file manager
  # window (see handle_file_manager_right_click clamp), so it is implicitly
  # covered by the file_manager region and does not need its own entry.
  #
  # Constants defined in mixin modules are referenced via the explicit
  # ModuleName::CONST form because picoruby does not resolve bare mixin
  # constants from the including class (see feedback memory).
  def update_composite_regions
    return unless @composite_regions_enabled
    return unless @gfx

    # While the user is dragging a window resize handle, the preview frame
    # is drawn at arbitrary screen coordinates on the desktop canvas. Fall
    # back to full-area transparent push so the outline is visible anywhere
    # on screen; region compositing is restored on resize_preview_end.
    if @resize_preview_active
      @gfx.set_composite_regions(nil)
      return
    end

    regions = [
      { dst_x: 0, dst_y: 0, w: @window_width, h: MENU_BAR_HEIGHT, transparent: false }
    ]
    if @starting_name
      # The plate sits below the menu bar, which is outside every other
      # region: without this it is drawn on the canvas and never composited,
      # which is exactly what "the indicator never appeared" turned out to be.
      # transparent: true so the band around the plate stays the wallpaper
      # rather than a full width bar.
      regions << { dst_x: 0, dst_y: STARTING_Y, w: @window_width, h: STARTING_H,
                   transparent: true }
    end
    if @dropdown_open
      dropdown_h = DROPDOWN_ITEM_H * DROPDOWN_ITEMS.size + 2
      regions << { dst_x: DROPDOWN_X, dst_y: DROPDOWN_Y, w: DROPDOWN_W, h: dropdown_h, transparent: false }
    end
    if @launcher_open
      regions << { dst_x: @launcher_x, dst_y: @launcher_y,
                   w: LauncherMixin::LAUNCHER_W, h: LauncherMixin::LAUNCHER_H, transparent: false }
    end
    if @file_selector_open
      regions << { dst_x: @fsel_x, dst_y: @fsel_y,
                   w: FileSelectorMixin::FSEL_W, h: FileSelectorMixin::FSEL_H, transparent: false }
    end
    if @file_manager_open
      regions << { dst_x: @fmgr_x, dst_y: @fmgr_y,
                   w: FileManagerMixin::FMGR_W, h: FileManagerMixin::FMGR_H, transparent: false }
    end
    if @cdlg_open
      regions << { dst_x: @cdlg_x, dst_y: @cdlg_y,
                   w: ConfirmDialogMixin::CDLG_W, h: ConfirmDialogMixin::CDLG_H, transparent: false }
    end
    if @clk_open
      regions << { dst_x: @clk_x, dst_y: @clk_y,
                   w: ClockSettingMixin::CLK_W, h: ClockSettingMixin::CLK_H, transparent: false }
    end
    if @cfg_open
      regions << { dst_x: @cfg_x, dst_y: @cfg_y,
                   w: ConfigDialogMixin::CFG_W, h: ConfigDialogMixin::CFG_H, transparent: false }
    end
    if @str_open
      regions << { dst_x: @str_x, dst_y: @str_y,
                   w: StorageDialogMixin::STR_W, h: StorageDialogMixin::STR_H, transparent: false }
    end
    if @net_open
      regions << { dst_x: @net_x, dst_y: @net_y,
                   w: NetworkDialogMixin::NET_W, h: NetworkDialogMixin::NET_H, transparent: false }
    end
    if @about_open
      regions << { dst_x: @about_x, dst_y: @about_y,
                   w: AboutDialogMixin::ABOUT_W, h: @about_h, transparent: false }
    end
    if @skey_open
      regions << { dst_x: @skey_x, dst_y: @skey_y,
                   w: ShortcutsDialogMixin::SKEY_W, h: @skey_h, transparent: false }
    end
    if @error_dlg_open
      regions << { dst_x: @error_dlg_x, dst_y: @error_dlg_y,
                   w: ErrorDialogMixin::EDLG_W, h: @error_dlg_h, transparent: false }
    end
    if @tbd_open
      regions << { dst_x: @tbd_x, dst_y: @tbd_y,
                   w: TbdDialogMixin::TBD_W, h: TbdDialogMixin::TBD_H, transparent: false }
    end

    @gfx.set_composite_regions(regions)
  end

  # FmrbUI's bg_painter: paint the ground where a widget used to be.
  #
  # This canvas is the foreground one. What is "behind" a widget is almost
  # always nothing at all: the pixel is left as the colour key and the
  # compositor shows the wallpaper from the canvas below. Filling such a hole
  # with the window background is what put white rectangles on the wallpaper.
  #
  # The exception is a widget that goes away while its panel stays open -- a
  # scrollbar that stopped being needed, the file selector's Save field in
  # open mode, the storage dialog swapping Yes/No for Close. There the ground
  # is the panel, and the key would punch a hole through it.
  #
  # Called from FmrbUI#flush, so: no allocation, no present, integer
  # comparisons only. The static dialogs (about, shortcuts, error, tbd) are
  # not listed because none of their widgets is ever hidden while they are
  # open; add one here if that changes.
  def paint_bg_rect(gfx, x, y, w, h)
    c = FmrbApp::TRANSPARENT_COLOR
    c = PANEL_BG if hole_inside_open_panel?(x, y, w, h)
    gfx.fill_rect(x, y, w, h, c)
    nil
  end

  def hole_inside_open_panel?(x, y, w, h)
    if @launcher_open
      return true if rect_covers?(@launcher_x, @launcher_y,
                                  LauncherMixin::LAUNCHER_W,
                                  LauncherMixin::LAUNCHER_H, x, y, w, h)
    end
    if @file_selector_open
      return true if rect_covers?(@fsel_x, @fsel_y,
                                  FileSelectorMixin::FSEL_W,
                                  FileSelectorMixin::FSEL_H, x, y, w, h)
    end
    if @file_manager_open
      return true if rect_covers?(@fmgr_x, @fmgr_y,
                                  FileManagerMixin::FMGR_W,
                                  FileManagerMixin::FMGR_H, x, y, w, h)
    end
    if @cdlg_open
      return true if rect_covers?(@cdlg_x, @cdlg_y,
                                  ConfirmDialogMixin::CDLG_W,
                                  ConfirmDialogMixin::CDLG_H, x, y, w, h)
    end
    if @clk_open
      return true if rect_covers?(@clk_x, @clk_y,
                                  ClockSettingMixin::CLK_W,
                                  ClockSettingMixin::CLK_H, x, y, w, h)
    end
    if @cfg_open
      return true if rect_covers?(@cfg_x, @cfg_y,
                                  ConfigDialogMixin::CFG_W,
                                  ConfigDialogMixin::CFG_H, x, y, w, h)
    end
    if @str_open
      return true if rect_covers?(@str_x, @str_y,
                                  StorageDialogMixin::STR_W,
                                  StorageDialogMixin::STR_H, x, y, w, h)
    end
    if @net_open
      return true if rect_covers?(@net_x, @net_y,
                                  NetworkDialogMixin::NET_W,
                                  NetworkDialogMixin::NET_H, x, y, w, h)
    end
    false
  end

  # Is the second rectangle wholly inside the first?
  def rect_covers?(px, py, pw, ph, x, y, w, h)
    x >= px && y >= py && x + w <= px + pw && y + h <= py + ph
  end

  # ---- Update loop ----

  def on_update()
    if @boot_anim_state != :done
      tick_boot_animation
      # After full reveal, hold the logo on screen for BOOT_HOLD_MS before
      # the :wait_to_finish tick swaps to the regular desktop.
      return BOOT_HOLD_MS if @boot_anim_state == :wait_to_finish
      return BOOT_FRAME_MS
    end

    # A rescan asked for by a right-click runs here rather than in the input
    # handler (see handle_launcher_right_click). Returning straight after it
    # gives the queue a turn to drain before anything else is done.
    return 50 if tick_rescan

    #draw_memory_stats
    @counter += 1

    # Update clock and taskbar every ~1 second (500ms * 2 = 1s). The spin
    # timeout below is also the idle window idle_gc steps in; it must clear
    # the largest observed GC step (336ms on device) or stepping stops after
    # the first big step and collections fall back to the allocation path.
    if @counter % 2 == 0
      tick_starting
      taskbar_changed = update_taskbar_apps
      tick_config_dialog if @cfg_open
      tick_network_dialog if @net_open
      if taskbar_changed || @net_open
        # Process set changed (taskbar layout moved) or the network dialog
        # is showing live info: repaint everything.
        draw_foreground
      else
        # Nothing structural changed this second. The canvas is persistent,
        # so repaint only the two menu-bar cells that follow the clock and
        # leave the rest (launcher grid, dialogs, taskbar) as drawn. This
        # keeps an open launcher from paying its full label layout and
        # redraw cost every second.
        draw_clock
        draw_wifi_icon
        draw_meminfo
        draw_ble_icon
        draw_kana_icon
        @gfx.present
      end
    end

    # GC pacing under sustained activity. Idle stepping stops whenever a
    # message is pending, so continuous UI use (launcher clicking) can
    # outrun collection; on the Tab5 the resulting watermark full-GC was
    # felt as a 1-2s freeze (PSRAM full-mark of the ~450KB live set).
    # Above 64% usage (baseline sits near 56%, so this means real garbage
    # accumulated), spend a TIME-boxed slice per update on collector
    # steps. The budget, not a step count, is the bound: single steps on
    # PSRAM can be large, and the box keeps the worst update under ~one
    # frame of hiccup. The 70% watermark full-GC stays as the backstop.
    if @counter % 2 == 1
      usage = FmrbApp.pool_usage
      if usage >= 64
        t0 = Machine.uptime_us
        while Machine.uptime_us - t0 < 40_000
          GC.step
        end
      end
    end

    # Watermark GC: the backstop when even busy stepping cannot keep up.
    # A full GC at ~570KB is a bounded hiccup instead of the storm that a
    # pool ceiling (observed at 801KB of 819KB) used to pay.
    if @counter % 10 == 0
      usage = FmrbApp.pool_usage
      if usage >= 70
        Log.info("desktop: watermark GC at #{usage}% pool usage")
        GC.start
      end
    end

    500
  end

  # ---- Event handling ----

  def on_control(msg)
    if msg["cmd"] == "file_select"
      open_file_selector(msg["requester_pid"], msg["mode"] || "open")
    elsif msg["cmd"] == "show_error"
      clear_starting
      err = FmrbApp._get_last_error
      if err
        open_error_dialog(err[:name] || "Unknown", err[:error] || "Unknown error")
      end
    elsif msg["cmd"] == "app_starting"
      # An app is being spawned. Loading and compiling its script takes
      # seconds on the device, and until now the screen said nothing about it.
      # The kernel decides who gets one (never a headless app) and clears it
      # again through "app_started"; @starting_at is only the backstop for a
      # clear that never arrives.
      @starting_name = msg["name"] || ""
      @starting_at = Machine.board_millis
      update_composite_regions
      draw_foreground if @boot_anim_state == :done
    elsif msg["cmd"] == "app_started"
      clear_starting
    elsif msg["cmd"] == "apps_changed"
      # A process started or ended. The taskbar used to pick this up on its own
      # once-a-second poll, so an app's icon appeared up to a second after its
      # window did. update_taskbar_apps still gates on ps_gen, so a redundant
      # notification costs one counter read.
      #
      # The list is kept current either way, but nothing is painted until the
      # boot animation is done: it owns the foreground canvas (black, revealed
      # by the iris) and finish_boot_animation draws the real one. Painting
      # here put the menu bar and taskbar on screen before the logo appeared.
      rebuilt = update_taskbar_apps
      draw_foreground if rebuilt && @boot_anim_state == :done
    elsif msg["cmd"] == "focus_changed"
      # The kernel reports every focus move (spawn, Ctrl+Tab, a click on a
      # window, an app exiting). Before this the taskbar only knew about its
      # own clicks, so the white frame around the focused icon was usually
      # missing or stuck on whatever was clicked last.
      pid = msg["pid"]
      if pid != @taskbar_focused_pid
        @taskbar_focused_pid = pid
        draw_foreground if @boot_anim_state == :done
      end
    elsif msg["cmd"] == "spawn_failed"
      # The kernel says why (it has the spawner's error code); this used to
      # blame a missing .toml whatever the cause.
      reason = (msg["reason"] || "See the log for the reason.").to_s
      open_error_dialog(msg["app"] || "Unknown", "Failed to launch.\n" + reason)
    elsif msg["cmd"] == "confirm_dialog"
      # Build callback data hash from message fields
      cb_data = {}
      msg.each do |k, v|
        next if k == "cmd" || k == "message" || k == "on_yes_cmd"
        cb_data[k] = v
      end
      open_confirm_dialog(msg["message"], msg["on_yes_cmd"], cb_data)
    elsif msg["cmd"] == "do_reboot"
      Log.info("Desktop: do_reboot received, calling FmrbApp.reboot")
      FmrbApp.reboot
    elsif msg["cmd"] == "resize_preview_start" || msg["cmd"] == "resize_preview_update"
      was_active = @resize_preview_active
      @resize_preview_active = true
      @resize_preview_x = msg["x"] || 0
      @resize_preview_y = msg["y"] || 0
      @resize_preview_w = msg["w"] || 0
      @resize_preview_h = msg["h"] || 0
      # State edge only: disable region compositing once at drag start so
      # the preview outline is not clipped to the menu bar / overlay rects.
      update_composite_regions unless was_active
      draw_foreground
    elsif msg["cmd"] == "resize_preview_end"
      @resize_preview_active = false
      update_composite_regions
      draw_foreground
    end
    nil  # keep a concrete return: a branch (FmrbApp.reboot) is noreturn, which
         # would otherwise make the if/elsif value void (Spinel)
  end

  # Everything FmrbUI reports lands here. One case per dialog keeps the
  # dispatch flat; the dialogs themselves decide what their ids mean.
  def handle_widget(id)
    handle_confirm_dialog_widget(id) if @cdlg_open
    handle_config_dialog_widget(id) if @cfg_open
    handle_storage_dialog_widget(id) if @str_open
    handle_network_dialog_widget(id) if @net_open
    handle_clock_setting_widget(id) if @clk_open
    handle_file_selector_widget(id) if @file_selector_open
    handle_file_manager_widget(id) if @file_manager_open
    handle_launcher_widget(id) if @launcher_open
    nil
  end

  # One notch is a row here, not a screenful: the launcher scrolls by rows of
  # icons and the two file panels by rows of names, and the machine's
  # wheel_lines already says how many of those a notch is worth.
  def handle_desktop_wheel(rows)
    n = rows > 0 ? rows : -rows
    up = rows > 0
    i = 0
    while i < n
      if @launcher_open
        up ? launcher_scroll_up : launcher_scroll_down
      elsif @file_selector_open
        fsel_scroll(up ? -1 : 1)
      elsif @file_manager_open
        handle_file_manager_scroll(up ? -1 : 1)
      end
      i += 1
    end
    nil
  end

  def on_event(ev)
    # Kana input mode. The host sends this to the focused app and, through the
    # kernel, here as well -- the mode applies to whatever has focus, so the
    # menu bar is the only place it can be seen from anywhere. Without it a
    # child has no way to tell why the letter shortcuts stopped working.
    if ev[:type] == :kana_mode
      @kana_mode = ev[:mode]
      draw_kana_icon
      @gfx.present
      return
    end

    if ev[:type] == :mouse_move
      handle_mouse_move(ev[:x], ev[:y])
      return
    end

    # The wheel moves whichever list the desktop currently has open. It
    # arrives here because the desktop has the focus, so there is no hit test
    # to do -- only one of these panels is up at a time.
    #
    # Two units meet here. The file panels list names one text row high, so a
    # notch is worth wheel_lines of them. The launcher lists tiles as tall as
    # five rows and shows three at a time, so wheel_lines sent it from top to
    # bottom in one flick: there, a notch is one row of icons.
    if ev[:type] == :mouse_wheel
      if @launcher_open
        n = wheel_notches(ev)
        handle_desktop_wheel(n) if n
      else
        rows = wheel_rows(ev)
        handle_desktop_wheel(rows) if rows
      end
      return
    end

    # FmrbUI first, for the mouse only: the widgets know their own rects, so
    # an event that lands on one never reaches the coordinate dispatch below.
    #
    # Keys are deliberately NOT fed here. The file selector's name field is
    # the only widget that wants them, and it has to take its turn after the
    # arrows and Enter that drive the list; feeding keys here as well made
    # every character arrive twice.
    if ev[:type] == :mouse_down || ev[:type] == :mouse_up
      wid = @ui.handle(ev)
      if wid
        handle_widget(wid)
        return
      end
      # Releasing takes the pressed look off whatever was held; nothing else
      # here would repaint it, and a scrollbar arrow that stays lit after the
      # click reads as a control that stopped working.
      @ui.flush
    end

    if ev[:type] == :mouse_down
      # Record the press position: the click decision happens at mouse_up,
      # and only when the cursor stayed near this point (drag != click).
      @mouse_down_x = ev[:x]
      @mouse_down_y = ev[:y]
      # Press feedback for the file manager: highlight the entry under the
      # cursor until release. Other overlays do not currently need this.
      if @file_manager_open && hit_file_manager?(ev[:x], ev[:y])
        handle_file_manager_press(ev[:x], ev[:y])
      end
      return
    end

    if ev[:type] == :mouse_up
      # (0,0) is not a release the user made: it is the kernel reporting that a
      # click landed outside the overlay this desktop said it had
      # (build_hid_close_overlay). It must not go through the drag filter
      # below. That filter measures against the last press the desktop itself
      # saw, and the press behind this signal went to another window, so a
      # stale position read as a drag and the signal was dropped -- while the
      # kernel had already stopped routing clicks into the overlay rect. That
      # left an error dialog on screen that no click on it could close; only
      # the menu bar, which routes to the desktop unconditionally, got rid of
      # it.
      if ev[:x] == 0 && ev[:y] == 0
        @mouse_down_x = nil
        @mouse_down_y = nil
        handle_click(0, 0)
        return
      end

      # A press that traveled since mouse_down is a drag, not a click.
      dx = (ev[:x] - (@mouse_down_x || ev[:x])).abs
      dy = (ev[:y] - (@mouse_down_y || ev[:y])).abs
      if dx > CLICK_MOVE_THRESHOLD || dy > CLICK_MOVE_THRESHOLD
        # Drop the file manager press highlight without activating anything
        if @fmgr_pressed_idx && @fmgr_pressed_idx >= 0
          @fmgr_pressed_idx = -1
          draw_foreground
        end
        return
      end

      button = ev[:button] || 1
      if button == 3 && @file_manager_open
        # Right click in file manager
        if hit_file_manager?(ev[:x], ev[:y])
          handle_file_manager_right_click(ev[:x], ev[:y])
          return
        end
      end
      if button == 3 && @launcher_open
        # Right click in launcher: rescan to pick up newly added apps
        if hit_launcher?(ev[:x], ev[:y])
          handle_launcher_right_click(ev[:x], ev[:y])
          return
        end
      end
      handle_click(ev[:x], ev[:y])
    end

    if ev[:type] == :key_down
      character = ev[:character] || 0
      keycode = ev[:keycode] || 0
      # The scancode is the HID usage ID on the device and the SDL scancode in
      # the simulator, and those are the same numbers -- so FmrbConst::KEY_* can
      # be compared against it on both. The keycode cannot: it is a character
      # here and an SDL keysym there.
      scancode = ev[:scancode] || 0

      # The two dialogs that are only there to be read close on any key, the
      # same as they close on any click. Reached from the menu bar, which is
      # now reachable from the keyboard, so leaving them mouse-only to dismiss
      # would be a dead end.
      if @skey_open
        close_shortcuts_dialog
        return
      end
      if @about_open
        close_about_dialog
        return
      end

      # Menu bar first: while it is open it owns the arrows and Enter.
      if @dropdown_open
        return if handle_dropdown_key(scancode)
      end

      # Launcher: arrows move the selection (scrolling as needed), Enter
      # launches. See handle_launcher_key in launcher.rb.
      if @launcher_open
        return if handle_launcher_key(keycode, character)
      end

      # File selector: arrows and Enter first. In save mode whatever is left
      # goes into the name field, which FmrbUI owns -- the widget takes the
      # key only because the dialog gave it the focus when it opened.
      if @file_selector_open
        return if handle_file_selector_key(scancode)
        if @file_selector_mode == "save"
          wid = @ui.handle(ev)
          if wid
            handle_widget(wid)
          else
            draw_foreground
          end
        end
        # A modal dialog swallows the rest: nothing behind it should act on a
        # key the dialog did not want.
        return
      end

      # File manager: the same list keys, plus the context menu's actions on
      # their initials (file_manager.rb).
      if @file_manager_open
        handle_file_manager_key(scancode, character)
        return
      end

      # F10 opens the menu bar. Nothing above consumed the key, so no list or
      # dialog that reads the keyboard is up; the rest are click-only dialogs,
      # and desktop_overlay_open? keeps the menu from opening behind one.
      if scancode == FmrbConst::KEY_F10
        open_dropdown_from_key unless desktop_overlay_open?
        return
      end

      # Keyboard shortcuts (only when no dialog/overlay is active)
      if character >= 32 && character <= 126
        handle_shortcut(character)
      end
    end
  end

  def handle_mouse_move(x, y)
    # Update dropdown hover highlight; redraw only when the hovered item changes
    if @dropdown_open
      dropdown_move_to(dropdown_item_at(x, y))
    end
  end

  def handle_click(x, y)
    # About dialog has highest priority — any click closes it
    if @about_open
      close_about_dialog
      return
    end

    # Shortcuts list — any click closes it
    if @skey_open
      close_shortcuts_dialog
      return
    end

    # TBD dialog — any click closes it
    if @tbd_open
      close_tbd_dialog
      return
    end

    # Error dialog has highest priority
    if @error_dlg_open
      if hit_error_dialog?(x, y)
        handle_error_dialog_click(x, y)
        return
      end
      close_error_dialog
      return
    end

    # Confirm dialog has highest priority. Its buttons are widgets and were
    # handled before this; a click anywhere else inside it is ignored, and
    # outside it closes the dialog.
    if @cdlg_open
      return if hit_confirm_dialog?(x, y)
      close_confirm_dialog
      return
    end

    # Clock setting dialog
    if @clk_open
      if hit_clock_setting?(x, y)
        handle_clock_setting_click(x, y)
        return
      end
      close_clock_setting
      return
    end

    # Config dialog
    if @cfg_open
      if hit_config_dialog?(x, y)
        handle_config_dialog_click(x, y)
        return
      end
      close_config_dialog
      return
    end

    # Storage dialog
    if @str_open
      if hit_storage_dialog?(x, y)
        # buttons are widgets; a click elsewhere inside the dialog is ignored
        return
      end
      close_storage_dialog
      return
    end

    # Network dialog
    if @net_open
      if hit_network_dialog?(x, y)
        # buttons are widgets
        return
      end
      close_network_dialog
      return
    end

    # File selector has priority
    if @file_selector_open
      if hit_file_selector?(x, y)
        handle_file_selector_click(x, y)
        return
      end
      close_file_selector(nil)
      return
    end

    # File manager has priority
    if @file_manager_open
      if hit_file_manager?(x, y)
        handle_file_manager_click(x, y)
        return
      end
      close_file_manager
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
    if y < MENU_BAR_HEIGHT
      if x < 80
        open_dropdown
      elsif @wifi_icon_x && x >= @wifi_icon_x - 2 && x < @wifi_icon_x + WIFI_ICON_W + 2
        open_network_dialog
      elsif @kana_icon_x && x >= @kana_icon_x && x < @kana_icon_x + KANA_CELL_W
        cycle_kana_mode
      else
        handle_taskbar_click(x, y)
      end
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
    close_dropdown
    run_dropdown_item(item_idx)
  end

  # What a menu entry does, whether it was clicked or chosen with the keyboard.
  def run_dropdown_item(item_idx)
    return if item_idx < 0 || item_idx >= DROPDOWN_ITEMS.size
    item = DROPDOWN_ITEMS[item_idx]

    case item[:key]
    when :launcher
      open_launcher
    when :editor
      # Windowed: F11 (or the menu bar's Full) takes it fullscreen from there.
      spawn_app("default/editor")
    when :file_manager
      open_file_manager
    when :log_viewer
      spawn_app("default/logviewer")
    when :monitor
      spawn_app("default/monitor")
    when :set_clock
      open_clock_setting
    when :about
      open_about_dialog
    when :shortcuts
      open_shortcuts_dialog
    when :config
      open_config_dialog
    when :storage
      open_storage_dialog
    when :network
      open_network_dialog
    when :ble_start
      Log.info("Desktop: manual BLE start requested")
      FmrbApp.ble_start
    when :reset
      open_confirm_dialog(FmrbI18n.t(:reboot_confirm), "reboot", {})
    else
      spawn_app(item[:app]) if item[:app]
    end
  end

  def on_suspend
    # Hide foreground canvas (clear to transparent)
    @gfx.clear(0x01)
    @gfx.present
    Log.info("Desktop suspended")
  end

  def on_resume
    # Anything still marked "starting" is stale: the desktop is only suspended
    # while another app owns the screen, so an indicator raised before that is
    # for an app which has long since started (or died), and its clear was
    # queued behind the suspend. Drop it rather than painting it now.
    @starting_name = nil
    @starting_at = nil
    update_composite_regions
    draw_foreground
    draw_background
    Log.info("Desktop resumed")
  end

  def on_destroy
    destroy_icon_sprites
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
