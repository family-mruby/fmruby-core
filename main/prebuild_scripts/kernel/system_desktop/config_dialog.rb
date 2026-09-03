# System Config dialog module for SystemDesktopApp.
#
# Edits /etc/system_conf.toml and offers reboot. All changes are line-level
# rewrites of the existing file so comments, [[shortcuts]] / [[sync_files]]
# entries, and other unrelated keys are preserved.
#
# Items (in display order):
#   language, keyboard_layout, mouse_scale_x/y, theme preset,
#   timezone, debug_mode, display_margin_x/y
#
# Save & Reboot is shown only on ESP32; on Linux the user must restart the
# host process manually after a plain Save.

module ConfigDialogMixin
  CFG_PATH    = "/etc/system_conf.toml"
  # Shipped with the firmware, never written at runtime. The kernel boots from
  # it when CFG_PATH is unreadable.
  CFG_FACTORY_PATH = "/etc/system_conf.factory.toml"
  CFG_W       = 280
  CFG_TITLE_H = 14
  CFG_ROW_H   = 14
  CFG_FOOTER_H= 22
  CFG_ARROW_W = 10

  CFG_BG     = FmrbConst::THEME_WINDOW_BG
  CFG_TEXT   = FmrbConst::THEME_TEXT
  CFG_HEADER = FmrbConst::THEME_MENU_BG
  CFG_SEL    = FmrbConst::THEME_HIGHLIGHT
  CFG_BTN    = FmrbConst::THEME_BUTTON
  CFG_BORDER = FmrbConst::THEME_BORDER

  # Named theme presets. On save, the chosen preset expands to nine color
  # entries written into the [theme] section.
  CFG_THEME_PRESETS = {
    # The machine's own look: the same nine values every config/system_conf_*
    # ships (wasm excepted) and the same ones page_settings_wasm.c puts back
    # when the browser is asked for the device palette. All three say "light";
    # keep them in step.
    "light" => {
      desktop_bg: 0xF6, menu_bg: 0xC5, window_bg: 0xFF, text: 0x00,
      text_light: 0xFF, highlight: 0xEE, border: 0x60, button: 0x60, dir_color: 0x03
    },
    "dark" => {
      desktop_bg: 0x00, menu_bg: 0x49, window_bg: 0x24, text: 0xFF,
      text_light: 0xFF, highlight: 0x6D, border: 0xB6, button: 0x49, dir_color: 0x1D
    },
    # What the browser build ships with. It is here so the machine can name
    # the colours it is actually wearing: without it the browser's own default
    # matched no preset and this row read "light", and saving made that true.
    # Keep in step with the [theme] block of config/system_conf_wasm.toml.
    "cyberpunk" => {
      desktop_bg: 0x00, menu_bg: 0x22, window_bg: 0x00, text: 0x1C,
      text_light: 0xFF, highlight: 0xE3, border: 0x0E, button: 0x46, dir_color: 0xFC
    },
  }
  CFG_THEME_KEYS = [:desktop_bg, :menu_bg, :window_bg, :text, :text_light,
                    :highlight, :border, :button, :dir_color]

  # Setting rows. :enum -> options[], :float / :int -> min/max/step,
  # :bool -> on/off. field is the TOML key, or :theme_preset (synthetic) which
  # expands to the nine [theme] entries on save.
  # Rows a browser has no answer for. The mouse scale is applied to a
  # relative device in usb_task; a page sends absolute canvas coordinates, so
  # nothing the slider does can reach them. The radios do not exist -- the web
  # config file says so in a comment, while Save was writing both keys back.
  CFG_WEB_HIDDEN = ["mouse_scale_x", "mouse_scale_y",
                    "ble_auto_start", "wifi_auto_start"]

  # Retro has one radio. The S3 runs WiFi or BLE, never both: software
  # coexistence is off in sdkconfig because it makes NimBLE lose sync about
  # ten seconds after boot, and the two also share the internal RAM
  # (main/drivers/wifi/wifi_task.c says the same at more length).
  #
  # Boot settles a both-on config by starting BLE and logging a warning, so
  # turning WiFi on and leaving BLE on looks like "I switched WiFi on and
  # nothing happened" -- the warning is in the serial log, which is not where
  # the person who used the dialog is looking. Turning one on here turns the
  # other off, so the dialog shows what the machine will actually do.
  #
  # Modern keeps both: its radios are on the C6, where they coexist.
  CFG_ONE_RADIO = (FmrbConst::HW_FAMILY == "retro")
  CFG_RADIO_OTHER = { "ble_auto_start"  => "wifi_auto_start",
                      "wifi_auto_start" => "ble_auto_start" }

  CFG_SETTINGS = [
    { key: :language,         field: "language",         type: :enum,  options: ["en", "ja"] },
    { key: :keyboard_layout,  field: "keyboard_layout",  type: :enum,  options: ["jp", "us"] },
    { key: :mouse_scale_x,    field: "mouse_scale_x",    type: :float, min: 0.1, max: 2.0, step: 0.1 },
    { key: :mouse_scale_y,    field: "mouse_scale_y",    type: :float, min: 0.1, max: 2.0, step: 0.1 },
    { key: :theme,            field: "theme_preset",     type: :enum,  options: ["light", "dark", "cyberpunk"] },
    # The choices are found when the dialog opens (cfg_scan_wallpapers), so
    # this list is only the two that always exist. "" means "whatever the
    # theme says"; a path names a file and beats the theme.
    { key: :wallpaper,        field: "wallpaper",        type: :enum,  options: ["", "none"] },
    { key: :timezone,         field: "timezone",         type: :enum,
      options: ["JST-9", "UTC", "EST5", "PST8", "CET-1", "CST-8"] },
    { key: :debug_mode,       field: "debug_mode",       type: :bool },
    { key: :ble_auto_start,   field: "ble_auto_start",   type: :bool },
    { key: :wifi_auto_start,  field: "wifi_auto_start",  type: :bool },
    { key: :display_margin_x, field: "display_margin_x", type: :int,   min: 0, max: 16, step: 1 },
    { key: :display_margin_y, field: "display_margin_y", type: :int,   min: 0, max: 16, step: 1 },
  ].reject { |s| FmrbConst::BOARD == "wasm" && CFG_WEB_HIDDEN.include?(s[:field]) }

  # Tall enough for the rows there actually are. It was a fixed 200, which the
  # rows outgrew the moment one was added: the last of them ended up under the
  # footer. The 4 is the gap between the last row and the separator line.
  CFG_H = CFG_TITLE_H + 2 + CFG_SETTINGS.size * CFG_ROW_H + 4 + CFG_FOOTER_H

  CFG_BTN_H = CFG_FOOTER_H - 8

  # ---- Widgets ----
  #
  # Only the footer buttons. The eleven setting rows stay hand-drawn: a row
  # is a label, a derived value and two arrows, where the value is typed
  # (enum / bool / int / float), its text is computed and sometimes
  # localized, and the row carries a selection highlight. Nothing in the
  # widget set means that, and making Enum mean it would need an app-side
  # table mapping shown text back to stored value -- the same "belongs to
  # the app" argument that kept List out.

  def build_config_widgets
    lc = FmrbI18n.t(:cancel)
    ls = FmrbI18n.t(:save)
    lr = FmrbI18n.t(:save_and_reboot)
    @ui.button(:cfg_cancel, 0, 0, FmrbI18n.text_width(lc) + 12, CFG_BTN_H, lc)
    @ui.button(:cfg_save, 0, 0, FmrbI18n.text_width(ls) + 12, CFG_BTN_H, ls)
    @ui.button(:cfg_reboot, 0, 0, FmrbI18n.text_width(lr) + 12, CFG_BTN_H, lr)
    show_config_widgets(false)
    nil
  end

  def show_config_widgets(on)
    @ui.set_visible(:cfg_cancel, on)
    @ui.set_visible(:cfg_save, on)
    # Rebooting is an ESP32 thing; on Linux the button is never shown.
    @ui.set_visible(:cfg_reboot, on && FmrbConst::PLATFORM == "esp32")
    nil
  end

  # Widths were fixed when the buttons were made; only where they sit
  # depends on the dialog, which moves.
  def place_config_widgets(btn_y)
    cancel = @ui.find(:cfg_cancel)
    save = @ui.find(:cfg_save)
    reboot = @ui.find(:cfg_reboot)
    esp32 = (FmrbConst::PLATFORM == "esp32")
    total = cancel.w + save.w + (esp32 ? reboot.w + 12 : 6)
    bx = @cfg_x + (CFG_W - total) / 2
    @ui.move(:cfg_cancel, bx, btn_y, cancel.w, CFG_BTN_H)
    bx += cancel.w + 6
    @ui.move(:cfg_save, bx, btn_y, save.w, CFG_BTN_H)
    bx += save.w + 6
    @ui.move(:cfg_reboot, bx, btn_y, reboot.w, CFG_BTN_H) if esp32
    nil
  end

  def handle_config_dialog_widget(id)
    case id
    when :cfg_cancel then close_config_dialog
    when :cfg_save   then cfg_do_save(false)
    when :cfg_reboot then cfg_do_save(true)
    end
    nil
  end

  # ---- Lifecycle ----

  def open_config_dialog
    @cfg_open = true
    show_config_widgets(true)
    @cfg_values = {}
    @cfg_file_lines = []
    @cfg_selected = -1
    @cfg_status = nil
    @cfg_status_until = 0
    @cfg_x = (@window_width - CFG_W) / 2
    @cfg_y = (@window_height - CFG_H) / 2
    # Keep below the menu bar. picoruby resolves bare constants in the mixin's
    # lexical scope only, so reach into the including class explicitly.
    menu_h = self.class::MENU_BAR_HEIGHT
    @cfg_y = menu_h + 2 if @cfg_y < menu_h + 2
    cfg_scan_wallpapers
    cfg_load
    close_launcher
    close_dropdown
    notify_overlay_state(true, @cfg_x, @cfg_y, CFG_W, CFG_H)
    update_composite_regions
    draw_foreground
  end

  def close_config_dialog
    return unless @cfg_open
    @cfg_open = false
    # Hidden and flushed before the repaint, or their holes land on whatever
    # was behind the dialog.
    show_config_widgets(false)
    @ui.flush
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def hit_config_dialog?(x, y)
    @cfg_open &&
      x >= @cfg_x && x < @cfg_x + CFG_W &&
      y >= @cfg_y && y < @cfg_y + CFG_H
  end

  # ---- Loading ----

  def cfg_load
    path = CFG_PATH
    @cfg_file_lines = []
    begin
      f = File.open(path, "r")
      content = f.read
      f.close
      @cfg_file_lines = content.split("\n", -1)
    rescue => e
      Log.warn("Config: cannot read #{CFG_PATH}: #{e.message}")
    end

    # The kernel boots from the factory copy when the live file is damaged, so
    # show the same thing here instead of an empty dialog that would save a
    # near-empty file over what is left.
    if @cfg_file_lines.empty? || (@cfg_file_lines.size == 1 && @cfg_file_lines[0].to_s.strip.empty?)
      begin
        f = File.open(CFG_FACTORY_PATH, "r")
        content = f.read
        f.close
        @cfg_file_lines = content.split("\n", -1)
        Log.warn("Config: #{CFG_PATH} unusable, showing factory settings")
      rescue => e
        Log.warn("Config: cannot read #{CFG_FACTORY_PATH}: #{e.message}")
      end
    end

    theme = {}
    in_theme = false
    other_section = false
    @cfg_file_lines.each do |raw|
      line = raw.strip
      next if line.empty? || line.start_with?("#")
      if line.start_with?("[")
        in_theme = (line == "[theme]")
        other_section = !in_theme
        next
      end
      idx = line.index("=")
      next unless idx
      key = line[0, idx].strip
      val_str = line[idx + 1, line.length - idx - 1].to_s
      hash_idx = val_str.index("#")
      val_str = val_str[0, hash_idx] if hash_idx
      val_str = val_str.strip
      val = cfg_parse_value(val_str)
      if in_theme
        theme[key.to_sym] = val
      elsif !other_section
        @cfg_values[key] = val
      end
    end

    @cfg_values["theme_preset"] = cfg_detect_preset(theme) || "light"
    CFG_SETTINGS.each { |s| cfg_ensure_default(s) }
  end

  def cfg_parse_value(s)
    return true  if s == "true"
    return false if s == "false"
    if s.length >= 2 && s.start_with?('"') && s.end_with?('"')
      return s[1, s.length - 2]
    end
    # 0x.. -- how every [theme] colour is written. Without this they came back
    # as the strings "0xC5" and so on, matched no preset (which holds numbers),
    # and the Theme row read "light" whatever the file actually said. Saving
    # then made that true.
    if cfg_hex?(s)
      return s[2, s.length - 2].to_s.to_i(16)
    end
    numeric, has_dot = cfg_classify_numeric(s)
    if numeric
      return has_dot ? s.to_f : s.to_i
    end
    s
  end

  # "0x" followed by at least one hex digit, in either case. No regex.
  def cfg_hex?(s)
    return false if s.length < 3
    return false unless s.getbyte(0) == 48                       # '0'
    b1 = s.getbyte(1)
    return false unless b1 == 120 || b1 == 88                    # 'x' 'X'
    i = 2
    while i < s.length
      b = s.getbyte(i)
      ok = (b >= 48 && b <= 57) || (b >= 65 && b <= 70) || (b >= 97 && b <= 102)
      return false unless ok
      i += 1
    end
    true
  end

  # Returns [numeric?, has_dot?]. Accepts optional leading '-' and decimal
  # digits with at most one '.'. Used in place of a regex.
  def cfg_classify_numeric(s)
    return [false, false] if s.length == 0
    i = 0
    i = 1 if s.getbyte(0) == 45  # '-'
    return [false, false] if i >= s.length
    has_dot = false
    saw_digit = false
    while i < s.length
      b = s.getbyte(i)
      if b == 46
        return [false, false] if has_dot
        has_dot = true
      elsif b >= 48 && b <= 57
        saw_digit = true
      else
        return [false, false]
      end
      i += 1
    end
    [saw_digit, has_dot]
  end

  # The preset the machine is wearing right now, from the constants every VM
  # gets at startup -- not from the file, which the desktop would otherwise
  # have to read before it can pick a wallpaper.
  # One radio (Retro): switching one on switches the other off. Only in that
  # direction -- switching a radio off says nothing about the other one.
  def cfg_enforce_one_radio(field)
    return nil unless CFG_ONE_RADIO
    return nil unless @cfg_values[field]
    other = CFG_RADIO_OTHER[field]
    return nil unless other
    @cfg_values[other] = false
    nil
  end

  # The wallpaper row's choices are whatever the machine has, so they are
  # found rather than listed: "" (follow the theme), "none" (no picture), the
  # shipped pictures, and any .png the user has put in /home. Scanned once
  # when the dialog opens -- a directory read per key press would be felt.
  def cfg_options_for(s)
    return s[:options] unless s[:key] == :wallpaper
    @cfg_wallpapers || s[:options]
  end

  def cfg_scan_wallpapers
    list = ["", "none"]
    ["/usr/share/backgrounds", "/home"].each do |dir|
      begin
        d = Dir.open(dir)
        names = []
        while (ent = d.read)
          names << ent if ent.end_with?(".png")
        end
        d.close
        names.sort.each { |n| list << "#{dir}/#{n}" }
      rescue
        # A machine without that directory simply offers fewer choices.
      end
    end
    @cfg_wallpapers = list
  end

  def cfg_current_preset
    cfg_detect_preset({
      desktop_bg: FmrbConst::THEME_DESKTOP_BG,
      menu_bg: FmrbConst::THEME_MENU_BG,
      window_bg: FmrbConst::THEME_WINDOW_BG,
      text: FmrbConst::THEME_TEXT,
      text_light: FmrbConst::THEME_TEXT_LIGHT,
      highlight: FmrbConst::THEME_HIGHLIGHT,
      border: FmrbConst::THEME_BORDER,
      button: FmrbConst::THEME_BUTTON,
      dir_color: FmrbConst::THEME_DIR_COLOR,
    })
  end

  def cfg_detect_preset(theme)
    CFG_THEME_PRESETS.each do |name, preset|
      match = true
      CFG_THEME_KEYS.each do |k|
        if preset[k] != theme[k]
          match = false
          break
        end
      end
      return name if match
    end
    nil
  end

  def cfg_ensure_default(s)
    return if @cfg_values.key?(s[:field])
    case s[:type]
    when :enum  then @cfg_values[s[:field]] = cfg_options_for(s)[0]
    when :bool  then @cfg_values[s[:field]] = false
    when :int, :float then @cfg_values[s[:field]] = s[:min]
    end
  end

  # ---- Drawing ----

  def draw_config_dialog
    return unless @cfg_open
    x = @cfg_x
    y = @cfg_y
    @gfx.fill_rect(x, y, CFG_W, CFG_H, CFG_BG)
    @gfx.draw_rect(x, y, CFG_W, CFG_H, CFG_BORDER)
    @gfx.fill_rect(x + 1, y + 1, CFG_W - 2, CFG_TITLE_H - 1, CFG_HEADER)
    @gfx.draw_text(x + 4, y + 3, FmrbI18n.t(:config), FmrbConst::THEME_TEXT_LIGHT, CFG_HEADER, mixed: true)
    CFG_SETTINGS.each_with_index { |s, i| cfg_draw_row(i, s) }
    cfg_draw_footer
  end

  def cfg_row_y(i)
    @cfg_y + CFG_TITLE_H + 2 + i * CFG_ROW_H
  end

  def cfg_arrow_l_x
    @cfg_x + CFG_W / 2 - 4
  end

  def cfg_arrow_r_x
    @cfg_x + CFG_W - 14
  end

  def cfg_value_x
    @cfg_x + CFG_W / 2 + 8
  end

  def cfg_value_text(s)
    v = @cfg_values[s[:field]]
    case s[:type]
    when :bool  then v ? FmrbI18n.t(:on) : FmrbI18n.t(:off)
    when :float then sprintf("%.1f", v.to_f)
    when :int   then v.to_i.to_s
    when :enum
      if s[:key] == :theme
        FmrbI18n.t("theme_#{v}".to_sym)
      elsif s[:key] == :wallpaper
        cfg_wallpaper_text(v.to_s)
      else
        v.to_s
      end
    else v.to_s
    end
  end

  # A path is too long for the value column, so only the file's own name is
  # shown; the two special values get words instead.
  def cfg_wallpaper_text(v)
    return FmrbI18n.t(:wallpaper_theme) if v.length == 0
    return FmrbI18n.t(:wallpaper_none) if v == "none"
    idx = v.rindex("/")
    idx ? v[idx + 1, v.length - idx - 1].to_s : v
  end

  def cfg_draw_row(i, s)
    y = cfg_row_y(i)
    bg = (i == @cfg_selected) ? CFG_SEL : CFG_BG
    @gfx.fill_rect(@cfg_x + 1, y - 1, CFG_W - 2, CFG_ROW_H, bg)
    @gfx.draw_text(@cfg_x + 4, y + 2, FmrbI18n.t(s[:key]), CFG_TEXT, bg, mixed: true)
    @gfx.draw_text(cfg_arrow_l_x, y + 2, "<", CFG_TEXT, bg)
    @gfx.draw_text(cfg_arrow_r_x, y + 2, ">", CFG_TEXT, bg)
    @gfx.draw_text(cfg_value_x, y + 2, cfg_value_text(s), CFG_TEXT, bg, mixed: true)
  end

  def cfg_draw_footer
    x = @cfg_x
    y = @cfg_y + CFG_H - CFG_FOOTER_H
    w = CFG_W
    # Inside the frame, not over it. Filling the full width from @cfg_x
    # painted out the dialog's left and right border columns and its bottom
    # row, which left the buttons looking as though they sat outside the
    # window: the last frame line anyone could see was the separator above
    # them.
    @gfx.fill_rect(x + 1, y + 1, w - 2, CFG_FOOTER_H - 2, CFG_BG)
    @gfx.draw_line(x + 1, y, x + w - 2, y, CFG_BORDER)

    btn_y = y + 4
    btn_h = CFG_FOOTER_H - 8
    place_config_widgets(btn_y)
    @ui.invalidate_all

    if @cfg_status
      @gfx.draw_text(x + 4, btn_y + 3, @cfg_status, CFG_TEXT, CFG_BG, mixed: true)
    end
  end

  # ---- Interaction ----

  def handle_config_dialog_click(x, y)
    CFG_SETTINGS.each_with_index do |s, i|
      ry = cfg_row_y(i)
      next unless y >= ry - 1 && y < ry + CFG_ROW_H - 1
      @cfg_selected = i
      if x >= cfg_arrow_l_x && x < cfg_arrow_l_x + CFG_ARROW_W
        cfg_cycle(s, -1)
      elsif x >= cfg_arrow_r_x && x < cfg_arrow_r_x + CFG_ARROW_W
        cfg_cycle(s, +1)
      end
      draw_foreground
      return
    end
  end

  def cfg_hit_rect?(x, y, r)
    return false unless r
    x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]
  end

  def cfg_cycle(s, dir)
    field = s[:field]
    case s[:type]
    when :enum
      opts = cfg_options_for(s)
      idx = opts.index(@cfg_values[field]) || 0
      idx = (idx + dir + opts.size) % opts.size
      @cfg_values[field] = opts[idx]
    when :bool
      @cfg_values[field] = !@cfg_values[field]
      cfg_enforce_one_radio(field)
    when :int
      v = @cfg_values[field].to_i + dir * s[:step]
      v = s[:min] if v < s[:min]
      v = s[:max] if v > s[:max]
      @cfg_values[field] = v
    when :float
      # Step by 0.1 in integer space to avoid float drift.
      cur = (@cfg_values[field].to_f * 10).round
      step = (s[:step] * 10).round
      cur += dir * step
      lo = (s[:min] * 10).round
      hi = (s[:max] * 10).round
      cur = lo if cur < lo
      cur = hi if cur > hi
      @cfg_values[field] = cur / 10.0
    end
  end

  # ---- Save ----

  def cfg_do_save(reboot)
    if cfg_write
      @cfg_status = FmrbI18n.t(:save_done)
      @cfg_status_until = 4
      draw_foreground
      if reboot
        FmrbApp.reboot
      end
    else
      @cfg_status = FmrbI18n.t(:save_failed)
      @cfg_status_until = 4
      draw_foreground
    end
    nil  # FmrbApp.reboot branch is noreturn -> pin a concrete return (Spinel)
  end

  # Rewrite /etc/system_conf.toml by walking the existing lines and replacing
  # only the keys we manage. User-added [[shortcuts]] / [[sync_files]] and
  # other content survive unchanged. Missing top-level keys get appended
  # before the first [section] header; a missing [theme] section gets
  # appended at the end.
  def cfg_write
    path = CFG_PATH

    top_updates = {}
    CFG_SETTINGS.each do |s|
      next if s[:key] == :theme
      top_updates[s[:field]] = cfg_format_value(@cfg_values[s[:field]])
    end

    preset_name = @cfg_values["theme_preset"]
    theme_updates = {}
    if preset_name && CFG_THEME_PRESETS[preset_name]
      CFG_THEME_PRESETS[preset_name].each do |k, v|
        theme_updates[k.to_s] = sprintf("0x%02X", v)
      end
    end

    out_lines = []
    pending_top = top_updates.dup
    pending_theme = theme_updates.dup
    in_theme = false
    other_section = false

    @cfg_file_lines.each do |raw|
      stripped = raw.strip
      if stripped.start_with?("[")
        if in_theme && !pending_theme.empty?
          pending_theme.each { |k, v| out_lines << "#{k} = #{v}" }
          pending_theme.clear
        end
        if !pending_top.empty? && !in_theme && !other_section
          pending_top.each { |k, v| out_lines << "#{k} = #{v}" }
          pending_top.clear
        end
        in_theme = (stripped == "[theme]")
        other_section = !in_theme
        out_lines << raw
        next
      end

      if stripped.empty? || stripped.start_with?("#")
        out_lines << raw
        next
      end

      idx = stripped.index("=")
      unless idx
        out_lines << raw
        next
      end
      key = stripped[0, idx].strip

      if in_theme && theme_updates.key?(key)
        out_lines << "#{key} = #{theme_updates[key]}"
        pending_theme.delete(key)
      elsif !in_theme && !other_section && top_updates.key?(key)
        out_lines << "#{key} = #{top_updates[key]}"
        pending_top.delete(key)
      else
        out_lines << raw
      end
    end

    pending_top.each { |k, v| out_lines << "#{k} = #{v}" }

    unless theme_updates.empty? && pending_theme.empty?
      has_theme_header = false
      out_lines.each do |l|
        if l.strip == "[theme]"
          has_theme_header = true
          break
        end
      end
      if pending_theme.size == theme_updates.size && !has_theme_header
        out_lines << ""
        out_lines << "[theme]"
        pending_theme.each { |k, v| out_lines << "#{k} = #{v}" }
      end
    end

    # Write beside the file and swap it in, rather than opening the real path
    # with "w". That truncates first, so a reset landing in the window between
    # truncate and write loses the whole config -- and a machine that cannot
    # read its config only comes back over USB.
    tmp = path + ".tmp"
    begin
      f = File.open(tmp, "w")
      f.write(out_lines.join("\n"))
      f.close
      File.rename(tmp, path)
      Log.info("Config: saved #{CFG_PATH}")
      true
    rescue => e
      Log.error("Config: write failed: #{e.message}")
      begin
        File.unlink(tmp)
      rescue => e2
        Log.warn("Config: leftover #{tmp}: #{e2.message}")
      end
      false
    end
  end

  def cfg_format_value(v)
    case v
    when true  then "true"
    when false then "false"
    when Float then sprintf("%.1f", v)
    when Integer then v.to_s
    else "\"#{v.to_s.gsub('"', '\"')}\""
    end
  end

  # Periodic tick from on_update: lets the "Saved" status fade after a few
  # update cycles so the user sees the feedback then it disappears.
  def tick_config_dialog
    return unless @cfg_open && @cfg_status && @cfg_status_until > 0
    @cfg_status_until -= 1
    if @cfg_status_until <= 0
      @cfg_status = nil
      draw_foreground
    end
  end
end
