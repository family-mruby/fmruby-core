# Shell commands mixin - filesystem, run, process management, tab completion

module ShellCommandsMixin
  # Full words, matching the service rows the same `ps` prints under the host
  # (running / stopped / failed). The abbreviations read as a different
  # vocabulary in the same listing.
  STATE_NAMES = ["free", "init", "running", "suspended", "stopping"]
  TYPE_NAMES = ["kernel", "system", "user"]
  # Index into TYPE_NAMES. Only user tasks can be ended with "kill".
  PROC_TYPE_USER = 2
  CP_CHUNK_SIZE = 4096  # cp/mv copy chunk size in bytes
  # Comment-embedded toml fence. Same window the spawner reads.
  COMMENT_TOML_FENCE = "#---fmrb"
  COMMENT_TOML_SCAN_BYTES = 512

  # Talking to the service host (doc/user_extension/services/plan.md).
  # Services live inside one app and have no pid of their own, so they are
  # addressed by name over Pub/Sub: requests go to one well-known topic and
  # the answers come back on a topic named after this shell's pid.
  SVC_CTL_TOPIC = "svc/ctl"
  # The host's app name in the task list. Used to tell "no services on this
  # machine" (say nothing) from "the host is not answering" (say so).
  SVC_HOST_NAME = "Services"
  # How long to wait for an answer before giving up, in milliseconds of wall
  # clock (on_update calls are not a clock: the shell's own frame time moves).
  # A list, a stop or a start is answered within a frame.
  SVC_REPLY_TIMEOUT_MS = 1000
  # An enable may have to load the service first, and loading means running
  # the compiler over its file: on a Tab5 that is around a second, and the
  # shorter budget reported "services host not running" and then, a moment
  # later, the answer.
  SVC_ENABLE_TIMEOUT_MS = 5000

  # +open_path+ asks the kernel to hand that file to the new app as soon as it
  # exists (as a file_selected control message). Needed for a fullscreen app:
  # spawning it suspends this shell, so a deferred relay from here never runs.
  def spawn_app(app_name, open_path = nil)
    app_name = "/app/tool/mruby.app.rb" if app_name == "mruby.app"
    app_name = "/app/tool/lua.app.lua" if app_name == "lua.app"
    app_name = "/app/tool/basic.app.bas" if app_name == "basic.app"
    Log.info("Requesting spawn: #{app_name}")

    data = {
      "cmd" => "spawn",
      "app_name" => app_name
    }
    data["open_path"] = open_path if open_path
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
    when "less"
      cmd_less(args)
    when "mkdir"
      cmd_mkdir(args)
    when "rm"
      cmd_rm(args)
    when "cp"
      cmd_cp(args)
    when "mv"
      cmd_mv(args)
    when "irb"
      cmd_irb
    when "run"
      cmd_run(args)
    when "open"
      cmd_open(args)
    when "ps"
      cmd_ps
    when "kill"
      cmd_kill(args)
    when "kill_job"
      cmd_kill_job(args)
    when "svc"
      cmd_svc(args)
    when "edit"
      cmd_edit(args)
    when "create_app"
      cmd_create_app(args)
    when "color"
      cmd_color(args)
    when "logo"
      show_logo
    when "help"
      @history << "Available commands:"
      @history << "  cd [path] - Change directory"
      @history << "  pwd - Print working directory"
      @history << "  ls [path] - List directory contents"
      @history << "  cat <file> - Display file contents"
      @history << "  less <file> - View file with paging (q to quit)"
      @history << "  mkdir <dir> - Create directory"
      @history << "  rm <path...> - Remove files (or empty directories)"
      @history << "  cp <src> <dst> - Copy file"
      @history << "  mv <src> <dst> - Move/rename file"
      @history << "  edit [-f] <file> - Open file in the editor (-f: fullscreen)"
      @history << "  create_app <name> - Generate /app/usr/<name>.app.{rb,toml} from template"
      @history << "  irb - Interactive Ruby ($shell = this shell app)"
      @history << "  open <file> - Open a file with the app it is associated with"
      @history << "  run <script> [&] - Run script"
      @history << "  run <script> > <file> - Redirect output"
      @history << "  ps - List tasks and jobs"
      @history << "  kill <pid|service> - End a user task (PID from ps), or stop a service by name"
      @history << "  kill_job <id> - Stop a background job (JOB id from ps)"
      @history << "  svc list - Background services (also shown by ps)"
      @history << "  svc start|stop <name> - Run/stop a service for this session"
      @history << "  svc enable|disable <name> - ...and remember it across boots"
      @history << "  color [bg|text <colour>] - The shell's own two colours"
      @history << "  logo - Draw the PicoRuby logo"
      @history << "  help - Show this help message"
    else
      @history << "Unknown command: #{cmd}"
      @history << "Type 'help' for available commands"
    end
  end

  # The shell's own two colours.
  #
  # The system theme decides them until somebody says otherwise here; what
  # they say is kept in /home/colors.toml under [shell] and applies at once,
  # with no restart, because the shell holds its colours in variables rather
  # than in constants (the editor cannot, which is why its Colors dialog asks
  # for a reopen).
  def cmd_color(args)
    if args.empty?
      @history << "bg   #{FmrbColors.to_text(@bg_col)}"
      @history << "text #{FmrbColors.to_text(@ch_col)}"
      @history << "Usage: color bg|text <colour> | color names | color reset"
      @history << "A colour is a name (skyblue) or a number (0x1F)."
      return
    end
    sub = args[0]
    if sub == "names"
      line = ""
      i = 0
      while i < FmrbColors.palette_size
        n = FmrbColors.name_at(i)
        if line.length + n.length + 1 > 46
          @history << line
          line = ""
        end
        line = line.length == 0 ? n : line + " " + n
        i += 1
      end
      @history << line if line.length > 0
      return
    end
    if sub == "reset"
      FmrbColors.clear("shell")
      @bg_col = FmrbConst::THEME_WINDOW_BG
      @ch_col = FmrbConst::THEME_TEXT
      apply_colors
      @history << "colours back to the theme"
      return
    end
    if sub != "bg" && sub != "text"
      @history << "Usage: color bg|text <colour> | color names | color reset"
      return
    end
    if args.size < 2
      @history << "Usage: color #{sub} <colour>   (color names lists them)"
      return
    end
    value = FmrbColors.to_color(args[1])
    if value.nil?
      @history << "not a colour: #{args[1]} (try `color names`, or 0x00 to 0xFF)"
      return
    end
    if sub == "bg"
      @bg_col = value
    else
      @ch_col = value
    end
    unless FmrbColors.set("shell", sub, value)
      @history << "(could not write /home/colors.toml -- this session only)"
    end
    apply_colors
    @history << "#{sub} = #{FmrbColors.to_text(value)}"
  end

  # The widgets carry their own copy of the background, and the update loop
  # owns the repaint, so both have to be told.
  def apply_colors
    if @ui
      @ui.bg = @bg_col
      @ui.invalidate_all
    end
    @need_full_redraw = true
    nil
  end

  # Filesystem root - "/" is the user-facing root. The HAL resolver maps it
  # to LittleFS (ESP32) or to the local "flash" directory (Linux).
  def detect_fs_root
    "/"
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

    # Normalize path: resolve "." / ".." segments and collapse "//" runs so
    # the result is a clean absolute path. Done in Ruby because some VFS
    # backings (e.g. LittleFS) do not understand ".." on opendir.
    resolved = []
    new_dir.split("/").each do |seg|
      next if seg.empty? || seg == "."
      if seg == ".."
        resolved.pop
      else
        resolved << seg
      end
    end
    new_dir = resolved.empty? ? "/" : "/" + resolved.join("/")
    Log.debug("new_dir (after normalize): #{new_dir}")

    # Existence check via opendir - the HAL recognises virtual mount-point
    # parents like "/mnt" so this works for them too.
    begin
      Log.debug("Trying to open directory: #{new_dir}")
      dir = Dir.open(new_dir)
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
      dir = Dir.open(virtual_path)
      entries = []
      while (entry = dir.read)
        entries << entry
      end
      dir.close

      if entries.empty?
        @history << "(empty directory)"
      else
        entries.sort.each do |entry|
          entry_virtual = virtual_path == "/" ? "/#{entry}" : "#{virtual_path}/#{entry}"
          is_dir = false
          begin
            d = Dir.open(entry_virtual)
            d.close
            is_dir = true
          rescue
          end
          label = is_dir ? "#{entry}/" : entry
          @history << "  #{label}"
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
      file = File.open(virtual_path, "r")
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

  # Resolve user-supplied path against @current_dir into a normalized virtual
  # path (always absolute, no trailing slash except for root, no "//").
  def resolve_virtual_path(path)
    v = if path.start_with?("/")
          path
        elsif @current_dir == "/"
          "/" + path
        else
          @current_dir + "/" + path
        end
    while v.include?("//")
      v = v.gsub("//", "/")
    end
    v = v[0...-1] if v.length > 1 && v.end_with?("/")
    v
  end

  def virtual_basename(virtual_path)
    return "" if virtual_path.nil? || virtual_path.empty?
    idx = virtual_path.rindex("/")
    idx ? virtual_path[(idx + 1)..-1] : virtual_path
  end

  def virtual_is_dir?(virtual_path)
    d = Dir.open(virtual_path)
    d.close
    true
  rescue
    false
  end

  def virtual_file_exists?(virtual_path)
    File.exist?(virtual_path)
  rescue
    false
  end

  def cmd_mkdir(args)
    if args.empty?
      @history << "Usage: mkdir <dir>"
      return
    end
    target = resolve_virtual_path(args[0])
    if virtual_is_dir?(target)
      @history << "mkdir: #{args[0]}: already exists"
      return
    end
    begin
      Dir.mkdir(target)
      Log.info("mkdir: #{target}")
    rescue => e
      @history << "mkdir: #{args[0]}: #{e.message}"
    end
  end

  def cmd_rm(args)
    if args.empty?
      @history << "Usage: rm <path...>"
      return
    end
    args.each do |arg|
      target = resolve_virtual_path(arg)
      if virtual_is_dir?(target)
        # Try to remove as empty directory
        begin
          Dir.delete(target)
          Log.info("rm: dir #{target}")
        rescue => e
          @history << "rm: #{arg}: #{e.message} (not empty?)"
        end
      elsif virtual_file_exists?(target)
        begin
          File.delete(target)
          Log.info("rm: file #{target}")
        rescue => e
          @history << "rm: #{arg}: #{e.message}"
        end
      else
        @history << "rm: #{arg}: No such file or directory"
      end
    end
  end

  # Copy a single file in CP_CHUNK_SIZE chunks. Returns true on success.
  def copy_file_chunked(src_virtual, dst_virtual)
    src = nil
    dst = nil
    begin
      src = File.open(src_virtual, "r")
      dst = File.open(dst_virtual, "w")
      while true
        chunk = src.read(CP_CHUNK_SIZE)
        break if chunk.nil? || chunk.length == 0
        dst.write(chunk)
      end
      true
    rescue => e
      @history << "Error: #{e.message}"
      false
    ensure
      src.close if src
      dst.close if dst
    end
  end

  # Resolve cp/mv destination: if dst is an existing directory,
  # append basename(src). Returns the final destination virtual path.
  def resolve_cp_dst(src_virtual, dst_arg)
    dst = resolve_virtual_path(dst_arg)
    if virtual_is_dir?(dst)
      base = virtual_basename(src_virtual)
      dst = dst == "/" ? "/" + base : dst + "/" + base
    end
    dst
  end

  def cmd_cp(args)
    if args.length < 2
      @history << "Usage: cp <src> <dst>"
      return
    end
    src = resolve_virtual_path(args[0])
    unless virtual_file_exists?(src)
      @history << "cp: #{args[0]}: No such file"
      return
    end
    if virtual_is_dir?(src)
      @history << "cp: #{args[0]}: is a directory (not supported)"
      return
    end
    dst = resolve_cp_dst(src, args[1])
    if src == dst
      @history << "cp: source and destination are the same"
      return
    end
    if copy_file_chunked(src, dst)
      Log.info("cp: #{src} -> #{dst}")
    end
  end

  def cmd_mv(args)
    if args.length < 2
      @history << "Usage: mv <src> <dst>"
      return
    end
    src = resolve_virtual_path(args[0])
    unless virtual_file_exists?(src)
      @history << "mv: #{args[0]}: No such file"
      return
    end
    if virtual_is_dir?(src)
      @history << "mv: #{args[0]}: is a directory (not supported)"
      return
    end
    dst = resolve_cp_dst(src, args[1])
    if src == dst
      @history << "mv: source and destination are the same"
      return
    end
    begin
      File.rename(src, dst)
      Log.info("mv: #{src} -> #{dst}")
    rescue => e
      @history << "mv: #{e.message}"
    end
  end

  # Read entire file in chunks, splitting into wrapped lines for the less
  # viewport. Returns an array of strings, each with length <= max_chars.
  def read_file_wrapped(virtual_path, max_chars)
    lines = []
    leftover = ""
    file = nil
    begin
      file = File.open(virtual_path, "r")
      while true
        chunk = file.read(CP_CHUNK_SIZE)
        break if chunk.nil? || chunk.length == 0
        leftover += chunk
        # Process complete lines from leftover
        while (nl = leftover.index("\n"))
          line = leftover[0...nl]
          leftover = leftover[(nl + 1)..-1] || ""
          append_wrapped(lines, line, max_chars)
        end
      end
      # Final partial line (no trailing newline)
      append_wrapped(lines, leftover, max_chars) unless leftover.empty?
    ensure
      file.close if file
    end
    lines
  end

  def append_wrapped(lines, line, max_chars)
    if line.empty?
      lines << ""
      return
    end
    pos = 0
    while pos < line.length
      lines << line[pos, max_chars]
      pos += max_chars
    end
  end

  def cmd_less(args)
    if args.empty?
      @history << "Usage: less <file>"
      return
    end
    target = resolve_virtual_path(args[0])
    unless virtual_file_exists?(target)
      @history << "less: #{args[0]}: No such file"
      return
    end
    if virtual_is_dir?(target)
      @history << "less: #{args[0]}: is a directory"
      return
    end
    begin
      lines = read_file_wrapped(target, @max_chars)
    rescue => e
      @history << "less: #{e.message}"
      return
    end
    enter_less_mode(args[0], lines)
  end

  # --- Edit command ---
  #
  # `edit [-f] <file>` spawns the default editor and forwards the resolved file
  # path to it. Mirrors system_desktop's file_manager edit flow: spawn first,
  # poll for the editor PID over a few on_update ticks, then send a
  # file_select_result via the kernel so the editor opens the file.

  def cmd_edit(args)
    # -f opens the fullscreen editor ("serious mode"): same editor, whole
    # screen, other apps suspended.
    fullscreen = false
    rest = []
    args.each do |a|
      if a == "-f"
        fullscreen = true
      else
        rest << a
      end
    end
    if rest.empty?
      @history << "Usage: edit [-f] <file>"
      return
    end
    virtual_path = resolve_script_path(rest.join(' '))
    file_path = virtual_path

    # Create an empty file when the target does not exist so the editor can
    # open it as a fresh buffer rather than reporting "file not found".
    unless File.exist?(file_path)
      begin
        File.open(file_path, "w") { |f| }
        @history << "edit: created #{virtual_path}"
      rescue => e
        @history << "edit: cannot create #{virtual_path}: #{e.message}"
        return
      end
    end

    if fullscreen
      # The kernel forwards the path: this shell is suspended the moment the
      # fullscreen editor comes up, so tick_pending_edit would never fire.
      spawn_app("default/editor_fs", file_path)
    else
      spawn_app("default/editor")
      @pending_edit_path = file_path
      @pending_edit_counter = 3
    end
  end

  # Drives the deferred file-path forwarding to the just-spawned editor.
  # Called once per on_update from shell.app.rb.
  def tick_pending_edit
    return unless @pending_edit_path && @pending_edit_counter
    @pending_edit_counter -= 1
    return if @pending_edit_counter > 0

    procs = FmrbApp.ps
    editor = nil
    if procs
      i = procs.size - 1
      while i >= 0
        p = procs[i]
        if p[:name] == "FM-Editor" && p[:state] == FmrbConst::PROC_STATE_RUNNING
          editor = p
          break
        end
        i -= 1
      end
    end

    if editor
      data = {
        "cmd" => "file_select_result",
        "target_pid" => editor[:id],
        "path" => @pending_edit_path,
        "mode" => "open"
      }
      send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
      Log.info("Sent file_selected to Editor PID #{editor[:id]}: #{@pending_edit_path}")
    else
      @history << "edit: editor process not found"
      @need_full_redraw = true
    end

    @pending_edit_path = nil
    @pending_edit_counter = nil
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
    file_path = toml_path
    begin
      f = File.open(file_path, "r")
      f.close
      return :spawn
    rescue
      # No sidecar. A .rb may still declare its spawn attributes in a comment
      # fence (doc/multivm_app/plan.md 3.2); that fence only means anything to
      # the spawner, so such a file is an app rather than a sandbox script.
      return comment_toml?(script_path) ? :spawn : :sandbox
    end
  end

  # True when the first lines of a .rb open the "#---fmrb" attribute fence.
  # Mirrors the spawner's rule: comments and blank lines may precede it, the
  # first line of real code ends the search.
  def comment_toml?(path)
    return false unless path.end_with?(".rb")
    head = nil
    begin
      f = File.open(path, "r")
      head = f.read(COMMENT_TOML_SCAN_BYTES)
      f.close
    rescue
      return false
    end
    return false unless head
    head.split("\n").each do |line|
      return true if line.start_with?(COMMENT_TOML_FENCE)
      stripped = line.strip
      next if stripped.empty? || stripped.start_with?("#")
      return false
    end
    false
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
    file_path = script_path

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
      out_path = resolve_script_path(redirect_out)
      mode = redirect_mode == :append ? "a" : "w"
      stdout_obj = File.open(out_path, mode)
    else
      stdout_obj = ShellStdout.new(self)
    end

    # Setup stdin
    if redirect_in
      in_path = resolve_script_path(redirect_in)
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
    file_path = script_path

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
        out_path = app_self.resolve_script_path(rout)
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
        # Preserve :killed if cmd_kill_job already marked it; otherwise mark done
        job_entry[:state] = :done unless job_entry[:state] == :killed
        app_self.append_output("[#{job_id}] #{job_entry[:state] == :killed ? 'Stopped' : 'Done'}: #{job_entry[:name]}")
      end
    end

    @history << "[#{job_id}] Running: #{script_path}"
  end

  # Open a file the way the association table says to (FmrbAssoc): "run"
  # spawns the file itself, "edit" opens the editor, and an app path spawns
  # that app with the file handed to it. `run` stays what it always was -- the
  # user naming the action -- and this is the one that asks the table.
  def cmd_open(args)
    if args.empty?
      @history << "Usage: open <file>   (uses /etc/associations.toml)"
      return
    end
    arg = args[0]
    path = if arg.start_with?("/")
             arg
           elsif @current_dir == "/"
             "/#{arg}"
           else
             "#{@current_dir}/#{arg}"
           end
    unless File.exist?(path)
      @history << "open: no such file: #{args[0]}"
      return
    end
    action = FmrbAssoc.resolve(path)
    if action == FmrbAssoc::RUN
      spawn_app(path)
    elsif action == FmrbAssoc::EDIT
      spawn_app("default/editor", path)
    else
      spawn_app(action, path)
    end
  end

  # --- Process / Job management ---

  def cmd_ps
    # Kernel-managed tasks (FmrbApp.ps). Type "user" ones can be ended with
    # `kill <pid>`; kernel and system tasks hold the machine up and cannot.
    @history << "Tasks (kernel-managed, 'user' can be killed):"
    @history << " PID TYPE    STATE     NAME"
    procs = FmrbApp.ps
    my_pid = FmrbApp.pid
    i = 0
    while i < procs.size
      p = procs[i]
      state = STATE_NAMES[p[:state]] || "?"
      type = TYPE_NAMES[p[:type]] || "?"
      mark = p[:id] == my_pid ? " (this shell)" : ""
      @history << "  #{p[:id]}  #{type.ljust(7)} #{state.ljust(9)} #{p[:name]}#{mark}"
      i += 1
    end

    # Sandbox background jobs. JOB ids are shell-local; use `kill_job <id>`.
    @history << "Jobs (sandbox bg, use 'kill_job <id>'):"
    if @jobs.size == 0
      @history << "  (none)"
    else
      @history << " JOB STATE   NAME"
      j = 0
      while j < @jobs.size
        job = @jobs[j]
        @history << "  #{j}  #{job[:state].to_s.ljust(7)} #{job[:name]}"
        j += 1
      end
    end

    # The services living inside the host task, as child rows.
    #
    # Asked for here and printed from on_control when the answers arrive --
    # the same shape as `kill`, and for the same reason: this runs on the
    # app's own callback, so waiting here would stop the very loop the answer
    # has to come back through. Last in the output for that reason too: the
    # rows land a frame or two after everything above them. Nothing is asked
    # on a machine with no host, so a plain `ps` there says nothing new.
    if svc_host_running?
      # Kept inside the shell's 47-column default window: a longer line wraps
      # and the child rows below it stop reading as a list.
      @history << "Services (kill <name> stops one):"
      svc_request("list", nil, false)
    end
  end

  # Ask a running app to end. The kernel does the asking (it is the one that
  # knows who requested it, and it refuses anything but a user task and the
  # requester itself); the checks here are only so a mistake is answered at
  # once instead of a tick later. The answer to the request arrives as a
  # "kill_result" control message (on_control in shell.app.rb).
  def cmd_kill(args)
    if args.empty?
      @history << "Usage: kill <pid>      (PID from ps, type user)"
      @history << "       kill <service>  (name from ps, stops it in the host)"
      return
    end
    # A pid is a number; anything else is a service name. Services have no pid
    # of their own, so this is the only way to address one -- and killing the
    # host itself is still `kill <pid>`, which is the difference the two forms
    # are meant to keep.
    unless numeric?(args[0])
      cmd_kill_service(args[0])
      return
    end
    pid = args[0].to_i
    procs = FmrbApp.ps
    target = nil
    i = 0
    while i < procs.size
      target = procs[i] if procs[i][:id] == pid
      i += 1
    end
    if target.nil?
      @history << "kill: no such task: #{args[0]}"
      return
    end
    if target[:type] != PROC_TYPE_USER
      @history << "kill: #{TYPE_NAMES[target[:type]] || "?"} tasks cannot be killed: #{target[:name]}"
      return
    end
    if pid == FmrbApp.pid
      @history << "kill: this is the shell itself; close its window instead"
      return
    end
    data = { "cmd" => "kill", "pid" => pid }
    sent = send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    @history << "kill: could not reach the kernel" unless sent
  end

  def numeric?(str)
    return false if str.nil? || str.empty?
    i = 0
    while i < str.length
      b = str.getbyte(i)
      return false if b < 48 || b > 57
      i += 1
    end
    true
  end

  def cmd_kill_service(name)
    svc_request("stop", name, true)
  end

  # `svc list` / `svc start` / `svc enable` / `svc disable`.
  #
  # Two pairs, and the difference between them is the point:
  #   start / stop (= kill <name>) act on THIS session. A reboot undoes them.
  #   enable / disable are remembered in /home/services_state.toml and so
  #     survive a reboot. disable also stops it now; enable also starts it now.
  def cmd_svc(args)
    sub = args.empty? ? "" : args[0]
    case sub
    when "list"
      svc_request("list", nil, true)
    when "start", "stop", "enable", "disable"
      if args.size < 2
        @history << "Usage: svc #{sub} <name>   (name from ps or svc list)"
        return
      end
      svc_request(sub, args[1], true)
    else
      @history << "Usage: svc list"
      @history << "       svc start <name>      - run it again, this session"
      @history << "       svc enable <name>     - and at every boot from now"
      @history << "       svc disable <name>    - stop it, this boot and after"
      @history << "  (kill <name> stops one for this session only)"
    end
  end

  # ---- Service host: requests and the answers they come back as ----
  #
  # The host answers on a topic named after this pid, so several tools can ask
  # at once and nobody has to be told who asked.

  def svc_reply_topic
    @svc_reply_topic = "svc/re/#{FmrbApp.pid}" unless @svc_reply_topic
    @svc_reply_topic
  end

  # Is the host up at all? Asked before `ps` sends anything, so a machine with
  # no services stays quiet instead of reporting a timeout every time.
  def svc_host_running?
    procs = FmrbApp.ps
    return false unless procs
    i = 0
    while i < procs.size
      p = procs[i]
      return true if p[:name] == SVC_HOST_NAME && p[:state] != FmrbConst::PROC_STATE_FREE
      i += 1
    end
    false
  end

  # +loud+ decides what happens when nothing answers: a command the user typed
  # about services should say so, a `ps` that threw in a list request should
  # not turn into an error report.
  def svc_request(cmd, name, loud)
    unless @svc_subscribed
      subscribe(svc_reply_topic)
      @svc_subscribed = true
    end
    req = { "cmd" => cmd, "reply_to" => svc_reply_topic }
    req["name"] = name if name
    unless publish(SVC_CTL_TOPIC, req)
      @history << "svc: could not reach the kernel"
      return
    end
    budget = cmd == "enable" ? SVC_ENABLE_TIMEOUT_MS : SVC_REPLY_TIMEOUT_MS
    @svc_wait_until = Machine.board_millis + budget
    @svc_wait_loud = loud
    @svc_wait_cmd = cmd
  end

  # Called once per on_update from shell.app.rb, like tick_pending_edit.
  # A subscribe that has not taken effect yet, or a host that died between the
  # process list and the request, would otherwise leave the user waiting for
  # an answer that is never coming.
  def tick_svc_wait
    return unless @svc_wait_until
    return if Machine.board_millis < @svc_wait_until
    loud = @svc_wait_loud
    @svc_wait_until = nil
    @svc_wait_loud = nil
    @svc_wait_cmd = nil
    append_output("svc: services host not running") if loud
  end

  # One answer from the host. "svc" rows arrive one per message (a message
  # payload is 176 bytes, which the whole list does not fit in), and "svc_end"
  # closes the list.
  def handle_svc_reply(msg)
    @svc_wait_until = nil
    @svc_wait_loud = nil
    case msg["cmd"]
    when "svc"
      s = msg["svc"]
      return unless s
      # ASCII only: the shell measures its columns in 6px cells, and a
      # box-drawing character would be drawn 8px wide by the fallback font,
      # putting the rest of the row out of step with the ones above it.
      counts = "t=#{s["ticks"]} ev=#{s["events"]} err=#{s["errors"]}"
      counts = "#{counts} wk=#{s["wakes"]}" if s["wakes"] && s["wakes"] != 0
      line = "  +- #{s["name"].to_s.ljust(12)} #{s["origin"]} " \
             "#{s["state"].to_s.ljust(8)} #{counts}"
      append_output(line)
    when "svc_end"
      append_output("  (no services)") if msg["count"] == 0
    when "svc_result"
      if msg["ok"]
        append_output("svc: #{msg["name"]} ok")
      else
        append_output("svc: #{msg["name"]} refused (#{msg["err"]})")
      end
    end
  end

  # Kernel answer to cmd_kill. "ok" means the app was asked; it ends itself
  # when it next reads its queue, so `ps` is what confirms it is gone.
  def handle_kill_result(msg)
    pid = msg["pid"]
    if msg["ok"]
      append_output("kill: asked PID #{pid} (#{msg["info"]}) to end")
    else
      append_output("kill: PID #{pid} refused (#{msg["info"]})")
    end
  end

  def cmd_kill_job(args)
    if args.empty?
      @history << "Usage: kill_job <job_id>"
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
      @history << "kill_job: no such job: #{job_id}"
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
      dir = Dir.open(search_virtual)
      candidates = []
      while (entry = dir.read)
        next if entry == "." || entry == ".."
        if name_prefix.empty? || entry.start_with?(name_prefix)
          # Check if entry is a directory
          entry_virtual = search_virtual == "/" ? "/#{entry}" : "#{search_virtual}/#{entry}"
          is_dir = false
          begin
            d = Dir.open(entry_virtual)
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
      # Multiple matches: extend to longest common prefix if any
      common = candidates[0][:name]
      i = 1
      while i < candidates.size
        name = candidates[i][:name]
        # Shrink common to the shared prefix with name
        max_len = common.length < name.length ? common.length : name.length
        j = 0
        while j < max_len && common[j] == name[j]
          j += 1
        end
        common = common[0...j]
        break if common.empty?
        i += 1
      end

      if common.length > name_prefix.length
        # Extend the input up to the common prefix (no trailing slash, since it's not a complete name)
        completed = dir_part + common
        if prefix.empty?
          @current_line += completed
        else
          parts[-1] = completed
          @current_line = parts.join(" ")
        end
        @need_line_redraw = true
      else
        # No further extension possible: show candidates
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

  # --- create_app: generate app from template ---

  TEMPLATE_RB_PATH   = "/usr/share/template/app.rb.template"
  TEMPLATE_TOML_PATH = "/usr/share/template/app.toml.template"

  def cmd_create_app(args)
    if args.empty?
      @history << "Usage: create_app <name>"
      @history << "  Generates /app/usr/<name>.app.{rb,toml} from template"
      return
    end

    name = args[0]
    unless valid_app_name?(name)
      @history << "Error: name must be lowercase letters/digits/underscore,"
      @history << "       starting with a letter (e.g. hello, my_clock)"
      return
    end

    rb_virtual   = "/app/usr/#{name}.app.rb"
    toml_virtual = "/app/usr/#{name}.app.toml"

    if app_file_exists?(rb_virtual)
      @history << "Error: #{rb_virtual} already exists"
      return
    end
    if app_file_exists?(toml_virtual)
      @history << "Error: #{toml_virtual} already exists"
      return
    end

    return unless ensure_app_usr_dir

    rb_tmpl   = read_template(TEMPLATE_RB_PATH)
    toml_tmpl = read_template(TEMPLATE_TOML_PATH)
    return if rb_tmpl.nil? || toml_tmpl.nil?

    class_name = to_class_name(name)
    title      = to_title_case(name)

    rb_content   = substitute_template(rb_tmpl, name, class_name, title)
    toml_content = substitute_template(toml_tmpl, name, class_name, title)

    return unless write_text_file(rb_virtual, rb_content)
    return unless write_text_file(toml_virtual, toml_content)

    @history << "Created: #{rb_virtual}"
    @history << "Created: #{toml_virtual}"
    @history << "Tip: edit it with `edit #{rb_virtual}`"
  end

  def valid_app_name?(name)
    return false if name.nil? || name.length == 0
    first = name[0]
    return false unless first >= 'a' && first <= 'z'
    i = 0
    while i < name.length
      c = name[i]
      ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'
      return false unless ok
      i += 1
    end
    true
  end

  def to_class_name(snake)
    parts = snake.split('_')
    result = ""
    parts.each do |p|
      next if p.length == 0
      result += p[0].upcase + (p.length > 1 ? p[1..-1] : "")
    end
    result + "App"
  end

  def to_title_case(snake)
    parts = snake.split('_')
    out = []
    parts.each do |p|
      next if p.length == 0
      out << p[0].upcase + (p.length > 1 ? p[1..-1] : "")
    end
    out.join(' ')
  end

  def app_file_exists?(virtual_path)
    begin
      f = File.open(virtual_path, "r")
      f.close
      return true
    rescue
      return false
    end
  end

  def ensure_app_usr_dir
    begin
      d = Dir.open("/app/usr")
      d.close
      return true
    rescue
      # Not present, fall through to mkdir
    end
    begin
      Dir.mkdir("/app/usr")
      return true
    rescue => e
      @history << "Error: cannot create /app/usr: #{e.message}"
      return false
    end
  end

  def read_template(virtual_path)
    begin
      f = File.open(virtual_path, "r")
      content = f.read
      f.close
      return content
    rescue => e
      @history << "Error: cannot read template #{virtual_path}: #{e.message}"
      return nil
    end
  end

  def substitute_template(template, name, class_name, title)
    out = template
    out = out.gsub("{{name}}", name)
    out = out.gsub("{{class}}", class_name)
    out = out.gsub("{{title}}", title)
    out
  end

  def write_text_file(virtual_path, content)
    begin
      f = File.open(virtual_path, "w")
      f.write(content)
      f.close
      return true
    rescue => e
      @history << "Error: cannot write #{virtual_path}: #{e.message}"
      return false
    end
  end
end
