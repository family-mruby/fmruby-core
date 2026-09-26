# In-place mutation of a String held in a global variable is lost.
$g = +"g"
$g << "ab"
puts $g          # CRuby: gab
$h = String.new
$h << "xy"
puts $h.size     # CRuby: 2
$u = +"g"
$u.upcase!
puts $u          # CRuby: G
