# Coverage: mixin constant resolution. input_router.rb notes that a bare
# constant inside a mixin method resolves against the mixin's own scope, so
# the constants (MIN_WINDOW_WIDTH etc.) must live in the module. This checks
# Spinel resolves such constants the same way CRuby does.

module SizingMixin
  MIN_W = 64
  MIN_H = 64
  DEFAULT_SCALE = 2

  def clamp_size(w, h)
    # bare constant references -- must resolve to this module's constants
    cw = w < MIN_W ? MIN_W : w
    ch = h < MIN_H ? MIN_H : h
    [cw, ch]
  end

  def scaled(v)
    v * DEFAULT_SCALE
  end
end

class Window
  include SizingMixin

  def initialize(w, h)
    @w = w
    @h = h
  end

  def fit
    clamp_size(@w, @h)
  end

  def double_width
    scaled(@w)
  end
end

# Concrete Integer args (not nested-array block destructuring): a poly value
# from `[[..]].each { |w,h| }` flowing into a constructor or a mixin arithmetic
# method miscompiles under Spinel (see coverage/UNSUPPORTED.md U-3). The target
# of this coverage is mixin constant resolution, so we keep the inputs concrete.
def report(w, h)
  win = Window.new(w, h)
  fw, fh = win.fit
  puts "in=(#{w},#{h}) fit=(#{fw},#{fh}) dbl_w=#{win.double_width}"
end

report(10, 10)
report(100, 50)
report(200, 300)

# module constants referenced through the class::CONST form as well
puts "MIN_W via module = #{SizingMixin::MIN_W}"
