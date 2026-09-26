# U-6: an inherited ivar gets a different C type in the subclass than in the
# base (poly vs poly_array). Inherited methods write through a (sp_Base *)
# cast, so every later field of Child is read from the wrong offset.
# Ui is never instantiated; its `app.attach(self)` is what widens @items.
class Base
  def initialize
    @items = []
    @w = 640
    @h = 480
  end
  def attach(x)
    @items << x
  end
  def size_text
    "#{@w}x#{@h}"
  end
end
class Ui
  def initialize(app)
    app.attach(self)
  end
end
class Child < Base
  def area
    @w * @h
  end
end
c = Child.new
puts c.size_text
puts c.area
