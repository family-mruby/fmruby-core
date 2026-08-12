# Selection and clipboard for the editor.
#
# Split out of editor.app.rb (doc/editor_refactor). Pure instance-variable
# state (@sel_*, @clipboard) and EditorCore calls -- no constants to share, so
# this mixin needs no shared constants, so it does not include EditorConst.
module EditorClipboard

  # ---- Selection ----

  def has_selection?
    !@sel_anchor_y.nil?
  end

  # Returns [sx, sy, ex, ey] with start <= end in document order, or nil.
  def selection_range
    return nil unless has_selection?
    if @sel_anchor_y < @cy || (@sel_anchor_y == @cy && @sel_anchor_x <= @cx)
      [@sel_anchor_x, @sel_anchor_y, @cx, @cy]
    else
      [@cx, @cy, @sel_anchor_x, @sel_anchor_y]
    end
  end

  def clear_selection
    return unless has_selection?
    # The whole highlighted span has to lose its background.
    mark_dirty_range(@sel_anchor_y, @cy)
    @sel_anchor_x = nil
    @sel_anchor_y = nil
  end

  def begin_selection_if_needed
    return if has_selection?
    @sel_anchor_x = @cx
    @sel_anchor_y = @cy
  end

  def select_all
    @sel_anchor_x = 0
    @sel_anchor_y = 0
    @cy = EditorCore.line_count - 1
    @cx = EditorCore.line_length(@cy)
    ensure_cursor_visible
    @need_redraw = true
  end

  # Pixel column range of the selection on +line_idx+, accounting for
  # multi-line spans. Returns [start_col, end_col_exclusive] or nil.
  def line_selection_cols(line_idx, line_len)
    range = selection_range
    return nil unless range
    sx, sy, ex, ey = range
    return nil if line_idx < sy || line_idx > ey
    start_col = (line_idx == sy) ? sx : 0
    end_col   = (line_idx == ey) ? ex : line_len
    end_col = line_len if end_col > line_len
    return nil if start_col >= end_col
    [start_col, end_col]
  end

  def delete_selection
    range = selection_range
    return false unless range
    sx, sy, ex, ey = range
    EditorCore.delete_range(sy, sx, ey, ex)
    @cy = sy
    @cx = sx
    clear_selection
    mark_edited
    ensure_cursor_visible
    # Single-line deletions touch one row; a multi-line one shifts the rest up.
    if sy == ey
      mark_dirty_line(sy)
    else
      mark_dirty_from(sy)
    end
    true
  end

  # ---- Clipboard ops ----

  def copy_selection
    return unless has_selection?
    sx, sy, ex, ey = selection_range
    n = EditorCore.copy_range(sy, sx, ey, ex)
    if n < 0
      doc_full
      return
    end
    Log.info("Copied #{n} bytes")
  end

  def cut_selection
    return unless has_selection?
    copy_selection
    delete_selection
  end

  def paste_clipboard
    return if EditorCore.clipboard_length == 0
    delete_selection if has_selection?
    start_y = @cy
    rec = EditorCore.paste_at(@cy, @cx)
    ny = EditorCore.pos_y(rec)
    nx = EditorCore.pos_x(rec)
    if ny == start_y
      mark_dirty_line(start_y)
    else
      mark_dirty_from(start_y)
    end
    @cy = ny
    @cx = nx
    mark_edited
    ensure_cursor_visible
  end

end
