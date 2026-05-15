# P5 Test - smoke test for the P5 wrapper API (see doc/p5.md)
# Click anywhere on the user area to cycle through pages. Window
# frame is left intact (we clear only the user area).

class P5TestApp < FmrbApp
  PAGES = %w[basics transform bezier text arc blend get_pixel image_masked]
  # Pages whose content changes every frame; everything else is drawn
  # once on page change and left alone.
  DYNAMIC_PAGES = %w[transform]

  def initialize
    super()
    @page = 0
    @t = 0
    @needs_redraw = true
  end

  def on_create
    @p5 = P5.new(@gfx)
    Log.info("P5 test started")
  end

  def on_update
    dynamic = DYNAMIC_PAGES.include?(PAGES[@page])
    if @needs_redraw || dynamic
      @t += 0.05 if dynamic
      draw_page
      @p5.present
      @needs_redraw = false
    end
    66
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_up
      close_btn_x = @window_width - 10
      return if ev[:x] >= close_btn_x && ev[:y] >= 2 && ev[:y] < 10
      @page = (@page + 1) % PAGES.size
      @needs_redraw = true
    end
  end

  def on_resume
    super
    @needs_redraw = true
  end

  def on_destroy
    # Masks and sprite-images are bound to this app's canvas on the
    # graphics-audio side, so DELETE_CANVAS (issued by FmrbApp#destroy
    # before on_destroy) frees them automatically. No explicit cleanup
    # needed here.
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
    when "image_masked" then draw_image_masked_page
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
    i = 0
    while i < 10
      @p5.point(uw - 30 + i, uh - 80 + i * 2)
      i += 1
    end
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
    swatches.each do |sw|
      color = sw[0]
      y = sw[2]
      @p5.fill(color); @p5.no_stroke
      @p5.rect(30, y, 60, 30)
    end
    # get_pixel is a sync command and forces a flush of queued draws on
    # the way down, so no explicit present is required for readback.
    @p5.fill(P5::WHITE)
    @p5.text_font(:default)
    @p5.text_align(:left, :top)
    swatches.each_with_index do |sw, _i|
      label = sw[1]
      y = sw[2]
      # Sample at the swatch center.
      sx = @user_area_x0 + 60
      sy = @user_area_y0 + y + 15
      value = @gfx.get_pixel(sx, sy)
      @p5.text(sprintf("%s read=0x%02X", label, value), 110, y + 11)
    end
  end

  # Builds a 1bpp circular mask (size x size pixels) as a binary string.
  # MSB-first per byte; 1 bit = pixel drawn.
  # Uses while loops because Integer#times block invocation is heavy in
  # picoruby and this runs over thousands of pixels at app start.
  def circle_mask(size)
    row_bytes = (size + 7) / 8
    buf = "\x00" * (row_bytes * size)
    r = size / 2
    cx = r
    cy = r
    r2 = r * r
    y = 0
    while y < size
      dy = y - cy
      row_off = y * row_bytes
      x = 0
      while x < size
        dx = x - cx
        if dx * dx + dy * dy <= r2
          idx = row_off + (x >> 3)
          buf.setbyte(idx, buf.getbyte(idx) | (0x80 >> (x & 7)))
        end
        x += 1
      end
      y += 1
    end
    buf
  end

  # Star-shaped mask: 5-pointed star using a simple radius-vs-angle test.
  def star_mask(size)
    row_bytes = (size + 7) / 8
    buf = "\x00" * (row_bytes * size)
    cx = size / 2
    cy = size / 2
    rmax = size / 2 - 1
    y = 0
    while y < size
      dy = y - cy
      row_off = y * row_bytes
      x = 0
      while x < size
        dx = x - cx
        d = Math.sqrt(dx * dx + dy * dy)
        if d <= rmax
          ang = Math.atan2(dy, dx)
          wave = Math.cos(5 * ang)
          limit = rmax * (0.55 + 0.45 * wave)
          if d <= limit
            idx = row_off + (x >> 3)
            buf.setbyte(idx, buf.getbyte(idx) | (0x80 >> (x & 7)))
          end
        end
        x += 1
      end
      y += 1
    end
    buf
  end

  def draw_image_masked_page
    # Lazy-create the source sprite and the long-lived masks once. This
    # is the pattern apps should follow — rebuilding masks every frame
    # works but wastes SPI bandwidth (each round trip uploads the mask
    # in N small chunks). Masks here are static per shape/size.
    @masked_sprite ||= build_masked_sprite
    @masks ||= {
      circle48: @gfx.create_mask(48, 48, circle_mask(48)),
      star48:   @gfx.create_mask(48, 48, star_mask(48)),
      circle32: @gfx.create_mask(32, 32, circle_mask(32)),
    }

    # Background bands so the cutout effect is visible against varied pixels.
    @p5.no_stroke
    @p5.fill(P5::BLUE);    @p5.rect(0, 20, uw, (uh - 20) / 3)
    @p5.fill(P5::MAGENTA); @p5.rect(0, 20 + (uh - 20) / 3, uw, (uh - 20) / 3)
    @p5.fill(P5::GREEN);   @p5.rect(0, 20 + 2 * (uh - 20) / 3, uw, (uh - 20) / 3 + 4)

    @p5.fill(P5::WHITE)
    @p5.text_align(:left, :top)
    @p5.text("image_masked: SpriteImage + 1bpp mask (cached)", 4, 22)

    sid = @masked_sprite.id
    @gfx.draw_image_masked(sid, @masks[:circle48], x: @user_area_x0 +  20, y: @user_area_y0 + 50)
    @gfx.draw_image_masked(sid, @masks[:star48],   x: @user_area_x0 +  90, y: @user_area_y0 + 50)
    @gfx.draw_image_masked(sid, @masks[:circle32], x: @user_area_x0 + 170, y: @user_area_y0 + 60)
  end

  # Build a 48x48 RGB332 sprite with a colorful gradient so the mask cutout
  # is visually distinctive. while-loops to avoid Integer#times overhead.
  def build_masked_sprite
    sprite = SpriteImage.new(@gfx, width: 48, height: 48)
    sprite.draw do |g|
      g.fill_rect(0, 0, 48, 48, FmrbGfx::WHITE)
      y = 0
      while y < 48
        col_r = (y * 7 / 47) << 5
        x = 0
        while x < 48
          col_g = (x * 7 / 47) << 2
          g.set_pixel(x, y, col_r | col_g | 0x02)
          x += 1
        end
        y += 1
      end
    end
    sprite
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
