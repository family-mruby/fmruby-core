# Family mruby Shell Application
# Mixins are loaded from shell/ directory at build time:
#   shell_io.rb       - OutputCapturer, ShellStdout, ShellStdin classes
#   shell_commands.rb - ShellCommandsMixin (filesystem, run, ps, tab completion)
#   shell_irb.rb      - ShellIrbMixin (interactive Ruby)
#   shell_scroll.rb   - ShellScrollMixin (wrapping, scrollback, drawing)

class ShellApp < FmrbApp
  include ShellCommandsMixin
  include ShellIrbMixin
  include ShellScrollMixin

  def initialize
    super()
    @current_line = ""
    @history = []  # Line history
    @cursor_x = 0
    @cursor_y = 0
    @char_width = 6
    # Cell metrics of efontJA_12, the font all shell text draws in (the
    # editor's terminal-cell model: half-width 6px = one cell, full-width
    # 12px = two). 12px rows, not 8: the default 6x8 font has no Japanese
    # glyphs, and kana typed at the prompt has to be visible.
    @char_height = 12
    @current_dir = "/home"  # Virtual working directory (user-visible)

    # User-facing filesystem root. The HAL resolver maps "/" to LittleFS on
    # ESP32 and to the local "flash" directory on Linux.
    @fs_root = detect_fs_root
    @prompt = "> "
    @need_full_redraw = false   # Full screen redraw (includes logo)
    @need_line_redraw = false   # Only current input line redraw
    @max_line_length = 100  # Maximum input line length
    @input_buffer = []  # Character buffer for getch
    @cursor_pos = 0     # Insertion point in @current_line, in characters
    # UTF-8 assembly for composed kana: the host's composition layer delivers
    # one byte of character per key event (same contract as the editor).
    @u8_buf = ""
    @u8_need = 0
    @frame_ms = 33
    @irb_mode = false  # IRB mode flag
    @irb_sandbox = nil  # Sandbox for IRB

    # Page and ink follow the system theme, so the shell restyles with the
    # rest of the machine. A user who wants this one window to differ writes
    # [shell] bg / text in /home/colors.toml, which wins here and nowhere
    # else; deleting the line goes back to the theme.
    shell_colors = FmrbColors.section("shell")
    @bg_col = shell_colors["bg"] || FmrbConst::THEME_WINDOW_BG
    @ch_col = shell_colors["text"] || FmrbConst::THEME_TEXT

    @fg_sandbox = nil   # Foreground sandbox (set during run_foreground)
    @script_input_line = nil  # Current script input text (shown during fg execution)
    @ctrl_pressed = false  # Track Ctrl key state for Ctrl-C detection
    @jobs = []          # Background job list

    @cmd_history = []       # Command history
    @cmd_history_index = -1 # Current position in history (-1 = not browsing)
    @cmd_saved_line = ""    # Saved current line when browsing history

    @scroll = 0           # Scroll position (first visible display row)
    @auto_scroll = true   # Auto-scroll to bottom on new output
    @scroll_hold = 0      # Scrollbar hold: -1=up, 0=none, 1=down
    @prev_input_rows = 1  # Track input row count for wrap change detection

    # `edit <file>` deferred dispatch: filled by cmd_edit, drained by
    # tick_pending_edit on later on_update ticks once the editor has spawned.
    @pending_edit_path = nil
    @pending_edit_counter = nil

    # less mode: modal pager state. While @less_mode is true, key events are
    # routed to handle_less_key instead of the normal shell input loop.
    @less_mode = false
    @less_lines = []
    @less_offset = 0
    @less_filename = ""
    @less_help_visible = false
  end

  # Key reference shown by the less-mode help overlay (toggled with '?').
  LESS_HELP_LINES = [
    "less keys:",
    "",
    "  q / ESC      quit",
    "  Space/PgDn   next page",
    "  b / PgUp     prev page",
    "  j / Down     line down",
    "  k / Up       line up",
    "  g            top",
    "  G            bottom",
    "  ?            toggle help"
  ]

  SB_W = 10          # scrollbar width, reserved from the text area

  def on_create()
    # Layout: reserve scrollbar width for consistent text wrapping
    @max_chars = (@user_area_width - 4 - SB_W) / @char_width
    @visible_rows = (@user_area_height - 2) / @char_height

    # The history scrollbar. Its height follows the visible rows, so it is
    # placed again in draw_history once those are known.
    @ui = FmrbUI.new(self, bg: @bg_col)
    @ui.scrollbar(:sb, @user_area_width - SB_W, 0, SB_W,
                  @user_area_height, 0, 1)

    clear_user_area(@bg_col)
    draw_window_frame
    show_greeting
    draw_prompt
    @gfx.present
    Log.info("on_create called")
    Log.info("user_area: x0=#{@user_area_x0}, y0=#{@user_area_y0}, width=#{@user_area_width}, height=#{@user_area_height}")
    Log.info("window: width=#{@window_width}, height=#{@window_height}")
    Log.info("char: width=#{@char_width}, height=#{@char_height}")
    max_chars = (@user_area_width - 4) / @char_width  # -4 for left margin
    Log.info("max displayable chars: ~#{max_chars} (including prompt)")

    Log.info("create task")

    # Capture self to maintain instance context in the task block
    app_self = self
    @shell_task = Task.new(name: "shell_task", priority: 100) do
      Log.info("[Task] loop start")
      app_self.shell_task
    end
    Log.info("create task done")
  end

  def shell_task
    while running?
      ch = getch
      break if ch.nil?  # getch returns nil when app is terminating

      # Scroll to bottom on any keyboard input
      unless @auto_scroll
        scroll_to_bottom
        @need_full_redraw = true
      end

      # A composed kana arrives as its UTF-8 bytes, one getch per byte;
      # anything else interrupts a half-built character.
      if ch >= 0x80
        s = utf8_feed(ch)
        insert_input_char(s) if s
        next
      end
      utf8_reset

      # Handle special keys
      case ch
      when 10, 13  # Enter (LF or CR)
        handle_enter
      when 8  # Backspace
        handle_backspace
      when 9  # Tab
        handle_tab
        @cursor_pos = @current_line.length
      when -1  # Arrow UP
        handle_history_up
      when -2  # Arrow DOWN
        handle_history_down
      when -3  # Arrow LEFT
        if @cursor_pos > 0
          @cursor_pos -= 1
          @need_line_redraw = true
        end
      when -4  # Arrow RIGHT
        if @cursor_pos < @current_line.length
          @cursor_pos += 1
          @need_line_redraw = true
        end
      when -5  # Home
        if @cursor_pos != 0
          @cursor_pos = 0
          @need_line_redraw = true
        end
      when -6  # End
        if @cursor_pos != @current_line.length
          @cursor_pos = @current_line.length
          @need_line_redraw = true
        end
      when -7  # Delete
        handle_delete
      when 32..126  # Printable characters
        insert_input_char(ch.chr)
      end
    end
  end

  # ---- Line editing at the cursor ----

  # Insert one character (String, possibly multi-byte) at the cursor.
  def insert_input_char(s)
    return if s.empty?
    if @current_line.length >= @max_line_length
      Log.warn("Warning: max line length (#{@max_line_length}) reached")
      return
    end
    head = @cursor_pos > 0 ? @current_line[0, @cursor_pos].to_s : ""
    tail = @current_line[@cursor_pos, @current_line.length - @cursor_pos].to_s
    @current_line = head + s + tail
    @cursor_pos += 1
    @cmd_history_index = -1  # Reset history browsing on new input
    @need_line_redraw = true
  end

  # Feed one byte of a possibly multi-byte character; returns the finished
  # character or nil while more bytes are needed. Same shape as the editor's.
  def utf8_feed(byte)
    if @u8_need == 0
      if byte >= 0xF0
        need = 3
      elsif byte >= 0xE0
        need = 2
      elsif byte >= 0xC0
        need = 1
      else
        return nil  # continuation byte with nothing in front of it
      end
      @u8_buf = one_byte_string(byte)
      @u8_need = need
      return nil
    end
    if byte < 0x80 || byte >= 0xC0
      utf8_reset
      return utf8_feed(byte)
    end
    @u8_buf += one_byte_string(byte)
    @u8_need -= 1
    return nil if @u8_need > 0
    s = @u8_buf
    utf8_reset
    s
  end

  def utf8_reset
    @u8_buf = ""
    @u8_need = 0
  end

  # One-character String holding +byte+ (no Array#pack in picoruby).
  def one_byte_string(byte)
    s = " ".dup
    s.setbyte(0, byte)
    s
  end

  # What the shell says when it opens.
  #
  # The logo used to be here, and it cost six of the window's rows plus a
  # blank one before anything could be typed -- on a screen this size that is
  # most of the history. Two lines say the same things: what this is, and what
  # it is built on. The name PicoRuby and its author stay in the text, because
  # the credit is the part of the logo that mattered; `logo` still draws it.
  def show_greeting
    @history << "FM-Shell -- powered by PicoRuby (@hasumikin)"
    @history << "Type 'help' for available commands"
    @history << ""
  end

  # Show MicroRuby logo as ASCII art in history
  def show_logo
    logo = [
      "011100100110011001110010010111001010",
      "010010101000100101001010010100101010",
      "011100101000100101110010010111001010",
      "010000101000100101001010010100100100",
      "010000100110011001001001100111000100",
      "000000000000000000000000000000000000"
    ]

    logo_width = logo[0].length
    author = "@hasumikin"
    space = " " * ((logo_width - author.length) / 2)
    author_line = space + author + space

    # Gradient settings (5 slices)
    grad_slice_width = logo_width / LOGO_GRAD_COLORS.length

    # Calculate margin for center alignment
    max_line_width = (@user_area_width - 4) / @char_width
    margin = " " * ((max_line_width - logo_width) / 2) if max_line_width > logo_width

    # Add shadow effect
    ly = 0
    while ly < logo.size
      break if logo[ly+1].nil?
      lx = 0
      lxn = logo[ly].length
      while lx < lxn
        if logo[ly][lx] == '1' && lx > 0 && logo[ly+1][lx-1] == '0'
          logo[ly+1][lx-1] = '2'
        end
        lx += 1
      end
      ly += 1
    end

    # Store logo lines as Hash entries for colored rendering
    y = 0
    while y < logo.size
      line = logo[y]
      @history << {
        :type => :logo_line,
        :data => line,
        :author_line => author_line,
        :margin => margin || "",
        :is_last_line => (y == logo.size - 1),
        :grad_slice_width => grad_slice_width
      }
      y += 1
    end

    @history << ""
  end

  # ---- Input buffer ----

  def getch
    while @input_buffer.empty? && running?
      sleep_ms @frame_ms
    end
    return nil if !running?  # App is terminating
    char = @input_buffer.shift
    char
  end

  def kbhit?
    !@input_buffer.empty?
  end

  def input_buffer_empty?
    @input_buffer.empty?
  end

  def getch_nonblock
    @input_buffer.shift
  end

  # ---- Output ----

  def append_output(line)
    @history << line
    # Keep ~3 pages of scrollback
    max_entries = @visible_rows * 3
    while @history.length > max_entries
      @history.shift
    end
    scroll_to_bottom
    @need_full_redraw = true
  end

  def redraw_script_input(partial_line)
    @script_input_line = partial_line
    begin_text_font
    content_x = @user_area_x0 + 2
    input_rows = partial_line.empty? ? 1 : (partial_line.length + @max_chars - 1) / @max_chars
    avail = @visible_rows - input_rows
    input_y = @user_area_y0 + 2 + avail * @char_height

    # Clear input area
    @gfx.fill_rect(@user_area_x0 + 1, input_y,
                    @user_area_width - 2 - FmrbApp::SCROLLBAR_W,
                    input_rows * @char_height, @bg_col)

    r = 0
    while r < input_rows
      chunk_start = r * @max_chars
      chunk = partial_line[chunk_start, @max_chars] || ""
      @gfx.draw_text(content_x, input_y + r * @char_height, chunk, @ch_col) unless chunk.empty?
      r += 1
    end

    # Cursor
    last_chars = partial_line.length % @max_chars
    last_chars = @max_chars if last_chars == 0 && !partial_line.empty?
    cursor_x = content_x + (last_chars * @char_width)
    cursor_y = input_y + (input_rows - 1) * @char_height + @char_height - 1
    @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, @ch_col)
    end_text_font
    @gfx.present
  end

  # ---- Update loop ----

  def on_update()
    tick_pending_edit
    tick_svc_wait

    # Continuous scroll while holding scrollbar
    if @scroll_hold != 0
      @scroll_hold > 0 ? scroll_down : scroll_up
      return 50
    end

    if @need_full_redraw
      # Full redraw: everything including logo (for scroll, etc.)
      redraw_screen
      @need_full_redraw = false
      @need_line_redraw = false
    elsif @need_line_redraw
      # Partial redraw: only current input line (for typing)
      redraw_input_line
      @need_line_redraw = false
    end
    @frame_ms # msec
  end

  # ---- Event handling ----

  def on_event(ev)
    # Call parent class handler first (for close button, etc.)
    super(ev)

    # The wheel scrolls the history, one row per row the machine's setting
    # says. less mode has its own viewport and pages with its own keys.
    rows = wheel_rows(ev)
    if rows && !@less_mode
      n = rows > 0 ? rows : -rows
      i = 0
      while i < n
        rows > 0 ? scroll_up : scroll_down
        i += 1
      end
      return
    end

    if ev[:type] == :mouse_down
      # Scrollbar hold start (suppressed while less mode owns the viewport)
      unless @less_mode
        # The widget says which way; how far is the shell's business
        # (a row at a time, and reaching the bottom turns auto-scroll back
        # on). Its own value is overwritten from @scroll on the next redraw.
        if @ui.handle(ev) == :sb
          d = @ui.direction(:sb)
          d < 0 ? scroll_up : scroll_down
          @scroll_hold = d
        end
      end
    elsif ev[:type] == :mouse_up
      @scroll_hold = 0
    end

    if ev[:type] == :key_down
      character = ev[:character] || 0
      keycode = ev[:keycode] || 0

      # Less mode swallows all key input for paging navigation.
      if @less_mode
        handle_less_key(character, keycode)
        return
      end

      # Track Ctrl key state (224=LCtrl, 228=RCtrl)
      if keycode == 224 || keycode == 228
        @ctrl_pressed = true
        return
      end
      # Detect Ctrl-C
      if @ctrl_pressed && (keycode == 99 || character == 99)  # 'c'
        @ctrl_pressed = false
        if @fg_sandbox
          Log.info("Ctrl-C: stopping foreground sandbox")
          @fg_sandbox.stop
          append_output("^C")
        end
        return
      end
      @ctrl_pressed = false

      # Special keys, by scancode (HID Usage ID -- uniform across the device
      # and the Linux sim, where ev[:keycode] is the SDL keysym instead).
      # Encoded as negative values so they travel the same @input_buffer as
      # characters and stay in order with them.
      case ev[:scancode] || 0
      when 0x4B  # PageUp: scrollback, handled here, not queued
        scroll_page_up
        return
      when 0x4E  # PageDown
        scroll_page_down
        return
      when 0x52 then @input_buffer << -1; return  # Up
      when 0x51 then @input_buffer << -2; return  # Down
      when 0x50 then @input_buffer << -3; return  # Left
      when 0x4F then @input_buffer << -4; return  # Right
      when 0x4A then @input_buffer << -5; return  # Home
      when 0x4D then @input_buffer << -6; return  # End
      when 0x4C then @input_buffer << -7; return  # Delete
      end
      if character > 0
        @input_buffer << character
      end
    elsif ev[:type] == :key_up
      keycode = ev[:keycode] || 0
      if keycode == 224 || keycode == 228
        @ctrl_pressed = false
      end
    end
  end

  # The window can be dragged to a new size (the entry in the spawner table
  # says resizable). @window_* and @user_area_* are updated by the C side
  # before this runs; everything the shell derives from them is on_create's
  # layout half, so it is done again here -- including the scrollbar, whose
  # widget is anchored to the user area at the moment it is built. The old
  # one is detached first, or clear_user_area would keep repainting it where
  # the window used to end.
  def on_resize(new_width, new_height)
    @max_chars = (@user_area_width - 4 - SB_W) / @char_width
    @visible_rows = (@user_area_height - 2) / @char_height
    detach_ui(@ui) if @ui
    @ui = FmrbUI.new(self, bg: @bg_col)
    @ui.scrollbar(:sb, @user_area_width - SB_W, 0, SB_W,
                  @user_area_height, 0, 1)
    # A narrower window rewraps the history into more rows, so a scroll
    # position taken at the old width can point past the end.
    total = total_display_rows
    max_scroll = total > @visible_rows ? total - @visible_rows : 0
    @scroll = max_scroll if @scroll > max_scroll
    @need_full_redraw = true
    Log.info("Shell resize: #{new_width}x#{new_height} " \
             "-> #{@max_chars}x#{@visible_rows}")
    nil
  end

  # ---- Command input handling ----

  def handle_enter
    Log.info("Command: #{@current_line}")

    # Add to command history (skip empty and duplicates)
    entered_line = @current_line
    if !entered_line.empty? && entered_line != @cmd_history.last
      @cmd_history << entered_line
    end
    @cmd_history_index = -1

    # Add current line to display history and clear input before executing
    @history << (@prompt + entered_line)
    @current_line = ""
    @cursor_pos = 0

    # Execute command or IRB eval
    begin
      if @irb_mode
        irb_eval(entered_line)
      else
        cmd, args = parse_command(entered_line)
        if cmd
          execute_command(cmd, args)
        end
      end
    rescue => e
      Log.error("Command error: #{e.message}")
      @history << "Error: #{e.message}"
    end

    # If the command entered less mode, leave the viewport drawn by cmd_less
    # alone — skip shell-history trim/scroll/redraw which would clobber it.
    return if @less_mode

    # Trim old entries (keep ~3 pages)
    max_entries = @visible_rows * 3
    while @history.length > max_entries
      @history.shift
    end

    # Auto-scroll to bottom
    scroll_to_bottom
    @need_full_redraw = true
  end

  def handle_backspace
    return if @cursor_pos <= 0
    head = @cursor_pos > 1 ? @current_line[0, @cursor_pos - 1].to_s : ""
    tail = @current_line[@cursor_pos, @current_line.length - @cursor_pos].to_s
    @current_line = head + tail
    @cursor_pos -= 1
    @need_line_redraw = true
  end

  def handle_delete
    return if @cursor_pos >= @current_line.length
    head = @cursor_pos > 0 ? @current_line[0, @cursor_pos].to_s : ""
    tail = @current_line[@cursor_pos + 1, @current_line.length - @cursor_pos - 1].to_s
    @current_line = head + tail
    @need_line_redraw = true
  end

  def handle_history_up
    return if @cmd_history.empty?
    if @cmd_history_index == -1
      # Start browsing: save current line, go to last entry
      @cmd_saved_line = @current_line
      @cmd_history_index = @cmd_history.size - 1
    elsif @cmd_history_index > 0
      @cmd_history_index -= 1
    else
      return  # Already at oldest
    end
    @current_line = @cmd_history[@cmd_history_index]
    @cursor_pos = @current_line.length
    @need_line_redraw = true
  end

  def handle_history_down
    return if @cmd_history_index == -1  # Not browsing
    if @cmd_history_index < @cmd_history.size - 1
      @cmd_history_index += 1
      @current_line = @cmd_history[@cmd_history_index]
    else
      # Back to current input
      @cmd_history_index = -1
      @current_line = @cmd_saved_line
    end
    @cursor_pos = @current_line.length
    @need_line_redraw = true
  end

  # ---- less mode (modal pager) ----

  def enter_less_mode(filename, lines)
    @less_mode = true
    @less_filename = filename
    @less_lines = lines
    @less_offset = 0
    draw_less_view
  end

  def exit_less_mode
    @less_mode = false
    @less_lines = []
    @less_offset = 0
    @less_help_visible = false
    @need_full_redraw = true
  end

  # Number of content rows in the less viewport (status bar takes 1 row).
  def less_page_rows
    rows = @visible_rows - 1
    rows < 1 ? 1 : rows
  end

  def clamp_less_offset
    max_offset = @less_lines.length - less_page_rows
    max_offset = 0 if max_offset < 0
    @less_offset = 0 if @less_offset < 0
    @less_offset = max_offset if @less_offset > max_offset
  end

  def draw_less_view
    clamp_less_offset
    # No draw_window_frame here: clear_user_area draws it. This runs on every
    # key press, and the frame is the expensive half of the wipe.
    clear_user_area(@bg_col)

    content_x = @user_area_x0 + 2
    rows = less_page_rows
    i = 0
    while i < rows
      idx = @less_offset + i
      break if idx >= @less_lines.length
      y = @user_area_y0 + 2 + i * @char_height
      line = @less_lines[idx]
      @gfx.draw_text(content_x, y, line, @ch_col) unless line.empty?
      i += 1
    end

    # Status bar on the last visible row. Keep concise; full key reference
    # is available via the '?' help overlay.
    total = @less_lines.length
    if total == 0
      status = "#{@less_filename}  (empty)  ?=help q=quit"
    else
      last = [@less_offset + rows, total].min
      first = @less_offset + 1
      status = "#{@less_filename}  #{first}-#{last}/#{total}  ?=help q=quit"
    end
    max_status = (@user_area_width - 4) / @char_width
    status = status[0...max_status] if status.length > max_status
    sy = @user_area_y0 + 2 + rows * @char_height
    @gfx.fill_rect(@user_area_x0 + 1, sy, @user_area_width - 2, @char_height, @ch_col)
    inv_col = @bg_col
    @gfx.draw_text(content_x, sy, status, inv_col)

    draw_less_help if @less_help_visible
    @gfx.present
  end

  def draw_less_help
    lines = LESS_HELP_LINES
    # Box: 1-char padding on each side
    box_chars = 26
    box_w = (box_chars + 2) * @char_width
    box_h = (lines.length + 2) * @char_height
    if box_w > @user_area_width - 4
      box_w = @user_area_width - 4
    end
    box_x = @user_area_x0 + (@user_area_width - box_w) / 2
    box_y = @user_area_y0 + (@user_area_height - box_h) / 2

    @gfx.fill_rect(box_x, box_y, box_w, box_h, @ch_col)
    @gfx.draw_rect(box_x, box_y, box_w, box_h, @bg_col)

    n = lines.length
    i = 0
    while i < n
      ty = box_y + @char_height + i * @char_height
      @gfx.draw_text(box_x + @char_width, ty, lines[i], @bg_col)
      i += 1
    end
  end

  def handle_less_key(character, keycode)
    rows = less_page_rows

    # '?' toggles the help overlay regardless of current state.
    if character == 63
      @less_help_visible = !@less_help_visible
      draw_less_view
      return
    end

    # While help overlay is visible, dismiss on any key. q/ESC still quits.
    if @less_help_visible
      @less_help_visible = false
      if character == 113 || character == 27
        exit_less_mode
      else
        draw_less_view
      end
      return
    end

    case
    when character == 113 || character == 27  # 'q' or ESC
      exit_less_mode
      return
    when character == 32 || keycode == 78  # Space or PageDown
      @less_offset += rows
    when character == 98 || keycode == 75  # 'b' or PageUp
      @less_offset -= rows
    when character == 106 || keycode == 81  # 'j' or Down
      @less_offset += 1
    when character == 107 || keycode == 82  # 'k' or Up
      @less_offset -= 1
    when character == 103  # 'g' top
      @less_offset = 0
    when character == 71  # 'G' bottom
      @less_offset = @less_lines.length - rows
    else
      return
    end
    draw_less_view
  end

  # Control messages the app base does not handle itself: the kernel's answer
  # to `kill`, and the service host's answers to ps / kill <name> / svc, which
  # arrive as Pub/Sub deliveries on this shell's own reply topic (see
  # shell/shell_commands.rb).
  def on_control(msg)
    handle_kill_result(msg) if msg["cmd"] == "kill_result"
    if msg["cmd"] == "topic_data" && msg["topic"] == svc_reply_topic
      handle_svc_reply(msg["data"] || {})
    end
  end

  def on_destroy
    # Pids are slot indices and get handed out again, so leaving the reply
    # topic subscribed would send the next shell in this slot these answers.
    unsubscribe(svc_reply_topic) if @svc_subscribed
    Log.info("Destroyed")
  end

end

# Create and start the system GUI app instance
Log.info("ShellApp.new")
begin
  app = ShellApp.new
  Log.info("ShellApp created successfully")
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
