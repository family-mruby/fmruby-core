# Several documents in one editor (doc/editor_multi_file).
#
# The text of every document is in EditorCore (C), which holds up to five and
# answers every call for the current one. What lives here is the rest of a
# document's state: the cursor, the view and the flags the editor keeps in its
# own ivars. Switching puts the current document's ivars aside, makes the
# other one current in C, and brings its ivars back -- so the rest of the
# editor keeps reading @cx, @current_file, ... and never knows there are more.
#
# The saved state is two flat arrays indexed by document: DOC_ST_N integers
# per document in @doc_ints (booleans as 0/1, a missing selection as -1) and
# one String per document in @doc_files ("" for a document with no name).
# One element type per array, so the Spinel build can type both of them.
module EditorDocs
  include EditorConst

  # Offsets into one document's run of integers in @doc_ints.
  DOC_ST_CX        = 0
  DOC_ST_CY        = 1
  DOC_ST_SCROLL_Y  = 2
  DOC_ST_ANCHOR    = 3
  DOC_ST_SCROLL_X  = 4
  DOC_ST_LINE_SEGS = 5
  DOC_ST_SEL_X     = 6
  DOC_ST_SEL_Y     = 7
  DOC_ST_WRAP      = 8
  DOC_ST_MODIFIED  = 9
  DOC_ST_HL        = 10
  DOC_ST_HL_MANUAL = 11
  DOC_ST_N         = 12

  # Called once from initialize, after the ivars it saves have their values:
  # the editor starts with one document, the one EditorCore opened.
  def docs_init
    @doc_ints = []
    @doc_files = []
    docs_append_entry
  end

  # Room for one more document's state, filled in when it is first put aside.
  def docs_append_entry
    i = 0
    while i < DOC_ST_N
      @doc_ints << 0
      i += 1
    end
    @doc_files << ""
  end

  def docs_count
    EditorCore.doc_count
  end

  # The "[2/3]" mark the status line shows while more than one is open.
  def docs_mark
    n = EditorCore.doc_count
    return "" if n < 2
    "[#{EditorCore.doc_index + 1}/#{n}] "
  end

  # ---- Saving and restoring one document's ivars ----

  def docs_save(idx)
    b = idx * DOC_ST_N
    @doc_ints[b + DOC_ST_CX] = @cx
    @doc_ints[b + DOC_ST_CY] = @cy
    @doc_ints[b + DOC_ST_SCROLL_Y] = @scroll_y
    @doc_ints[b + DOC_ST_ANCHOR] = @anchor_seg
    @doc_ints[b + DOC_ST_SCROLL_X] = @scroll_x
    @doc_ints[b + DOC_ST_LINE_SEGS] = @cur_line_segs
    docs_save_flags(b)
    f = @current_file
    @doc_files[idx] = f ? f : ""
  end

  # Split from docs_save to keep each method's frame small (Spinel inlines).
  def docs_save_flags(b)
    sx = @sel_anchor_x
    sy = @sel_anchor_y
    @doc_ints[b + DOC_ST_SEL_X] = sx ? sx : -1
    @doc_ints[b + DOC_ST_SEL_Y] = sy ? sy : -1
    @doc_ints[b + DOC_ST_WRAP] = @wrap_on ? 1 : 0
    @doc_ints[b + DOC_ST_MODIFIED] = @modified ? 1 : 0
    @doc_ints[b + DOC_ST_HL] = @hl_enabled ? 1 : 0
    @doc_ints[b + DOC_ST_HL_MANUAL] = @hl_manual ? 1 : 0
  end

  def docs_restore(idx)
    b = idx * DOC_ST_N
    @cx = @doc_ints[b + DOC_ST_CX]
    @cy = @doc_ints[b + DOC_ST_CY]
    @scroll_y = @doc_ints[b + DOC_ST_SCROLL_Y]
    @anchor_seg = @doc_ints[b + DOC_ST_ANCHOR]
    @scroll_x = @doc_ints[b + DOC_ST_SCROLL_X]
    @cur_line_segs = @doc_ints[b + DOC_ST_LINE_SEGS]
    docs_restore_flags(b)
    f = @doc_files[idx]
    @current_file = f.length > 0 ? f : nil
  end

  def docs_restore_flags(b)
    sx = @doc_ints[b + DOC_ST_SEL_X]
    sy = @doc_ints[b + DOC_ST_SEL_Y]
    if sx < 0 || sy < 0
      @sel_anchor_x = nil
      @sel_anchor_y = nil
    else
      @sel_anchor_x = sx
      @sel_anchor_y = sy
    end
    @wrap_on = @doc_ints[b + DOC_ST_WRAP] == 1
    @modified = @doc_ints[b + DOC_ST_MODIFIED] == 1
    @hl_enabled = @doc_ints[b + DOC_ST_HL] == 1
    @hl_manual = @doc_ints[b + DOC_ST_HL_MANUAL] == 1
  end

  # The ivars of a document that has just been made: what initialize gives
  # the first one.
  def docs_fresh_state
    @cx = 0
    @cy = 0
    @scroll_y = 0
    @anchor_seg = 0
    @scroll_x = 0
    @cur_line_segs = 1
    @sel_anchor_x = nil
    @sel_anchor_y = nil
    @wrap_on = true
    @modified = false
    @current_file = nil
    @hl_enabled = true
    @hl_manual = false
  end

  # Drop the entry of document +idx+; the ones after it move down by one,
  # as EditorCore's do when it closes a document.
  def docs_remove_entry(idx)
    ints = []
    files = []
    n = @doc_files.length
    d = 0
    while d < n
      if d != idx
        b = d * DOC_ST_N
        k = 0
        while k < DOC_ST_N
          ints << @doc_ints[b + k]
          k += 1
        end
        files << @doc_files[d]
      end
      d += 1
    end
    @doc_ints = ints
    @doc_files = files
  end

  # ---- Before and after a switch ----

  # Whatever is on screen for the document being left goes first: dropdowns,
  # dialogs, the completion list, a help page (which has the document itself
  # put aside), half a typed character, a held key, the problem markers.
  def docs_leave
    close_menu if @active_menu
    close_search_dialog if @search_open
    close_palette_dialog if @palette_open
    close_keys_list if @keys_open
    close_completion
    close_help if @help_open
    utf8_reset
    @held_keycode = nil
    @hold_frames = 0
    clear_diagnostics
    clear_status_message
  end

  # The document now current in EditorCore is shown from its own state.
  def docs_enter
    apply_hl_enabled
    clamp_cy
    clamp_cx
    ensure_cursor_visible
    @dirty_lines.clear
    @dirty_from = nil
    @need_redraw = true
  end

  # ---- The operations (keys in editor.app.rb) ----

  # A new empty document, after the current one. Refused, with the current
  # document untouched, when five are open or there is no room for it.
  def docs_new
    cur = EditorCore.doc_index
    docs_leave
    docs_save(cur)
    r = EditorCore.doc_add
    if r < 0
      if r == -1
        flash_status("Max #{EditorCore.doc_count} files open")
      else
        doc_full
      end
      Log.info("doc_add refused (#{r}); docs=#{EditorCore.doc_count} arena=#{EditorCore.mem_used}")
      @need_redraw = true
      return
    end
    docs_append_entry
    docs_fresh_state
    docs_enter
    Log.info("doc_add: #{r + 1}/#{EditorCore.doc_count} arena=#{EditorCore.mem_used}")
  end

  # Make document +idx+ the current one (0-based, in the order they were
  # opened).
  def docs_switch(idx)
    cur = EditorCore.doc_index
    n = EditorCore.doc_count
    return if idx == cur || idx < 0 || idx >= n
    docs_leave
    docs_save(cur)
    return if EditorCore.doc_switch(idx) < 0
    docs_restore(idx)
    docs_enter
  end

  # Next / previous, wrapping around. +step+ is 1 or -1.
  def docs_cycle(step)
    n = EditorCore.doc_count
    return if n < 2
    docs_switch((EditorCore.doc_index + step + n) % n)
  end

  # Close the current document. One with unsaved edits is not closed (asking
  # whether to save comes with the tab row). Closing the last one leaves an
  # empty document with no name, as a fresh editor has.
  def docs_close
    # A help page stands in for the document while it is open; the question
    # is about the document underneath.
    close_help if @help_open
    if @modified
      flash_status("Unsaved: Ctrl+S first")
      return
    end
    cur = EditorCore.doc_index
    n = EditorCore.doc_count
    docs_leave
    r = EditorCore.doc_close
    if r < 0
      doc_full
      return
    end
    if n <= 1
      docs_fresh_state
    else
      docs_remove_entry(cur)
      docs_restore(r)
    end
    docs_enter
    Log.info("doc_close: now #{EditorCore.doc_index + 1}/#{EditorCore.doc_count} arena=#{EditorCore.mem_used}")
  end
end
