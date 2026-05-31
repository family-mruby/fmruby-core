# Kamon (Japanese family crest) generator
#
# Port of tmp/sketch/public/events/kamon.html (a p5.rb sketch) to Family
# mruby. 5 motifs (maru / hishi / hana / ougi / uroko) rendered with
# rotational symmetry, optional center decoration, count / size / invert
# parameters. UI is a column of virtual buttons on the right, picked with
# mouse_up events.
#
# Layout (window 300x200, title bar ~11px):
#   x=  4..184  (180x180)  preview area, center origin at (94, user_y+94)
#   x=188..296  (108 wide) control panel

class KamonApp < FmrbApp
  TWO_PI  = Math::PI * 2
  HALF_PI = Math::PI / 2
  PI      = Math::PI

  MOTIFS  = ["maru", "hishi", "hana", "ougi", "uroko"].freeze
  CENTERS = ["none", "white", "black", "ring"].freeze

  COUNT_MIN = 3
  COUNT_MAX = 12
  SIZE_MIN  = 20
  SIZE_MAX  = 100
  SIZE_STEP = 10

  PREVIEW_X    = 4
  PREVIEW_Y    = 4
  PREVIEW_SIZE = 180
  PANEL_X      = PREVIEW_X + PREVIEW_SIZE + 4   # 188
  PANEL_W      = 300 - PANEL_X - 4              # 108

  def initialize
    super()
    @motif    = "maru"
    @center   = "white"
    @count    = 5
    @size     = 0.7
    @inverted = false
    @needs_redraw = true
  end

  def on_create
    @p5 = P5.new(@gfx)
    @buttons = build_buttons
  end

  def on_update
    if @needs_redraw
      draw_all
      @needs_redraw = false
    end
    100
  end

  def on_destroy
    # Restore the global font in case we were interrupted mid-draw with
    # the ja font selected.
    @gfx.set_font(:default) if @gfx
  end

  def on_event(ev)
    super(ev)
    return unless @running
    return unless ev[:type] == :mouse_up
    # Close button (top-right of title bar) is consumed by super.
    close_btn_x = @window_width - 10
    return if ev[:x] >= close_btn_x && ev[:y] >= 2 && ev[:y] < 10

    b = hit_test(ev[:x], ev[:y])
    return unless b
    handle_button(b)
    @needs_redraw = true
  end

  private

  def clamp(v, lo, hi)
    return lo if v < lo
    return hi if v > hi
    v
  end

  # -----------------------------------------------------------------------
  # Button table
  # -----------------------------------------------------------------------

  def build_buttons
    base_y = @user_area_y0
    px = PANEL_X
    list = []

    # 5 motif buttons (2 cols x 3 rows; the 5th sits alone on the bottom row).
    # Japanese single-kanji labels for at-a-glance recognition.
    motif_labels = ["円", "菱", "花", "扇", "鱗"]
    motif_labels.each_with_index do |lab, i|
      col = i % 2
      row = i / 2
      list << {
        id: :motif, value: MOTIFS[i], label: lab, group: :motif,
        x: px + col * 58, y: base_y + 14 + row * 18, w: 50, h: 16,
      }
    end

    # 4 center buttons (2x2).
    center_labels = ["無", "白", "黒", "輪"]
    center_labels.each_with_index do |lab, i|
      col = i % 2
      row = i / 2
      list << {
        id: :center, value: CENTERS[i], label: lab, group: :center,
        x: px + col * 58, y: base_y + 82 + row * 18, w: 50, h: 16,
      }
    end

    # Count stepper: [<] (value display) [>]
    list << {
      id: :count_dec, value: -1, label: "<", group: :count_step,
      x: px,         y: base_y + 130, w: 22, h: 14,
    }
    list << {
      id: :count_inc, value: 1, label: ">", group: :count_step,
      x: px + 86,    y: base_y + 130, w: 22, h: 14,
    }

    # Size stepper.
    list << {
      id: :size_dec, value: -1, label: "<", group: :size_step,
      x: px,         y: base_y + 160, w: 22, h: 14,
    }
    list << {
      id: :size_inc, value: 1, label: ">", group: :size_step,
      x: px + 86,    y: base_y + 160, w: 22, h: 14,
    }

    # Full-width invert toggle.
    list << {
      id: :invert, value: nil, label: "INVERT", group: :invert,
      x: px, y: base_y + 176, w: PANEL_W, h: 12,
    }

    list
  end

  def hit_test(mx, my)
    @buttons.each do |b|
      if mx >= b[:x] && mx < b[:x] + b[:w] &&
         my >= b[:y] && my < b[:y] + b[:h]
        return b
      end
    end
    nil
  end

  def handle_button(b)
    case b[:group]
    when :motif
      @motif = b[:value]
    when :center
      @center = b[:value]
    when :count_step
      @count = clamp(@count + b[:value], COUNT_MIN, COUNT_MAX)
    when :size_step
      pct = (@size * 100).round + b[:value] * SIZE_STEP
      @size = clamp(pct, SIZE_MIN, SIZE_MAX) / 100.0
    when :invert
      @inverted = !@inverted
    end
  end

  # -----------------------------------------------------------------------
  # Drawing
  # -----------------------------------------------------------------------

  def draw_all
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                   @user_area_width, @user_area_height, FmrbGfx::WHITE)
    draw_preview
    draw_panel
    draw_window_frame
    @p5.present
  end

  def draw_preview
    cx = @user_area_x0 + PREVIEW_X + PREVIEW_SIZE / 2
    cy = @user_area_y0 + PREVIEW_Y + PREVIEW_SIZE / 2

    @p5.reset_matrix
    @p5.push_matrix
    @p5.translate(cx, cy)

    r = (PREVIEW_SIZE * 0.38).to_i  # outer radius ~68

    # Outer ring (thick) and inner ring (thin).
    @p5.no_fill
    @p5.stroke(FmrbGfx::BLACK)
    @p5.stroke_weight([(r * 0.08).round, 1].max)
    @p5.circle(0, 0, r)

    @p5.stroke_weight([(r * 0.022).round, 1].max)
    @p5.circle(0, 0, (r * 0.91).round)

    # Solid or outline mode for the motif.
    if @inverted
      @p5.no_fill
      @p5.stroke(FmrbGfx::BLACK)
      @p5.stroke_weight([(r * 0.02).round, 1].max)
    else
      @p5.fill(FmrbGfx::BLACK)
      @p5.no_stroke
    end

    s = r * 0.87 * @size

    case @motif
    when "maru"  then draw_maru(s)
    when "hishi" then draw_hishi(s)
    when "hana"  then draw_hana(s)
    when "ougi"  then draw_ougi(s)
    when "uroko" then draw_uroko(s)
    end

    # Optional center decoration painted on top of the motif.
    center_d = s * 0.35
    case @center
    when "white"
      @p5.fill(FmrbGfx::WHITE); @p5.no_stroke
      @p5.circle(0, 0, (center_d / 2).round)
    when "black"
      @p5.fill(FmrbGfx::BLACK); @p5.no_stroke
      @p5.circle(0, 0, (center_d / 2).round)
    when "ring"
      @p5.no_fill; @p5.stroke(FmrbGfx::BLACK)
      @p5.stroke_weight([(r * 0.02).round, 1].max)
      @p5.circle(0, 0, (center_d / 2).round)
    end

    @p5.pop_matrix
  end

  # -----------------------------------------------------------------------
  # Motif renderers (ported from kamon.html draw_*)
  # NOTE: kamon.html passes diameters to circle()/arc(); p5.rb wants radii,
  # so all sizes are halved here.
  # -----------------------------------------------------------------------

  def draw_maru(s)
    petal_r = s * 0.31
    dist    = s * 0.69
    @count.times do |i|
      a = TWO_PI / @count * i - HALF_PI
      @p5.circle(Math.cos(a) * dist, Math.sin(a) * dist, petal_r.round)
    end
  end

  def draw_hishi(s)
    @count.times do |i|
      a = TWO_PI / @count * i
      ca = Math.cos(a); sa = Math.sin(a)
      px = -sa;          py = ca
      d  = s * 0.93
      w  = s * 0.14
      @p5.quad(
        0,                                    0,
        ca * d * 0.5 + px * w,                sa * d * 0.5 + py * w,
        ca * d,                               sa * d,
        ca * d * 0.5 - px * w,                sa * d * 0.5 - py * w,
      )
    end
  end

  def draw_hana(s)
    @count.times do |i|
      a = TWO_PI / @count * i - HALF_PI
      ca = Math.cos(a); sa = Math.sin(a)
      px = -sa;          py = ca
      tip = s
      w   = s * 0.26

      @p5.begin_shape
      steps = 12
      (steps + 1).times do |st|
        t  = st.to_f / steps
        d  = tip * t
        pw = Math.sin(t * PI) * w
        @p5.vertex(ca * d + px * pw, sa * d + py * pw)
      end
      steps.downto(0) do |st|
        t  = st.to_f / steps
        d  = tip * t
        pw = Math.sin(t * PI) * w
        @p5.vertex(ca * d - px * pw, sa * d - py * pw)
      end
      @p5.end_shape(true)
    end
  end

  def draw_ougi(s)
    span = TWO_PI / @count * 0.35
    d    = s * 1.86
    @count.times do |i|
      a = TWO_PI / @count * i - HALF_PI
      @p5.arc(0, 0, (d / 2).round, a - span, a + span)
    end
  end

  def draw_uroko(s)
    tip_d  = s * 0.93
    base_d = s * 0.3
    base_w = s * 0.28
    @count.times do |i|
      a = TWO_PI / @count * i - HALF_PI
      ca = Math.cos(a); sa = Math.sin(a)
      px = -sa;          py = ca
      @p5.triangle(
        ca * tip_d,                  sa * tip_d,
        ca * base_d + px * base_w,   sa * base_d + py * base_w,
        ca * base_d - px * base_w,   sa * base_d - py * base_w,
      )
    end
  end

  # -----------------------------------------------------------------------
  # Control panel
  # -----------------------------------------------------------------------

  # Panel draws everything with the misaki 8px Japanese font so that kanji
  # labels render. ASCII glyphs (used by the < / > stepper buttons and the
  # numeric readouts) are passed through the same font via mixed: true so
  # they stay legible. The window frame title still uses the default font
  # because draw_window_frame sets it itself.
  def draw_panel
    base_y = @user_area_y0
    @gfx.set_font(:ja, 8)

    @gfx.draw_text(PANEL_X, base_y + 4,   "紋様",   FmrbGfx::BLACK)
    @gfx.draw_text(PANEL_X, base_y + 72,  "中央",   FmrbGfx::BLACK)
    @gfx.draw_text(PANEL_X, base_y + 120, "数",     FmrbGfx::BLACK)
    @gfx.draw_text(PANEL_X, base_y + 148, "大きさ", FmrbGfx::BLACK)

    @buttons.each { |b| draw_button(b) }

    draw_value_label(PANEL_X + 24, base_y + 130, 60, 14, "#{@count}")
    draw_value_label(PANEL_X + 24, base_y + 160, 60, 14, "#{(@size * 100).round}%")

    @gfx.set_font(:default)
  end

  def draw_button(b)
    selected = button_selected?(b)
    if selected
      @gfx.fill_rect(b[:x], b[:y], b[:w], b[:h], FmrbGfx::BLACK)
      text_color = FmrbGfx::WHITE
    else
      @gfx.fill_rect(b[:x], b[:y], b[:w], b[:h], FmrbGfx::WHITE)
      @gfx.draw_rect(b[:x], b[:y], b[:w], b[:h], FmrbGfx::BLACK)
      text_color = FmrbGfx::BLACK
    end

    label = b[:label]
    label = "反転 #{@inverted ? '入' : '切'}" if b[:group] == :invert

    tw = @gfx.text_width(label, :ja, 8)
    tx = b[:x] + (b[:w] - tw) / 2
    ty = b[:y] + (b[:h] - 8) / 2
    @gfx.draw_text(tx, ty, label, text_color)
  end

  def draw_value_label(x, y, w, h, label)
    @gfx.fill_rect(x, y, w, h, FmrbGfx::WHITE)
    @gfx.draw_rect(x, y, w, h, FmrbGfx::GRAY)
    tw = @gfx.text_width(label, :ja, 8)
    tx = x + (w - tw) / 2
    ty = y + (h - 8) / 2
    @gfx.draw_text(tx, ty, label, FmrbGfx::BLACK)
  end

  def button_selected?(b)
    case b[:group]
    when :motif  then @motif  == b[:value]
    when :center then @center == b[:value]
    when :invert then @inverted
    else false
    end
  end
end

Log.info("KamonApp.new")
begin
  app = KamonApp.new
  Log.info("KamonApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
