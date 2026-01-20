# Simple output capturer for IRB
class OutputCapturer
  def initialize
    @output = []
  end

  def write(str)
    @output << str
  end

  def puts(str = "")
    @output << str.to_s
    @output << "\n" unless str.to_s.end_with?("\n")
  end

  def print(str)
    @output << str.to_s
  end

  def flush
    # No-op
  end

  def get_output
    result = @output.join
    @output.clear
    result
  end
end

class ShellApp < FmrbApp
  # Gradient colors for logo (red -> magenta in RGB332)
  LOGO_GRAD_COLORS = [0xE0, 0xE1, 0xE2, 0xE3, 0xE3]
  # Shadow colors (light gray, close to white: 0xDB = R6 G6 B3)
  LOGO_SHADOW_COLORS = [0xDB, 0xDB, 0xDB, 0xDB, 0xDB]
  # Author text color (dark red: 0x60 = R3 G0 B0)
  LOGO_AUTHOR_COLOR = 0x60

  def initialize
    super()
    @current_line = ""
    @history = []  # Line history
    @cursor_x = 0
    @cursor_y = 0
    @char_width = 6
    @char_height = 8
    @current_dir = "/"  # Current working directory
    @prompt = "> "
    @need_full_redraw = false   # Full screen redraw (includes logo)
    @need_line_redraw = false   # Only current input line redraw
    @max_line_length = 100  # Maximum input line length
    @input_buffer = []  # Character buffer for getch
    @frame_ms = 33
    @irb_mode = false  # IRB mode flag
    @irb_sandbox = nil  # Sandbox for IRB
  end

  def on_create()
    @gfx.clear(FmrbGfx::WHITE)
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
    loop do
      ch = getch

      # Handle special keys
      case ch
      when 10, 13  # Enter (LF or CR)
        handle_enter
      when 8  # Backspace
        handle_backspace
      when 9  # Tab
        # TODO: tab completion
      when 32..126  # Printable characters
        if @current_line.length < @max_line_length
          @current_line += ch.chr
          @need_line_redraw = true
        end
      end
    end
  end

  # Show MicroRuby logo as ASCII art in history
  def show_logo
    # MicroRuby logo bitmap from picoruby-shell (RUBY_ENGINE == "mruby")
    # logo = [
    #   "01110000111001100011111100111111000011111100011111100011000011001111110011000011",
    #   "01111001111001100110000000110001100110000110011000110011000011001100011001100110",
    #   "01101111011001100110000000111111000110000110011111100011000011001111110000111100",
    #   "01100110011001100110000000110001100110000110011000110011000011001100011000011000",
    #   "01100000011001100011111100110001100011111100011000110001111110001111110000011000",
    #   "00000000000000000000000000000000000000000000000000000000000000000000000000000000"
    # ]
    logo = [
      "010001010011011100011001110010010111001010",
      "011011010100010010100101001010010100101010",
      "010101010100011100100101110010010111001010",
      "010001010100010010100101001010010100100100",
      "010001010011010010011001001001100111000100",
      "000000000000000000000000000000000000000000"
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
    logo.size.times do |y|
      break if logo[y+1].nil?
      logo[y].length.times do |x|
        if logo[y][x] == '1' && x > 0 && logo[y+1][x-1] == '0'
          logo[y+1][x-1] = '2'
        end
      end
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

  def getch
    while @input_buffer.empty?
      sleep_ms @frame_ms
    end
    char = @input_buffer.shift
    #Log.debug("[getch] Returning character: #{char} (#{char.chr})")
    char
  end

  def kbhit?
    !@input_buffer.empty?
  end

  def spawn_app(app_name)
    app_name = "/app/sample/mruby.app.rb" if app_name == "mruby.app"
    app_name = "/app/sample/lua.app.lua" if app_name == "lua.app"
    Log.info("Requesting spawn: #{app_name}")

    data = {
      "cmd" => "spawn",
      "app_name" => app_name
    }
    success = send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    if success
      @history << "Spawned: #{app_name}"
      Log.info("Spawn request sent successfully")
    else
      @history << "Error: Failed to spawn #{app_name}"
      Log.error("Failed to send spawn request")
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
    when "cd"
      cmd_cd(args)
    when "pwd"
      cmd_pwd
    when "ls"
      cmd_ls(args)
    when "cat"
      cmd_cat(args)
    when "irb"
      cmd_irb
    when "run"
      if args.empty?
        @history << "Error: run requires an app path"
        return
      end
      app_path = args.join(' ')
      spawn_app(app_path)
    when "help"
      @history << "Available commands:"
      @history << "  cd [path] - Change directory"
      @history << "  pwd - Print working directory"
      @history << "  ls [path] - List directory contents"
      @history << "  cat <file> - Display file contents"
      @history << "  irb - Interactive Ruby"
      @history << "  run <app_path> - Launch an application"
      @history << "  help - Show this help message"
    else
      @history << "Unknown command: #{cmd}"
      @history << "Type 'help' for available commands"
    end
  end

  def cmd_cd(args)
    Log.debug("cmd_cd called with args: #{args.inspect}")
    target_dir = if args.empty?
                   "/"  # cd without args goes to root
                 else
                   args[0]
                 end
    Log.debug("target_dir: #{target_dir}")

    # Resolve relative path
    new_dir = if target_dir.start_with?("/")
                target_dir  # Absolute path
              else
                # Relative path - append to current directory
                if @current_dir == "/"
                  "/" + target_dir
                else
                  @current_dir + "/" + target_dir
                end
              end
    Log.debug("new_dir (before normalize): #{new_dir}")

    # Normalize path (remove duplicate slashes)
    while new_dir.include?("//")
      new_dir = new_dir.gsub("//", "/")
    end
    # Remove trailing slash except for root
    new_dir = new_dir[0...-1] if new_dir.length > 1 && new_dir.end_with?("/")
    Log.debug("new_dir (after normalize): #{new_dir}")

    # Check if directory exists
    begin
      # Try to open directory to verify it exists
      Log.debug("Trying to open directory: #{new_dir}")
      dir = Dir.open(new_dir)
      dir.close
      @current_dir = new_dir
      Log.info("Changed to directory: #{@current_dir}")
    rescue => e
      Log.error("Error: #{e.message}")
      @history << "cd: #{target_dir}: #{e.message}"
    end
  end

  def cmd_pwd
    @history << @current_dir
  end

  def cmd_ls(args)
    path = if args.empty?
             @current_dir  # Use current directory
           elsif args[0].start_with?("/")
             args[0]  # Absolute path
           else
             # Relative path
             if @current_dir == "/"
               "/" + args[0]
             else
               @current_dir + "/" + args[0]
             end
           end

    begin
      dir = Dir.open(path)
      entries = []
      while (entry = dir.read)
        entries << entry
      end
      dir.close

      if entries.empty?
        @history << "(empty directory)"
      else
        entries.each do |entry|
          @history << "  #{entry}"
        end
      end
    rescue => e
      @history << "Error: #{e.message}"
    end
  end

  def cmd_cat(args)
    if args.empty?
      @history << "Error: cat requires a file path"
      return
    end

    path = args.join(' ')

    # Resolve relative path
    path = if path.start_with?("/")
             path  # Absolute path
           else
             # Relative path
             if @current_dir == "/"
               "/" + path
             else
               @current_dir + "/" + path
             end
           end

    begin
      file = File.open(path, "r")
      content = file.read
      file.close

      # Split content by newlines and add to history
      lines = content.split("\n")
      if lines.empty?
        @history << "(empty file)"
      else
        lines.each do |line|
          # Truncate long lines to avoid display issues
          max_display_width = (@user_area_width - 4) / @char_width
          if line.length > max_display_width
            @history << line[0...max_display_width] + "..."
          else
            @history << line
          end
        end
      end
    rescue => e
      @history << "Error: #{e.message}"
    end
  end

  def cmd_irb
    @history << "IRB mode - Type 'exit' or 'quit' to return"
    @need_full_redraw = true
    @irb_mode = true
    @prompt = "irb> "  # Change prompt for IRB mode
    @irb_sandbox = Sandbox.new
  end

  def irb_eval(script)
    # Skip empty input
    if script.empty?
      @need_full_redraw = true
      return
    end

    if script == "exit" || script == "quit"
      @irb_mode = false
      @irb_sandbox.terminate if @irb_sandbox
      @irb_sandbox = nil
      @history << "Exited IRB mode"
      @prompt = "> "
      @need_full_redraw = true
      return
    end

    # Capture stdout
    old_stdout = $stdout
    capturer = OutputCapturer.new
    $stdout = capturer

    begin
      # Try to compile and execute the script
      Log.debug("[IRB] Compiling: #{script}")
      if @irb_sandbox.compile("begin; _ = (#{script}); rescue => _; end; _")
        # Execute and get result
        Log.debug("[IRB] Executing...")
        executed = @irb_sandbox.execute
        Log.debug("[IRB] Executed: #{executed}")
        if executed
          Log.debug("[IRB] Waiting...")
          @irb_sandbox.wait(timeout: 5000)
          Log.debug("[IRB] Suspending...")
          @irb_sandbox.suspend
          Log.debug("[IRB] Getting result...")
          result = @irb_sandbox.result
          Log.debug("[IRB] Result: #{result.inspect}")

          # Get captured output
          output = capturer.get_output

          # Display captured output (without debug logs)
          output.each_line do |line|
            next if line.start_with?("[IRB]")
            @history << line.chomp
          end

          # Display result if not nil
          @history << "=> #{result.inspect}" unless result.nil?
        else
          @history << "Error: Execution failed"
        end
      else
        @history << "Error: Compilation failed"
      end
    rescue => e
      Log.error("[IRB] Exception: #{e.message}")
      @history << "Error: #{e.message}"
    ensure
      # Restore stdout
      $stdout = old_stdout
    end

    @need_full_redraw = true
  end

  def on_update()
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

  # Draw a single logo line with gradient background colors
  def draw_logo_line(x, y, logo_entry)
    data = logo_entry[:data]
    author_line = logo_entry[:author_line]
    margin = logo_entry[:margin] || ""
    is_last_line = logo_entry[:is_last_line]
    grad_slice_width = logo_entry[:grad_slice_width] || (data.length / LOGO_GRAD_COLORS.length)

    # Apply margin offset
    char_x = x + (margin.length * @char_width)

    data.length.times do |i|
      c = data[i]

      # Calculate gradient index (0 to LOGO_GRAD_COLORS.length - 1)
      grad_index = i / grad_slice_width
      grad_index = LOGO_GRAD_COLORS.length - 1 if grad_index >= LOGO_GRAD_COLORS.length

      # On the last line, author text takes priority over shadow
      if is_last_line && i < author_line.length && author_line[i] != ' '
        # Author text character (dark red on white background)
        @gfx.draw_text(char_x, y, author_line[i], LOGO_AUTHOR_COLOR, FmrbGfx::WHITE)
      else
        case c
        when '1'
          # Logo body: space with gradient color background
          @gfx.draw_text(char_x, y, " ", FmrbGfx::WHITE, LOGO_GRAD_COLORS[grad_index])
        when '2'
          # Shadow: space with darker gradient color background
          @gfx.draw_text(char_x, y, " ", FmrbGfx::WHITE, LOGO_SHADOW_COLORS[grad_index])
        # when '0' - skip (use existing background)
        end
      end
      char_x += @char_width
    end
  end

  def draw_prompt
    # Draw all history lines
    @history.each_with_index do |entry, i|
      x = @user_area_x0 + 2
      y = @user_area_y0 + 2 + (i * @char_height)

      if entry.is_a?(Hash) && entry[:type] == :logo_line
        draw_logo_line(x, y, entry)
      else
        # Normal text line
        @gfx.draw_text(x, y, entry.to_s, FmrbGfx::BLACK)
      end
    end

    # Draw current input line
    x = @user_area_x0 + 2
    y = @user_area_y0 + 2 + (@history.length * @char_height)
    full_line = @prompt + @current_line
    #Log.debug("draw_prompt: drawing '#{full_line}' (length=#{full_line.length}) at (#{x}, #{y})")
    @gfx.draw_text(x, y, full_line, FmrbGfx::BLACK)

    # Draw cursor (underline at end of input)
    cursor_x = x + (full_line.length * @char_width)
    cursor_y = y + @char_height - 1
    @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, FmrbGfx::BLACK)
  end

  def redraw_screen
    # Full redraw: Clear user area and redraw everything including logo
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                    @user_area_width, @user_area_height, FmrbGfx::WHITE)
    draw_window_frame
    draw_prompt
    @gfx.present
  end

  def redraw_input_line
    # Partial redraw: Only redraw the current input line
    x = @user_area_x0 + 2
    y = @user_area_y0 + 2 + (@history.length * @char_height)

    # Clear only the input line area
    @gfx.fill_rect(x, y, @user_area_width - 4, @char_height, FmrbGfx::WHITE)

    # Draw prompt and current input
    full_line = @prompt + @current_line
    @gfx.draw_text(x, y, full_line, FmrbGfx::BLACK)

    # Draw cursor (underline at end of input)
    cursor_x = x + (full_line.length * @char_width)
    cursor_y = y + @char_height - 1
    @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, FmrbGfx::GRAY)

    @gfx.present
  end

  def on_event(ev)
    #Log.debug("on_event: shell app")
    #p ev

    if ev[:type] == :key_down
      # Add character to input buffer for getch
      character = ev[:character] || 0
      if character > 0
        @input_buffer << character
      end

      # Don't call handle_key_input - let shell_task handle it via getch
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
    #Log.debug("keycode=#{keycode}, character=#{character} (#{character.chr if character != 0})")

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
        #Log.debug("Adding character: '#{char_str}' (ASCII #{character}), line was: '#{@current_line}'")
        @current_line += char_str
        #Log.debug("Line is now: '#{@current_line}' (length=#{@current_line.length})")
        @need_line_redraw = true
      else
        Log.warn("Warning: max line length (#{@max_line_length}) reached")
      end
    else
      Log.debug("Character #{character} not in printable range (32-126)")
    end
  end

  def handle_enter
    Log.info("Command: #{@current_line}")

    # Add current line to history
    @history << (@prompt + @current_line)

    # Execute command or IRB eval
    if @irb_mode
      irb_eval(@current_line)
    else
      cmd, args = parse_command(@current_line)
      if cmd
        execute_command(cmd, args)
      end
    end

    # Clear current line
    @current_line = ""

    # Check if we need to scroll
    max_lines = @user_area_height / @char_height
    while @history.length >= max_lines - 1
      # Remove oldest line to make room
      @history.shift
    end

    @need_full_redraw = true
  end

  def handle_backspace
    if @current_line.length > 0
      @current_line = @current_line[0...-1]
      @need_line_redraw = true
    end
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
