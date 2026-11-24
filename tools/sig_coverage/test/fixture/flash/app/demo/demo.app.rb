class DemoApp < FmrbApp
  TUNES = [1, 2].freeze

  attr_reader :speed

  def on_create
    @foo = Foo.make
    @foo.bar
    Foo.vanish
    Foo.bar
    t = Machine.board_millis
    t2 = Machine.board_millis
    half = Math::PI / 2
    "abc".byteslice(0, 1)
    "abc".size
    TUNES.each { |x| helper(x) }
    Mine.build
    LibThing.go
    Clock.now
    Net::HTTP.get_response(nil)
    @gfx.draw
    @gfx.nope
    key = FmrbConst::KEY_UP
    w = FmrbGfx::WHITE
    random_int(3)
    roll(6)
    send(:helper, 1)
    speed
    nil.no_such_method
  end

  def helper(x) = x
end

class Mine
  def self.build = new
end
