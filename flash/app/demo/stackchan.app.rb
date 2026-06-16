# StackChan parametric face
#
# Face design derived from M5Stack StackChan (https://github.com/m5stack/StackChan),
# MIT License, Copyright (c) 2026 M5Stack Technology CO LTD. No StackChan source
# code or image assets are included; the geometry/animation is reimplemented in
# Ruby. See THIRD_PARTY_LICENSES.md.
#
# Port of the M5Stack "StackChan" default avatar to Family mruby.
#
# Reproduces the full StackChan expression set with FmrbGfx primitives:
#   - 6 emotions : Neutral / Happy / Angry / Sad / Doubt / Sleepy
#                  (eye openness + tilted eyelid; Happy uses a bottom lid so the
#                   eyes form an upward "^_^" arc)
#   - 5 emotes   : anger mark / heart / shy blush / sweat drop / dizzy spirals
#   - talking    : mouth opens/closes (occasionally on its own, 180ms toggle)
#   - blink, breathing, and gaze-follow (eyes track the mouse cursor)
#
# Design canvas is 320x240 (the original M5Stack screen). Center-origin design
# params are mapped to the window's user area with a uniform scale so the face
# stays proportional when the window is resized.
#
# Left click cycles through every expression/emote preset.
#
# Remote control (inter-app pub/sub): other apps drive the face by
# `publish("stackchan", payload)` where payload is a small hash keyed by "type":
#   {"type"=>"talk",    "on"=>true/false}     # start/stop the talking animation
#   {"type"=>"mouth",   "level"=>0..100}      # set mouth open directly (lip-sync;
#                                             #   auto-released if no update ~250ms)
#   {"type"=>"emotion", "name"=>"Happy"}      # Neutral/Happy/Angry/Sad/Doubt/Sleepy
#   {"type"=>"emote",   "name"=>"heart"}      # heart/shy/sweat/angry/dizzy, nil=clear
# See stackchan_remote.app.rb for a demo publisher.

class StackChanApp < FmrbApp
  TOPIC = "stackchan"         # pub/sub topic other apps publish to (remote control)
  FRAME_INTERVAL    = 50      # ms (20fps)
  DESIGN_W = 320              # reference screen the center-origin params assume
  DESIGN_H = 240

  # Breathing (StackChan: amplitude 16px, cycle 6600ms)
  BREATH_CYCLE_MS   = 6600
  BREATH_AMP_DESIGN = 16

  # Blinking
  BLINK_CLOSE_MS    = 90
  BLINK_OPEN_MS     = 90

  # Gaze following
  EYE_MOVE_DESIGN   = 16      # max eye offset (design px) when tracking the cursor
  GAZE_EASE         = 0.35

  # Talking (StackChan SpeakingModifier: 180ms toggle, open 40-80, closed 0-20)
  TALK_TOGGLE_MS    = 180
  EXT_MOUTH_HOLD    = 250     # ms to hold an externally-set mouth level (lip-sync)

  # Eye / mouth base geometry (design space, center origin).
  # StackChan default eye is a 20px circle (radius 10); mouth params from mouth.cpp.
  EYE_R_DESIGN      = 10
  EYE_DX_DESIGN     = 70
  EYE_DY_DESIGN     = -16
  MOUTH_DY_DESIGN   = 26
  BLINK_WEIGHT      = 25      # StackChan blinks to weight 25 (not fully shut)

  # Custom RGB332 colors for the emote decorators
  C_ORANGE = 0xF5   # anger mark   (~0xFDB034)
  C_RED    = 0xE0   # heart        (~0xE13232)
  C_PINK   = 0xF6   # shy blush    (~0xF7A59E)
  C_CYAN   = 0x7B   # sweat drop   (~0x75E1FF)

  # Per-emotion eye + mouth parameters, taken verbatim from the StackChan
  # DefaultEyes::setEmotion() table (eyes.cpp).
  #   w     : eye weight 0..100 (eyelid coverage = (100-w)/100; 100=fully open)
  #   rot   : eyelid rotation in 0.1deg units (LVGL). Applied +rot to the left
  #           eye and -rot to the right (mirrored), exactly like the original.
  #           Happy's 1550 (=155deg) flips the lid below center -> smiling eyes.
  #   mouth : resting mouth open weight (0 closed .. 100 open)
  EYE_PARAMS = {
    "Neutral" => { w: 100, rot:    0, mouth: 10 },
    "Happy"   => { w:  72, rot: 1550, mouth: 70 },
    "Angry"   => { w:  70, rot:  450, mouth: 25 },
    "Sad"     => { w:  70, rot: -400, mouth: 10 },
    "Doubt"   => { w:  75, rot:    0, mouth: 15 },
    "Sleepy"  => { w:  35, rot:  -50, mouth:  5 },
  }

  # Expression / emote presets cycled by left click. Covers all 6 emotions,
  # all 5 decorators, and an explicit talking preset.
  PRESETS = [
    { name: "Neutral", emo: "Neutral", deco: nil,     dizzy: false, talk: false },
    { name: "Happy",   emo: "Happy",   deco: nil,     dizzy: false, talk: false },
    { name: "Love",    emo: "Happy",   deco: :heart,  dizzy: false, talk: false },
    { name: "Shy",     emo: "Happy",   deco: :shy,    dizzy: false, talk: false },
    { name: "Doubt",   emo: "Doubt",   deco: nil,     dizzy: false, talk: false },
    { name: "Angry",   emo: "Angry",   deco: :angry,  dizzy: false, talk: false },
    { name: "Sad",     emo: "Sad",     deco: nil,     dizzy: false, talk: false },
    { name: "Worried", emo: "Sad",     deco: :sweat,  dizzy: false, talk: false },
    { name: "Sleepy",  emo: "Sleepy",  deco: nil,     dizzy: false, talk: false },
    { name: "Dizzy",   emo: "Neutral", deco: nil,     dizzy: true,  talk: false },
    { name: "Talking", emo: "Neutral", deco: nil,     dizzy: false, talk: true  },
  ]

  def initialize
    super()
  end

  def on_create
    @elapsed_ms    = 0
    @idx           = 0
    @blink_state   = :open
    @blink_t0      = 0
    @next_blink_at = 2000 + rand(4000)
    @gaze_x  = 0.0
    @gaze_y  = 0.0
    @gaze_tx = 0.0
    @gaze_ty = 0.0
    @spin    = 0.0           # dizzy spiral phase
    # Talking state
    @talk_active     = false
    @talk_end_at     = 0
    @next_talk_at    = @elapsed_ms + 4000 + rand(6000)
    @mouth_toggle_at = 0
    @mouth_open      = false
    @talk_weight     = 0
    # Remote-control state (driven by on_control via the "stackchan" topic)
    @remote_talk     = false
    @ext_mouth_level = 0
    @ext_mouth_until = 0
    subscribe(TOPIC)
    apply_preset(0)
    draw_face
  end

  def on_update
    @elapsed_ms += FRAME_INTERVAL
    @spin += 0.30
    draw_face
    FRAME_INTERVAL
  end

  def on_event(ev)
    super(ev)
    return unless @running
    if ev[:type] == :mouse_move
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
      cbx = @window_width - CLOSE_BTN_CX_OFFSET
      return if (ev[:x] - cbx).abs <= CLOSE_BTN_HIT_R &&
                (ev[:y] - CLOSE_BTN_CY).abs <= CLOSE_BTN_HIT_R
      @idx = (@idx + 1) % PRESETS.size
      apply_preset(@idx)
    end
  end

  # Apply a local preset to the independent face state (also used as the base
  # state that remote-control commands then override piecemeal).
  def apply_preset(idx)
    p = PRESETS[idx]
    @emotion     = p[:emo]
    @deco        = p[:deco]
    @dizzy       = p[:dizzy]
    @preset_talk = p[:talk]
    @mode_name   = p[:name]
    log_mode
  end

  def log_mode
    ep = EYE_PARAMS[@emotion]
    arch = (ep[:rot].abs >= 900) ? " (^_^)" : ""   # lid swings below center
    Log.info("mode: #{@mode_name} (emotion=#{@emotion}, deco=#{@deco || "none"}, talk=#{@preset_talk}, dizzy=#{@dizzy})#{arch}")
  end

  # Remote control: another app published to the "stackchan" topic. Only update
  # state here; the next on_update repaints (avoids reentrant drawing).
  def on_control(msg)
    return unless msg["cmd"] == "topic_data" && msg["topic"] == TOPIC
    data = msg["data"]
    return unless data
    case data["type"]
    when "talk"
      @remote_talk = data["on"] ? true : false
      Log.info("remote: talk=#{@remote_talk}")
    when "mouth"
      lv = data["level"].to_i
      lv = 0 if lv < 0
      lv = 100 if lv > 100
      @ext_mouth_level = lv
      @ext_mouth_until = @elapsed_ms + EXT_MOUTH_HOLD
    when "emotion"
      name = data["name"]
      if name && EYE_PARAMS[name]
        @emotion = name
        Log.info("remote: emotion=#{name}")
      end
    when "emote"
      name = data["name"]
      if name == "dizzy"
        @dizzy = true
        @deco = nil
      else
        @dizzy = false
        @deco = name ? name.to_sym : nil
      end
      Log.info("remote: emote=#{name || "none"}")
    end
  end

  def on_destroy
    unsubscribe(TOPIC)
  end

  def on_resize(new_width, new_height)
  end

  private

  def compute_scale
    sx = @user_area_width  / DESIGN_W.to_f
    sy = @user_area_height / DESIGN_H.to_f
    sx < sy ? sx : sy
  end

  def compute_breath_dy(sc)
    phase = (@elapsed_ms % BREATH_CYCLE_MS) / BREATH_CYCLE_MS.to_f
    (Math.sin(2 * Math::PI * phase) * BREATH_AMP_DESIGN * sc).to_i
  end

  # Blink pulse 0(open)..1(closed), driven by a state machine on @elapsed_ms.
  def compute_blink_frac
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
  end

  # Resolve the mouth open weight. Priority:
  #   1. external lip-sync level (recent "mouth" command)
  #   2. talking (remote talk, the "Talking" preset, or occasional auto-talk)
  #   3. the current emotion's resting mouth
  def resolve_mouth
    return @ext_mouth_level if @elapsed_ms < @ext_mouth_until

    base = EYE_PARAMS[@emotion][:mouth]
    forced = @preset_talk || @remote_talk
    unless @dizzy || forced
      if !@talk_active && @elapsed_ms >= @next_talk_at
        @talk_active = true
        @talk_end_at = @elapsed_ms + 1500 + rand(1500)
      end
      if @talk_active && @elapsed_ms >= @talk_end_at
        @talk_active = false
        @next_talk_at = @elapsed_ms + 5000 + rand(7000)
      end
    end
    talking = forced || @talk_active
    return base unless talking
    if @elapsed_ms >= @mouth_toggle_at
      @mouth_open = !@mouth_open
      @talk_weight = @mouth_open ? (40 + rand(41)) : rand(21)
      @mouth_toggle_at = @elapsed_ms + TALK_TOGGLE_MS
    end
    @talk_weight
  end

  # --- Eye rendering ---------------------------------------------------------

  def fill_quad(ax, ay, bx, by, cx, cy, dx, dy, color)
    @gfx.fill_triangle(ax.to_i, ay.to_i, bx.to_i, by.to_i, cx.to_i, cy.to_i, color)
    @gfx.fill_triangle(ax.to_i, ay.to_i, cx.to_i, cy.to_i, dx.to_i, dy.to_i, color)
  end

  # Faithful StackChan eyelid: a black square the size of the eye, slid by
  # `weight` and rotated by `deg` about the eye CENTER (eyes.cpp). We model it as
  # a black half-plane whose boundary line, before rotation, sits at signed
  # height f*r above center (f = 1 - weight/50) with the covered side toward the
  # top, then rotate that line+normal about the center by `deg`.
  #   weight 100 -> f=-1 : edge at top, covers nothing (fully open)
  #   weight   0 -> f=+1 : edge at bottom, covers everything (shut)
  #   weight  72 -> f=-0.44 + rot 155deg -> lid swings below -> smiling eyes
  # Quads extend beyond the circle; overdraw lands on background (eyes never
  # overlap, mouth/decorators redraw on top), so no clipping is needed.
  def cover_eyelid(ex, ey, r, weight, deg)
    f = 1.0 - weight / 50.0
    return if f <= -1.0            # fully open
    t = deg * Math::PI / 180.0
    c = Math.cos(t)
    s = Math.sin(t)
    # foot of the boundary line, rotated about center: R(0, f*r)
    footx = ex - (f * r) * s
    footy = ey + (f * r) * c
    nx =  s                        # covered-side normal: R(0,-1)
    ny = -c
    dx =  c                        # line direction: R(1,0)
    dy =  s
    al    = 2.6 * r
    depth = 2.8 * r
    ax = footx - al * dx; ay = footy - al * dy
    bx = footx + al * dx; by = footy + al * dy
    cx = bx + depth * nx; cy = by + depth * ny
    ddx = ax + depth * nx; ddy = ay + depth * ny
    fill_quad(ax, ay, bx, by, cx, cy, ddx, ddy, FmrbGfx::BLACK)
  end

  # weight: 0..100 openness (blink lowers it toward BLINK_WEIGHT); rot in 0.1deg.
  def draw_eye(ex, ey, r, weight, rot_units)
    @gfx.fill_circle(ex, ey, r, FmrbGfx::WHITE)
    cover_eyelid(ex, ey, r, weight, rot_units / 10.0)
  end

  # Spinning spiral that replaces an eye when dizzy.
  def draw_spiral(ex, ey, r, phase)
    n = 14
    px = ex.to_f
    py = ey.to_f
    i = 1
    while i <= n
      frac = i.to_f / n
      ang  = phase + frac * Math::PI * 4.0
      rad  = r * frac
      x = ex + Math.cos(ang) * rad
      y = ey + Math.sin(ang) * rad
      @gfx.draw_thick_line(px.to_i, py.to_i, x.to_i, y.to_i, 2, FmrbGfx::WHITE)
      px = x; py = y
      i += 1
    end
  end

  def draw_mouth(mx, my, sc, weight)
    t  = weight / 100.0
    pw = ((90 + (60 - 90) * t) * sc).to_i
    ph = (( 6 + (50 -  6) * t) * sc).to_i
    pr = (( 0 + (16 -  0) * t) * sc).to_i
    cap = (pw < ph ? pw : ph) / 2
    pr = cap if pr > cap
    @gfx.fill_round_rect(mx - pw / 2, my - ph / 2, pw, ph, pr, FmrbGfx::WHITE)
  end

  # --- Decorators (emote overlays) -------------------------------------------

  # Rotate point (px,py) by `t` radians around pivot (ox,oy). Returns [x,y] ints.
  def rot_pt(px, py, ox, oy, cosv, sinv)
    dx = px - ox
    dy = py - oy
    [(ox + dx * cosv - dy * sinv).to_i, (oy + dx * sinv + dy * cosv).to_i]
  end

  # Heart centered at (x,y), tilted by deg (StackChan pulses it 15deg<->20deg).
  def draw_heart(x, y, s, deg)
    t = deg * Math::PI / 180.0
    c = Math.cos(t)
    si = Math.sin(t)
    lobe = (s * 0.7).to_i
    lx, ly = rot_pt(x - s / 2, y, x, y, c, si)
    rx, ry = rot_pt(x + s / 2, y, x, y, c, si)
    @gfx.fill_circle(lx, ly, lobe, C_RED)
    @gfx.fill_circle(rx, ry, lobe, C_RED)
    ax, ay = rot_pt(x - s, y, x, y, c, si)
    bx, by = rot_pt(x + s, y, x, y, c, si)
    tpx, tpy = rot_pt(x, y + (s * 1.5), x, y, c, si)
    @gfx.fill_triangle(ax, ay, bx, by, tpx, tpy, C_RED)
  end

  def draw_shy_blush(x, y, rx, ry)
    @gfx.fill_ellipse(x, y, rx, ry, C_PINK)
  end

  def draw_sweat(x, y, s)
    @gfx.fill_circle(x, y, s, C_CYAN)
    @gfx.fill_triangle(x - s, y - (s * 0.2).to_i, x + s, y - (s * 0.2).to_i,
                       x, y - (s * 2.0).to_i, C_CYAN)
  end

  # Manga-style anger burst (rotating asterisk of orange spokes).
  def draw_angry_mark(x, y, s, phase)
    k = 0
    while k < 6
      a  = phase + k * (Math::PI / 3.0)
      x1 = x + Math.cos(a) * s * 0.4
      y1 = y + Math.sin(a) * s * 0.4
      x2 = x + Math.cos(a) * s
      y2 = y + Math.sin(a) * s
      @gfx.draw_thick_line(x1.to_i, y1.to_i, x2.to_i, y2.to_i, 2, C_ORANGE)
      k += 1
    end
  end

  # --- Frame -----------------------------------------------------------------

  def draw_face
    ep     = EYE_PARAMS[@emotion]
    sc     = compute_scale

    blink     = compute_blink_frac
    breath_dy = compute_breath_dy(sc)
    mouth_w   = resolve_mouth

    cx = @user_area_x0 + @user_area_width  / 2
    cy = @user_area_y0 + @user_area_height / 2

    @gaze_x += (@gaze_tx - @gaze_x) * GAZE_EASE
    @gaze_y += (@gaze_ty - @gaze_y) * GAZE_EASE
    gx = (@gaze_x * sc).to_i
    gy = (@gaze_y * sc).to_i

    clear_user_area(FmrbGfx::BLACK)

    r  = (EYE_R_DESIGN * sc).round
    r  = 2 if r < 2
    lx = cx + (-EYE_DX_DESIGN * sc).to_i + gx
    rx = cx + ( EYE_DX_DESIGN * sc).to_i + gx
    ey = cy + ( EYE_DY_DESIGN * sc).to_i + breath_dy + gy

    if @dizzy
      draw_spiral(lx, ey, r, @spin)
      draw_spiral(rx, ey, r, @spin + Math::PI / 4.0)
    else
      # Blink eases the eye weight toward BLINK_WEIGHT (StackChan blinks to 25).
      ew = ep[:w] + (BLINK_WEIGHT - ep[:w]) * blink
      draw_eye(lx, ey, r, ew,  ep[:rot])
      draw_eye(rx, ey, r, ew, -ep[:rot])
    end

    mx = cx + gx / 2
    my = cy + (MOUTH_DY_DESIGN * sc).to_i + breath_dy
    draw_mouth(mx, my, sc, mouth_w)

    draw_decorator(@deco, cx, cy, sc, breath_dy)

    draw_window_frame
    @gfx.present
  end

  def draw_decorator(deco, cx, cy, sc, breath_dy)
    return if deco.nil?
    case deco
    when :heart
      # StackChan: a single heart at (108,-70), pulsing 15deg<->20deg.
      s   = (12 * sc).round
      deg = (@elapsed_ms % 1000 < 500) ? 15 : 20
      draw_heart(cx + (108 * sc).to_i, cy + (-70 * sc).to_i + breath_dy, s, deg)
    when :shy
      # StackChan: blush at the outer cheeks (-108,28) and (108,28).
      rx = (16 * sc).round
      ry = (9 * sc).round
      by = cy + (28 * sc).to_i + breath_dy
      draw_shy_blush(cx + (-108 * sc).to_i, by, rx, ry)
      draw_shy_blush(cx + ( 108 * sc).to_i, by, rx, ry)
    when :sweat
      s = (8 * sc).round
      # Falling drop: cycle the vertical offset over ~2.1s (matches the 5-frame fall).
      fall = (@elapsed_ms % 2100) * 34 / 2100   # 0..34 design px
      y = cy + (-70 * sc).to_i + (fall * sc).to_i + breath_dy
      draw_sweat(cx + (-72 * sc).to_i, y, s)
    when :angry
      s = (14 * sc).round
      draw_angry_mark(cx + (72 * sc).to_i, cy + (-72 * sc).to_i + breath_dy, s, @spin)
    end
  end
end

begin
  app = StackChanApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
