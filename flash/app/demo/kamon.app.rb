# Kamon (Japanese family crest) generator
#
# Port of a p5.js sketch by ksbmyk, at
# https://ksbmyk.github.io/sketch/events/kamon.html. The credit line at the
# bottom of the window carries the full URL, so a reader can go and look at
# the original.
#
# 5 motifs (maru / hishi / hana / ougi / uroko) rendered with rotational
# symmetry, optional center decoration, count / size / invert parameters.
#
# The control panel is built with FmrbUI: toggles in two exclusive groups
# for motif and center, two steppers for count and size, one plain toggle
# for invert. FmrbUI owns the hit test, the pressed look and the redraw, so
# this app only answers "which widget did the user operate" and repaints its
# own picture. Drawing is event driven; on_update never draws.
#
# Layout (window 300x200, coordinates below are user-area relative):
#   x=  4..184  (180x180)  preview area
#   x=188..296  (108 wide) control panel

class KamonApp < FmrbApp
  TWO_PI  = Math::PI * 2
  HALF_PI = Math::PI / 2
  PI      = Math::PI

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

  # Where this sketch comes from. 49 ASCII characters at 6px is 294 of the
  # 298px user area, so it only fits as a full-width line under everything
  # else - which is why the invert toggle sits at 174 rather than the bottom.
  SOURCE_URL = "https://ksbmyk.github.io/sketch/events/kamon.html"

  def initialize
    super()
    @motif    = "maru"
    @center   = "white"
    @count    = 5
    @size     = 0.7
    @inverted = false
  end

  def on_create
    @p5 = P5.new(@gfx)
    build_panel
    clear_user_area(FmrbGfx::WHITE)
    draw_window_frame
    draw_preview
    @ui.flush
  end

  def on_update
    100
  end

  def on_destroy
    # Restore the global font in case we were interrupted mid-draw with
    # the ja font selected.
    @gfx.set_font(:default) if @gfx
  end

  def on_event(ev)
    return unless running?
    # The close button is handled by super; FmrbUI widgets live inside the
    # user area, so there is nothing to steer around here.
    id = @ui.handle(ev)
    if id.nil?
      @ui.flush
      return
    end

    case id
    when :m_maru  then @motif = "maru"
    when :m_hishi then @motif = "hishi"
    when :m_hana  then @motif = "hana"
    when :m_ougi  then @motif = "ougi"
    when :m_uroko then @motif = "uroko"
    when :c_none  then @center = "none"
    when :c_white then @center = "white"
    when :c_black then @center = "black"
    when :c_ring  then @center = "ring"
    when :count   then @count = @count_st.value
    when :size    then @size = @size_st.value / 100.0
    when :invert  then @inverted = @ui.on?(:invert)
    end
    draw_preview
    @ui.flush
  end

  private

  # -----------------------------------------------------------------------
  # Control panel
  # -----------------------------------------------------------------------

  # Single-kanji labels for at-a-glance recognition. Kanji and ASCII are both
  # drawn by FmrbUI's mixed renderer, so no font switching happens here.
  def build_panel
    @ui = FmrbUI.new(self)
    x = PANEL_X

    @ui.label(:l_motif, x, 4, PANEL_W, 10, "紋様")
    @ui.toggle(:m_maru,  x,      14, 50, 16, "円", group: :motif, on: true)
    @ui.toggle(:m_hishi, x + 58, 14, 50, 16, "菱", group: :motif)
    @ui.toggle(:m_hana,  x,      32, 50, 16, "花", group: :motif)
    @ui.toggle(:m_ougi,  x + 58, 32, 50, 16, "扇", group: :motif)
    @ui.toggle(:m_uroko, x,      50, 50, 16, "鱗", group: :motif)

    @ui.label(:l_center, x, 72, PANEL_W, 10, "中央")
    @ui.toggle(:c_none,  x,      82, 50, 16, "無", group: :center)
    @ui.toggle(:c_white, x + 58, 82, 50, 16, "白", group: :center, on: true)
    @ui.toggle(:c_black, x,     100, 50, 16, "黒", group: :center)
    @ui.toggle(:c_ring,  x + 58, 100, 50, 16, "輪", group: :center)

    @ui.label(:l_count, x, 120, PANEL_W, 10, "数")
    @count_st = @ui.stepper(:count, x, 130, PANEL_W, 14,
                            @count, COUNT_MIN, COUNT_MAX)

    @ui.label(:l_size, x, 148, PANEL_W, 10, "大きさ")
    @size_st = @ui.stepper(:size, x, 160, PANEL_W, 14,
                           (@size * 100).round, SIZE_MIN, SIZE_MAX, SIZE_STEP)
    @size_st.suffix = "%"

    @ui.toggle(:invert, x, 174, PANEL_W, 12, "反転 切", on_text: "反転 入")

    # Credit line, full width under both columns.
    @ui.label(:source, 2, 188, @user_area_width - 4, 9, SOURCE_URL)
    nil
  end

  # -----------------------------------------------------------------------
  # Drawing
  # -----------------------------------------------------------------------

  def draw_preview
    @gfx.fill_rect(@user_area_x0 + PREVIEW_X, @user_area_y0 + PREVIEW_Y,
                   PREVIEW_SIZE, PREVIEW_SIZE, FmrbGfx::WHITE)
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
