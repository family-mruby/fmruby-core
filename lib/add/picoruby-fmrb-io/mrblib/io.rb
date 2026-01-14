class IO
  # Print arguments to IO with newlines
  def puts(*args)
    if args.empty?
      write("\n")
    else
      args.each do |arg|
        str = arg.to_s
        write(str)
        write("\n") unless str.end_with?("\n")
      end
    end
    nil
  end

  # Print arguments to IO without newlines
  def print(*args)
    args.each do |arg|
      write(arg.to_s)
    end
    nil
  end

  # Flush output (no-op for now)
  def flush
    self
  end
end

# Define standard IO streams
STDIN  = IO.open(0, "r")
STDOUT = IO.open(1, "w")
STDERR = IO.open(2, "w")

# Set global variables for compatibility
$stdin  = STDIN
$stdout = STDOUT
$stderr = STDERR
