# The editor's key list: the Keys item on the menu bar, or Alt-K.
#
# Named Keys rather than Help because the editor already has a help: F1 opens
# the API documentation for the symbol under the cursor (editor/ti_ui.rb). This
# panel is the other question -- "what can I press" -- and says so.
#
# The rows are written out here rather than read back from the input handler.
# The keys are decided in several places (function keys and Ctrl in
# editor.app.rb, the Alt letters from the menu bar, the debugger's run
# control), and a list that tried to derive them would be harder to keep honest
# than one kept beside the code it describes.
module EditorKeys
  include EditorConst

  KEYS_LINE_H = CHAR_H + 1
  KEYS_PAD    = 6

  # [key, what it does]. An empty key makes the row a heading.
  def keys_rows
    rows = []
    rows << ["", FmrbI18n.t(:k_file_run).to_s]
    rows << ["F5", FmrbI18n.t(:k_run).to_s]
    rows << ["Ctrl+S", FmrbI18n.t(:save).to_s]
    rows << ["Ctrl+X", FmrbI18n.t(:exit).to_s]
    rows << ["F11", FmrbI18n.t(:m_full).to_s]

    rows << ["", FmrbI18n.t(:k_editing).to_s]
    rows << ["Ctrl+C", FmrbI18n.t(:copy).to_s]
    rows << ["Ctrl+V", FmrbI18n.t(:paste).to_s]
    rows << ["Ctrl+A", FmrbI18n.t(:select_all).to_s]

    rows << ["", FmrbI18n.t(:k_search).to_s]
    rows << ["Alt+S", FmrbI18n.t(:k_find).to_s]
    rows << ["F3", FmrbI18n.t(:k_find_next).to_s]

    rows << ["", FmrbI18n.t(:k_types).to_s]
    rows << ["F1", FmrbI18n.t(:k_api_help).to_s]
    rows << ["Ctrl+T", FmrbI18n.t(:k_type_at).to_s]
    rows << ["Ctrl+E", FmrbI18n.t(:k_type_errors).to_s]

    rows << ["", FmrbI18n.t(:k_menus).to_s]
    rows << ["Alt+K", FmrbI18n.t(:k_this_list).to_s]
    rows << ["Alt+F/E/R/V", FmrbI18n.t(:k_menu_letters).to_s]
    rows << ["Alt+H / Alt+W", FmrbI18n.t(:k_toggles).to_s]

    if dbg_menu_visible?
      rows << ["", FmrbI18n.t(:k_debug).to_s]
      rows << ["Ctrl+D", FmrbI18n.t(:k_debug_menu).to_s]
      rows << ["F9", FmrbI18n.t(:k_breakpoint).to_s]
    end
    rows
  end

  def open_keys_list
    @keys_rows = keys_rows
    @keys_page = 0
    @keys_open = true
    @need_redraw = true
  end

  # A key or a click moves to the next page, and closes the panel after the
  # last one. The editor window can be short enough that the list does not fit
  # in one panel, and dropping the rest silently would hide exactly the keys a
  # reader has not learned yet.
  def keys_advance
    if @keys_page + 1 < keys_page_count
      @keys_page += 1
      @need_redraw = true
    else
      close_keys_list
    end
  end

  def keys_rows_per_page
    rows = @keys_rows || []
    return 1 if rows.empty?
    usable = (@user_area_height - 4) - KEYS_PAD * 2 - KEYS_LINE_H  # footer line
    n = usable / KEYS_LINE_H
    n < 1 ? 1 : n
  end

  def keys_page_count
    rows = @keys_rows || []
    per = keys_rows_per_page
    count = (rows.size + per - 1) / per
    count < 1 ? 1 : count
  end

  # The rows of the page being shown.
  def keys_page_rows
    rows = @keys_rows || []
    per = keys_rows_per_page
    first = @keys_page * per
    last = first + per
    last = rows.size if last > rows.size
    out = []
    i = first
    while i < last
      out << rows[i]
      i += 1
    end
    out
  end

  def close_keys_list
    return unless @keys_open
    @keys_open = false
    @keys_rows = nil
    @keys_page = 0
    @need_redraw = true
  end

  # Measured from the rows: two columns, the widest key and the widest text.
  # Clamped to the user area so a narrow window shows a panel that fits and
  # drops the rows past the bottom rather than drawing outside itself.
  def keys_rect
    rows = keys_page_rows
    key_w = 0
    text_w = 0
    i = 0
    while i < rows.size
      kw = FmrbI18n.text_width(rows[i][0])
      tw = FmrbI18n.text_width(rows[i][1])
      key_w = kw if kw > key_w
      text_w = tw if tw > text_w
      i += 1
    end
    w = KEYS_PAD * 2 + key_w + CHAR_W + text_w
    max_w = @user_area_width - 4
    w = max_w if w > max_w
    h = KEYS_PAD * 2 + (rows.size + 1) * KEYS_LINE_H   # + the footer line
    max_h = @user_area_height - 4
    h = max_h if h > max_h
    x = @user_area_x0 + (@user_area_width - w) / 2
    y = @user_area_y0 + (@user_area_height - h) / 2
    [x, y, w, h, key_w]
  end

  def draw_keys_list
    return unless @keys_open
    rows = keys_page_rows
    x, y, w, h, key_w = keys_rect

    @gfx.fill_rect(x, y, w, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, w, h, 0x60)

    ty = y + KEYS_PAD
    i = 0
    while i < rows.size
      break if ty + KEYS_LINE_H > y + h - KEYS_PAD
      key = rows[i][0]
      text = rows[i][1]
      if key.empty?
        @gfx.draw_text(x + KEYS_PAD, ty, text, DROPDOWN_SEL_BG, DROPDOWN_BG, mixed: true)
      else
        @gfx.draw_text(x + KEYS_PAD, ty, key, MENU_KEY_DARK, DROPDOWN_BG)
        @gfx.draw_text(x + KEYS_PAD + key_w + CHAR_W, ty, text,
                       DROPDOWN_TEXT, DROPDOWN_BG, mixed: true)
      end
      ty += KEYS_LINE_H
      i += 1
    end

    # Which page this is and what the next key does, so a panel that had to be
    # split does not look like the whole list.
    # On the last page the next key closes the panel, so say that rather than
    # promising a page that is not there.
    pages = keys_page_count
    tail = (@keys_page + 1 < pages) ? FmrbI18n.t(:k_more).to_s : FmrbI18n.t(:k_close).to_s
    footer = pages > 1 ? "#{@keys_page + 1}/#{pages} " + tail : tail
    @gfx.draw_text(x + KEYS_PAD, y + h - KEYS_PAD - CHAR_H, footer,
                   DROPDOWN_SEL_BG, DROPDOWN_BG, mixed: true)
  end
end
