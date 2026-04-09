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

# Stdout wrapper that pushes output directly to ShellApp's history
class ShellStdout
  def initialize(shell_app)
    @shell = shell_app
    @remainder = ""
  end

  def write(str)
    @remainder += str.to_s
    flush_lines
    str.to_s.size
  end

  def puts(*args)
    if args.empty?
      write("\n")
    else
      i = 0
      while i < args.size
        str = args[i].to_s
        write(str)
        write("\n") unless str.end_with?("\n")
        i += 1
      end
    end
    nil
  end

  def print(*args)
    i = 0
    while i < args.size
      write(args[i].to_s)
      i += 1
    end
    nil
  end

  def flush
    unless @remainder.empty?
      @shell.append_output(@remainder)
      @remainder = ""
    end
    self
  end

  # Return and clear remainder without adding to history (used by ShellStdin#gets)
  def drain_remainder
    r = @remainder
    @remainder = ""
    r
  end

  private

  def flush_lines
    while (idx = @remainder.index("\n"))
      line = @remainder[0...idx]
      @shell.append_output(line)
      @remainder = @remainder[(idx + 1)..-1] || ""
    end
  end
end

# Stdin wrapper that reads from ShellApp's keyboard input buffer
class ShellStdin
  def initialize(shell_app)
    @shell = shell_app
  end

  def gets
    # Drain pending partial output (e.g. "Name? " from print) as input prompt prefix
    prompt_prefix = ""
    if $stdout.respond_to?(:drain_remainder)
      prompt_prefix = $stdout.drain_remainder
    end
    # Show the prompt prefix immediately
    @shell.redraw_script_input(prompt_prefix) unless prompt_prefix.empty?

    line_buf = ""
    loop do
      ch = @shell.getch
      return nil if ch.nil?

      case ch
      when 10, 13  # Enter
        @shell.append_output(prompt_prefix + line_buf)
        return line_buf + "\n"
      when 8  # Backspace
        if line_buf.length > 0
          line_buf = line_buf[0...-1]
          @shell.redraw_script_input(prompt_prefix + line_buf)
        end
      when 32..126
        line_buf += ch.chr
        @shell.redraw_script_input(prompt_prefix + line_buf)
      end
    end
  end

  def getch
    ch = @shell.getch
    return nil if ch.nil?
    ch.chr
  end

  def read_nonblock(maxlen)
    result = ""
    while !@shell.input_buffer_empty? && result.length < maxlen
      ch = @shell.getch_nonblock
      break if ch.nil?
      result += ch.chr
    end
    result.empty? ? nil : result
  end

  def flush
    self
  end
end

class ShellApp < FmrbApp
  # Gradient colors for logo (red -> magenta in RGB332)
  LOGO_GRAD_COLORS = [0xE0, 0xE1, 0xE2, 0xE3, 0xE3]
  # Shadow colors (dark gray for dark background: 0x49 = R2 G2 B1)
  LOGO_SHADOW_COLORS = [0x49, 0x49, 0x49, 0x49, 0x49]
  # Author text color (bright red: 0xE0 = R7 G0 B0)
  LOGO_AUTHOR_COLOR = 0xE3

  def initialize
    super()
    @current_line = ""
    @history = []  # Line history
    @cursor_x = 0
    @cursor_y = 0
    @char_width = 6
    @char_height = 8
    @current_dir = "/home"  # Virtual working directory (user-visible)

    # Filesystem root mapping: Dir.open uses OS paths, user sees "/"
    # Linux: "flash" (relative), ESP32: "/flash" (LittleFS mount)
    @fs_root = detect_fs_root
    @prompt = "> "
    @need_full_redraw = false   # Full screen redraw (includes logo)
    @need_line_redraw = false   # Only current input line redraw
    @max_line_length = 100  # Maximum input line length
    @input_buffer = []  # Character buffer for getch
    @frame_ms = 33
    @irb_mode = false  # IRB mode flag
    @irb_sandbox = nil  # Sandbox for IRB

    @bg_col = 0x00
    @ch_col = 0xFF

    @fg_sandbox = nil   # Foreground sandbox (set during run_foreground)
    @script_input_line = nil  # Current script input text (shown during fg execution)
    @ctrl_pressed = false  # Track Ctrl key state for Ctrl-C detection
    @jobs = []          # Background job list

    @cmd_history = []       # Command history
    @cmd_history_index = -1 # Current position in history (-1 = not browsing)
    @cmd_saved_line = ""    # Saved current line when browsing history

  end

  def on_create()
    @gfx.clear(@bg_col)
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
      # Check if app is still running
      break if !@running

      ch = getch
      break if ch.nil?  # getch returns nil when app is terminating

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
    while @input_buffer.empty? && @running
      sleep_ms @frame_ms
    end
    return nil if !@running  # App is terminating
    char = @input_buffer.shift
    #Log.debug("[getch] Returning character: #{char} (#{char.chr})")
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

  def append_output(line)
    @history << line
    max_lines = @user_area_height / @char_height
    while @history.length >= max_lines - 1
      @history.shift
    end
    @need_full_redraw = true
  end

  def redraw_script_input(partial_line)
    @script_input_line = partial_line
    x = @user_area_x0 + 2
    y = @user_area_y0 + 2 + (@history.length * @char_height)
    @gfx.fill_rect(x, y, @user_area_width - 4, @char_height, @bg_col)
    @gfx.draw_text(x, y, partial_line, @ch_col)
    cursor_x = x + (partial_line.length * @char_width)
    cursor_y = y + @char_height - 1
    @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, @ch_col)
    @gfx.present
  end

  def spawn_app(app_name)
    app_name = "/app/sample/mruby.app.rb" if app_name == "mruby.app"
    app_name = "/app/sample/lua.app.lua" if app_name == "lua.app"
    app_name = "/app/sample/basic.app.bas" if app_name == "basic.app"
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
      cmd_run(args)
    when "ps"
      cmd_ps
    when "kill"
      cmd_kill(args)
    when "help"
      @history << "Available commands:"
      @history << "  cd [path] - Change directory"
      @history << "  pwd - Print working directory"
      @history << "  ls [path] - List directory contents"
      @history << "  cat <file> - Display file contents"
      @history << "  irb - Interactive Ruby"
      @history << "  run <script> [&] - Run script"
      @history << "  run <script> > <file> - Redirect output"
      @history << "  ps - List processes"
      @history << "  kill <id> - Kill background job"
      @history << "  help - Show this help message"
    else
      @history << "Unknown command: #{cmd}"
      @history << "Type 'help' for available commands"
    end
  end

  # Filesystem root path for Dir.open (platform-dependent)
  def detect_fs_root
    @platform == :linux ? "flash" : "/flash"
  end

  # Convert virtual path (user-visible "/") to OS path for Dir.open
  def to_os_dir_path(virtual_path)
    if virtual_path == "/"
      @fs_root
    else
      "#{@fs_root}#{virtual_path}"
    end
  end

  # Convert virtual path to path for File.open (HAL adds "flash/" on Linux)
  # On ESP32: HAL expects path like "/app/..." (it prepends "/flash")
  # On Linux: HAL expects path like "app/..." (it prepends "flash/")
  def to_file_path(virtual_path)
    # Strip leading "/" - HAL adds the flash prefix
    virtual_path.start_with?("/") ? virtual_path[1..-1] : virtual_path
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

    # Check if directory exists using OS path
    begin
      os_path = to_os_dir_path(new_dir)
      Log.debug("Trying to open directory: #{os_path}")
      dir = Dir.open(os_path)
      dir.close
      @current_dir = new_dir
      Log.info("Changed to directory: #{@current_dir}")
    rescue => e
      Log.error("Error: #{e.message}")
      @history << "cd: #{target_dir}: No such directory"
    end
  end

  def cmd_pwd
    @history << @current_dir
  end

  def cmd_ls(args)
    virtual_path = if args.empty?
                     @current_dir
                   elsif args[0].start_with?("/")
                     args[0]
                   else
                     if @current_dir == "/"
                       "/" + args[0]
                     else
                       @current_dir + "/" + args[0]
                     end
                   end

    begin
      dir = Dir.open(to_os_dir_path(virtual_path))
      entries = []
      while (entry = dir.read)
        entries << entry
      end
      dir.close

      if entries.empty?
        @history << "(empty directory)"
      else
        entries.sort.each do |entry|
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

    # Resolve to virtual path first
    virtual_path = if path.start_with?("/")
                     path
                   else
                     if @current_dir == "/"
                       "/" + path
                     else
                       @current_dir + "/" + path
                     end
                   end

    begin
      file = File.open(to_file_path(virtual_path), "r")
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

  # --- Run command ---

  def cmd_run(args)
    if args.empty?
      @history << "Usage: run <script> [args] [&]"
      return
    end

    background = (args.last == "&")
    args.pop if background

    script_args, redirect_in, redirect_out, redirect_mode = parse_redirects(args)
    if script_args.empty?
      @history << "Usage: run <script> [args] [&]"
      return
    end

    script_path = resolve_script_path(script_args.shift)
    mode = detect_run_mode(script_path)

    case mode
    when :spawn
      spawn_app(script_path)
    when :sandbox
      if background
        run_background(script_path, script_args, redirect_out, redirect_mode)
      else
        run_foreground(script_path, script_args, redirect_in, redirect_out, redirect_mode)
      end
    end
  end

  def detect_run_mode(script_path)
    # Build TOML path by replacing last extension (no regex in mruby)
    parts = script_path.split(".")
    if parts.size > 1
      parts[-1] = "toml"
      toml_path = parts.join(".")
    else
      toml_path = script_path + ".toml"
    end
    file_path = to_file_path(toml_path)
    begin
      f = File.open(file_path, "r")
      f.close
      return :spawn
    rescue
      return :sandbox
    end
  end

  def resolve_script_path(path)
    if path.start_with?("/")
      path
    else
      if @current_dir == "/"
        "/" + path
      else
        @current_dir + "/" + path
      end
    end
  end

  def parse_redirects(args)
    script_args = []
    redirect_in = nil
    redirect_out = nil
    redirect_mode = nil

    i = 0
    while i < args.length
      case args[i]
      when "<"
        redirect_in = args[i + 1]
        i += 2
      when ">"
        redirect_out = args[i + 1]
        redirect_mode = :write
        i += 2
      when ">>"
        redirect_out = args[i + 1]
        redirect_mode = :append
        i += 2
      else
        script_args << args[i]
        i += 1
      end
    end

    [script_args, redirect_in, redirect_out, redirect_mode]
  end

  def run_foreground(script_path, script_args, redirect_in, redirect_out, redirect_mode)
    file_path = to_file_path(script_path)

    # Read script file first
    Log.info("run_foreground: loading #{file_path}")
    script_code = nil
    begin
      f = File.open(file_path, "r")
      script_code = f.read
      f.close
    rescue => e
      append_output("Error: #{e.message}")
      return
    end

    if script_code.nil? || script_code.empty?
      append_output("Error: empty script: #{script_path}")
      return
    end

    Log.info("run_foreground: script loaded (#{script_code.size} bytes)")

    # Setup stdout
    if redirect_out
      out_path = to_file_path(resolve_script_path(redirect_out))
      mode = redirect_mode == :append ? "a" : "w"
      stdout_obj = File.open(out_path, mode)
    else
      stdout_obj = ShellStdout.new(self)
    end

    # Setup stdin
    if redirect_in
      in_path = to_file_path(resolve_script_path(redirect_in))
      stdin_obj = File.open(in_path, "r")
    else
      stdin_obj = ShellStdin.new(self)
    end

    old_stdout = $stdout
    old_stdin = $stdin

    begin
      $stdout = stdout_obj
      $stdin = stdin_obj
      @fg_sandbox = Sandbox.new("run")
      Log.info("run_foreground: compiling script")
      unless @fg_sandbox.compile(script_code)
        $stdout = old_stdout
        append_output("Error: compile failed: #{script_path}")
        return
      end
      Log.info("run_foreground: executing")
      @fg_sandbox.execute
      @fg_sandbox.wait(timeout: nil)
      Log.info("run_foreground: finished (state=#{@fg_sandbox.state})")
      if error = @fg_sandbox.error
        $stdout = old_stdout
        append_output("Error: #{error.message}")
      end
    rescue => e
      $stdout = old_stdout
      append_output("Error: #{e.message}")
    ensure
      stdout_obj.flush if stdout_obj.respond_to?(:flush)
      stdout_obj.close if redirect_out && stdout_obj.respond_to?(:close)
      stdin_obj.close if redirect_in && stdin_obj.respond_to?(:close)
      $stdout = old_stdout
      $stdin = old_stdin
      @fg_sandbox = nil
      @script_input_line = nil
      @need_full_redraw = true
    end
  end

  def run_background(script_path, script_args, redirect_out, redirect_mode)
    file_path = to_file_path(script_path)

    # Read script file first (before spawning task)
    script_code = nil
    begin
      f = File.open(file_path, "r")
      script_code = f.read
      f.close
    rescue => e
      append_output("Error: #{e.message}")
      return
    end

    if script_code.nil? || script_code.empty?
      append_output("Error: empty script: #{script_path}")
      return
    end

    job_id = @jobs.length

    job_entry = {
      :name => script_path,
      :state => :running,
      :sandbox => nil,
      :task => nil
    }
    @jobs << job_entry

    app_self = self
    code = script_code
    rout = redirect_out
    rmode = redirect_mode

    job_entry[:task] = Task.new(name: "bg_#{job_id}", priority: 50) do
      if rout
        out_path = app_self.to_file_path(app_self.resolve_script_path(rout))
        mode = rmode == :append ? "a" : "w"
        stdout_obj = File.open(out_path, mode)
      else
        stdout_obj = ShellStdout.new(app_self)
      end

      old_stdout = $stdout
      old_stdin = $stdin
      begin
        $stdout = stdout_obj
        $stdin = nil
        sandbox = Sandbox.new("bg")
        job_entry[:sandbox] = sandbox
        if sandbox.compile(code)
          sandbox.execute
          sandbox.wait(timeout: nil)
        else
          app_self.append_output("[#{job_id}] Error: compile failed")
        end
        if error = sandbox.error
          app_self.append_output("[#{job_id}] Error: #{error.message}")
        end
      rescue => e
        app_self.append_output("[#{job_id}] Error: #{e.message}")
      ensure
        stdout_obj.flush if stdout_obj.respond_to?(:flush)
        stdout_obj.close if rout && stdout_obj.respond_to?(:close)
        $stdout = old_stdout
        $stdin = old_stdin
        job_entry[:state] = :done
        app_self.append_output("[#{job_id}] Done: #{job_entry[:name]}")
      end
    end

    @history << "[#{job_id}] Running: #{script_path}"
  end

  # --- Process / Job management ---

  STATE_NAMES = ["free", "init", "run", "suspend", "stop"]
  TYPE_NAMES = ["kernel", "system", "user"]

  def cmd_ps
    # Kernel-spawned processes (FmrbApp.ps)
    @history << " PID TYPE    STATE   NAME"
    procs = FmrbApp.ps
    i = 0
    while i < procs.size
      p = procs[i]
      state = STATE_NAMES[p[:state]] || "?"
      type = TYPE_NAMES[p[:type]] || "?"
      @history << "  #{p[:id]}  #{type.ljust(7)} #{state.ljust(7)} #{p[:name]}"
      i += 1
    end

    # Sandbox background jobs
    if @jobs.size > 0
      @history << " JOB STATE   NAME"
      j = 0
      while j < @jobs.size
        job = @jobs[j]
        @history << "  #{j}  #{job[:state].to_s.ljust(7)} #{job[:name]}"
        j += 1
      end
    end
  end

  def cmd_kill(args)
    if args.empty?
      @history << "Usage: kill <job_id>"
      return
    end
    job_id = args[0].to_i
    if job_id < @jobs.size
      job = @jobs[job_id]
      if job[:state] == :running && job[:sandbox]
        job[:sandbox].stop
        job[:state] = :killed
        @history << "[#{job_id}] Killed: #{job[:name]}"
      else
        @history << "[#{job_id}] Not running"
      end
    else
      @history << "kill: no such job: #{job_id}"
    end
  end

  # --- IRB ---

  def cmd_irb
    @history << "IRB mode - Type 'exit' or 'quit' to return"
    @need_full_redraw = true
    @irb_mode = true
    @prompt = "irb> "  # Change prompt for IRB mode
    @irb_sandbox = Sandbox.new
    @irb_sandbox.compile("_ = nil")
    @irb_sandbox.execute
    @irb_sandbox.wait(timeout: nil)
    @irb_sandbox.suspend
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
        # Author text character on background
        @gfx.draw_text(char_x, y, author_line[i], LOGO_AUTHOR_COLOR, @bg_col)
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
        @gfx.draw_text(x, y, entry.to_s, @ch_col)
      end
    end

    # Draw input line at bottom of history
    x = @user_area_x0 + 2
    y = @user_area_y0 + 2 + (@history.length * @char_height)
    if @fg_sandbox && @script_input_line
      # During foreground execution: redraw script input (e.g. "Name? user_input")
      @gfx.draw_text(x, y, @script_input_line, @ch_col)
      cursor_x = x + (@script_input_line.length * @char_width)
      cursor_y = y + @char_height - 1
      @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, @ch_col)
    elsif !@fg_sandbox
      # Normal shell prompt
      full_line = @prompt + @current_line
      @gfx.draw_text(x, y, full_line, @ch_col)
      cursor_x = x + (full_line.length * @char_width)
      cursor_y = y + @char_height - 1
      @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, @ch_col)
    end
  end

  def redraw_screen
    # Full redraw: Clear user area and redraw everything including logo
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                    @user_area_width, @user_area_height, @bg_col)
    draw_window_frame
    draw_prompt
    @gfx.present
  end

  def redraw_input_line
    # Partial redraw: Only redraw the current input line
    x = @user_area_x0 + 2
    y = @user_area_y0 + 2 + (@history.length * @char_height)

    # Clear only the input line area
    @gfx.fill_rect(x, y, @user_area_width - 4, @char_height, @bg_col)

    # Draw prompt and current input
    full_line = @prompt + @current_line
    @gfx.draw_text(x, y, full_line, @ch_col)

    # Draw cursor (underline at end of input)
    cursor_x = x + (full_line.length * @char_width)
    cursor_y = y + @char_height - 1
    @gfx.draw_line(cursor_x, cursor_y, cursor_x + @char_width - 1, cursor_y, @ch_col)

    @gfx.present
  end

  def on_event(ev)
    # Call parent class handler first (for close button, etc.)
    super(ev)

    if ev[:type] == :key_down
      character = ev[:character] || 0
      keycode = ev[:keycode] || 0
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

  def handle_tab
    # Tab completion: complete file/directory names
    parts = @current_line.split(" ")

    # Determine the prefix to complete
    if parts.size >= 2
      prefix = parts.last
    elsif parts.size == 1 && @current_line.end_with?(" ")
      prefix = ""
    else
      return  # Nothing to complete
    end

    # Split prefix into directory part and name part
    # e.g. "sub/foo" -> search_dir="sub", name_prefix="foo"
    # e.g. "foo"     -> search_dir="", name_prefix="foo"
    # e.g. ""        -> search_dir="", name_prefix=""
    last_slash = prefix.rindex("/")
    if last_slash
      dir_part = prefix[0..last_slash]   # includes trailing /
      name_prefix = prefix[(last_slash + 1)..-1] || ""
    else
      dir_part = ""
      name_prefix = prefix
    end

    # Resolve search directory
    if dir_part.empty?
      search_virtual = @current_dir
    elsif dir_part.start_with?("/")
      search_virtual = dir_part
    else
      search_virtual = @current_dir == "/" ? "/#{dir_part}" : "#{@current_dir}/#{dir_part}"
    end

    # List entries matching name_prefix
    begin
      dir = Dir.open(to_os_dir_path(search_virtual))
      candidates = []
      while (entry = dir.read)
        next if entry == "." || entry == ".."
        if name_prefix.empty? || entry.start_with?(name_prefix)
          # Check if entry is a directory
          entry_virtual = search_virtual == "/" ? "/#{entry}" : "#{search_virtual}/#{entry}"
          is_dir = false
          begin
            d = Dir.open(to_os_dir_path(entry_virtual))
            d.close
            is_dir = true
          rescue
          end
          candidates << { :name => entry, :dir => is_dir }
        end
      end
      dir.close
    rescue
      return
    end

    return if candidates.empty?

    if candidates.size == 1
      # Single match: complete it
      c = candidates[0]
      completed = dir_part + c[:name]
      completed += "/" if c[:dir]
      if prefix.empty?
        @current_line += completed
      else
        parts[-1] = completed
        @current_line = parts.join(" ")
      end
      @need_line_redraw = true
    else
      # Multiple matches: show candidates
      append_output(@prompt + @current_line)
      i = 0
      while i < candidates.size
        c = candidates[i]
        label = c[:dir] ? "#{c[:name]}/" : c[:name]
        append_output("  #{label}")
        i += 1
      end
      @need_full_redraw = true
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
