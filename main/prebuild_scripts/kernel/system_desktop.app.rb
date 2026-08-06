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
  BG_IMAGE_PATH = "/usr/share/backgrounds/bg_426x240.png"
  BOOT_IMAGE_PATH = "/boot/boot.png"
  BOOT_TILE_W = 32
  BOOT_TILE_H = 24
  BOOT_TILES_PER_FRAME = 5
  BOOT_FRAME_MS = 60
  BOOT_HOLD_MS = 1000  # Wait time after full reveal, before swapping to desktop

  # Dropdown menu items. `:key` drives both the dispatch in handle_dropdown_click
  # and the localized label resolved via FmrbI18n.t(:key) at draw time, so the
  # menu picks up the system language ("ja" / "en") from system_conf.toml.
  # Network is Modern-only (ESP32-P4 has WiFi via the C6 coprocessor).
  # Reset is ESP32-only (esp_restart); on Linux the host process just exits.
  DROPDOWN_ITEMS = [
    { key: :launcher },
    { key: :file_manager },
    { key: :log_viewer },
    { key: :monitor },
    { key: :set_clock },
    { key: :config },
    { key: :storage },
  ] + (FmrbConst::HAS_WIFI ? [{ key: :network }] : []) +
    # Retro only: manual BLE start for the ble_auto_start=false configuration
    # (on Modern the C6 radio path manages itself). Harmless when BLE is
    # already up -- the C side is idempotent.
    (FmrbConst::PLATFORM == "esp32" && FmrbConst::CHIP_MODEL != "ESP32-P4" ? [{ key: :ble_start }] : []) + [
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
    @fmgr_pending_edit_path = nil

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

    # Boot animation state
    @boot_anim_state = :init  # :init -> :revealing -> :wait_to_finish -> :done
    @boot_anim_idx = 0
    @boot_tiles = nil
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
    # The desktop's idle window is the 500ms clock wait, far wider than the
    # rhythm-holding MIDI apps IDLE_GC_STEP_LIMIT was tuned for; let each
    # step do more work so collection keeps pace with the UI churn.
    GC.step_limit = 2000 if @idle_gc

    # Load keyboard shortcuts from config
    @shortcuts = load_shortcuts

    # Position launcher near top-left (below menu bar, with margin)
    @launcher_x = 8
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

    # Precompute tile positions covering the whole foreground.
    cols = (@window_width + BOOT_TILE_W - 1) / BOOT_TILE_W
    rows = (@window_height + BOOT_TILE_H - 1) / BOOT_TILE_H
    tiles = []
    r = 0
    while r < rows
      c = 0
      while c < cols
        tiles << [c * BOOT_TILE_W, r * BOOT_TILE_H]
        c += 1
      end
      r += 1
    end
    # Fisher-Yates shuffle. Use a temporary; picoruby/mruby breaks on
    # multi-assignment whose LHS targets are array element references
    # (tiles[i], tiles[j] = tiles[j], tiles[i] raises TypeError).
    i = tiles.size - 1
    while i > 0
      j = rand(i + 1)
      tmp = tiles[i]
      tiles[i] = tiles[j]
      tiles[j] = tmp
      i -= 1
    end
    @boot_tiles = tiles
    @boot_anim_idx = 0
    @boot_anim_state = :revealing

    @boot_audio = FmrbAudio.new(self)
    @boot_audio_tick = 0
    @boot_audio_finished = false
  end

  def tick_boot_animation
    case @boot_anim_state
    when :revealing
      tick_boot_jingle
      bt = 0
      while bt < BOOT_TILES_PER_FRAME
        break if @boot_anim_idx >= @boot_tiles.size
        tx, ty = @boot_tiles[@boot_anim_idx]
        # 0x01 is the foreground canvas' color key -> pixel becomes transparent.
        @gfx.fill_rect(tx, ty, BOOT_TILE_W, BOOT_TILE_H, 0x01)
        @boot_anim_idx += 1
        bt += 1
      end
      @gfx.present
      if @boot_anim_idx >= @boot_tiles.size
        @gfx.clear(0x01)  # Ensure full transparency
        @gfx.present
        @boot_tiles = nil
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
  end

  def load_shortcuts
    entries = FmrbApp.config("shortcuts")
    return [] unless entries
    entries.map { |e| { key: e["key"], app: e["app"] } }
  rescue => e
    Log.error("Failed to load shortcuts: #{e.message}")
    []
  end

  def handle_shortcut(character)
    return false if @dropdown_open || @launcher_open || @file_selector_open ||
                    @file_manager_open || @cdlg_open || @error_dlg_open || @clk_open
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
    @shortcuts.each do |sc|
      next unless sc[:key] && sc[:key].downcase == ch.downcase
      case sc[:app]
      when "launcher" then open_launcher
      when "file_manager" then open_file_manager
      when "log_viewer" then spawn_app("default/logviewer")
      else spawn_app(sc[:app])
      end
      return true
    end
    false
  end

  # ---- Background layer (@bg_gfx) ----

  def draw_background
    return unless @bg_gfx
    @bg_gfx.clear(BG_COLOR)
    img = @bg_gfx.create_image(BG_IMAGE_PATH)
    if img
      @bg_gfx.draw_image(img[:id], x: 0, y: 0)
      @bg_gfx.delete_image(img[:id])
    end
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
    draw_file_manager if @file_manager_open
    draw_confirm_dialog if @cdlg_open
    draw_clock_setting if @clk_open
    draw_config_dialog if @cfg_open
    draw_storage_dialog if @str_open
    draw_network_dialog if @net_open
    draw_error_dialog if @error_dlg_open
    draw_about_dialog if @about_open
    draw_tbd_dialog if @tbd_open
    draw_resize_preview if @resize_preview_active
    @gfx.present
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
    @gfx.draw_line(0, MENU_BAR_HEIGHT - 1, @window_width, MENU_BAR_HEIGHT - 1, FmrbConst::THEME_BORDER)
  end

  # Hoisted: an inline string literal allocates a fresh RString slot on
  # every 1Hz redraw; a constant lookup does not.
  CLOCK_FMT = "%02d/%02d %02d:%02d:%02d"

  def draw_clock
    wc = FmrbApp.wallclock
    return unless wc
    text = sprintf(CLOCK_FMT,
                   wc[:month], wc[:day], wc[:hour], wc[:minute], wc[:second])
    @gfx.draw_text(@window_width - 90, 2, text, FmrbGfx::WHITE, MENU_BG)
  end

  # Network status icon just left of the clock. Shown on Modern (WiFi) and
  # Linux (host network); wifi_info is nil on Retro and the icon is hidden.
  # Signal-bars pictogram; gray bars with a red slash while disconnected.
  # Clicking the icon opens the network dialog (see handle_click).
  WIFI_ICON_W = 10

  def draw_wifi_icon
    # Capability probe once (wifi_info allocates a Hash + 3 Strings); the
    # 1Hz redraw then only needs the allocation-free connected? bool.
    @wifi_supported = FmrbApp.wifi_info ? true : false if @wifi_supported.nil?
    unless @wifi_supported
      @wifi_icon_x = nil
      return
    end
    x = @window_width - 90 - WIFI_ICON_W - 4
    @wifi_icon_x = x
    # Clear the icon cell first: this draw must be self-contained now that
    # the 1Hz tick repaints it without a full menu-bar repaint underneath
    # (stale disconnect slashes would linger otherwise).
    @gfx.fill_rect(x - 1, 2, WIFI_ICON_W + 2, 10, MENU_BG)
    connected = FmrbApp.wifi_connected?
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

  # ---- Update loop ----

  def on_update()
    if @boot_anim_state != :done
      tick_boot_animation
      # After full reveal, hold the logo on screen for BOOT_HOLD_MS before
      # the :wait_to_finish tick swaps to the regular desktop.
      return BOOT_HOLD_MS if @boot_anim_state == :wait_to_finish
      return BOOT_FRAME_MS
    end

    #draw_memory_stats
    @counter += 1

    # Update clock and taskbar every ~1 second (500ms * 2 = 1s). The spin
    # timeout below is also the idle window idle_gc steps in; it must clear
    # the largest observed GC step (336ms on device) or stepping stops after
    # the first big step and collections fall back to the allocation path.
    if @counter % 2 == 0
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
        @gfx.present
      end
    end

    # Watermark GC: launcher/dialog bursts allocate faster than idle
    # stepping can collect, and a pool that reaches its ceiling pays with
    # a multi-second GC storm (observed at 801KB of 819KB). Collect
    # deliberately at a frame boundary once usage crosses 70% -- a full GC
    # at ~570KB is a bounded hiccup instead of the storm it prevents.
    if @counter % 10 == 0
      usage = FmrbApp.pool_usage
      if usage >= 70
        Log.info("desktop: watermark GC at #{usage}% pool usage")
        GC.start
      end
    end

    # Deferred: send file path to editor after it has started
    if @fmgr_pending_edit_path && @fmgr_pending_edit_counter
      @fmgr_pending_edit_counter -= 1
      if @fmgr_pending_edit_counter <= 0
        # Find editor PID from window list (most recently spawned)
        processes = FmrbApp.ps
        if processes
          editor = processes.select { |p| p[:name] == "FM-Editor" && p[:state] == FmrbConst::PROC_STATE_RUNNING }.last
          if editor
            editor_pid = editor[:id]
            # Use file_select_result via kernel to forward to editor
            data = {
              "cmd" => "file_select_result",
              "target_pid" => editor_pid,
              "path" => @fmgr_pending_edit_path,
              "mode" => "open"
            }
            send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
            Log.info("Sent file_selected to Editor PID #{editor_pid}: #{@fmgr_pending_edit_path}")
          end
        end
        @fmgr_pending_edit_path = nil
        @fmgr_pending_edit_counter = nil
      end
    end

    500
  end

  # ---- Event handling ----

  def on_control(msg)
    if msg["cmd"] == "file_select"
      open_file_selector(msg["requester_pid"], msg["mode"] || "open")
    elsif msg["cmd"] == "show_error"
      err = FmrbApp._get_last_error
      if err
        open_error_dialog(err[:name] || "Unknown", err[:error] || "Unknown error")
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

  def on_event(ev)
    if ev[:type] == :mouse_move
      handle_mouse_move(ev[:x], ev[:y])
      return
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

      # Launcher: arrow keys scroll the visible range while it is open.
      if @launcher_open
        case keycode
        when FmrbConst::KEY_UP
          launcher_scroll_up
          return
        when FmrbConst::KEY_DOWN
          launcher_scroll_down
          return
        end
      end

      # Key input for file selector filename (save mode)
      if @file_selector_open && @file_selector_mode == "save"
        if character == 10 || character == 13  # Enter
          if @file_selector_filename.length > 0
            path = if @file_selector_dir == "/"
                     "/#{@file_selector_filename}"
                   else
                     "#{@file_selector_dir}/#{@file_selector_filename}"
                   end
            close_file_selector(path)
          end
        elsif character == 8  # Backspace
          if @file_selector_filename.length > 0
            @file_selector_filename = @file_selector_filename[0...-1]
            draw_foreground
          end
        elsif character >= 32 && character <= 126  # Printable
          _c1 = "\x00"       # Integer#chr -> setbyte (no sp_str_chr in runtime)
          _c1.setbyte(0, character)
          @file_selector_filename += _c1
          draw_foreground
        end
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
      idx = dropdown_item_at(x, y)
      if idx != @dropdown_hover_idx
        @dropdown_hover_idx = idx
        draw_foreground
      end
    end
  end

  def handle_click(x, y)
    # About dialog has highest priority — any click closes it
    if @about_open
      close_about_dialog
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

    # Confirm dialog has highest priority
    if @cdlg_open
      if hit_confirm_dialog?(x, y)
        handle_confirm_dialog_click(x, y)
        return
      end
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
        handle_storage_dialog_click(x, y)
        return
      end
      close_storage_dialog
      return
    end

    # Network dialog
    if @net_open
      if hit_network_dialog?(x, y)
        handle_network_dialog_click(x, y)
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

    item = DROPDOWN_ITEMS[item_idx]
    close_dropdown

    case item[:key]
    when :launcher
      open_launcher
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
