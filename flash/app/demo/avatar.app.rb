# Avatar: Moku, a small puff with a sprout who lives on the desktop
# (doc/frameless_window W4). A frameless window (window_frame = "none"): the
# base draws no frame and leaves the ground see-through, the kernel moves it
# from anywhere and turns a press-and-release into a click, and clicks on the
# see-through parts go to what is behind. The app only draws the picture.
#
# Movable (doc/checkpoint): `amove` takes it to another machine with its
# state -- its name, how many times it was patted and the places it has been.
# The picture is in this file (strings, one letter per colour), so it looks
# the same wherever it lands; on_restore adds the new place and draws it all
# again (nothing drawn travels in the image).
#
# Drawing follows what changes: the sprout sways (2 rows), the eyes blink
# (2 rows), a click shows a happy face (4 rows) and a speech bubble above.
# Only the rows that differ from what is on screen are drawn again.
class AvatarApp < FmrbApp
  SCALE = 3
  ART_W = 16
  ART_H = 16
  WIN_W = 120
  AV_X = 36          # (WIN_W - ART_W * SCALE) / 2
  AV_Y = 38          # below the bubble
  BUBBLE_H = 32      # the bubble's box (two lines)
  TAIL_Y = 37        # the tip of the bubble's tail

  # Colours (RGB332). Never 0x01: that is the see-through key.
  INK = 0x49         # outline: dark slate
  BODY = 0xDB        # pale lavender
  EYE = 0x00
  CHEEK = 0xED       # pink
  MOUTH = 0xC4       # red
  LEAF = 0x14        # dark green
  PAPER = 0xFF       # the bubble

  # The sprout, rows 0-2: leaning right and leaning left.
  SPROUT = [
    ["........GGG.....",
     ".......GG.......",
     ".......G........"],
    [".....GGG........",
     "......GG........",
     ".......G........"],
  ]

  # The body, rows 3-15; rows 7-10 come from a face.
  BODY_ROWS = [
    ".....KKKKKK.....",
    "...KKBBBBBBKK...",
    "..KBBBBBBBBBBK..",
    ".KBBBBBBBBBBBBK.",
    nil, nil, nil, nil,
    "KBBBBBBBBBBBBBBK",
    ".KBBBBBBBBBBBBK.",
    "..KKBBBBBBBBKK..",
    "...KKKKKKKKKK...",
    "...KKK....KKK...",
  ]

  # Faces, rows 7-10: 0 calm, 1 blinking, 2 happy.
  FACES = [
    [".KBBEBBBBBBEBBK.",
     "KBBBEBBBBBBEBBBK",
     "KBBBBBBKKBBBBBBK",
     "KBBBBBBBBBBBBBBK"],
    [".KBBBBBBBBBBBBK.",
     "KBBKKKBBBBKKKBBK",
     "KBBBBBBKKBBBBBBK",
     "KBBBBBBBBBBBBBBK"],
    [".KBBKBBBBBBKBBK.",
     "KBBKBKBBBBKBKBBK",
     "KBBPPBBMMBBPPBBK",
     "KBBBBBBMMBBBBBBK"],
  ]

  SWAY_MS = 900
  BLINK_MS = 160
  HAPPY_MS = 1500
  BUBBLE_MS = 3000
  # Gaps between blinks, in turn (not too regular).
  BLINK_GAPS = [2600, 4100, 900, 3300, 5200]

  def on_create
    @name = "Moku"
    @pats = 0
    @places = [place_here]
    @gfx.set_font(:ja, 12)
    now = Machine.board_millis
    @leaf = 0
    @next_sway = now + SWAY_MS
    @blink_i = 0
    @next_blink = now + BLINK_GAPS[0]
    @blink_until = 0
    @happy_until = 0
    @bubble_until = 0
    @shown = nil
    paint_avatar
    if ja?
      say(["こんにちは", "#{@name} だよ"], now)
    else
      say(["Hello,", "I'm #{@name}"], now)
    end
    @gfx.present
    Log.info("avatar: hello at #{@places[-1]}")
  end

  # This machine's name: its Asterism node ID (as FmrbAsterism would pick
  # it, but only reading), else the board. Asked again on every arrival.
  def place_here
    name = ""
    begin
      if Object.const_defined?(:FmrbAsterism)
        if ::File.exist?(::FmrbAsterism::CONFIG_FILE)
          name = ::FmrbAsterism.read_config["node"].to_s
        end
        name = ::FmrbAsterism.hostname.to_s if name.empty?
        name = ::FmrbAsterism.system_conf("asterism_node").to_s if name.empty?
      end
    rescue => e
      Log.info("avatar: no node ID (#{e.message})")
      name = ""
    end
    name = FmrbConst::BOARD.to_s if name.empty?
    name
  end

  def ja?
    FmrbApp.language == "ja"
  end

  # --- the picture ---------------------------------------------------------

  def color_of(c)
    if c == "K" then INK
    elsif c == "B" then BODY
    elsif c == "E" then EYE
    elsif c == "P" then CHEEK
    elsif c == "M" then MOUTH
    elsif c == "G" then LEAF
    else TRANSPARENT_COLOR
    end
  end

  def face_now(now)
    return 2 if now < @happy_until
    return 1 if now < @blink_until
    0
  end

  def row_of(r, face)
    return SPROUT[@leaf][r] if r < 3
    return FACES[face][r - 7] if r >= 7 && r <= 10
    BODY_ROWS[r - 3]
  end

  # Draws the rows that differ from what is on screen (all of them when
  # nothing is). Returns whether anything was drawn.
  def paint_avatar
    face = face_now(Machine.board_millis)
    drawn = false
    @shown = Array.new(ART_H) unless @shown
    r = 0
    while r < ART_H
      row = row_of(r, face)
      unless row == @shown[r]
        paint_row(r, row)
        @shown[r] = row
        drawn = true
      end
      r += 1
    end
    drawn
  end

  # One row of the art: the band cleared to the see-through key, then a
  # rectangle per run of one colour.
  def paint_row(r, row)
    y = AV_Y + r * SCALE
    @gfx.fill_rect(AV_X, y, ART_W * SCALE, SCALE, TRANSPARENT_COLOR)
    x = 0
    while x < ART_W
      c = row[x]
      if c == "."
        x += 1
        next
      end
      x2 = x + 1
      x2 += 1 while x2 < ART_W && row[x2] == c
      @gfx.fill_rect(AV_X + x * SCALE, y, (x2 - x) * SCALE, SCALE, color_of(c))
      x = x2
    end
  end

  # --- the speech bubble ---------------------------------------------------

  # Shows lines (one or two) above the avatar until BUBBLE_MS from now.
  def say(lines, now)
    @bubble = lines
    @bubble_until = now + BUBBLE_MS
    paint_bubble
  end

  def paint_bubble
    clear_bubble
    lines = @bubble
    return unless lines
    lh = line_height
    max_w = WIN_W - 10
    widest = 0
    texts = []
    i = 0
    while i < lines.size
      t = @gfx.fit_text(lines[i], max_w, "..")
      texts << t
      w = @gfx.text_width(t)
      widest = w if w > widest
      i += 1
    end
    bw = widest + 10
    bh = texts.size * lh + 6
    bx = (WIN_W - bw) / 2
    by = BUBBLE_H - bh
    cx = WIN_W / 2
    @gfx.fill_round_rect(bx, by, bw, bh, 4, PAPER)
    @gfx.draw_round_rect(bx, by, bw, bh, 4, INK)
    @gfx.fill_triangle(cx - 4, BUBBLE_H - 1, cx + 4, BUBBLE_H - 1, cx, TAIL_Y, PAPER)
    @gfx.draw_line(cx - 4, BUBBLE_H - 1, cx, TAIL_Y, INK)
    @gfx.draw_line(cx + 4, BUBBLE_H - 1, cx, TAIL_Y, INK)
    i = 0
    while i < texts.size
      @gfx.draw_text(bx + 5, by + 3 + i * lh, texts[i], EYE)
      i += 1
    end
  end

  def clear_bubble
    @gfx.fill_rect(0, 0, WIN_W, AV_Y, TRANSPARENT_COLOR)
  end

  # The height of a line in the font this machine picked (efont 12, or
  # misaki 8 where there is no efont).
  def line_height
    m = FmrbGfx::FONT_METRICS[@gfx.current_font]
    m ? m[:line_h] : 12
  end

  # --- what happens --------------------------------------------------------

  def on_event(ev)
    # The kernel holds a left press and sends it with the release when the
    # pointer did not move away (a click); a drag moves the window instead.
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    pat(Machine.board_millis)
  end

  def pat(now)
    @pats += 1
    @happy_until = now + HAPPY_MS
    if @pats % 5 == 0
      route = @places.join(">")
      if ja?
        say(["#{@places.size} か所めぐったよ", route], now)
      else
        say(["Been to #{@places.size}:", route], now)
      end
    elsif ja?
      say(["えへへ", "なでなで #{@pats} 回目"], now)
    else
      say(["Hehe!", "Pats: #{@pats}"], now)
    end
    paint_avatar
    @gfx.present
    Log.info("avatar: pat #{@pats}")
  end

  def on_update
    now = Machine.board_millis
    drawn = false
    if now >= @next_sway
      @leaf = 1 - @leaf
      @next_sway = now + SWAY_MS
    end
    if now >= @next_blink
      @blink_until = now + BLINK_MS
      @blink_i += 1
      @next_blink = now + BLINK_GAPS[@blink_i % BLINK_GAPS.size]
    end
    if @bubble && now >= @bubble_until
      @bubble = nil
      clear_bubble
      drawn = true
    end
    drawn = true if paint_avatar
    @gfx.present if drawn
    next_wake(now)
  end

  # Milliseconds to the next thing that changes the picture.
  def next_wake(now)
    t = @next_sway
    t = @next_blink if @next_blink < t
    t = @blink_until if @blink_until > now && @blink_until < t
    t = @happy_until if @happy_until > now && @happy_until < t
    t = @bubble_until if @bubble && @bubble_until < t
    d = t - now
    d = 20 if d < 20
    d
  end

  # Arrived on another machine: a new place, and the whole picture again.
  def on_restore
    now = Machine.board_millis
    here = place_here
    @places << here
    @shown = nil
    paint_avatar
    if ja?
      say(["#{@places.size} か所目だよ", here], now)
    else
      say(["Stop #{@places.size}:", here], now)
    end
    @gfx.present
    Log.info("avatar: arrived at #{here} (#{@places.join(' > ')}), pats=#{@pats}")
  end

  def on_checkpoint
    Log.info("avatar: leaving #{@places[-1]}, pats=#{@pats}")
  end
end

begin
  app = AvatarApp.new
  app.start
rescue => e
  Log.error("avatar: #{e.class}: #{e.message}")
end
