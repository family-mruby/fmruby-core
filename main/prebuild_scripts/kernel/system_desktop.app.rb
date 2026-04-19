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
  include ErrorDialogMixin
  include ClockSettingMixin
  include TaskbarMixin

  MENU_BAR_HEIGHT = 13
  MENU_BG = FmrbConst::THEME_MENU_BG
  DROPDOWN_BG = FmrbConst::THEME_WINDOW_BG
  DROPDOWN_TEXT = FmrbConst::THEME_TEXT
  DROPDOWN_HIGHLIGHT = FmrbConst::THEME_HIGHLIGHT
  BG_COLOR = FmrbConst::THEME_DESKTOP_BG
  BG_IMAGE_PATH = "/data/BG_sample.png"
  BOOT_IMAGE_PATH = "/boot/boot.png"
  BOOT_TILE_W = 32
  BOOT_TILE_H = 24
  BOOT_TILES_PER_FRAME = 5
  BOOT_FRAME_MS = 60
  BOOT_HOLD_MS = 1000  # Wait time after full reveal, before swapping to desktop

  # Dropdown menu items
  DROPDOWN_ITEMS = [
    { label: "Launcher" },
    { label: "File Manager" },
    { label: "Log Viewer" },
    { label: "Monitor" },
    { label: "Set Clock" },
    { label: "Config", app: "default/config" },
  ]

  DROPDOWN_X = 0
  DROPDOWN_Y = MENU_BAR_HEIGHT
  DROPDOWN_W = 80
  DROPDOWN_ITEM_H = 12

  def initialize
    super()
    @counter = 0
    @mem_update_interval = 30

    @dropdown_open = false
    @launcher_open = false
    @launcher_selected = -1
    @launcher_scroll = 0
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

    # Boot animation state
    @boot_anim_state = :init  # :init -> :revealing -> :wait_to_finish -> :done
    @boot_anim_idx = 0
    @boot_tiles = nil
    @boot_img = nil
  end

  def on_create()
    Log.info("on_create called")

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

    # Center file manager
    @fmgr_x = (@window_width - FMGR_W) / 2
    @fmgr_y = MENU_BAR_HEIGHT + (@window_height - MENU_BAR_HEIGHT - FMGR_H) / 2

    init_taskbar
    @taskbar_focused_pid = nil

    start_boot_animation
  end

  # ---- Boot animation ----

  def start_boot_animation
    return unless @gfx && @bg_gfx

    # Cover both canvases with opaque black first.
    @bg_gfx.clear(0x00)
    @bg_gfx.present
    @gfx.clear(0x00)
    @gfx.present

    # Load logo onto background layer (hidden under black @gfx).
    @boot_img = @bg_gfx.create_image(BOOT_IMAGE_PATH)
    if @boot_img
      @bg_gfx.draw_image(@boot_img[:id], x: 0, y: 0)
      @bg_gfx.present
    else
      Log.warn("Boot logo not found: #{BOOT_IMAGE_PATH}")
    end

    # Precompute tile positions covering the whole foreground.
    cols = (@window_width + BOOT_TILE_W - 1) / BOOT_TILE_W
    rows = (@window_height + BOOT_TILE_H - 1) / BOOT_TILE_H
    tiles = []
    rows.times do |r|
      cols.times do |c|
        tiles << [c * BOOT_TILE_W, r * BOOT_TILE_H]
      end
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
  end

  def tick_boot_animation
    case @boot_anim_state
    when :revealing
      BOOT_TILES_PER_FRAME.times do
        break if @boot_anim_idx >= @boot_tiles.size
        tx, ty = @boot_tiles[@boot_anim_idx]
        # 0x01 is the foreground canvas' color key -> pixel becomes transparent.
        @gfx.fill_rect(tx, ty, BOOT_TILE_W, BOOT_TILE_H, 0x01)
        @boot_anim_idx += 1
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

  def finish_boot_animation
    if @boot_img
      @bg_gfx.delete_image(@boot_img[:id])
      @boot_img = nil
    end
    draw_background
    draw_foreground
    FmrbApp.enable_cursor
    @boot_anim_state = :done
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
    ch = character.chr rescue nil
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
    draw_error_dialog if @error_dlg_open
    @gfx.present
  end

  def draw_menu_bar
    @gfx.clear(0x01)  # Transparent (clear foreground, 0x01 = transparent color key)
    @gfx.fill_rect(0, 0, @window_width, MENU_BAR_HEIGHT, MENU_BG)
    @gfx.draw_text(2, 2, "Family mruby", FmrbGfx::WHITE)
    draw_taskbar
    draw_clock
    @gfx.draw_line(0, MENU_BAR_HEIGHT - 1, @window_width, MENU_BAR_HEIGHT - 1, 0x60)
  end

  def draw_clock
    wc = FmrbApp.wallclock
    return unless wc
    text = sprintf("%02d/%02d %02d:%02d:%02d",
                   wc[:month], wc[:day], wc[:hour], wc[:minute], wc[:second])
    @gfx.draw_text(@window_width - 90, 2, text, FmrbGfx::WHITE, MENU_BG)
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

  def notify_overlay_state(active, x, y, w, h)
    data = {
      "cmd" => "overlay_state",
      "active" => active,
      "rect_x" => x, "rect_y" => y,
      "rect_w" => w, "rect_h" => h
    }
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
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

    # Update clock and taskbar every ~1 second (330ms * 3 = ~1s)
    if @counter % 3 == 0
      update_taskbar_apps
      draw_foreground
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

    330
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
    elsif msg["cmd"] == "confirm_dialog"
      # Build callback data hash from message fields
      cb_data = {}
      msg.each do |k, v|
        next if k == "cmd" || k == "message" || k == "on_yes_cmd"
        cb_data[k] = v
      end
      open_confirm_dialog(msg["message"], msg["on_yes_cmd"], cb_data)
    end
  end

  def on_event(ev)
    if ev[:type] == :mouse_up
      button = ev[:button] || 1
      if button == 3 && @file_manager_open
        # Right click in file manager
        if hit_file_manager?(ev[:x], ev[:y])
          handle_file_manager_right_click(ev[:x], ev[:y])
          return
        end
      end
      handle_click(ev[:x], ev[:y])
    end

    if ev[:type] == :key_down
      character = ev[:character] || 0

      # Key input for file selector filename (save mode)
      if @file_selector_open && @file_selector_mode == "save"
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
        return
      end

      # Keyboard shortcuts (only when no dialog/overlay is active)
      if character >= 32 && character <= 126
        handle_shortcut(character)
      end
    end
  end

  def handle_click(x, y)
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

    case item[:label]
    when "Launcher"
      open_launcher
    when "File Manager"
      open_file_manager
    when "Log Viewer"
      spawn_app("default/logviewer")
    when "Monitor"
      spawn_app("default/monitor")
    when "Set Clock"
      open_clock_setting
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
