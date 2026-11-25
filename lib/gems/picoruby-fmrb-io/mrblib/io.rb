class IO
  def initialize(fd, mode = "r")
    _new(fd, mode)
  end
  # Class method: open an IO object
  def self.open(fd, mode = "r")
    new(fd, mode)
  end

  # Print arguments to IO with newlines, as mruby's IO#puts (mruby-io's
  # io_puts): each element of an Array on its own line, nested ones too, an
  # empty Array as a single newline, no argument as a newline, and no newline
  # added after a string that already ends with one. Each piece is written as
  # it comes, so nothing is joined into one large string first. Arrays
  # nested 16 deep print "[...]" instead (mruby's IO_PUTS_MAX_DEPTH, the guard
  # against an array that contains itself).
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
        _puts_ary(a, 0)
      else
        _puts_str(a)
      end
      i += 1
    end
    nil
  end

  private

  def _puts_str(obj)
    str = obj.to_s
    write(str)
    write("\n") unless str.end_with?("\n")
    nil
  end

  def _puts_ary(ary, depth)
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
        _puts_ary(e, depth + 1)
      else
        _puts_str(e)
      end
      i += 1
    end
    nil
  end

  public

  # Print arguments to IO without newlines
  def print(*args)
    args.each do |arg|
      write(arg.to_s)
    end
    nil
  end

  # Flush internal stdio buffer to the underlying storage.
  def flush
    _flush
    self
  end
end

# Note: Kernel#puts, Kernel#print, and Kernel#p are defined in picoruby-machine/mrblib/kernel.rb
# We don't redefine them here to avoid conflicts and ensure proper initialization order
