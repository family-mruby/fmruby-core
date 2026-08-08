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
  CFG_H       = 200
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
    "light" => {
      desktop_bg: 0xF6, menu_bg: 0xC5, window_bg: 0xFF, text: 0x00,
      text_light: 0xFF, highlight: 0xEE, border: 0x60, button: 0x60, dir_color: 0x03
    },
    "dark" => {
      desktop_bg: 0x00, menu_bg: 0x49, window_bg: 0x24, text: 0xFF,
      text_light: 0xFF, highlight: 0x6D, border: 0xB6, button: 0x49, dir_color: 0x1D
    },
    "classic" => {
      desktop_bg: 0x48, menu_bg: 0xA9, window_bg: 0xDB, text: 0x00,
      text_light: 0xFF, highlight: 0xE0, border: 0x60, button: 0x80, dir_color: 0x07
    },
  }
  CFG_THEME_KEYS = [:desktop_bg, :menu_bg, :window_bg, :text, :text_light,
                    :highlight, :border, :button, :dir_color]

  # Setting rows. :enum -> options[], :float / :int -> min/max/step,
  # :bool -> on/off. field is the TOML key, or :theme_preset (synthetic) which
  # expands to the nine [theme] entries on save.
  CFG_SETTINGS = [
    { key: :language,         field: "language",         type: :enum,  options: ["en", "ja"] },
    { key: :keyboard_layout,  field: "keyboard_layout",  type: :enum,  options: ["jp", "us"] },
    { key: :mouse_scale_x,    field: "mouse_scale_x",    type: :float, min: 0.1, max: 2.0, step: 0.1 },
    { key: :mouse_scale_y,    field: "mouse_scale_y",    type: :float, min: 0.1, max: 2.0, step: 0.1 },
    { key: :theme,            field: "theme_preset",     type: :enum,  options: ["light", "dark", "classic"] },
    { key: :timezone,         field: "timezone",         type: :enum,
      options: ["JST-9", "UTC", "EST5", "PST8", "CET-1", "CST-8"] },
    { key: :debug_mode,       field: "debug_mode",       type: :bool },
    { key: :ble_auto_start,   field: "ble_auto_start",   type: :bool },
    { key: :wifi_auto_start,  field: "wifi_auto_start",  type: :bool },
    { key: :display_margin_x, field: "display_margin_x", type: :int,   min: 0, max: 16, step: 1 },
    { key: :display_margin_y, field: "display_margin_y", type: :int,   min: 0, max: 16, step: 1 },
  ]

  # ---- Lifecycle ----

  def open_config_dialog
    @cfg_open = true
    @cfg_values = {}
    @cfg_file_lines = []
    @cfg_selected = -1
    @cfg_status = nil
    @cfg_status_until = 0
    @cfg_save_btn_rect = nil
    @cfg_cancel_btn_rect = nil
    @cfg_reboot_btn_rect = nil
    @cfg_x = (@window_width - CFG_W) / 2
    @cfg_y = (@window_height - CFG_H) / 2
    # Keep below the menu bar. picoruby resolves bare constants in the mixin's
    # lexical scope only, so reach into the including class explicitly.
    menu_h = self.class::MENU_BAR_HEIGHT
    @cfg_y = menu_h + 2 if @cfg_y < menu_h + 2
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
    numeric, has_dot = cfg_classify_numeric(s)
    if numeric
      return has_dot ? s.to_f : s.to_i
    end
    s
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
    when :enum  then @cfg_values[s[:field]] = s[:options][0]
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
    @gfx.draw_text(x + 4, y + 3, FmrbI18n.t(:config), FmrbGfx::WHITE, CFG_HEADER, mixed: true)
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
      else
        v.to_s
      end
    else v.to_s
    end
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
    @gfx.fill_rect(x, y, w, CFG_FOOTER_H, CFG_BG)
    @gfx.draw_line(x, y, x + w, y, CFG_BORDER)

    btn_y = y + 4
    btn_h = CFG_FOOTER_H - 8
    label_cancel = FmrbI18n.t(:cancel)
    label_save   = FmrbI18n.t(:save)
    label_reboot = FmrbI18n.t(:save_and_reboot)
    bw_cancel = FmrbI18n.text_width(label_cancel) + 12
    bw_save   = FmrbI18n.text_width(label_save)   + 12
    bw_reboot = FmrbI18n.text_width(label_reboot) + 12

    esp32 = (FmrbConst::PLATFORM == "esp32")
    total = bw_cancel + bw_save + (esp32 ? bw_reboot + 12 : 6)
    bx = x + (w - total) / 2

    @cfg_cancel_btn_rect = [bx, btn_y, bw_cancel, btn_h]
    @gfx.fill_rect(bx, btn_y, bw_cancel, btn_h, CFG_BTN)
    @gfx.draw_text(bx + 6, btn_y + 3, label_cancel, FmrbGfx::WHITE, CFG_BTN, mixed: true)
    bx += bw_cancel + 6

    @cfg_save_btn_rect = [bx, btn_y, bw_save, btn_h]
    @gfx.fill_rect(bx, btn_y, bw_save, btn_h, CFG_BTN)
    @gfx.draw_text(bx + 6, btn_y + 3, label_save, FmrbGfx::WHITE, CFG_BTN, mixed: true)
    bx += bw_save + 6

    if esp32
      @cfg_reboot_btn_rect = [bx, btn_y, bw_reboot, btn_h]
      @gfx.fill_rect(bx, btn_y, bw_reboot, btn_h, CFG_BTN)
      @gfx.draw_text(bx + 6, btn_y + 3, label_reboot, FmrbGfx::WHITE, CFG_BTN, mixed: true)
    else
      @cfg_reboot_btn_rect = nil
    end

    if @cfg_status
      @gfx.draw_text(x + 4, btn_y + 3, @cfg_status, CFG_TEXT, CFG_BG, mixed: true)
    end
  end

  # ---- Interaction ----

  def handle_config_dialog_click(x, y)
    if cfg_hit_rect?(x, y, @cfg_cancel_btn_rect)
      close_config_dialog
      return
    end
    if cfg_hit_rect?(x, y, @cfg_save_btn_rect)
      cfg_do_save(false)
      return
    end
    if cfg_hit_rect?(x, y, @cfg_reboot_btn_rect)
      cfg_do_save(true)
      return
    end

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
      opts = s[:options]
      idx = opts.index(@cfg_values[field]) || 0
      idx = (idx + dir + opts.size) % opts.size
      @cfg_values[field] = opts[idx]
    when :bool
      @cfg_values[field] = !@cfg_values[field]
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
