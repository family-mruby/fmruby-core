# FPS benchmark app: rounded-corner window with split composite regions.
# Counterpart to fps_bench_rounded (full-area transparent). Visual is identical
# (rounded corners stay rounded) but the compositor on the graphics-audio side
# copies only the listed sub-rects each frame:
#   - 4 small (4x4) corner regions: per-pixel transparent compare
#   - 3 large opaque regions covering the interior: memcpy fast path
#
# This validates that set_composite_regions wires through the protocol and
# delivers most of the BenchOpaque speedup while keeping rounded corners.

class FpsBenchRegion < FmrbApp
  CORNER = 4 # matches FmrbApp::CORNER_R for the rounded window frame

  def on_create
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 8,
                   "FPS Bench (region)", FmrbGfx::WHITE)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 24,
                   "rounded + region split", FmrbGfx::GRAY)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 40,
                   "4 transparent corners +", FmrbGfx::GRAY)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 56,
                   "3 opaque interior strips", FmrbGfx::GRAY)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 80,
                   "Watch graphics_task log:", FmrbGfx::CYAN)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 96,
                   "  fps / render_avg / max", FmrbGfx::CYAN)
    draw_window_frame
    @gfx.present

    w = @window_width
    h = @window_height
    c = CORNER
    @gfx.set_composite_regions([
      # 4 corner regions: only these need per-pixel transparent compare so
      # the rounded shape composites correctly against the background below.
      { dst_x: 0,         dst_y: 0,         w: c,         h: c,         transparent: true  },
      { dst_x: w - c,     dst_y: 0,         w: c,         h: c,         transparent: true  },
      { dst_x: 0,         dst_y: h - c,     w: c,         h: c,         transparent: true  },
      { dst_x: w - c,     dst_y: h - c,     w: c,         h: c,         transparent: true  },
      # Three opaque strips covering everything between the corners. memcpy
      # fast path: no per-pixel branch.
      { dst_x: c,         dst_y: 0,         w: w - 2 * c, h: c,         transparent: false },
      { dst_x: c,         dst_y: h - c,     w: w - 2 * c, h: c,         transparent: false },
      { dst_x: 0,         dst_y: c,         w: w,         h: h - 2 * c, transparent: false },
    ])
  end

  def on_update
    1000 # idle 1 second; compositor in GA still runs every frame
  end
end

app = FpsBenchRegion.new
app.start
