# `+=` on a global / class variable String whose right side is a poly value
# (a symbol-keyed hash value) emits ill-typed C: the poly is passed to
# sp_str_concat unconverted. Locals, ivars and constants are fine.
h = { a: "ab", n: 1 }
$g = +"g"
$g += h[:a]
puts $g
class C
  @@v = +"v"
  def self.f(h)
    @@v += h[:a]
    @@v
  end
end
puts C.f(h)
