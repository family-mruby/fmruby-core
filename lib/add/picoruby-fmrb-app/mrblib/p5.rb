# P5 - Processing/p5.js compatible drawing API for family-mruby
#
# Wraps an FmrbGfx instance and exposes a Processing-style API
# (fill/stroke state, affine transform stack, basic shape primitives,
# text alignment, bezier/Catmull-Rom curves, blend modes).
#
# Ported from harucom-os' p5.rb. See doc/p5.md for the full list of
# differences against the harucom-os version (most importantly:
# construct as `P5.new(@gfx)`, image() uses image_id, and a subset of
# blend modes is supported).
#
# Typical usage from inside an FmrbApp:
#
#   class MyApp < FmrbApp
#     def on_create
#       @p5 = P5.new(@gfx)
#     end
#     def on_update
#       @p5.background(P5::BLACK)
#       @p5.fill(P5::RED)
#       @p5.rect(10, 10, 60, 40)
#       @p5.present
#       33
#     end
#   end

class P5
  # RGB332 color constants mirrored from FmrbGfx for ergonomic use.
  BLACK   = FmrbGfx::BLACK
  WHITE   = FmrbGfx::WHITE
  RED     = FmrbGfx::RED
  GREEN   = FmrbGfx::GREEN
  BLUE    = FmrbGfx::BLUE
  YELLOW  = FmrbGfx::YELLOW
  CYAN    = FmrbGfx::CYAN
  MAGENTA = FmrbGfx::MAGENTA
  GRAY    = FmrbGfx::GRAY

  # Blend mode constants. fmruby-core's backend only natively supports
  # ADD and XOR (via FmrbGfx#blend_rect), and only on rectangular shapes.
  # The remaining modes are defined for source compatibility with
  # harucom-os' P5, but behave as REPLACE.
  REPLACE  = 0
  ADD      = 1
  XOR      = 2
  SUBTRACT = 3  # unsupported: falls back to REPLACE
  MULTIPLY = 4  # unsupported: falls back to REPLACE
  SCREEN   = 5  # unsupported: falls back to REPLACE

  def initialize(gfx)
    @gfx = gfx
    @fill_color = WHITE
    @fill_enabled = true
    @stroke_color = WHITE
    @stroke_enabled = true
    @stroke_weight = 1
    @blend_mode = REPLACE
    @font_family = :default
    @font_size = nil
    @text_color = WHITE
    @text_align_h = :left
    @text_align_v = :top
    @text_leading = 0
    @matrix = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]
    @matrix_stack = []
  end

  # Screen and frame control

  def width
    @gfx.canvas_width
  end

  def height
    @gfx.canvas_height
  end

  def background(color)
    if @blend_mode == ADD || @blend_mode == XOR
      @gfx.blend_rect(0, 0, width, height, color,
                      @blend_mode == ADD ? FmrbGfx::BLEND_ADD : FmrbGfx::BLEND_XOR)
    else
      @gfx.clear(color)
    end
  end

  def present
    @gfx.present
  end
  alias_method :commit, :present

  # Fill / stroke state

  def fill(color)
    @fill_color = color
    @fill_enabled = true
  end

  def no_fill
    @fill_enabled = false
  end

  def stroke(color)
    @stroke_color = color
    @stroke_enabled = true
  end

  def no_stroke
    @stroke_enabled = false
  end

  def stroke_weight(w)
    @stroke_weight = w
  end

  # Blend mode

  def blend_mode(mode)
    @blend_mode = mode
  end

  # No backend support yet; defined for source compatibility.
  def alpha(_value)
    # no-op
  end

  # Text state

  # font: :default for ASCII Font0; :ja for Japanese (size 8 or 12).
  # wide_font is accepted but ignored; the backend selects the wide
  # glyph path automatically when :ja is active.
  def text_font(font, _wide_font = nil)
    @font_family = font
    @font_size = (font == :ja ? 8 : nil)
  end

  def text_color(color)
    @text_color = color
  end

  # horizontal: :left, :center, :right
  # vertical:   :top,  :center, :bottom
  def text_align(horizontal, vertical = :top)
    @text_align_h = horizontal
    @text_align_v = vertical
  end

  def text_leading(pixels)
    @text_leading = pixels
  end

  def text_width(str)
    if @font_size
      @gfx.text_width(str, @font_family, @font_size)
    else
      @gfx.text_width(str, @font_family)
    end
  end

  # Coordinate transforms
  #
  # The state is a 2x3 affine matrix [a, b, c, d, tx, ty]:
  #   x' = a*x + b*y + tx
  #   y' = c*x + d*y + ty
  # Operations post-multiply (M' = M * op) so that
  # `translate(tx, ty); rotate(theta); rect(...)` rotates the rectangle
  # around the translated local origin, matching p5.js / Processing.
  # Drawing methods take a fast path when the matrix is translation-only.

  def translate(tx, ty)
    @matrix = matrix_multiply(@matrix, [1.0, 0.0, 0.0, 1.0, tx.to_f, ty.to_f])
  end

  def rotate(angle)
    c = Math.cos(angle)
    s = Math.sin(angle)
    @matrix = matrix_multiply(@matrix, [c, -s, s, c, 0.0, 0.0])
  end

  def scale(sx, sy = sx)
    @matrix = matrix_multiply(@matrix, [sx.to_f, 0.0, 0.0, sy.to_f, 0.0, 0.0])
  end

  def push_matrix
    @matrix_stack.push(@matrix.dup)
  end

  def pop_matrix
    @matrix = @matrix_stack.pop if @matrix_stack.length > 0
  end

  def reset_matrix
    @matrix = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]
  end

  # Shape primitives

  def point(x, y)
    return unless @stroke_enabled
    px, py = transform(x, y)
    @gfx.set_pixel(px, py, @stroke_color)
  end

  def line(x0, y0, x1, y1)
    return unless @stroke_enabled
    ax, ay = transform(x0, y0)
    bx, by = transform(x1, y1)
    draw_edge(ax, ay, bx, by)
  end

  def rect(x, y, w, h)
    if translate_only?
      tx = @matrix[4].round
      ty = @matrix[5].round
      fill_rect_blended(x + tx, y + ty, w, h, @fill_color) if @fill_enabled
      @gfx.draw_rect(x + tx, y + ty, w, h, @stroke_color) if @stroke_enabled
    else
      x0, y0 = transform(x, y)
      x1, y1 = transform(x + w, y)
      x2, y2 = transform(x + w, y + h)
      x3, y3 = transform(x, y + h)
      if @fill_enabled
        @gfx.fill_triangle(x0, y0, x1, y1, x2, y2, @fill_color)
        @gfx.fill_triangle(x0, y0, x2, y2, x3, y3, @fill_color)
      end
      if @stroke_enabled
        draw_edge(x0, y0, x1, y1)
        draw_edge(x1, y1, x2, y2)
        draw_edge(x2, y2, x3, y3)
        draw_edge(x3, y3, x0, y0)
      end
    end
  end

  def circle(cx, cy, r)
    tcx, tcy = transform(cx, cy)
    if translate_only?
      @gfx.fill_circle(tcx, tcy, r, @fill_color) if @fill_enabled
      @gfx.draw_circle(tcx, tcy, r, @stroke_color) if @stroke_enabled
    else
      sx = Math.sqrt(@matrix[0] * @matrix[0] + @matrix[2] * @matrix[2])
      sy = Math.sqrt(@matrix[1] * @matrix[1] + @matrix[3] * @matrix[3])
      rx = (r * sx).round
      ry = (r * sy).round
      @gfx.fill_ellipse(tcx, tcy, rx, ry, @fill_color) if @fill_enabled
      @gfx.draw_ellipse(tcx, tcy, rx, ry, @stroke_color) if @stroke_enabled
    end
  end

  def ellipse(cx, cy, rx, ry)
    tcx, tcy = transform(cx, cy)
    if translate_only?
      @gfx.fill_ellipse(tcx, tcy, rx, ry, @fill_color) if @fill_enabled
      @gfx.draw_ellipse(tcx, tcy, rx, ry, @stroke_color) if @stroke_enabled
    else
      sx = Math.sqrt(@matrix[0] * @matrix[0] + @matrix[2] * @matrix[2])
      sy = Math.sqrt(@matrix[1] * @matrix[1] + @matrix[3] * @matrix[3])
      trx = (rx * sx).round
      tryv = (ry * sy).round
      @gfx.fill_ellipse(tcx, tcy, trx, tryv, @fill_color) if @fill_enabled
      @gfx.draw_ellipse(tcx, tcy, trx, tryv, @stroke_color) if @stroke_enabled
    end
  end

  def triangle(x0, y0, x1, y1, x2, y2)
    ax, ay = transform(x0, y0)
    bx, by = transform(x1, y1)
    cx, cy = transform(x2, y2)
    @gfx.fill_triangle(ax, ay, bx, by, cx, cy, @fill_color) if @fill_enabled
    if @stroke_enabled
      draw_edge(ax, ay, bx, by)
      draw_edge(bx, by, cx, cy)
      draw_edge(cx, cy, ax, ay)
    end
  end

  # Pie-slice arc, with angles in radians (0 = right, PI/2 = down).
  # FmrbGfx#fill_arc / #draw_arc expect angles in degrees (LovyanGFX
  # convention), so we convert here. The fill path uses r0=0 for a pie;
  # the stroke path uses a 1-pixel wide ring at the outer radius.
  def arc(cx, cy, r, start_angle, stop_angle)
    tcx, tcy = transform(cx, cy)
    a0 = (start_angle * 180.0 / Math::PI).round
    a1 = (stop_angle * 180.0 / Math::PI).round
    if @fill_enabled
      @gfx.fill_arc(tcx, tcy, 0, r, a0, a1, @fill_color)
    end
    if @stroke_enabled
      inner = r > 0 ? r - 1 : 0
      @gfx.draw_arc(tcx, tcy, inner, r, a0, a1, @stroke_color)
    end
  end

  # Cubic bezier from (x1,y1) to (x4,y4) with controls (x2,y2),(x3,y3).
  # Rasterized as 20 line segments.
  def bezier(x1, y1, x2, y2, x3, y3, x4, y4)
    return unless @stroke_enabled
    segments = 20
    px, py = transform(x1, y1)
    segments.times do |i|
      t = (i + 1).to_f / segments
      t2 = t * t
      t3 = t2 * t
      mt = 1.0 - t
      mt2 = mt * mt
      mt3 = mt2 * mt
      nx = mt3 * x1 + 3 * mt2 * t * x2 + 3 * mt * t2 * x3 + t3 * x4
      ny = mt3 * y1 + 3 * mt2 * t * y2 + 3 * mt * t2 * y3 + t3 * y4
      qx, qy = transform(nx, ny)
      draw_edge(px, py, qx, qy)
      px = qx
      py = qy
    end
  end

  # Catmull-Rom spline through (x2,y2) to (x3,y3), shaped by (x1,y1)
  # and (x4,y4). Rasterized as 20 line segments.
  def curve(x1, y1, x2, y2, x3, y3, x4, y4)
    return unless @stroke_enabled
    segments = 20
    px, py = transform(x2, y2)
    segments.times do |i|
      t = (i + 1).to_f / segments
      t2 = t * t
      t3 = t2 * t
      nx = 0.5 * ((2 * x2) + (-x1 + x3) * t +
                  (2 * x1 - 5 * x2 + 4 * x3 - x4) * t2 +
                  (-x1 + 3 * x2 - 3 * x3 + x4) * t3)
      ny = 0.5 * ((2 * y2) + (-y1 + y3) * t +
                  (2 * y1 - 5 * y2 + 4 * y3 - y4) * t2 +
                  (-y1 + 3 * y2 - 3 * y3 + y4) * t3)
      qx, qy = transform(nx, ny)
      draw_edge(px, py, qx, qy)
      px = qx
      py = qy
    end
  end

  # Text rendering. Translation-only positioning; rotation/scale on text
  # is not supported by the backend.
  def text(str, x, y)
    tx, ty = transform(x, y)
    if @text_align_h == :center
      tx -= text_width(str) / 2
    elsif @text_align_h == :right
      tx -= text_width(str)
    end
    if @text_align_v == :center
      ty -= font_height / 2
    elsif @text_align_v == :bottom
      ty -= font_height
    end
    saved_font = @gfx.current_font
    saved_size = @gfx.current_text_size
    if @font_size
      @gfx.set_font(@font_family, @font_size)
    else
      @gfx.set_font(@font_family)
    end
    @gfx.draw_text(tx, ty, str, @text_color)
    # Restore previous font selection so callers (e.g. window frame)
    # are not affected by the P5 state.
    if saved_font.length == 2
      @gfx.set_font(saved_font[0], saved_font[1])
    else
      @gfx.set_font(saved_font[0])
    end
    @gfx.set_text_size(saved_size) if saved_size != 1
  end

  # Image rendering. Unlike harucom-os' raw-buffer image(), this takes
  # an image_id obtained via @gfx.create_image(path).
  def image(image_id, x, y, scale_x = 1.0, scale_y = 0.0)
    tx, ty = transform(x, y)
    @gfx.draw_image(image_id, x: tx, y: ty, scale_x: scale_x, scale_y: scale_y)
  end

  # Not implemented: the graphics backend has no masked-blit primitive.
  def image_masked(_data, _mask, _x, _y, _w, _h)
    raise NotImplementedError, "image_masked: not supported by fmruby-core backend"
  end

  # Pixel access. set_pixel intentionally bypasses the transform stack
  # (matches harucom-os behavior).

  def set_pixel(x, y, color)
    @gfx.set_pixel(x, y, color)
  end

  # Synchronous round trip to the graphics backend; sparse use only.
  def get_pixel(x, y)
    @gfx.get_pixel(x, y)
  end

  # RGB888 -> RGB332 packed byte, matching FmrbGfx.rgb_to_332.
  def color(r, g, b)
    FmrbGfx.rgb_to_332(r, g, b)
  end

  private

  def font_height
    if @font_size
      @gfx.font_height(@font_family, @font_size)
    else
      @gfx.font_height(@font_family)
    end
  end

  def transform(x, y)
    m = @matrix
    [(m[0] * x + m[1] * y + m[4]).round,
     (m[2] * x + m[3] * y + m[5]).round]
  end

  def translate_only?
    m = @matrix
    m[0] == 1.0 && m[1] == 0.0 && m[2] == 0.0 && m[3] == 1.0
  end

  def matrix_multiply(a, b)
    [a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
     a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
     a[0] * b[4] + a[1] * b[5] + a[4], a[2] * b[4] + a[3] * b[5] + a[5]]
  end

  def draw_edge(x0, y0, x1, y1)
    if @stroke_weight > 1
      @gfx.draw_thick_line(x0, y0, x1, y1, @stroke_weight, @stroke_color)
    else
      @gfx.draw_line(x0, y0, x1, y1, @stroke_color)
    end
  end

  # Honor @blend_mode for fill_rect when set to ADD/XOR; otherwise use
  # the plain fill_rect path.
  def fill_rect_blended(x, y, w, h, color)
    case @blend_mode
    when ADD
      @gfx.blend_rect(x, y, w, h, color, FmrbGfx::BLEND_ADD)
    when XOR
      @gfx.blend_rect(x, y, w, h, color, FmrbGfx::BLEND_XOR)
    else
      @gfx.fill_rect(x, y, w, h, color)
    end
  end
end
