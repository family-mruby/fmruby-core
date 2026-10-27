# Shell linux mixin - "!" lines run on Linux (doc/linux_native L10)
#
#   !<command>  runs with /bin/sh -c in a pseudo terminal, in the real place
#               of the current directory (/mnt/host/src is ~/src). Its output
#               comes into the history; until it ends, keys go to it.
#   !           the interactive Linux shell (bash, else sh); `exit` or
#               Ctrl+D comes back.
#
# Keys are sent as they are typed and the terminal's own line discipline
# does the rest: it echoes, edits the line (backspace), hands the line over
# on Enter, turns Ctrl+C into SIGINT for the running command and Ctrl+D into
# the end of input. What comes back -- echo included -- goes through
# ShellTerm, which keeps lines, \r rewrites and the SGR colours.
#
# Only machines with Linux (the native program) have this; elsewhere a "!"
# line answers with one line saying so.
module ShellLinuxMixin
  LINUX_READS_PER_TURN = 16   # 4 KB reads per wake before drawing
  LINUX_IDLE_MS = 10          # wait between looks while it is quiet
  # Scrollback on a Linux machine, in history entries: a command's output
  # has to stay readable (the devices keep about 3 pages).
  LINUX_SCROLLBACK = 3000

  # SGR colours 0..15 in RGB332, two sets: for a dark page and for a light
  # one (the theme decides the Shell's page). On a dark page black is a dark
  # grey and blue is lightened, as most terminals do, so that nothing
  # disappears into the page; on a light page yellow and white darken.
  LINUX_COLORS_DARK = [
    0x49, 0xC0, 0x18, 0xD8, 0x27, 0xC3, 0x1B, 0xB6,
    0x92, 0xE9, 0x5D, 0xFD, 0x6F, 0xEB, 0x5F, 0xFF
  ]
  LINUX_COLORS_LIGHT = [
    0x00, 0xA0, 0x10, 0x90, 0x03, 0x82, 0x12, 0x6D,
    0x49, 0xE0, 0x14, 0xB4, 0x07, 0xC3, 0x16, 0x49
  ]
  # Backgrounds: the usual terminal colours on either page.
  LINUX_COLORS_BG = [
    0x00, 0xA0, 0x14, 0xB4, 0x03, 0xA2, 0x16, 0xB6,
    0x49, 0xE9, 0x5D, 0xFD, 0x6F, 0xEB, 0x5F, 0xFF
  ]

  def linux_scrollback
    if @linux_ok.nil?
      @linux_ok = FmrbApp.linux_available?
    end
    @linux_ok ? LINUX_SCROLLBACK : @visible_rows * 3
  end

  def trim_history
    max_entries = linux_scrollback
    while @history.length > max_entries
      @history.shift
    end
  end

  # The 16 colours for this Shell's page.
  def linux_palette
    c = @bg_col
    lum = ((c >> 5) & 7) * 36 * 3 + ((c >> 2) & 7) * 36 * 6 + (c & 3) * 85
    lum >= 1280 ? LINUX_COLORS_LIGHT : LINUX_COLORS_DARK
  end

  def cmd_linux(rest)
    unless FmrbApp.linux_available?
      @history << "!: Linux commands are not available on this machine"
      return
    end
    command = rest.strip
    command = nil if command.empty?
    begin
      h = FmrbApp.linux_start(command, @current_dir, @max_chars, @visible_rows)
    rescue => e
      @history << "!: #{e.message}"
      return
    end
    @linux_term = ShellTerm.new
    @linux_h = h
    @linux_colors = linux_palette
    code = nil
    lines = 0
    begin
      lines, code = linux_loop(h)
    ensure
      FmrbApp.linux_close(h)
      @linux_h = nil
      @linux_out = ""
      @paste_q = []
    end
    linux_add_lines(@linux_term.finish)
    @linux_term = nil
    Log.info("linux: #{command || '(shell)'}: exit #{code.inspect}, #{lines} lines")
    @history << "[exit #{code}]" if code && code != 0
  end

  # Until the program ends (or the Shell does): keys in, output out.
  # Returns [lines received, exit code (nil when the Shell ended first)].
  def linux_loop(h)
    lines = 0
    while running?
      linux_send_keys(h)
      got = false
      eof = false
      k = 0
      while k < LINUX_READS_PER_TURN
        s = FmrbApp.linux_read(h)
        if s.nil?
          eof = true
          break
        end
        break if s.empty?
        @linux_term.feed(s)
        got = true
        k += 1
      end
      if got
        new_lines = @linux_term.take_lines
        lines += new_lines.size
        linux_add_lines(new_lines)
      end
      code = FmrbApp.linux_exit_code(h)
      if code
        # It ended: what it printed is in the terminal already.
        while (s = FmrbApp.linux_read(h)) && !s.empty?
          @linux_term.feed(s)
        end
        rest = @linux_term.take_lines
        lines += rest.size
        linux_add_lines(rest)
        return [lines, code]
      end
      sleep_ms(got && !eof ? 1 : LINUX_IDLE_MS)
    end
    [lines, nil]
  end

  def linux_add_lines(entries)
    i = 0
    while i < entries.size
      @history << entries[i]
      i += 1
    end
    trim_history
    scroll_to_bottom if @auto_scroll
    @need_full_redraw = true
  end

  # Keys typed since the last look, as the bytes a terminal sends, after
  # whatever its input did not take last time. A paste (-8, shell.app.rb)
  # is its text with line breaks as CR.
  def linux_send_keys(h)
    out = @linux_out
    @linux_out = ""
    while (ch = getch_nonblock)
      if ch == -8
        text = @paste_q.shift
        out << linux_paste_bytes(text) if text
      elsif ch >= 0
        if ch == 10 || ch == 13
          out << "\r"
        elsif ch == 8
          out << "\x7f"
        else
          b = " ".dup
          b.setbyte(0, ch & 0xFF)
          out << b
        end
      else
        case ch
        when -1 then out << "\e[A"
        when -2 then out << "\e[B"
        when -3 then out << "\e[D"
        when -4 then out << "\e[C"
        when -5 then out << "\e[H"
        when -6 then out << "\e[F"
        when -7 then out << "\e[3~"
        end
      end
    end
    return if out.empty?
    n = FmrbApp.linux_write(h, out)
    if n >= 0 && n < out.bytesize
      @linux_out = out.byteslice(n, out.bytesize - n).to_s
    end
  end

  # Pasted text as a terminal sends it: "\n" as CR.
  def linux_paste_bytes(text)
    out = ""
    i = 0
    n = text.bytesize
    start = 0
    while i < n
      if text.getbyte(i) == 10
        out << text.byteslice(start, i - start).to_s if i > start
        out << "\r"
        start = i + 1
      end
      i += 1
    end
    out << text.byteslice(start, n - start).to_s if n > start
    out
  end

  # A key while a program runs (on_event). Ctrl+letter becomes its control
  # byte (Ctrl+C 3, Ctrl+D 4); true when the key was taken here.
  def linux_key(ev)
    sc = ev[:scancode] || 0
    if (ev_ctrl?(ev) || @ctrl_pressed) && sc >= 0x04 && sc <= 0x1D
      queue_key(sc - 0x04 + 1)
      return true
    end
    if sc == 0x29  # Esc
      queue_key(27)
      return true
    end
    false
  end

  # ---- Drawing coloured entries ----

  def ansi_entry?(entry)
    entry.is_a?(Hash) && entry[:type] == :ansi
  end

  # Draw row r (chunk) of a coloured entry: each span's part of the chunk in
  # its colours. first is the chunk's first character in the entry's text.
  def draw_ansi_chunk(x, y, chunk, first, spans)
    last = first + chunk.length
    colors = @linux_colors || linux_palette
    s = 0
    n = spans.size
    # Text before the first span is in the Shell's colours.
    if n == 0 || spans[0][0] > first
      upto = n == 0 ? last : spans[0][0]
      upto = last if upto > last
      seg = chunk[0, upto - first].to_s
      @gfx.draw_text(x, y, seg, @ch_col) unless seg.empty?
    end
    while s < n
      from = spans[s][0]
      to = s + 1 < n ? spans[s + 1][0] : last
      a = spans[s][1]
      s += 1
      next if to <= first || from >= last
      from = first if from < first
      to = last if to > last
      seg = chunk[from - first, to - from].to_s
      next if seg.empty?
      fg = a & 31
      bg = (a >> 5) & 31
      fgc = fg == 0 ? @ch_col : colors[fg - 1]
      sx = x + text_cols(chunk[0, from - first].to_s) * @char_width
      if bg == 0
        @gfx.draw_text(sx, y, seg, fgc)
      else
        @gfx.draw_text(sx, y, seg, fgc, LINUX_COLORS_BG[bg - 1])
      end
    end
  end

  # Draw rows [r0, r0 + count) of entry at y; the chunks are wrap_chunks of
  # its text.
  def draw_entry_rows(x, y, entry, chunks, r0, count)
    spans = entry[:spans]
    first = 0
    r = 0
    while r < r0 && r < chunks.size
      first += chunks[r].length
      r += 1
    end
    k = 0
    while k < count && r < chunks.size
      draw_ansi_chunk(x, y + k * @char_height, chunks[r], first, spans)
      first += chunks[r].length
      r += 1
      k += 1
    end
  end
end
