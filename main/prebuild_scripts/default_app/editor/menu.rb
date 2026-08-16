# The File/Edit dropdown menus and the File > Template list for the editor.
# Split out of editor.app.rb (doc/editor_refactor). Shared constants
# (DROPDOWN_*, MENU_*_HOTKEYS, CHAR_H, TEMPLATE_DIR) come from EditorConst
# (included below).
module EditorMenu
  include EditorConst

  # ---- Menu dropdown (File / Edit) ----

  # Dropdown contents. Built per open rather than held in a constant: the words
  # depend on the language, and a menu opens rarely enough for the allocation
  # not to matter.
  def menu_file_items
    [FmrbI18n.t(:open).to_s, FmrbI18n.t(:save).to_s, FmrbI18n.t(:save_as).to_s,
     FmrbI18n.t(:template).to_s, FmrbI18n.t(:exit).to_s]
  end

  def menu_edit_items
    [FmrbI18n.t(:cut).to_s, FmrbI18n.t(:copy).to_s, FmrbI18n.t(:paste).to_s,
     FmrbI18n.t(:select_all).to_s]
  end

  # The display toggles, with their state in the label. "[x] Wrap" says what is
  # on without the reader having to know that a trailing "*" meant anything,
  # which is what the menu bar used to show.
  def menu_view_items
    [view_toggle_label(@hl_enabled, :m_hilight),
     view_toggle_label(@wrap_on, :m_wrap),
     view_toggle_label(@fullscreen, :m_full)]
  end

  def view_toggle_label(on, key)
    (on ? "[x] " : "[ ] ") + FmrbI18n.t(key).to_s
  end

  def menu_items
    case @active_menu
    when :file then menu_file_items
    when :edit then menu_edit_items
    when :view then menu_view_items
    when :template then @template_labels
    when :debug then dbg_menu_items
    end
  end

  def menu_hotkeys
    case @active_menu
    when :file then MENU_FILE_HOTKEYS
    when :edit then MENU_EDIT_HOTKEYS
    when :view then MENU_VIEW_HOTKEYS
    end
  end

  # Dropdown panel width: the widest item plus padding. Measured rather than
  # fixed, for the same reason the menu bar is.
  def dropdown_width(items)
    widest = 0
    items.each do |item|
      w = FmrbI18n.text_width(item.to_s)
      widest = w if w > widest
    end
    widest + 10
  end

  def menu_width
    case @active_menu
    when :template then dropdown_width(@template_labels)
    when :view then dropdown_width(menu_view_items)
    when :debug then dbg_menu_width
    else dropdown_width(menu_items)
    end
  end

  # Top-left of the dropdown panel for the active menu (just below its label).
  # The Debug label sits at the right end of the menu bar, so its dropdown is
  # right-anchored to stay inside the (narrow) editor window.
  def menu_origin
    case @active_menu
    when :file then [@menu_file_x, @menu_y + CHAR_H]
    when :template then [@menu_file_x, @menu_y + CHAR_H]
    when :edit then [@menu_edit_x, @menu_y + CHAR_H]
    when :view
      # Right-anchored like Debug: View sits well along the bar, and its items
      # ("[x] Highlight") are wider than the label above them.
      vx = @user_area_x0 + @user_area_width - menu_width - 2
      vx = @menu_view_x if @menu_view_x && vx > @menu_view_x
      vx = @user_area_x0 if vx < @user_area_x0
      [vx, @menu_y + CHAR_H]
    when :debug
      dx = @user_area_x0 + @user_area_width - menu_width - 2
      dx = @menu_debug_x if dx > @menu_debug_x
      [dx, @menu_y + CHAR_H]
    else            [0, 0]
    end
  end

  def draw_active_menu
    return unless @active_menu
    items = menu_items
    w = menu_width
    x, y = menu_origin
    h = DROPDOWN_ITEM_H * items.size + 2

    @gfx.fill_rect(x, y, w, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, w, h, 0x60)

    items.each_with_index do |item, i|
      item_y = y + 1 + i * DROPDOWN_ITEM_H
      if i == @menu_idx
        @gfx.fill_rect(x + 1, item_y, w - 2, DROPDOWN_ITEM_H, DROPDOWN_SEL_BG)
        @gfx.draw_text(x + 4, item_y + 1, item.to_s,
                       DROPDOWN_SEL_TEXT, DROPDOWN_SEL_BG, mixed: true)
      else
        @gfx.draw_text(x + 4, item_y + 1, item.to_s,
                       DROPDOWN_TEXT, DROPDOWN_BG, mixed: true)
      end
    end
  end

  def open_menu(kind)
    @active_menu = kind
    @menu_idx = 0
    render_with_dropdown
  end

  # Ask for a full repaint; redraw_all puts the open dropdown on top of it.
  # (Used on menu open and on dropdown navigation.)
  def render_with_dropdown
    @need_redraw = true
  end

  def close_menu
    @active_menu = nil
    @need_redraw = true
  end

  # Modal key handling while a menu dropdown is open.
  # keycode/scancode are USB HID Usage IDs (uniform across ESP32 / SDL2).
  def handle_menu_key(ev)
    keycode = ev[:keycode] || 0
    scancode = ev[:scancode] || 0
    items = menu_items

    # Enter / ESC by scancode (HID Usage ID) so this works on the Linux sim too,
    # where ev[:keycode] carries the SDL keysym (13/27) instead of 40/41.
    if scancode == 40 || scancode == 88  # Enter / Keypad-Enter
      activate_menu_item(@menu_idx)
      return
    elsif scancode == 41  # ESC
      close_menu
      return
    end

    case keycode
    when 82  # Up
      n = items.size
      @menu_idx = (@menu_idx + n - 1) % n
      render_with_dropdown
      return
    when 81  # Down
      @menu_idx = (@menu_idx + 1) % items.size
      render_with_dropdown
      return
    end

    # Per-item letter hotkey (scancode-based; case-insensitive by nature).
    # The Debug menu has no letter hotkeys (menu_hotkeys is nil there).
    hk = menu_hotkeys
    if hk
      idx = hk.index(scancode)
      activate_menu_item(idx) if idx
    end
  end

  def handle_menu_click(x, y)
    dx, dy = menu_origin
    w = menu_width
    items = menu_items
    if x >= dx && x < dx + w && y >= dy
      idx = (y - dy - 1) / DROPDOWN_ITEM_H
      if idx >= 0 && idx < items.size
        activate_menu_item(idx)
        return
      end
    end
    close_menu
  end

  def activate_menu_item(idx)
    kind = @active_menu
    close_menu
    case kind
    when :file then activate_file_item(idx)
    when :edit then activate_edit_item(idx)
    when :view then activate_view_item(idx)
    when :template then insert_template(idx)
    when :debug then dbg_activate_item(idx)
    end
  end

  def activate_view_item(idx)
    case idx
    when 0 then toggle_highlight
    when 1 then toggle_wrap
    when 2 then toggle_fullscreen
    end
  end

  def activate_file_item(idx)
    case idx
    when 0  # Open
      @pending_file_op = :open
      request_file_select("open")
    when 1  # Save
      save_file
    when 2  # Save as
      @pending_file_op = :save
      request_file_select("save")
    when 3  # Template
      open_template_menu
    when 4  # Exit
      stop
    end
  end

  def activate_edit_item(idx)
    case idx
    when 0 then cut_selection
    when 1 then copy_selection
    when 2 then paste_clipboard
    when 3 then select_all
    end
  end

  # ---- Templates (File > Template) ----
  #
  # Skeletons are files under TEMPLATE_DIR rather than text baked into this
  # app, so a user can drop their own in and see it in the list. The list is
  # the ordinary menu dropdown, which already has the keyboard and mouse
  # handling; only the item text is different.

  def open_template_menu
    load_template_list
    if @template_labels.empty?
      flash_status(FmrbI18n.t(:b_no_templates).to_s)
      return
    end
    open_menu(:template)
  end

  def load_template_list
    names = []
    labels = []
    begin
      dir = Dir.open(TEMPLATE_DIR)
      while (e = dir.read)
        name = e.to_s
        names << name if name.end_with?(".rb")
      end
      dir.close
    rescue => err
      Log.error("Cannot list #{TEMPLATE_DIR}: #{err.message}")
      names = []
    end
    names = names.sort
    names.each do |n|
      labels << n[0, n.length - 3]
    end
    @template_names = names
    @template_labels = labels
  end

  # Insert the chosen skeleton at the cursor. Same shape as a paste: the
  # document model does the work and reports where the cursor ended up.
  def insert_template(idx)
    return if idx < 0 || idx >= @template_names.size
    path = "#{TEMPLATE_DIR}/#{@template_names[idx]}"
    text = nil
    begin
      f = File.open(path, "r")
      text = f.read
      f.close
    rescue => err
      flash_status(FmrbI18n.t(:b_load_failed).to_s)
      Log.error("Template read failed: #{path} (#{err.message})")
      return
    end
    body = text.to_s
    if body.bytesize == 0
      flash_status(FmrbI18n.t(:b_empty).to_s)
      return
    end
    delete_selection if has_selection?
    start_y = @cy
    rec = EditorCore.insert_multiline(@cy, @cx, body)
    @cy = EditorCore.pos_y(rec)
    @cx = EditorCore.pos_x(rec)
    mark_dirty_from(start_y)
    mark_edited
    ensure_cursor_visible
    flash_status(FmrbI18n.t(:b_inserted).to_s)
  end

end
