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

# Standard IO streams are defined after IO class is initialized
# Note: These constants are intentionally NOT created at load time
# to avoid initialization order issues. They should be created
# by C code in mrb_picoruby_fmrb_io_gem_init after defining IO class

# Define Kernel#puts and Kernel#print to delegate to $stdout
# This allows code to use puts/print at the top level without explicitly
# referencing IO class
module Kernel
  def puts(*args)
    $stdout.puts(*args)
  end

  def print(*args)
    $stdout.print(*args)
  end

  def p(*args)
    args.each { |arg| $stdout.puts(arg.inspect) }
    args.length <= 1 ? args.first : args
  end
end
