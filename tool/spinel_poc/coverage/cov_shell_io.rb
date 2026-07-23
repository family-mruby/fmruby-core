# Coverage: shell_io.rb-style `def puts(*args)` splat + delegation pattern.
# Exercises splat params, args.empty?, to_s, end_with?, respond_to?(literal).

class Sink
  def initialize
    @buf = ""
  end

  def write(s)
    @buf << s
  end

  def result
    @buf
  end

  # the real shell_io signature
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
end

s = Sink.new
s.puts("hello")
s.puts
s.puts("a", "b", "c")
s.puts("already\n")
s.print("x", "y")
s.puts(42)
s.puts([1, 2, 3])

# respond_to? with a literal method name (allowed by the coding rule)
puts "responds to write: #{s.respond_to?(:write)}"
puts "responds to bogus: #{s.respond_to?(:no_such_method)}"

# print the accumulated buffer with visible newlines
out = s.result
i = 0
shown = ""
while i < out.bytesize
  b = out.getbyte(i)
  shown << (b == 10 ? "\\n" : out[i, 1])
  i += 1
end
puts "buf=#{shown}"
