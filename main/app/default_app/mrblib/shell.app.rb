class ShellApp < FmrbApp
  def initialize
    super()
    @current_line = ""
    @history = []  # Line history
    @cursor_x = 0
    @cursor_y = 0
    @char_width = 6
    @char_height = 8
    @prompt = "> "
    @need_redraw = false
    @max_line_length = 100  # Maximum input line length
  end

  def on_create()
    @gfx.clear(FmrbGfx::WHITE)
    draw_window_frame
    draw_prompt
    @gfx.present
    puts "[ShellApp] on_create called"
    puts "[ShellApp] user_area: x0=#{@user_area_x0}, y0=#{@user_area_y0}, width=#{@user_area_width}, height=#{@user_area_height}"
    puts "[ShellApp] window: width=#{@window_width}, height=#{@window_height}"
    puts "[ShellApp] char: width=#{@char_width}, height=#{@char_height}"
    max_chars = (@user_area_width - 4) / @char_width  # -4 for left margin
    puts "[ShellApp] max displayable chars: ~#{max_chars} (including prompt)"
  end

  def spawn_app(app_name)
    puts "[ShellApp] Requesting spawn: #{app_name}"

    # Build message payload: subtype + app_name(FMRB_MAX_PATH_LEN)
    data = FmrbConst::APP_CTRL_SPAWN.chr
    data += app_name.ljust(FmrbConst::MAX_PATH_LEN, "\x00")

    # Send to Kernel
    success = send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)

    if success
      @history << "Spawned: #{app_name}"
      puts "[ShellApp] Spawn request sent successfully"
    else
      @history << "Error: Failed to spawn #{app_name}"
      puts "[ShellApp] Failed to send spawn request"
    end
  end

  def parse_command(line)
    # Strip whitespace and split by space
    parts = line.strip.split(' ')
    return nil, [] if parts.empty?

    cmd = parts[0]
    args = parts[1..-1] || []
    return cmd, args
  end

  def execute_command(cmd, args)
    case cmd
    when "run"
      if args.empty?
        @history << "Error: run requires an app path"
        return
      end
      app_path = args.join(' ')
      spawn_app(app_path)
    when "help"
      @history << "Available commands:"
      @history << "  run <app_path> - Launch an application"
      @history << "  help - Show this help message"
    else
      @history << "Unknown command: #{cmd}"
      @history << "Type 'help' for available commands"
    end
  end

  def on_update()
    if @need_redraw
      #puts "[Shell] on_update: need_redraw=true, calling redraw_screen"
      redraw_screen
      @need_redraw = false
    end
    33 # msec
  end

  def draw_prompt
    # Draw all history lines
    @history.each_with_index do |line, i|
      x = @user_area_x0 + 2
      y = @user_area_y0 + 2 + (i * @char_height)
      @gfx.draw_text(x, y, line, FmrbGfx::BLACK)
    end

    # Draw current input line
    x = @user_area_x0 + 2
    y = @user_area_y0 + 2 + (@history.length * @char_height)
    full_line = @prompt + @current_line
    puts "[Shell] draw_prompt: drawing '#{full_line}' (length=#{full_line.length}) at (#{x}, #{y})"
    @gfx.draw_text(x, y, full_line, FmrbGfx::BLACK)
  end

  def redraw_screen
    # Clear user area
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                    @user_area_width, @user_area_height, FmrbGfx::WHITE)
    draw_window_frame
    draw_prompt
    @gfx.present
  end

  def on_event(ev)
    #puts "on_event: shell app"
    #p ev

    if ev[:type] == :key_down
      handle_key_input(ev)
    elsif ev[:type] == :key_up
      handle_key_up(ev)
    end
  end

  def handle_key_up(ev)
    keycode = ev[:keycode]

    # Window position control with arrow keys
    x = 0
    y = 0
    case keycode
    when 82  # up
      y = -10
    when 81  # down
      y = 10
    when 80  # left
      x = -10
    when 79  # right
      x = 10
    end

    if x != 0 || y != 0
      set_window_position(@pos_x + x, @pos_y + y)
    end
  end

  def handle_key_input(ev)
    keycode = ev[:keycode]
    character = ev[:character] || 0
    #puts "[Shell] keycode=#{keycode}, character=#{character} (#{character.chr if character != 0})"

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
        #puts "[Shell] Adding character: '#{char_str}' (ASCII #{character}), line was: '#{@current_line}'"
        @current_line += char_str
        #puts "[Shell] Line is now: '#{@current_line}' (length=#{@current_line.length})"
        @need_redraw = true
      else
        puts "[Shell] Warning: max line length (#{@max_line_length}) reached"
      end
    else
      puts "[Shell] Character #{character} not in printable range (32-126)"
    end
  end

  def handle_enter
    puts "[Shell] Command: #{@current_line}"

    # Add current line to history
    @history << (@prompt + @current_line)

    # Execute command
    cmd, args = parse_command(@current_line)
    if cmd
      execute_command(cmd, args)
    end

    # Clear current line
    @current_line = ""

    # Check if we need to scroll
    max_lines = @user_area_height / @char_height
    while @history.length >= max_lines - 1
      # Remove oldest line to make room
      @history.shift
    end

    @need_redraw = true
  end

  def handle_backspace
    if @current_line.length > 0
      @current_line = @current_line[0...-1]
      @need_redraw = true
    end
  end

  def on_destroy
    puts "[ShellApp] Destroyed"
  end

end

# Create and start the system GUI app instance
puts "[ShellApp] ShellApp.new"
begin
  app = ShellApp.new
  puts "[ShellApp] ShellApp created successfully"
  app.start
rescue => e
  puts "[ShellApp] Exception caught: #{e.class}"
  puts "[ShellApp] Message: #{e.message}"
  puts "[ShellApp] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end
puts "[ShellApp] Script ended"
