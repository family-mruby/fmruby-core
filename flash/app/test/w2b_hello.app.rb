# The SDK's hello (c/examples/hello) written as a Ruby app, for the W2 memory
# comparison (doc/wasm_apps/report/w2.md): a counter redrawn every 100 ms.
class W2HelloApp < FmrbApp
  def on_create
    @count = 0
    @key = "-"
    @bg = 0xFF
    @fg = 0x00
    Log.info("hello: started")
  end

  def on_update
    @count += 1
    g = @gfx
    g.fill_rect(0, 0, g.area_width, g.area_height, @bg)
    g.draw_text(4, 4, "Hello, Family mruby!", @fg)
    g.draw_text(4, 16, "count #{@count}", @fg)
    g.draw_text(4, 28, "key   #{@key}", @fg)
    g.present
    100
  end
end
