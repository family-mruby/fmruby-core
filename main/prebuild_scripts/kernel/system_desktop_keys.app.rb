# System desktop, keyboard-only variant (doc/linux_native, L4).
#
# The desktop a machine without a pointer gets: the Cardputer Zero's 320x170
# screen and its keyboard. The machine definition picks it (desktop.variant
# = "keys"), the spawner starts this program in place of the mouse desktop
# (system_desktop.app.rb), and the rest of the machine sees an ordinary
# "system_desktop".
#
# The screen:
#   - a thin bar along the top, always there: the app in front, the kana
#     input mode, the clock (and the battery once there is a value, L6);
#   - below it, the whole rest of the screen: the menu of apps, the File
#     Manager, the file dialog, or the app in front. Apps always run full
#     screen there (the kernel places them, see desktop_keys).
#
# Keys: arrows and Enter in the menu; the switcher (kernel cycle_focus, the
# key system_conf's keys_switcher_key names) lists the running apps, Enter
# brings one to the front, Delete ends it, Esc goes back. Esc held for
# keys_esc_long_ms closes the app in front (host_task), and on this desktop
# ends Family mruby.
#
# Shared with the mouse desktop (system_desktop/): the strings (i18n.rb), the
# File Manager and the file dialog, the error dialog, the built-in app list
# and spawn_app (desktop_common.rb). The rake side combines the files named
# on the next line with this one (hosted_regen_mrb!).
#
# combine: system_desktop/i18n.rb system_desktop/desktop_common.rb system_desktop/error_dialog.rb system_desktop/file_manager.rb system_desktop/file_selector.rb

FmrbI18n.add(
  "en" => {
    keys_menu_title:   "Apps",
    keys_launch:       "Menu (start an app)",
    keys_switch_title: "Running apps",
    keys_switch_help:  "Enter:show  Del:quit  Esc:back",
  },
  "ja" => {
    keys_menu_title:   "アプリ",
    keys_launch:       "メニュー (アプリを起動)",
    keys_switch_title: "動いているアプリ",
    keys_switch_help:  "Enter:表示 Del:終了 Esc:戻る",
  }
)

class KeysDesktopApp < FmrbApp
  include DesktopCommonMixin
  include FileSelectorMixin
  include FileManagerMixin
  include ErrorDialogMixin

  # The bar. Ten rows: the 8-pixel font with a row above and below. The
  # spawner's app area starts here too (fmrb_app_spawner.c,
  # KEYS_BAR_H) -- the two must agree.
  BAR_H = 10
  BAR_BG = FmrbConst::THEME_MENU_BG
  BAR_FG = FmrbGfx::WHITE
  PANEL_BG = FmrbConst::THEME_WINDOW_BG
  TEXT = FmrbConst::THEME_TEXT
  HILITE = FmrbConst::THEME_HIGHLIGHT
  HILITE_TEXT = FmrbConst::THEME_TEXT_LIGHT
  BORDER = FmrbConst::THEME_BORDER
  BG_COLOR = FmrbConst::THEME_DESKTOP_BG

  ROW_H = 12
  TITLE_H = 12
  CLOCK_W = 84          # "MM/DD hh:mm:ss" in the 6-pixel font
  KANA_W = 10
  KANA_LABELS = ["A", "あ", "ア"]

  # What a menu entry may say about the screens it can draw on and whether it
  # needs a pointer (the .app.toml keys `screens` and `needs_pointer`).
  SCREEN_FORM = "320x170"

  def initialize
    super()
    @counter = 0
    @front_pid = -1
    @kana_mode = 0
    @menu_items = []
    @menu_sel = 0
    @menu_top = 0
    @switcher_open = false
    @sw_items = []
    @sw_sel = 0
    @starting_name = nil
    @starting_at = 0

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
    @fmgr_pressed_idx = -1
  end

  # ---- The shared panels at full size ----
  #
  # The mouse desktop shows them as windows of a fixed size; here they fill
  # the area under the bar.
  def fmgr_w; @window_width; end
  def fmgr_h; @window_height - BAR_H; end
  def fsel_w; @window_width; end
  def fsel_h; @window_height - BAR_H; end

  # Nothing of these on this desktop; the shared panels close them on open.
  def close_launcher; nil; end
  def close_dropdown; nil; end

  # The kernel keeps the rectangle of a mouse overlay for its hit testing.
  # There is no pointer here, so there is nothing to tell it.
  def notify_overlay_state(active, x, y, w, h, keyless = false)
    nil
  end

  def on_create
    Log.info("keys desktop: #{@window_width}x#{@window_height}")
    self.idle_gc = true
    self.closable = false
    @ui = FmrbUI.new(self, bg_painter: self)
    @ui.set_origin(0, 0)
    build_file_selector_widgets
    build_file_manager_widgets
    @fsel_x = 0
    @fsel_y = BAR_H
    @fmgr_x = 0
    @fmgr_y = BAR_H
    build_menu
    # Where apps go. The kernel places every app it starts there, full
    # screen, and asks this desktop before it shows the menu again.
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => "desktop_keys", "x" => 0, "y" => BAR_H,
                   "w" => @window_width, "h" => @window_height - BAR_H })
    @bg_gfx.clear(BG_COLOR) if @bg_gfx
    @bg_gfx.present if @bg_gfx
    update_composite_regions
    draw_foreground
    FmrbKernel.boot_complete!
    startup = read_conf_string("startup_app")
    spawn_app(startup) if startup && !startup.empty?
  end

  # ---- The menu ----

  # Built-ins first (Editor, Shell, File Manager), then the apps under /app
  # that say they draw on this screen and need no pointer.
  # The order the built-ins are offered in here: the editor first (the user's
  # list, 2026-10-26), so Enter alone opens it.
  BUILTIN_ORDER = ["default/editor", "default/shell"]

  def build_menu
    items = []
    catalog = builtin_app_catalog
    BUILTIN_ORDER.each do |app|
      catalog.each do |b|
        next unless b[:app] == app
        next unless keys_capable?(b[:screens], b[:needs_pointer])
        items << { label: b[:label], app: b[:app] }
      end
    end
    items << { label: FmrbI18n.t(:file_manager), app: "file_manager" }
    scan_declared_apps(items)
    @menu_items = items
    @menu_sel = 0 if @menu_sel >= items.size
    @menu_top = 0
  end

  def keys_capable?(screens, needs_pointer)
    return false if needs_pointer.nil? || needs_pointer
    return false unless screens
    screens.include?(SCREEN_FORM)
  end

  # /app/<category>/<name>.toml next to its script, or a bundle
  # /app/<category>/<name>/<name>.toml. Only the declaration is read; a toml
  # without both keys is not listed (there is no shrinking an app to fit).
  def scan_declared_apps(items)
    excluded = keys_excluded_dirs
    cats = read_names("/app")
    return unless cats
    found = []
    cats.sort.each do |cat|
      next if cat.include?(".")
      next if excluded.include?(cat)
      base = "/app/#{cat}"
      names = read_names(base)
      next unless names
      names.sort.each do |n|
        if n.end_with?(".toml")
          stem = n[0, n.length - 5]
          script = script_for(names, stem)
          add_declared(found, "#{base}/#{n}", script ? "#{base}/#{script}" : nil)
        elsif !n.include?(".")
          inner = read_names("#{base}/#{n}")
          next unless inner
          next unless inner.include?("#{n}.toml")
          script = script_for(inner, n)
          add_declared(found, "#{base}/#{n}/#{n}.toml", script ? "#{base}/#{n}/#{script}" : nil)
        end
      end
    end
    found.each { |f| items << f }
  rescue => e
    Log.warn("keys desktop: app scan: #{e.message}")
  end

  def keys_excluded_dirs
    list = []
    entries = FmrbApp.config("launcher_exclude")
    if entries
      entries.each do |e|
        d = e["dir"]
        list << d if d && !d.empty?
      end
    end
    list
  rescue
    []
  end

  def read_names(path)
    dir = Dir.open(path)
    out = []
    while (ent = dir.read)
      out << ent unless ent == "." || ent == ".."
    end
    dir.close
    out
  rescue
    nil
  end

  def script_for(names, stem)
    ["rb", "lua", "bas", "py"].each do |ext|
      return "#{stem}.#{ext}" if names.include?("#{stem}.#{ext}")
    end
    nil
  end

  def add_declared(found, toml, script)
    return unless script
    text = File.open(toml, "r") { |f| f.read }
    return unless text
    label = nil
    label_lang = nil
    screens = nil
    needs_pointer = nil
    lang_key = "app_screen_name_#{FmrbI18n.lang}"
    text.split("\n").each do |raw|
      line = raw.strip
      next if line.empty? || line.start_with?("#")
      break if line.start_with?("[")
      eq = line.index("=")
      next unless eq
      key = line[0, eq].strip
      val = line[eq + 1, line.length - eq - 1].strip
      case key
      when "screens" then screens = val
      when "needs_pointer" then needs_pointer = val.start_with?("true")
      when "app_screen_name" then label = unquote(val)
      when lang_key then label_lang = unquote(val)
      end
    end
    return unless keys_capable?(screens, needs_pointer)
    name = label_lang || label
    if name.nil? || name.empty?
      slash = script.rindex("/")
      name = slash ? script[slash + 1, script.length - slash - 1] : script
    end
    found << { label: name, app: script }
  rescue => e
    Log.warn("keys desktop: #{toml}: #{e.message}")
  end

  def unquote(v)
    h = v.index("#")
    v = v[0, h].strip if h && h > 0
    if v.length >= 2 && v.start_with?("\"") && v.end_with?("\"")
      return v[1, v.length - 2]
    end
    v
  end

  def menu_rows
    (@window_height - BAR_H - TITLE_H - 2) / ROW_H
  end

  def draw_menu
    y0 = BAR_H
    @gfx.fill_rect(0, y0, @window_width, @window_height - y0, PANEL_BG)
    @gfx.draw_text(4, y0 + 2, FmrbI18n.t(:keys_menu_title), BORDER, PANEL_BG, mixed: true)
    @gfx.draw_line(2, y0 + TITLE_H, @window_width - 3, y0 + TITLE_H, BORDER)
    rows = menu_rows
    i = 0
    while i < rows
      idx = @menu_top + i
      break if idx >= @menu_items.size
      y = y0 + TITLE_H + 2 + i * ROW_H
      item = @menu_items[idx]
      if idx == @menu_sel
        @gfx.fill_rect(2, y, @window_width - 4, ROW_H, HILITE)
        @gfx.draw_text(8, y + 2, item[:label], HILITE_TEXT, HILITE, mixed: true)
      else
        @gfx.draw_text(8, y + 2, item[:label], TEXT, PANEL_BG, mixed: true)
      end
      i += 1
    end
  end

  def menu_move(delta)
    n = @menu_items.size
    return if n == 0
    sel = @menu_sel + delta
    sel = 0 if sel < 0
    sel = n - 1 if sel >= n
    @menu_sel = sel
    rows = menu_rows
    @menu_top = sel if sel < @menu_top
    @menu_top = sel - rows + 1 if sel >= @menu_top + rows
    draw_foreground
  end

  def menu_activate
    item = @menu_items[@menu_sel]
    return unless item
    if item[:app] == "file_manager"
      open_file_manager
    else
      spawn_app(item[:app])
    end
    # The menu comes back at the top, where Enter opens the editor.
    @menu_sel = 0
    @menu_top = 0
  end

  # ---- The switcher ----
  #
  # Opened by the kernel (its cycle_focus, the configured key): the running
  # apps, then the menu. It covers the app in front only while it is open.

  def open_switcher
    items = []
    procs = FmrbApp.ps
    i = 0
    while i < procs.size
      p = procs[i]
      i += 1
      next unless p[:state] >= 1 && p[:state] <= 3
      next if p[:type] == 0 || p[:headless]
      next if p[:name] == name
      items << { pid: p[:id], label: p[:name].to_s }
    end
    items << { pid: -1, label: FmrbI18n.t(:keys_launch) }
    @sw_items = items
    @sw_sel = 0
    j = 0
    while j < items.size
      @sw_sel = j if @front_pid >= 0 && items[j][:pid] == @front_pid
      j += 1
    end
    @switcher_open = true
    update_composite_regions
    draw_foreground
  end

  def close_switcher
    @switcher_open = false
    update_composite_regions
    draw_foreground
  end

  def switcher_rect
    rows = @sw_items.size + 1
    h = TITLE_H + 2 + rows * ROW_H + 2
    max_h = @window_height - BAR_H - 8
    h = max_h if h > max_h
    w = @window_width - 40
    [20, BAR_H + 4, w, h]
  end

  def draw_switcher
    r = switcher_rect
    x = r[0]
    y = r[1]
    w = r[2]
    h = r[3]
    @gfx.fill_rect(x, y, w, h, PANEL_BG)
    @gfx.draw_rect(x, y, w, h, BORDER)
    @gfx.fill_rect(x + 1, y + 1, w - 2, TITLE_H - 1, BAR_BG)
    @gfx.draw_text(x + 4, y + 2, FmrbI18n.t(:keys_switch_title), BAR_FG, BAR_BG, mixed: true)
    i = 0
    while i < @sw_items.size
      iy = y + TITLE_H + 2 + i * ROW_H
      break if iy + ROW_H > y + h - ROW_H
      it = @sw_items[i]
      label = it[:label]
      label = "* #{label}" if it[:pid] >= 0 && it[:pid] == @front_pid
      if i == @sw_sel
        @gfx.fill_rect(x + 2, iy, w - 4, ROW_H, HILITE)
        @gfx.draw_text(x + 6, iy + 2, label, HILITE_TEXT, HILITE, mixed: true)
      else
        @gfx.draw_text(x + 6, iy + 2, label, TEXT, PANEL_BG, mixed: true)
      end
      i += 1
    end
    @gfx.draw_text(x + 4, y + h - ROW_H, FmrbI18n.t(:keys_switch_help), BORDER, PANEL_BG, mixed: true)
  end

  def handle_switcher_key(scancode)
    case scancode
    when FmrbConst::KEY_UP
      @sw_sel -= 1 if @sw_sel > 0
      draw_foreground
    when FmrbConst::KEY_DOWN
      @sw_sel += 1 if @sw_sel < @sw_items.size - 1
      draw_foreground
    when FmrbConst::KEY_ENTER
      it = @sw_items[@sw_sel]
      @switcher_open = false
      if it && it[:pid] >= 0
        kernel_cmd("keys_show", it[:pid])
      else
        kernel_cmd("keys_menu", -1)
      end
      update_composite_regions
      draw_foreground
    when FmrbConst::KEY_DELETE, FmrbConst::KEY_BACKSPACE
      it = @sw_items[@sw_sel]
      if it && it[:pid] >= 0
        @switcher_open = false
        kernel_cmd("keys_close", it[:pid])
        update_composite_regions
        draw_foreground
      end
    when FmrbConst::KEY_ESC
      @switcher_open = false
      # Back to whatever was in front (the kernel gives it the keyboard).
      kernel_cmd(@front_pid >= 0 ? "keys_show" : "keys_menu", @front_pid)
      update_composite_regions
      draw_foreground
    end
    nil
  end

  def kernel_cmd(cmd, pid)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => cmd, "pid" => pid })
  end

  # ---- Drawing ----

  def menu_visible?
    @front_pid < 0
  end

  def draw_bar
    @gfx.fill_rect(0, 0, @window_width, BAR_H, BAR_BG)
    @gfx.draw_text(2, 1, bar_title, BAR_FG, BAR_BG, mixed: true)
    draw_bar_status
  end

  def bar_title
    return FmrbI18n.t(:file_manager) if @file_manager_open && !@file_selector_open
    if @front_pid >= 0
      info = front_name
      return info if info
    end
    "Family mruby"
  end

  def front_name
    procs = FmrbApp.ps
    i = 0
    while i < procs.size
      p = procs[i]
      return p[:name].to_s if p[:id] == @front_pid
      i += 1
    end
    nil
  end

  # The right end: kana cell, clock. The battery joins them once the OS
  # service gives a value (L6); with none there is nothing to show.
  def draw_bar_status
    cx = @window_width - CLOCK_W - 1
    @gfx.draw_wallclock(cx, 1, BAR_FG, BAR_BG)
    kx = cx - 4 - KANA_W
    @gfx.fill_rect(kx, 0, KANA_W, BAR_H, BAR_FG)
    @gfx.draw_text(kx + 1, 1, KANA_LABELS[@kana_mode] || "A", BAR_BG, BAR_FG, mixed: true)
  end

  def draw_foreground
    @gfx.clear(FmrbApp::TRANSPARENT_COLOR)
    draw_bar
    if @file_selector_open
      draw_file_selector
    elsif @file_manager_open
      draw_file_manager
    elsif menu_visible?
      draw_menu
    end
    draw_switcher if @switcher_open
    draw_starting if @starting_name
    draw_error_dialog if @error_dlg_open
    @gfx.present unless @ui.flush
  end

  def draw_starting
    label = "#{@starting_name} #{FmrbI18n.t(:starting)}"
    w = FmrbI18n.text_width(label) + 16
    w = @window_width - 8 if w > @window_width - 8
    x = (@window_width - w) / 2
    y = @window_height - 24
    @gfx.fill_rect(x, y, w, 18, PANEL_BG)
    @gfx.draw_rect(x, y, w, 18, BORDER)
    @gfx.draw_text(x + 8, y + 5, label, TEXT, PANEL_BG, mixed: true)
  end

  # The bar always; the rest of the canvas only while something of the
  # desktop's is up there (the menu, a panel, the switcher, a dialog). With an
  # app in front and nothing open, the app shows through.
  def update_composite_regions
    return unless @gfx
    full = @file_selector_open || @file_manager_open || menu_visible?
    if full
      @gfx.set_composite_regions([
        { dst_x: 0, dst_y: 0, w: @window_width, h: @window_height, transparent: false }
      ])
      return
    end
    regions = [{ dst_x: 0, dst_y: 0, w: @window_width, h: BAR_H, transparent: false }]
    if @switcher_open
      r = switcher_rect
      regions << { dst_x: r[0], dst_y: r[1], w: r[2], h: r[3], transparent: false }
    end
    if @starting_name
      regions << { dst_x: 0, dst_y: @window_height - 24, w: @window_width, h: 18, transparent: true }
    end
    if @error_dlg_open
      regions << { dst_x: @error_dlg_x, dst_y: @error_dlg_y,
                   w: ErrorDialogMixin::EDLG_W, h: @error_dlg_h, transparent: false }
    end
    @gfx.set_composite_regions(regions)
  end

  # FmrbUI's bg_painter (a hidden widget's ground): the panel when one is
  # open, else the colour key.
  def paint_bg_rect(gfx, x, y, w, h)
    c = FmrbApp::TRANSPARENT_COLOR
    c = PANEL_BG if @file_selector_open || @file_manager_open
    gfx.fill_rect(x, y, w, h, c)
    nil
  end

  def handle_widget(id)
    handle_file_selector_widget(id) if @file_selector_open
    handle_file_manager_widget(id) if @file_manager_open
    nil
  end

  # ---- Update loop ----

  def on_update
    @counter += 1
    if @counter % 2 == 0
      if @starting_name && Machine.board_millis - @starting_at > 25000
        @starting_name = nil
        update_composite_regions
        draw_foreground
      else
        draw_bar_status
        @gfx.present
      end
    end
    if @counter % 10 == 0
      GC.start if FmrbApp.pool_usage >= 70
    end
    500
  end

  # ---- Messages from the kernel ----

  def on_control(msg)
    cmd = msg["cmd"]
    if cmd == "keys_front"
      pid = msg["pid"] || -1
      @front_pid = pid
      @switcher_open = false
      update_composite_regions
      draw_foreground
    elsif cmd == "keys_switcher"
      open_switcher
    elsif cmd == "file_select"
      open_file_selector(msg["requester_pid"], msg["mode"] || "open")
    elsif cmd == "app_starting"
      @starting_name = msg["name"] || ""
      @starting_at = Machine.board_millis
      update_composite_regions
      draw_foreground
    elsif cmd == "app_started"
      if @starting_name
        @starting_name = nil
        update_composite_regions
        draw_foreground
      end
    elsif cmd == "show_error"
      @starting_name = nil
      err = FmrbApp._get_last_error
      open_error_dialog(err[:name] || "Unknown", err[:error] || "Unknown error") if err
    elsif cmd == "spawn_failed"
      reason = (msg["reason"] || "See the log for the reason.").to_s
      open_error_dialog(msg["app"] || "Unknown", "Failed to launch.\n" + reason)
    elsif cmd == "apps_changed"
      draw_foreground if @switcher_open || @front_pid >= 0
    elsif cmd == "do_reboot"
      FmrbApp.reboot
    end
    nil
  end

  # ---- Keys (only while this desktop has the keyboard) ----

  def on_event(ev)
    if ev[:type] == :kana_mode
      @kana_mode = ev[:mode] || 0
      draw_bar_status
      @gfx.present
      return
    end
    return unless ev[:type] == :key_down
    scancode = ev[:scancode] || 0
    character = ev[:character] || 0

    if @error_dlg_open
      close_error_dialog
      return
    end
    if @switcher_open
      handle_switcher_key(scancode)
      return
    end
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
      return
    end
    if @file_manager_open
      handle_file_manager_key(scancode, character)
      return
    end
    return unless menu_visible?
    case scancode
    when FmrbConst::KEY_UP then menu_move(-1)
    when FmrbConst::KEY_DOWN then menu_move(1)
    when FmrbConst::KEY_PGUP then menu_move(-menu_rows)
    when FmrbConst::KEY_PGDN then menu_move(menu_rows)
    when FmrbConst::KEY_ENTER then menu_activate
    end
    nil
  end
end

app = KeysDesktopApp.new
app.start
