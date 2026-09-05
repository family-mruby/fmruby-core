# Shortcuts Dialog module for SystemDesktopApp
# Lists the keys that work from the desktop, in any app, and in the file
# manager. The app letters are read from the running configuration rather than
# written out here, so a machine with its own system_conf.toml shows its own.

module ShortcutsDialogMixin
  SKEY_W = 236
  SKEY_LINE_H = 10
  SKEY_BG = FmrbConst::THEME_WINDOW_BG
  SKEY_BORDER = FmrbConst::THEME_BORDER
  SKEY_TITLE_BG = FmrbConst::THEME_MENU_BG
  SKEY_TITLE_TEXT = FmrbConst::THEME_TEXT_LIGHT
  SKEY_TEXT = FmrbConst::THEME_TEXT
  SKEY_LABEL = FmrbGfx::GRAY
  SKEY_HEAD = FmrbConst::THEME_MENU_BG

  # A row is [key, description]; a row with an empty key is a section heading.
  def open_shortcuts_dialog
    @skey_open = true
    @skey_rows = build_shortcut_rows
    h = 20 + @skey_rows.size * SKEY_LINE_H + 12
    # Taller than the screen is possible in principle (a long shortcuts list in
    # system_conf.toml); clamp so the frame stays on screen and drop the rows
    # that do not fit rather than drawing over the menu bar.
    max_h = @window_height - 24
    if h > max_h
      h = max_h
      keep = (h - 32) / SKEY_LINE_H
      @skey_rows = @skey_rows[0, keep] if keep > 0
    end
    @skey_h = h
    @skey_x = (@window_width - SKEY_W) / 2
    @skey_y = (@window_height - @skey_h) / 2
    @skey_y = 12 if @skey_y < 12
    notify_overlay_state(true, @skey_x, @skey_y, SKEY_W, @skey_h)
    update_composite_regions
    draw_foreground
  end

  def close_shortcuts_dialog
    return unless @skey_open
    @skey_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def build_shortcut_rows
    rows = []
    rows << ["", FmrbI18n.t(:sc_desktop).to_s]
    rows << ["F10", FmrbI18n.t(:sc_menu_bar).to_s]
    # The letters come from the configuration the desktop actually loaded.
    i = 0
    while i < @shortcuts.size
      sc = @shortcuts[i]
      i += 1
      key = sc[:key]
      next unless key
      rows << [key.upcase, sc[:app].to_s]
    end

    rows << ["", FmrbI18n.t(:sc_any_app).to_s]
    rows << ["Ctrl+Q", FmrbI18n.t(:sc_quit_app).to_s]
    # Two rows rather than one: the key column is 66 px, eleven characters, and
    # both spellings on one line would run under the description. The comma is
    # not a second-class alternative either -- it is the only one that works in
    # a browser, where Ctrl+Tab belongs to the browser and never reaches us.
    rows << ["Ctrl+Tab", FmrbI18n.t(:sc_switch_app).to_s]
    rows << ["Ctrl+,", FmrbI18n.t(:sc_switch_app_alt).to_s]

    rows << ["", FmrbI18n.t(:sc_lists).to_s]
    rows << ["Up/Down", FmrbI18n.t(:sc_move).to_s]
    rows << ["Enter", FmrbI18n.t(:sc_open).to_s]
    rows << ["Tab", FmrbI18n.t(:sc_actions).to_s]
    rows << ["E / C / V", FmrbI18n.t(:sc_edit_copy_paste).to_s]
    rows << ["Del", FmrbI18n.t(:sc_delete).to_s]
    rows << ["Esc", FmrbI18n.t(:sc_close).to_s]
    rows
  end

  def draw_shortcuts_dialog
    return unless @skey_open
    x = @skey_x
    y = @skey_y
    h = @skey_h

    @gfx.fill_rect(x, y, SKEY_W, h, SKEY_BG)
    @gfx.draw_rect(x, y, SKEY_W, h, SKEY_BORDER)
    @gfx.fill_rect(x + 1, y + 1, SKEY_W - 2, 13, SKEY_TITLE_BG)
    @gfx.draw_text(x + 6, y + 3, FmrbI18n.t(:shortcuts).to_s, SKEY_TITLE_TEXT, SKEY_TITLE_BG,
                   mixed: true)

    ty = y + 20
    i = 0
    while i < @skey_rows.size
      row = @skey_rows[i]
      key = row[0]
      text = row[1]
      if key.empty?
        # Section heading: no key column, so it reads as a break in the list.
        @gfx.draw_text(x + 6, ty + SKEY_LINE_H * i, text, SKEY_HEAD, SKEY_BG, mixed: true)
      else
        @gfx.draw_text(x + 10, ty + SKEY_LINE_H * i, key, SKEY_LABEL, SKEY_BG)
        @gfx.draw_text(x + 76, ty + SKEY_LINE_H * i, text, SKEY_TEXT, SKEY_BG, mixed: true)
      end
      i += 1
    end

    @gfx.draw_text(x + SKEY_W - 96, y + h - 10, "(click to close)", SKEY_LABEL)
  end

  def hit_shortcuts_dialog?(x, y)
    @skey_open &&
      x >= @skey_x && x < @skey_x + SKEY_W &&
      y >= @skey_y && y < @skey_y + @skey_h
  end
end
