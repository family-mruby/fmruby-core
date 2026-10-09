# case/in: two shapes that the Prism-based mruby compiler (mruby-compiler /
# picoruby/mruby-compiler2) gets wrong at picoruby/mruby-compiler2 10408c3.
# Runs on CRuby, mruby and PicoRuby (plain `puts`, no other gems).
# CRuby 3.2+ prints "ok" four times.

def check(label, got, want)
  puts "#{got == want ? 'ok' : 'NG'} #{label}: got #{got.inspect}, want #{want.inspect}"
end

# 1. A class (or a range) as the value of a hash pattern never matches.
r = case {x: 1}
    in {x: Integer} then :int
    else :no
    end
check("in {x: Integer}", r, :int)

r = case {x: 1}
    in {x: 0..2} then :range
    else :no
    end
check("in {x: 0..2}", r, :range)

# 2. A pattern inside a block does not bind a local of the enclosing scope.
def bind_in_block
  pre = nil
  [{a: 1}].each do |m|
    case m
    in {a: pre} then nil
    end
  end
  pre
end
check("in {a: pre} inside a block", bind_in_block, 1)

def rightward_in_block
  a = nil
  [[1]].each { |x| x => [a] }
  a
end
check("x => [a] inside a block", rightward_in_block, 1)
