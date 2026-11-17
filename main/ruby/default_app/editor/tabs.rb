# The tab row (doc/editor_multi_file): one line under the menu bar naming the
# open documents, shown only while there is more than one. With a single
# document the editor looks exactly as it did before there could be several.
#
# A tab is the file name without its folder (the status line's word for a
# document with no name yet), with a "*" after it while it has unsaved edits.
# The current one is drawn inverted in the menu row's own two colours. When
# the names do not fit, they are cut from the back, all to the same length,
# down to TAB_NAME_MIN characters; when even that is too wide, the row starts
# far enough along that the current tab is on it.
#
# Positions are kept for the click test, the way the menu bar keeps its own.
# The row is drawn with the whole screen, and on its own only when the current
# document's "*" comes or goes (redraw_dirty), so typing never redraws it.
module EditorTabs
  include EditorConst

  # The current document reads from the live ivars, the others from the state
  # put aside for them (editor/docs.rb). While a help page stands in for the
  # current document, its tab still names the document underneath.
  def tab_path(idx, cur)
    if idx == cur
      f = @help_open ? @help_return_file : @current_file
      return f ? f : ""
    end
    @doc_files[idx]
  end

  def tab_modified?(idx, cur)
    return cur_tab_modified? if idx == cur
    docs_saved_modified?(idx)
  end

  def cur_tab_modified?
    @help_open ? @help_return_modified : @modified
  end

  def tab_base_name(path)
    return FmrbI18n.t(:st_new).to_s if path.length == 0
    path.split("/").last.to_s
  end

  # The text of every tab, each name cut to +cap+ characters. +mods+ is 0/1
  # per tab (an Integer array, so the Spinel build types it).
  def tab_texts(names, mods, cap)
    out = []
    i = 0
    while i < names.size
      nm = names[i]
      nm = nm[0, cap] if nm.length > cap
      out << (mods[i] == 1 ? nm + "*" : nm)
      i += 1
    end
    out
  end

  def tab_widths(texts)
    out = []
    i = 0
    while i < texts.size
      out << FmrbI18n.text_width(texts[i]) + TAB_PAD * 2
      i += 1
    end
    out
  end

  # Pixels tabs +from+..+to+ take, gaps included.
  def tab_span(widths, from, to)
    total = 0
    i = from
    while i <= to
      total += widths[i]
      total += TAB_GAP if i > from
      i += 1
    end
    total
  end

  def draw_tab_row
    y = @tab_y
    @gfx.fill_rect(0, y, @user_area_width, TAB_ROW_H, MENU_BG)
    @tab_xs = []
    @tab_ws = []
    @tab_ids = []
    n = EditorCore.doc_count
    cur = EditorCore.doc_index
    names = []
    mods = []
    longest = 0
    i = 0
    while i < n
      nm = tab_base_name(tab_path(i, cur))
      names << nm
      mods << (tab_modified?(i, cur) ? 1 : 0)
      longest = nm.length if nm.length > longest
      i += 1
    end
    avail = @user_area_width - 4
    cap = longest
    texts = tab_texts(names, mods, cap)
    widths = tab_widths(texts)
    while cap > TAB_NAME_MIN && tab_span(widths, 0, n - 1) > avail
      cap -= 1
      texts = tab_texts(names, mods, cap)
      widths = tab_widths(texts)
    end
    first = 0
    while first < cur && tab_span(widths, first, cur) > avail
      first += 1
    end

    x = 2
    right = @user_area_width - 2
    i = first
    while i < n
      w = widths[i]
      break if x + w > right
      if i == cur
        @gfx.fill_rect(x, y, w, TAB_ROW_H - 1, MENU_TEXT)
        @gfx.draw_text(x + TAB_PAD, y + 1, texts[i], MENU_BG, MENU_TEXT, mixed: true)
      else
        @gfx.draw_text(x + TAB_PAD, y + 1, texts[i], MENU_TEXT, MENU_BG, mixed: true)
      end
      @tab_xs << x
      @tab_ws << w
      @tab_ids << i
      x += w + TAB_GAP
      i += 1
    end
    @tab_drawn_mod = cur_tab_modified?
  end

  # Whether a point is on the tab row.
  def tab_row_hit?(mx, my)
    @tabs_on && my >= @tab_y && my < @tab_y + TAB_ROW_H
  end

  # The document under a click x on the tab row, or -1.
  def tab_hit(mx)
    i = 0
    while i < @tab_xs.size
      return @tab_ids[i] if mx >= @tab_xs[i] && mx < @tab_xs[i] + @tab_ws[i]
      i += 1
    end
    -1
  end
end
