# SPINEL_WARN_UNRESOLVED=1 spinel U-13.rb -c -o u13.c
#
# A user method named with `?`, called on a poly receiver, is reported as
# "unresolved call 'foo?' on poly receiver" although the generated C
# dispatches it correctly (a cls_id switch to W#foo?, NoMethodError default)
# and the program prints the same as CRuby. `hit` (no `?`) is not reported.
#
# Cause: the analysis records a builtin answer for every `?` name (bool) in
# poly_builtin_ty, so emit_poly_builtin_default tries a builtin arm for the
# switch's default. No builtin has `foo?`, the trial reaches the unresolved
# fallback (which prints the warning) and is then thrown away.
class W
  def initialize(id); @id = id; end
  def hit(x); x > 0; end
  def foo?; @id > 1; end
end
class B < W
end
class S < W
  def hit(x); x > 1; end
end
a = [B.new(1), S.new(2)]
w = a[1]
p w.hit(3)
p w.foo?
