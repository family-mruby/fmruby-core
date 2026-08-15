# GfxBlock test app: animates a rectangle + label inside a GfxBlock.
# Verifies end-to-end: compile, define, exec with changing register values, destroy.

class GfxBlockTestApp < FmrbApp
  def on_create
    @ox = @user_area_x0
    @oy = @user_area_y0
    @w  = @user_area_width
    @h  = @user_area_height

    @x = 0
    @dx = 2
    @color = 7

    @block = GfxBlock.new(@gfx, x: @ox + 10, y: @oy + 20, w: 40, h: 30, color: @color) do |r, x:, y:, w:, h:, color:|
      r.fill_rect @ox, @oy, @w, @h, FmrbGfx::COLOR_BLACK
      r.fill_rect x, y, w, h, color
      r.draw_rect x, y, w, h, FmrbGfx::COLOR_WHITE
      r.draw_text @ox + 4, @oy + 4, "GfxBlock", FmrbGfx::COLOR_CYAN
    end

    draw_window_frame
    @gfx.present
    Log.info("GfxBlockTest: initial draw done")
  end

  def on_update
    @x += @dx
    if @x > @w - 50 || @x < 0
      @dx = -@dx
      @color = (@color + 1) & 0x1F
    end

    @block.draw(x: @ox + @x, y: @oy + 20, w: 40, h: 30, color: @color)
    draw_window_frame
    @gfx.present
    40
  end

  def on_destroy
    @block&.destroy
    Log.info("GfxBlockTest: destroyed")
  end
end

begin
  app = GfxBlockTestApp.new
  app.start
rescue => e
  Log.error("GfxBlockTest: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
