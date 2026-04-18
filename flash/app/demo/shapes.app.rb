# Shapes Demo - Showcase of all GFX drawing APIs
# Cycles through different shape demonstrations

class ShapesApp < FmrbApp
  DEMOS = ["Shapes", "Arcs", "Text"]

  def initialize
    super()
    @demo_idx = 0
    @tick = 0
  end

  def on_create
    Log.info("Shapes demo started")
    draw_current
  end

  def on_update
    @tick += 1
    100
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_up
      # Skip if close button area (handled by super)
      close_btn_x = @window_width - 10
      if ev[:x] >= close_btn_x && ev[:y] >= 2 && ev[:y] < 10
        return
      end
      @demo_idx = (@demo_idx + 1) % DEMOS.size
      draw_current
    end
  end

  def draw_current
    case @demo_idx
    when 0 then draw_shapes
    when 1 then draw_arcs
    when 2 then draw_text_demo
    end
  end

  def draw_shapes
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height

    @gfx.fill_rect(x0, y0, w, h, FmrbGfx::BLACK)

    # Title
    @gfx.draw_text(x0 + 4, y0 + 2, "Shapes (click to next)", FmrbGfx::WHITE)

    # Round rectangles
    @gfx.draw_round_rect(x0 + 8, y0 + 16, 60, 30, 6, FmrbGfx::CYAN)
    @gfx.fill_round_rect(x0 + 78, y0 + 16, 60, 30, 6, FmrbGfx::GREEN)
    @gfx.draw_text(x0 + 14, y0 + 48, "round_rect", FmrbGfx::GRAY)

    # Ellipses
    @gfx.draw_ellipse(x0 + 38, y0 + 80, 30, 18, FmrbGfx::YELLOW)
    @gfx.fill_ellipse(x0 + 108, y0 + 80, 30, 18, FmrbGfx::RED)
    @gfx.draw_text(x0 + 30, y0 + 102, "ellipse", FmrbGfx::GRAY)

    # Triangles
    @gfx.draw_triangle(x0 + 8, y0 + 150, x0 + 58, y0 + 115, x0 + 68, y0 + 150, FmrbGfx::MAGENTA)
    @gfx.fill_triangle(x0 + 78, y0 + 150, x0 + 128, y0 + 115, x0 + 138, y0 + 150, FmrbGfx::BLUE)
    @gfx.draw_text(x0 + 20, y0 + 155, "triangle", FmrbGfx::GRAY)

    # Mixed composition
    @gfx.fill_round_rect(x0 + 150, y0 + 16, 80, 150, 10, 0x24)
    @gfx.fill_circle(x0 + 190, y0 + 50, 20, FmrbGfx::RED)
    @gfx.fill_ellipse(x0 + 190, y0 + 95, 30, 15, FmrbGfx::CYAN)
    @gfx.draw_triangle(x0 + 165, y0 + 150, x0 + 190, y0 + 120, x0 + 215, y0 + 150, FmrbGfx::YELLOW)
    @gfx.draw_text(x0 + 158, y0 + 155, "composed", FmrbGfx::GRAY)

    draw_window_frame
    @gfx.present
  end

  def draw_arcs
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height

    @gfx.fill_rect(x0, y0, w, h, FmrbGfx::BLACK)
    @gfx.draw_text(x0 + 4, y0 + 2, "Arcs (click to next)", FmrbGfx::WHITE)

    cx = x0 + 70
    cy = y0 + 90

    # Pie chart style
    @gfx.fill_arc(cx, cy, 0, 50, 0, 120, FmrbGfx::RED)
    @gfx.fill_arc(cx, cy, 0, 50, 120, 220, FmrbGfx::GREEN)
    @gfx.fill_arc(cx, cy, 0, 50, 220, 360, FmrbGfx::BLUE)
    @gfx.draw_text(x0 + 30, y0 + 145, "pie chart", FmrbGfx::GRAY)

    # Progress ring
    cx2 = x0 + 180
    cy2 = y0 + 90
    @gfx.draw_arc(cx2, cy2, 35, 45, 0, 360, 0x24)
    @gfx.fill_arc(cx2, cy2, 35, 45, 270, 270 + 252, FmrbGfx::CYAN)  # 70%
    @gfx.draw_text(cx2 - 12, cy2 - 4, "70%", FmrbGfx::WHITE)
    @gfx.draw_text(x0 + 148, y0 + 145, "progress", FmrbGfx::GRAY)

    draw_window_frame
    @gfx.present
  end

  def draw_text_demo
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height

    @gfx.fill_rect(x0, y0, w, h, FmrbGfx::BLACK)
    @gfx.draw_text(x0 + 4, y0 + 2, "Text Size (click to next)", FmrbGfx::WHITE)

    y = y0 + 20

    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, y, "Size 1: Hello!", FmrbGfx::CYAN)
    y += 14

    @gfx.set_text_size(2)
    @gfx.draw_text(x0 + 8, y, "Size 2: Hi!", FmrbGfx::GREEN)
    y += 24

    @gfx.set_text_size(3)
    @gfx.draw_text(x0 + 8, y, "Size 3", FmrbGfx::YELLOW)
    y += 34

    @gfx.set_text_size(4)
    @gfx.draw_text(x0 + 8, y, "Sz 4", FmrbGfx::RED)

    # Reset to default
    @gfx.set_text_size(1)

    draw_window_frame
    @gfx.present
  end

  def on_destroy
    @gfx.set_text_size(1) if @gfx
    Log.info("Shapes demo destroyed")
  end
end

Log.info("ShapesApp.new")
begin
  app = ShapesApp.new
  Log.info("ShapesApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
