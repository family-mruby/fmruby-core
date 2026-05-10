# Shell I/O classes - stdout/stdin wrappers for script execution

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
    while true
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
