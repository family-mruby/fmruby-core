# Find / Find-next dialog for the editor.
#
# Split out of editor.app.rb (doc/editor_refactor). Owns its SEARCH_ constants;
# the theme colors and the reused quit-dialog frame colors come from
# EditorConst, which it includes below.
module EditorSearch
  include EditorConst

  SEARCH_QUERY_MAX = 32
  SEARCH_NOT_FOUND = FmrbGfx.rgb_to_332(180, 0, 0)

  # ---- Search (Find / Find-next) dialog ----

  def search_dialog_rect
    w = (SEARCH_QUERY_MAX + 2) * CHAR_W + 8
    h = 5 * CHAR_H + 14
    x = @user_area_x0 + (@user_area_width  - w) / 2
    y = @user_area_y0 + (@user_area_height - h) / 2
    [x, y, w, h]
  end

  def draw_search_dialog
    x, y, w, h = search_dialog_rect
    @gfx.fill_rect(x, y, w, h, QUIT_DLG_BG)
    @gfx.draw_rect(x, y, w, h, QUIT_DLG_BORDER)
    @gfx.draw_rect(x + 1, y + 1, w - 2, h - 2, QUIT_DLG_BORDER)

    tx = x + 4
    ty = y + 4
    @gfx.draw_text(tx, ty, FmrbI18n.t(:find).to_s,
                   QUIT_DLG_TEXT, QUIT_DLG_BG, mixed: true)

    iy = ty + CHAR_H + 4
    iw = SEARCH_QUERY_MAX * CHAR_W + 2
    @gfx.fill_rect(tx, iy, iw, CHAR_H + 2, BG_COLOR)
    @gfx.draw_rect(tx, iy, iw, CHAR_H + 2, QUIT_DLG_BORDER)
    # mixed: the query can hold kana now, and the caret follows the pixel
    # width rather than the character count (a kana is two cells wide).
    @gfx.draw_text(tx + 1, iy + 1, @search_query, TEXT_COLOR, BG_COLOR,
                   mixed: true)
    cur_x = tx + 1 + FmrbI18n.text_width(@search_query)
    @gfx.fill_rect(cur_x, iy + 1, CHAR_W, CHAR_H, CURSOR_COLOR)

    sy = iy + CHAR_H + 4
    if @search_status && @search_status.length > 0
      @gfx.draw_text(tx, sy, @search_status, SEARCH_NOT_FOUND, QUIT_DLG_BG,
                     mixed: true)
    end

    hy = sy + CHAR_H + 2
    @gfx.draw_text(tx, hy, FmrbI18n.t(:find_keys).to_s,
                   QUIT_DLG_TEXT, QUIT_DLG_BG, mixed: true)
  end

  def open_search_dialog
    @search_open = true
    # Pre-fill with the previous query so the user can re-search quickly.
    @search_query = @search_last.dup
    # Pre-filled query: Enter acts as Find-Next.
    # User-modified query: Enter searches from the current cursor position.
    @search_query_dirty = false
    @search_status = ""
    @need_redraw = true
  end

  def close_search_dialog
    @search_open = false
    @search_status = ""
    @need_redraw = true
  end

  def handle_search_dialog_key(ev)
    # Enter / ESC by scancode (HID Usage ID), like handle_menu_key: on the Linux
    # sim ev[:keycode] carries the SDL keysym (13 / 27) instead of 40 / 41, so
    # the keycode form never fired there.
    scancode = ev[:scancode] || 0
    character = ev[:character] || 0

    case scancode
    when 40, 88  # Enter / Keypad-Enter
      if @search_query.length == 0
        close_search_dialog
      else
        @search_last = @search_query
        # Unchanged pre-filled query advances; edited query searches from cursor.
        after_cursor = !@search_query_dirty
        if find_from_cursor(@search_query, after_cursor)
          close_search_dialog
        else
          @search_status = FmrbI18n.t(:not_found).to_s
          @need_redraw = true
        end
      end
      return
    when 41  # ESC
      close_search_dialog
      return
    end

    # Kana reach the field the same way they reach the document: as the bytes
    # of one UTF-8 character. This is what makes searching for a Japanese
    # word possible at all.
    if character >= 0x80
      s = utf8_feed(character)
      if s && FmrbI18n.text_width(@search_query) < SEARCH_QUERY_MAX * CHAR_W
        @search_query += s
        @search_query_dirty = true
        @search_status = ""
        @need_redraw = true
      end
      return
    end
    utf8_reset

    case character
    when 8, 127  # Backspace / Delete
      if @search_query.length > 0
        @search_query = @search_query[0, @search_query.length - 1]
        @search_query_dirty = true
        @search_status = ""
        @need_redraw = true
      end
    when 32..126  # Printable
      if @search_query.length < SEARCH_QUERY_MAX
        @search_query += printable_char(character)
        @search_query_dirty = true
        @search_status = ""
        @need_redraw = true
      end
    end
  end

  # Find +query+ starting from the cursor, wrapping to the top once.
  # When +after_cursor+ is true (F3 / Find Next), skip the character at the
  # cursor so we advance past the previous hit. Returns true on match.
  # EditorCore searches line by line inside the arena, so the document is never
  # joined into one String here. A query cannot span a newline (the Find field is
  # a single line), which the old whole-document search allowed in theory.
  def find_from_cursor(query, after_cursor)
    return false if query.nil? || query.length == 0
    rec = EditorCore.find(query, @cy, @cx, after_cursor)
    return false unless EditorCore.found?(rec)
    @cy = EditorCore.find_y(rec)
    @cx = EditorCore.find_x(rec)
    ensure_cursor_visible
    @need_redraw = true
    true
  end

  def find_next
    return false if @search_last.length == 0
    found = find_from_cursor(@search_last, true)
    Log.info("Find: not found '#{@search_last}'") unless found
    found
  end
end
