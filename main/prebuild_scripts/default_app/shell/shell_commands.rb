# Shell commands mixin - filesystem, run, process management, tab completion

module ShellCommandsMixin
  STATE_NAMES = ["free", "init", "run", "suspend", "stop"]
  TYPE_NAMES = ["kernel", "system", "user"]

  def spawn_app(app_name)
    app_name = "/app/tool/mruby.app.rb" if app_name == "mruby.app"
    app_name = "/app/tool/lua.app.lua" if app_name == "lua.app"
    app_name = "/app/tool/basic.app.bas" if app_name == "basic.app"
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

  # Convert virtual path to path for File.open (HAL adds "flash/" on Linux)
  # On ESP32: HAL expects path like "/app/..." (it prepends "/flash")
  # On Linux: HAL expects path like "app/..." (it prepends "flash/")
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

  # --- Tab completion ---

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
end
