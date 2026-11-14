# Coverage: begin/rescue/ensure/retry, custom exception classes, nested
# rescue (the fmrb_kernel.rb top-level rescue pattern).

class AppError < StandardError; end
class FatalError < StandardError
  def initialize(code)
    @code = code
    super("fatal #{code}")
  end

  def code
    @code
  end
end

def risky(n)
  raise AppError, "n too small" if n < 0
  raise FatalError.new(n) if n > 100
  n * 2
end

[-1, 5, 200].each do |n|
  begin
    r = risky(n)
    puts "ok #{n} -> #{r}"
  rescue AppError => ae
    # distinct names per arm so each specializes to its exception subclass
    # (see coverage/UNSUPPORTED.md U-2: one `e` bound by two arms of different
    # classes cannot specialize, so ae.code / subclass methods stay unresolved)
    puts "app-error #{n}: #{ae.message}"
  rescue FatalError => fe
    puts "fatal #{n}: #{fe.message} code=#{fe.code}"
  ensure
    puts "  (ensure #{n})"
  end
end

# retry with a bounded counter
attempts = 0
begin
  attempts += 1
  raise "transient" if attempts < 3
  puts "succeeded after #{attempts} attempts"
rescue => e
  if attempts < 3
    retry
  else
    puts "gave up: #{e.message}"
  end
end

# nested rescue (distinct names per arm; see UNSUPPORTED.md U-2)
begin
  begin
    raise AppError, "inner"
  rescue AppError => ie
    puts "caught inner: #{ie.message}"
    raise FatalError.new(9)
  end
rescue FatalError => oe
  puts "caught outer: #{oe.message} code=#{oe.code}"
end
