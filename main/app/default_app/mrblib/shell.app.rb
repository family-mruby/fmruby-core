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
  end

  def on_create()
    @gfx.clear(FmrbGfx::WHITE)
    draw_window_frame
    draw_prompt
    @gfx.present
    puts "[ShellApp] on_create called"
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
    @gfx.draw_text(x, y, @prompt + @current_line, FmrbGfx::BLACK)
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
    puts "on_event: shell app"
    p ev

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
    modifier = ev[:modifier] || 0
    shift_pressed = (modifier & 0x03) != 0  # 0x01 = Left Shift, 0x02 = Right Shift
    puts "[Shell] keycode=#{keycode}, modifier=#{modifier}, shift=#{shift_pressed}"

    # Enter key (keycode 13 = CR)
    if keycode == 13
      handle_enter
      return
    end

    # Backspace key (keycode 8 = BS)
    if keycode == 8
      handle_backspace
      return
    end

    # Ignore arrow keys (79-82)
    if keycode >= 79 && keycode <= 82
      return
    end

    # Ignore standalone Shift keys (225, 229)
    if keycode == 225 || keycode == 229
      return
    end

    # Convert keycode to character (ASCII printable range)
    if keycode >= 32 && keycode <= 126
      char = keycode_to_char(keycode, shift_pressed)
      if char
        @current_line += char
        @need_redraw = true
      end
    end
  end

  def keycode_to_char(keycode, shift)
    # Shift key mapping for special characters (US keyboard layout compatible)
    if shift
      case keycode
      when 45 then return "_"  # - -> _
      when 61 then return "+"  # = -> +
      when 91 then return "{"  # [ -> {
      when 93 then return "}"  # ] -> }
      when 92 then return "_"  # \ -> _ (for Japanese keyboard Shift+\)
      when 59 then return ":"  # ; -> :
      when 39 then return "\""  # ' -> "
      when 44 then return "<"  # , -> <
      when 46 then return ">"  # . -> >
      when 47 then return "?"  # / -> ?
      when 96 then return "~"  # ` -> ~
      when 49 then return "!"  # 1 -> !
      when 50 then return "@"  # 2 -> @
      when 51 then return "#"  # 3 -> #
      when 52 then return "$"  # 4 -> $
      when 53 then return "%"  # 5 -> %
      when 54 then return "^"  # 6 -> ^
      when 55 then return "&"  # 7 -> &
      when 56 then return "*"  # 8 -> *
      when 57 then return "("  # 9 -> (
      when 48 then return ")"  # 0 -> )
      else
        # For letters (a-z -> A-Z)
        if keycode >= 97 && keycode <= 122
          return (keycode - 32).chr
        else
          return keycode.chr
        end
      end
    else
      return keycode.chr
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
