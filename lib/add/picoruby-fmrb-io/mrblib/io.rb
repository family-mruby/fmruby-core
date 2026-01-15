class IO
  def initialize(fd, mode = "r")
    _new(fd, mode)
  end
  # Class method: open an IO object
  def self.open(fd, mode = "r")
    new(fd, mode)
  end

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

# Note: Kernel#puts, Kernel#print, and Kernel#p are defined in picoruby-machine/mrblib/kernel.rb
# We don't redefine them here to avoid conflicts and ensure proper initialization order
