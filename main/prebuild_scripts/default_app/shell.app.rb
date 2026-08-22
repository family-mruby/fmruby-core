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
    @char_height = 8
    @current_dir = "/home"  # Virtual working directory (user-visible)

    # User-facing filesystem root. The HAL resolver maps "/" to LittleFS on
    # ESP32 and to the local "flash" directory on Linux.
    @fs_root = detect_fs_root
    @prompt = "> "
    @need_full_redraw = false   # Full screen redraw (includes logo)
    @need_line_redraw = false   # Only current input line redraw
    @max_line_length = 100  # Maximum input line length
    @input_buffer = []  # Character buffer for getch
    @frame_ms = 33
    @irb_mode = false  # IRB mode flag
    @irb_sandbox = nil  # Sandbox for IRB

    @bg_col = FmrbGfx.rgb_to_332(255, 230, 240)  # Nearly white pink
    @ch_col = FmrbGfx.rgb_to_332(0, 0, 0)        # Black text

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
    show_logo
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
    while @running
      ch = getch
      break if ch.nil?  # getch returns nil when app is terminating

      # Scroll to bottom on any keyboard input
      unless @auto_scroll
        scroll_to_bottom
        @need_full_redraw = true
      end

      # Handle special keys
      case ch
      when 10, 13  # Enter (LF or CR)
        handle_enter
      when 8  # Backspace
        handle_backspace
      when 9  # Tab
        handle_tab
      when -1  # Arrow UP
        handle_history_up
      when -2  # Arrow DOWN
        handle_history_down
      when 32..126  # Printable characters
        if @current_line.length < @max_line_length
          @current_line += ch.chr
          @cmd_history_index = -1  # Reset history browsing on new input
          @need_line_redraw = true
        end
      end
    end
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
    @history << "Family mruby Shell"
    @history << "Type 'help' for available commands"
    @history << ""
  end

  # ---- Input buffer ----

  def getch
    while @input_buffer.empty? && @running
      sleep_ms @frame_ms
    end
    return nil if !@running  # App is terminating
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
    @gfx.present
  end

  # ---- Update loop ----

  def on_update()
    tick_pending_edit

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

      # PageUp / PageDown for scrollback
      if keycode == 75  # PageUp
        scroll_page_up
        return
      elsif keycode == 78  # PageDown
        scroll_page_down
        return
      end

      # Arrow keys: encode as negative values (character=0 for these)
      if keycode == 82  # UP
        @input_buffer << -1
        return
      elsif keycode == 81  # DOWN
        @input_buffer << -2
        return
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

  def on_resize(new_width, new_height)
    # Called when window is resized
    # @window_width, @window_height, @user_area_* are already updated by C code
    puts "[ShellApp] Resize event: #{new_width}x#{new_height}"

    # Trigger full redraw
    @need_full_redraw = true
  end

  def handle_key_input(ev)
    keycode = ev[:keycode]
    character = ev[:character] || 0

    # Enter key
    if character == 10 || character == 13  # LF or CR
      handle_enter
      return
    end

    # Backspace key
    if character == 8  # BS
      handle_backspace
      return
    end

    # Tab key
    if character == 9  # TAB
      # TODO: tab completion
      return
    end

    # Ignore arrow keys and other control keys (keycode-based check)
    if keycode >= 79 && keycode <= 82
      return
    end

    # Ignore standalone modifier keys (keycode 225-229)
    if keycode >= 225 && keycode <= 229
      return
    end

    # If we have a printable character, add it (with length limit)
    if character >= 32 && character <= 126
      if @current_line.length < @max_line_length
        char_str = character.chr
        @current_line += char_str
        @need_line_redraw = true
      else
        Log.warn("Warning: max line length (#{@max_line_length}) reached")
      end
    else
      Log.debug("Character #{character} not in printable range (32-126)")
    end
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
    if @current_line.length > 0
      @current_line = @current_line[0...-1]
      @need_line_redraw = true
    end
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
    clear_user_area(@bg_col)
    draw_window_frame

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

  # Control messages the app base does not handle itself. Today that is the
  # kernel's answer to `kill` (see cmd_kill in shell/shell_commands.rb).
  def on_control(msg)
    handle_kill_result(msg) if msg["cmd"] == "kill_result"
  end

  def on_destroy
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
