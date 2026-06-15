# StackChan parametric face
#
# Face design derived from M5Stack StackChan (https://github.com/m5stack/StackChan),
# MIT License, Copyright (c) 2026 M5Stack Technology CO LTD. No StackChan source
# code or image assets are included; the geometry/animation is reimplemented in
# Ruby. See THIRD_PARTY_LICENSES.md.
#
# Port of the M5Stack "StackChan" default avatar to Family mruby. The original face is drawn with LVGL containers: two filled circles
# (eyes, with a sliding eyelid for blinking) and a rounded rectangle (mouth)
# whose size/shape morphs by a "weight" parameter. Emotions, blinking and a
# breathing idle animation are all parametric. We reproduce the same geometry
# directly with FmrbGfx primitives (fill_circle / fill_rect / fill_round_rect).
#
# Design canvas is 320x240 (same as the original M5Stack screen). Center-origin
# design params are mapped to the window's user area with a uniform scale so the
# face stays proportional when the window is resized.
#
# Interaction: left click cycles the emotion (Neutral -> Happy -> Sleepy ->
# Sad -> Angry -> ...).

class StackChanApp < FmrbApp
  FRAME_INTERVAL    = 50      # ms (20fps). breath 6600/50 = 132 steps, smooth enough
  BREATH_CYCLE_MS   = 6600
  BREATH_AMP_DESIGN = 16      # design px, scaled by sc at draw time
  BLINK_CLOSE_MS    = 90
  BLINK_OPEN_MS     = 90
  DESIGN_W = 320              # reference screen the center-origin params assume
  DESIGN_H = 240
  EYE_MOVE_DESIGN   = 16      # max eye offset (design px) when tracking the cursor
  GAZE_EASE         = 0.35    # per-frame easing toward the target gaze (0..1)

  # Emotion table (ordered for click cycling).
  #   eye_weight   : resting eye openness (100 open .. 25 closed)
  #   eye_tilt     : design-px vertical offset, applied +/- to L/R eye to hint brow
  #   mouth_weight : mouth open amount (0 closed .. 100 wide open)
  EMOTIONS = [
    { name: "Neutral", eye_weight: 100, eye_tilt:  0, mouth_weight:  0 },
    { name: "Happy",   eye_weight:  72, eye_tilt: -6, mouth_weight: 65 },
    { name: "Sleepy",  eye_weight:  35, eye_tilt:  0, mouth_weight: 10 },
    { name: "Sad",     eye_weight:  80, eye_tilt:  6, mouth_weight:  0 },
    { name: "Angry",   eye_weight:  90, eye_tilt: -8, mouth_weight: 25 },
  ]

  def initialize
    super()
  end

  def on_create
    @elapsed_ms    = 0
    @emotion_idx   = 0
    @blink_state   = :open
    @blink_t0      = 0
    @next_blink_at = 2000 + rand(4000)
    @gaze_x  = 0.0   # current eye offset (design px), eased toward target
    @gaze_y  = 0.0
    @gaze_tx = 0.0   # target eye offset set from mouse_move
    @gaze_ty = 0.0
    draw_face
  end

  def on_update
    @elapsed_ms += FRAME_INTERVAL
    draw_face
    FRAME_INTERVAL
  end

  def on_event(ev)
    super(ev)
    return unless @running
    if ev[:type] == :mouse_move
      # Aim the gaze at the cursor: normalize its position relative to the face
      # center (-1..1), clamp, and scale to the max eye offset (design space).
      cx = @user_area_x0 + @user_area_width  / 2
      cy = @user_area_y0 + @user_area_height / 2
      nx = (ev[:x] - cx) / (@user_area_width  / 2.0)
      ny = (ev[:y] - cy) / (@user_area_height / 2.0)
      nx = -1.0 if nx < -1.0; nx = 1.0 if nx > 1.0
      ny = -1.0 if ny < -1.0; ny = 1.0 if ny > 1.0
      @gaze_tx = nx * EYE_MOVE_DESIGN
      @gaze_ty = ny * EYE_MOVE_DESIGN
      return
    end
    if ev[:type] == :mouse_up && ev[:button] == 1
      # Avoid the close-button hit zone (top-right), same as shapes.app.rb.
      cbx = @window_width - CLOSE_BTN_CX_OFFSET
      return if (ev[:x] - cbx).abs <= CLOSE_BTN_HIT_R &&
                (ev[:y] - CLOSE_BTN_CY).abs <= CLOSE_BTN_HIT_R
      @emotion_idx = (@emotion_idx + 1) % EMOTIONS.size
      # Next on_update (<= FRAME_INTERVAL away) repaints; no forced redraw.
    end
  end

  def on_resize(new_width, new_height)
    # Scale is recomputed from @user_area_* every frame, so nothing to do here.
  end

  private

  # Uniform design->pixel scale that fits the 320x240 design into the user area.
  def compute_scale
    sx = @user_area_width  / DESIGN_W.to_f
    sy = @user_area_height / DESIGN_H.to_f
    sx < sy ? sx : sy
  end

  # Vertical breathing offset (pixels) from a sine wave over BREATH_CYCLE_MS.
  def compute_breath_dy(sc)
    phase = (@elapsed_ms % BREATH_CYCLE_MS) / BREATH_CYCLE_MS.to_f
    (Math.sin(2 * Math::PI * phase) * BREATH_AMP_DESIGN * sc).to_i
  end

  # Eye closed fraction (0 open .. 1 closed): emotion resting openness combined
  # additively with the blink pulse so even a sleepy face fully closes on blink.
  def compute_closed_frac(emotion)
    blink_frac =
      case @blink_state
      when :open
        if @elapsed_ms >= @next_blink_at
          @blink_state = :closing
          @blink_t0 = @elapsed_ms
        end
        0.0
      when :closing
        f = (@elapsed_ms - @blink_t0) / BLINK_CLOSE_MS.to_f
        if f >= 1.0
          f = 1.0
          @blink_state = :opening
          @blink_t0 = @elapsed_ms
        end
        f
      when :opening
        f = 1.0 - (@elapsed_ms - @blink_t0) / BLINK_OPEN_MS.to_f
        if f <= 0.0
          f = 0.0
          @blink_state = :open
          @next_blink_at = @elapsed_ms + 2000 + rand(4000)
        end
        f
      else
        0.0
      end

    rest_closed = (100 - emotion[:eye_weight]) / 75.0
    rest_closed = 0.0 if rest_closed < 0.0
    rest_closed = 1.0 if rest_closed > 1.0
    cf = rest_closed + (1.0 - rest_closed) * blink_frac
    cf = 0.0 if cf < 0.0
    cf = 1.0 if cf > 1.0
    cf
  end

  # White eye circle, then a black rect drops from the top as the eyelid.
  # A 2px sliver is kept when fully closed so it reads as a cute closed line.
  def draw_eye(ex, ey, r, closed_frac)
    @gfx.fill_circle(ex, ey, r, FmrbGfx::WHITE)
    lid_h = (2 * r * closed_frac).to_i
    lid_h = 2 * r - 2 if lid_h > 2 * r - 2
    @gfx.fill_rect(ex - r, ey - r, 2 * r + 1, lid_h, FmrbGfx::BLACK) if lid_h > 0
  end

  # Rounded rectangle mouth, W/H/radius interpolated by weight (0 closed..100 open).
  def draw_mouth(mx, my, sc, weight)
    t  = weight / 100.0
    pw = ((90 + (60 - 90) * t) * sc).to_i
    ph = (( 6 + (50 -  6) * t) * sc).to_i
    pr = (( 0 + (16 -  0) * t) * sc).to_i
    cap = (pw < ph ? pw : ph) / 2
    pr = cap if pr > cap
    @gfx.fill_round_rect(mx - pw / 2, my - ph / 2, pw, ph, pr, FmrbGfx::WHITE)
  end

  def draw_face
    emotion     = EMOTIONS[@emotion_idx]
    sc          = compute_scale
    closed_frac = compute_closed_frac(emotion)
    breath_dy   = compute_breath_dy(sc)

    cx = @user_area_x0 + @user_area_width  / 2
    cy = @user_area_y0 + @user_area_height / 2

    clear_user_area(FmrbGfx::BLACK)

    # Ease the gaze toward the cursor target, then offset both eyes by it.
    @gaze_x += (@gaze_tx - @gaze_x) * GAZE_EASE
    @gaze_y += (@gaze_ty - @gaze_y) * GAZE_EASE
    gx = (@gaze_x * sc).to_i
    gy = (@gaze_y * sc).to_i

    r    = (16 * sc).round
    r    = 1 if r < 1
    lx   = cx + (-70 * sc).to_i + gx
    rx   = cx + ( 70 * sc).to_i + gx
    ey   = cy + (-16 * sc).to_i + breath_dy + gy
    tilt = (emotion[:eye_tilt] * sc).to_i
    draw_eye(lx, ey + tilt, r, closed_frac)
    draw_eye(rx, ey - tilt, r, closed_frac)

    mx = cx
    my = cy + (26 * sc).to_i + breath_dy
    draw_mouth(mx, my, sc, emotion[:mouth_weight])

    draw_window_frame
    @gfx.present
  end
end

begin
  app = StackChanApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
