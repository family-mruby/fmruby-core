# Error Dialog module for SystemDesktopApp
# Displays script compilation errors and uncaught exceptions with backtrace

module ErrorDialogMixin
  EDLG_W = 310
  EDLG_LINE_H = 10
  EDLG_MAX_LINES = 12
  # Near-black opaque color. FmrbGfx::BLACK (0x00) collides with the foreground
  # canvas color-key on some paths and renders transparent.
  EDLG_BG = 0x24
  # Deliberately NOT from the theme, unlike every other dialog here: this one
  # appears when something has already gone wrong, and a theme is one of the
  # things that can be wrong (a palette where ink and page are the same colour
  # would hide the very message explaining it). Red on near-black, always.
  EDLG_BORDER = FmrbGfx::RED
  EDLG_TITLE_BG = FmrbGfx::RED
  EDLG_TEXT = FmrbGfx::YELLOW
  # White: the explanation and backtrace lines, and the "(click to close)" hint,
  # must be easy to read on the near-black page. The yellow first line still
  # stands apart from them.
  EDLG_TRACE = FmrbGfx::WHITE
  EDLG_HINT = FmrbGfx::WHITE

  # The text of a "spawn_failed" from the kernel: a head line and the reason,
  # in the user's language. The kernel sends the fmrb_err.h code and its own
  # English sentence (Kernel#spawn_error_reason, also what the log says);
  # Japanese has a short sentence per code here (i18n.rb, spawn_err_*). The
  # code stays in the head line: it is what a user can read out to whoever
  # helps them.
  def spawn_failed_text(msg)
    code = (msg["code"] || 0).to_i
    reason = (msg["reason"] || "See the log for the reason.").to_s
    head = FmrbI18n.t(:launch_failed)
    head = "#{head} (#{FmrbI18n.t(:err_code)} #{code})" if code != 0
    if FmrbI18n.lang == "ja"
      ja = spawn_failed_reason_ja(code)
      reason = ja if ja
    end
    head + "\n" + reason
  end

  # nil for a code without a sentence (the kernel's English one is shown).
  def spawn_failed_reason_ja(code)
    key = case code
          when -11 then :spawn_err_no_pool
          when -10 then :spawn_err_read_only
          when -9 then :spawn_err_no_slot
          when -8 then :spawn_err_state
          when -7 then :spawn_err_not_found
          when -6 then :spawn_err_failed
          when -5 then :spawn_err_busy
          when -4 then :spawn_err_unsupported
          when -3 then :spawn_err_timeout
          when -2 then :spawn_err_no_memory
          when -1 then :spawn_err_param
          else nil
          end
    key ? FmrbI18n.t(key) : nil
  end

  # +line+ cut into pieces no wider than +max_px+ as the mixed renderer draws
  # them (FmrbI18n.text_width: 6 px for ASCII, 8 px for a full-width glyph), on
  # character boundaries. An ASCII line breaks where it always did (49
  # characters of 6 px in the 294 px of this dialog) unless it has a space to
  # break at.
  def edlg_wrap(line, max_px)
    out = []
    cur = ""
    cur_w = 0
    i = 0
    n = line.bytesize
    while i < n
      b = line.getbyte(i)
      step = 1
      if b >= 0xF0
        step = 4
      elsif b >= 0xE0
        step = 3
      elsif b >= 0xC0
        step = 2
      end
      ch = line.byteslice(i, step)
      cw = FmrbI18n.text_width(ch)
      if cur_w + cw > max_px && cur_w > 0
        # At the last space when the line has one (an English sentence is not
        # cut inside a word, and no line starts with the space); else here.
        sp = ch == " " ? nil : cur.rindex(" ")
        if sp && sp > 0
          out << cur[0, sp]
          cur = cur[sp + 1, cur.length - sp - 1].to_s
          cur_w = FmrbI18n.text_width(cur)
        else
          out << cur
          cur = ""
          cur_w = 0
          if ch == " "
            i += step
            next
          end
        end
      end
      cur += ch
      cur_w += cw
      i += step
    end
    out << cur
    out
  end

  def open_error_dialog(app_name, error_msg)
    @error_dlg_open = true
    @error_dlg_name = app_name

    # Split error message into lines (backtrace separated by \n)
    # Then wrap long lines by their drawn width (a Japanese line is 8 px a
    # glyph, not 6).
    max_px = EDLG_W - 16
    raw_lines = (error_msg || "").split("\n")
    @error_dlg_lines = []
    @error_dlg_colors = []
    raw_lines.each_with_index do |line, i|
      color = (i == 0) ? EDLG_TEXT : EDLG_TRACE
      edlg_wrap(line, max_px).each do |piece|
        @error_dlg_lines << piece
        @error_dlg_colors << color
      end
    end

    # Calculate dialog height based on content
    n_lines = @error_dlg_lines.length
    if n_lines > EDLG_MAX_LINES
      n_lines = EDLG_MAX_LINES
    end
    @error_dlg_h = 18 + n_lines * EDLG_LINE_H + 16
    @error_dlg_x = (@window_width - EDLG_W) / 2
    @error_dlg_y = (@window_height - @error_dlg_h) / 2
    if @error_dlg_y < 12
      @error_dlg_y = 12
    end

    # Keyless: clicking it must not move the keyboard to the desktop for good;
    # it stays with whatever app had it (e.g. the one a refused launch fell
    # back to). take_keys: while the dialog is up the desktop holds the
    # keyboard, so that Return closes it, and the kernel gives it back then.
    notify_overlay_state(true, @error_dlg_x, @error_dlg_y, EDLG_W, @error_dlg_h, true, true)
    update_composite_regions
    draw_foreground
  end

  def close_error_dialog
    return unless @error_dlg_open
    @error_dlg_open = false
    notify_overlay_after_dialog
    update_composite_regions
    draw_foreground
  end

  def draw_error_dialog
    return unless @error_dlg_open
    x = @error_dlg_x
    y = @error_dlg_y
    h = @error_dlg_h

    # Window frame
    @gfx.fill_rect(x, y, EDLG_W, h, EDLG_BG)
    @gfx.draw_rect(x, y, EDLG_W, h, EDLG_BORDER)
    @gfx.draw_rect(x + 1, y + 1, EDLG_W - 2, h - 2, EDLG_BORDER)

    # Title bar
    @gfx.fill_rect(x + 2, y + 2, EDLG_W - 4, 13, EDLG_TITLE_BG)
    # Cut by drawn width, inside the bar (4 px each side): an app's name may
    # be Japanese (app_screen_name_ja), 8 px a glyph.
    title = "#{FmrbI18n.t(:err_title)}: #{@error_dlg_name}"
    title = FmrbI18n.truncate_to(title, EDLG_W - 12)
    @gfx.draw_text(x + 4, y + 4, title, FmrbGfx::WHITE, EDLG_TITLE_BG, mixed: true)

    # Error message + backtrace lines (pre-wrapped)
    ty = y + 18
    lines = @error_dlg_lines || []
    colors = @error_dlg_colors || []
    displayed = 0
    lines.each_with_index do |line, i|
      break if displayed >= EDLG_MAX_LINES
      color = colors[i] || EDLG_TRACE
      @gfx.draw_text(x + 8, ty, line, color, EDLG_BG, mixed: true)
      ty += EDLG_LINE_H
      displayed += 1
    end

    # Footer
    # Right-aligned by its drawn width, with the same 8 px margin the text
    # lines keep on the left (the English one: 25 characters of 6 px, at -158).
    hint = FmrbI18n.t(:err_close_hint)
    @gfx.draw_text(x + EDLG_W - 8 - FmrbI18n.text_width(hint), y + h - 12, hint, EDLG_HINT, EDLG_BG, mixed: true)
  end

  def handle_error_dialog_click(x, y)
    close_error_dialog
  end

  def hit_error_dialog?(x, y)
    @error_dlg_open &&
      x >= @error_dlg_x && x < @error_dlg_x + EDLG_W &&
      y >= @error_dlg_y && y < @error_dlg_y + @error_dlg_h
  end
end
