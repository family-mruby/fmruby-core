# Shell I/O classes - stdout/stdin wrappers for script execution

# puts as IO#puts does it (lib/gems/picoruby-fmrb-io, after mruby's
# mruby-io): each element of an Array on its own line, nested ones too, an
# empty Array as a newline, no argument as a newline, and no newline added
# after a string that already ends with one. The two stand-ins for $stdout
# below write through their own #write.
module ShellPuts
  def puts(*args)
    n = args.size
    if n == 0
      write("\n")
      return nil
    end
    i = 0
    while i < n
      a = args[i]
      if a.is_a?(Array)
        shell_puts_ary(a, 0)
      else
        shell_puts_str(a)
      end
      i += 1
    end
    nil
  end

  def shell_puts_str(obj)
    str = obj.to_s
    write(str)
    write("\n") unless str.end_with?("\n")
    nil
  end

  # 16 deep prints "[...]", as IO#puts (an array that contains itself).
  def shell_puts_ary(ary, depth)
    if depth >= 16
      write("[...]\n")
      return nil
    end
    n = ary.size
    if n == 0
      write("\n")
      return nil
    end
    i = 0
    while i < n
      e = ary[i]
      if e.is_a?(Array)
        shell_puts_ary(e, depth + 1)
      else
        shell_puts_str(e)
      end
      i += 1
    end
    nil
  end
end

# Simple output capturer for IRB
class OutputCapturer
  include ShellPuts

  def initialize
    @output = []
  end

  def write(str)
    @output << str
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
  include ShellPuts

  def initialize(shell_app)
    @shell = shell_app
    @remainder = ""
  end

  def write(str)
    @remainder += str.to_s
    flush_lines
    str.to_s.size
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
    while true
      ch = @shell.getch
      return nil if ch.nil?
      # Skip the shell's negative-encoded special keys (arrows, Home/End,
      # Delete): .chr would raise on them and a script asked for a character.
      return ch.chr if ch >= 0
    end
  end

  def read_nonblock(maxlen)
    result = ""
    while !@shell.input_buffer_empty? && result.length < maxlen
      ch = @shell.getch_nonblock
      break if ch.nil?
      result += ch.chr if ch >= 0
    end
    result.empty? ? nil : result
  end

  def flush
    self
  end
end
