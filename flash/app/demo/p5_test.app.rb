# P5 Test - smoke test for the P5 wrapper API (see doc/p5.md)
# Click anywhere on the user area to cycle through pages. Window
# frame is left intact (we clear only the user area).

class P5TestApp < FmrbApp
  PAGES = %w[basics transform bezier text arc blend get_pixel]

  def initialize
    super()
    @page = 0
    @t = 0
  end

  def on_create
    @p5 = P5.new(@gfx)
    Log.info("P5 test started")
  end

  def on_update
    @t += 0.05
    draw_page
    @p5.present
    33
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_up
      close_btn_x = @window_width - 10
      return if ev[:x] >= close_btn_x && ev[:y] >= 2 && ev[:y] < 10
      @page = (@page + 1) % PAGES.size
    end
  end

  def draw_page
    # Reset P5 state so coordinates below are in user-area space.
    @p5.reset_matrix
    @p5.translate(@user_area_x0, @user_area_y0)
    @p5.no_stroke
    @p5.text_font(:default)
    @p5.text_color(P5::WHITE)
    @p5.text_align(:left, :top)

    # Clear only the user area; the FmrbApp window frame stays intact.
    clear_user_area(P5::BLACK)
    @p5.text("P5 #{PAGES[@page]} (click)", 2, 2)

    case PAGES[@page]
    when "basics"    then draw_basics
    when "transform" then draw_transform
    when "bezier"    then draw_bezier
    when "text"      then draw_text_page
    when "arc"       then draw_arc_page
    when "blend"     then draw_blend
    when "get_pixel" then draw_get_pixel
    end
  end

  # Width/height of the canvas we may use safely (in P5-local coords).
  def uw; @user_area_width;  end
  def uh; @user_area_height; end

  def draw_basics
    @p5.fill(P5::RED); @p5.stroke(P5::WHITE); @p5.stroke_weight(1)
    @p5.rect(10, 20, 60, 40)
    @p5.fill(P5::YELLOW); @p5.no_stroke
    @p5.circle(120, 40, 18)
    @p5.no_fill; @p5.stroke(P5::CYAN); @p5.stroke_weight(2)
    @p5.ellipse(200, 40, 36, 18)
    @p5.fill(P5::GREEN); @p5.stroke(P5::WHITE); @p5.stroke_weight(1)
    @p5.triangle(20, uh - 30, 80, uh - 60, 60, uh - 10)
    @p5.stroke(P5::MAGENTA); @p5.stroke_weight(3)
    @p5.line(100, uh - 60, uw - 50, uh - 40)
    @p5.no_stroke; @p5.fill(P5::BLUE)
    10.times { |i| @p5.point(uw - 30 + i, uh - 80 + i * 2) }
  end

  def draw_transform
    # Rotating rect around (uw/2, uh/2).
    @p5.push_matrix
    @p5.translate(uw / 2, uh / 2)
    @p5.rotate(@t)
    @p5.fill(P5::CYAN); @p5.stroke(P5::WHITE); @p5.stroke_weight(1)
    @p5.rect(-30, -20, 60, 40)
    @p5.pop_matrix
    # Pulsing circle on the left.
    @p5.push_matrix
    @p5.translate(60, uh / 2)
    @p5.scale(1.0 + Math.sin(@t) * 0.4, 1.0)
    @p5.fill(P5::YELLOW); @p5.no_stroke
    @p5.circle(0, 0, 24)
    @p5.pop_matrix
  end

  def draw_bezier
    @p5.no_fill
    @p5.stroke(P5::GREEN); @p5.stroke_weight(2)
    @p5.bezier(10, uh - 20, 80, 30, uw - 100, uh - 10, uw - 20, 50)
    @p5.stroke(P5::CYAN); @p5.stroke_weight(1)
    @p5.curve(10, 100, 60, 60, uw - 100, 90, uw - 20, 40)
  end

  def draw_text_page
    @p5.fill(P5::WHITE)
    @p5.text_align(:left, :top)
    @p5.text("left/top", 10, 30)
    @p5.text_align(:center, :center)
    @p5.text("center", uw / 2, uh / 2)
    @p5.text_align(:right, :bottom)
    @p5.text("right/bottom", uw - 4, uh - 4)
    @p5.text_font(:ja, 12)
    @p5.text_align(:left, :top)
    @p5.text("日本語", 10, uh - 40)
  end

  def draw_arc_page
    cy = uh / 2
    @p5.fill(P5::YELLOW); @p5.stroke(P5::WHITE); @p5.stroke_weight(1)
    @p5.arc(60, cy, 40, 0, Math::PI)
    @p5.fill(P5::MAGENTA); @p5.no_stroke
    @p5.arc(uw / 2, cy, 40, -Math::PI / 4, Math::PI / 2)
    @p5.no_fill; @p5.stroke(P5::CYAN)
    @p5.arc(uw - 60, cy, 30, 0, Math::PI * 1.5)
  end

  # Reads back a few pixels via @gfx.get_pixel to confirm the sync round
  # trip works. Three color swatches are drawn, then their pixel values
  # are sampled at the swatch centers and printed.
  def draw_get_pixel
    swatches = [
      [P5::RED,    "RED    0xE0", 30],
      [P5::GREEN,  "GREEN  0x1C", 70],
      [P5::CYAN,   "CYAN   0x1F", 110],
    ]
    swatches.each do |(color, _label, y)|
      @p5.fill(color); @p5.no_stroke
      @p5.rect(30, y, 60, 30)
    end
    # get_pixel is a sync command and forces a flush of queued draws on
    # the way down, so no explicit present is required for readback.
    @p5.fill(P5::WHITE)
    @p5.text_font(:default)
    @p5.text_align(:left, :top)
    swatches.each_with_index do |(_color, label, y), _i|
      # Sample at the swatch center.
      sx = @user_area_x0 + 60
      sy = @user_area_y0 + y + 15
      value = @gfx.get_pixel(sx, sy)
      @p5.text(sprintf("%s read=0x%02X", label, value), 110, y + 11)
    end
  end

  def draw_blend
    @p5.fill(P5::RED); @p5.no_stroke
    @p5.rect(20, 40, 120, 100)
    @p5.blend_mode(P5::ADD)
    @p5.fill(P5::BLUE)
    @p5.rect(80, 70, 120, 100)
    @p5.blend_mode(P5::REPLACE)
    @p5.fill(P5::WHITE)
    @p5.text("ADD blend (rect only)", 20, uh - 20)
  end
end

app = P5TestApp.new
Log.info("App created successfully")
app.start
