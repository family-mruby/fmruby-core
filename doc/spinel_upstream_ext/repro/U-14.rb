# The documented ext-kernel shape (docs/spin.md): entry methods plus an
# `if __FILE__ == $0` driver. CRuby runs the driver; a compiled program does
# not, because $0 is the binary's path while __FILE__ is the source path.
module K
  def self.twice(n)
    n * 2
  end
end

if __FILE__ == $0
  p K.twice(21)
end
