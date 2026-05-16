# FPS benchmark app: default rounded_corners (transparent compositing enabled).
# Pair with fps_bench_opaque.app.rb to compare graphics_task fps logs and
# measure the cost of per-pixel transparent compare in the compositor.
#
# Both bench apps draw once in on_create and idle in on_update (1s sleep),
# so they do not contribute Ruby-side CPU to the measurement. Only the
# GA-side compositing path differs between the two variants.
#
# Watch the graphics_task INFO line emitted every 5 seconds:
#   [I][graphics_task] loop: fps=XX.X render_avg=YYms render_max=ZZms ...

class FpsBenchRounded < FmrbApp
  def on_create
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 8,
                   "FPS Bench (rounded)", FmrbGfx::WHITE)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 24,
                   "rounded_corners = true", FmrbGfx::GRAY)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 40,
                   "use_transparent canvas", FmrbGfx::GRAY)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 64,
                   "Watch graphics_task log:", FmrbGfx::CYAN)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 80,
                   "  fps / render_avg / max", FmrbGfx::CYAN)
    draw_window_frame
    @gfx.present
  end

  def on_update
    1000 # idle 1 second; compositor in GA still runs every frame
  end
end

app = FpsBenchRounded.new
app.start
