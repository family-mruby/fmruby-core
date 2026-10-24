# Walk Scroller: a pixel-art walker whose every step comes from a step
# sensor on Asterism, over a background that scrolls in layers (parallax).
#
# - Steps: subscribes to asterism/<node>/step/pedometer/step, the value a
#   step sensor app puts for each step (flash/app/test/step_sensor.app.rb:
#   a MessagePack map with steps, interval_ms, cadence, event, ...). A rise
#   in `steps` is that many steps; the first value from a node only sets
#   where it starts (unless it is a "step" event), a fall is a reset. Any
#   node drives the walk unless the script names one (`node`).
# - Each step moves the ground by step_px, over about the time between the
#   last two steps (faster cadence, faster walk). The walker's legs follow
#   the distance: one step is half a walking cycle, and a step's value
#   arrives on the stride. No step for stop_ms: the walker stands.
# - Script: what to show at which step count, in a small TOML file (or
#   JSON with the same keys). The first found of /home/walk_scroller.toml,
#   /home/walk_scroller.json and the sample next to this app
#   (/app/demo/walk_scroller/sample.toml, which documents the keys) is read
#   at start and again with L. A file opened with this app (file_selected)
#   is read instead.
# - Keys (they work with or without the network): Space / Right one step,
#   N next mark, B previous mark, R back to the start, L read the script
#   again, H hide / show the status line, D frame timing in the status
#   line (frames per second and the ms a frame takes here, over the last
#   second).
# - Network: node ID and router from FmrbAsterism.settings (as
#   asterism_demo; the browser build reaches zenohd over ws/). Tries again
#   every RETRY_MS while it cannot connect or after the connection is lost.
#   After the frame that shows a step it puts its progress on
#   asterism/<ID>/walk/scroller/progress (count, mark, src, src_steps), so
#   a sender can time a step from its put to the screen.
# - Drawing: the scenery is drawn once per scene into three SpriteImages
#   (far, middle, ground), each as wide as the screen and seamless at its
#   ends; a frame is the sky bands, two draw_tile calls per layer at its own
#   offset, and the sprites (walker, text, status). Text is drawn into a
#   sprite only when it changes.
#
# From the PC (fmruby-core): ruby tool/imu/step_send.rb --help

class WalkScrollerApp < FmrbApp
  APP = "walk"
  SAMPLE_SCRIPT = "/app/demo/walk_scroller/sample.toml"
  HOME_SCRIPTS = ["/home/walk_scroller.toml", "/home/walk_scroller.json"]
  RETRY_MS = 3000
  FRAME_MS = 33         # while something moves
  IDLE_MS = 20          # standing still: only the network is polled
  STATS_MS = 10000
  MAX_BURST = 8         # steps taken at once when values were missed
  # Transparent key of the sprites: black, so nothing in them is pure
  # black (the outlines are 0x24). The Modern compositor matches the key
  # as a 24-bit colour, which only black survives unchanged.
  TKEY = 0x00
  INK = 0x24            # the darkest colour drawn in sprites
  SCENES = 5

  # The walker, facing right. K outline, H cap, S skin, E eye, B shirt,
  # A arm, P near leg, Q far leg, O shoes. Rows 0-17 are shared, the legs
  # (rows 18-23) and the arm differ per frame.
  CHAR_BODY = [
    "................",
    ".....KKKKK......",
    "....KHHHHHK.....",
    "...KHHHHHHHK....",
    "...KHHHHHHHKKK..",
    "...KKKKKKKKKKKK.",
    "...KSSSSSSSK....",
    "...KSSSSSSESK...",
    "...KSSSSSSSSK...",
    "...KKSSSSSSK....",
    "....KKKKKKK.....",
    "...KBBBBBBK.....",
    "..KBBBBBBBBK....",
    "..KBBBBBBBBK....",
    "..KBBBBBBBBK....",
    "..KBBBBBBBBK....",
    "...KPPPPPPK.....",
    "...KPPPPPPK....."
  ]
  LEGS_STAND = [
    "....KPPKPPK.....",
    "....KPPKPPK.....",
    "....KPPKPPK.....",
    "....KPPKPPK.....",
    "....KOOKOOOK....",
    "....KKKKKKKK...."
  ]
  LEGS_STRIDE = [
    "...KQQKKPPK.....",
    "..KQQK..KPPK....",
    "..KQQK...KPPK...",
    ".KQQK....KPPK...",
    "KOOOK....KOOOK..",
    "KKKK......KKKK.."
  ]
  LEGS_PASS = [
    "....KPPKQQK.....",
    "....KPPKQQK.....",
    "....KPPKKQQK....",
    "....KPPK.KOOK...",
    "....KOOOKKKK....",
    "....KKKKK......."
  ]
  # [row, col, pixels] laid over the body
  ARM_BACK = [[12, 1, "KA"], [13, 0, "KSA"], [14, 0, "KK"]]
  ARM_FRONT = [[12, 10, "AK"], [13, 10, "ASK"], [14, 11, "KK"]]
  ARM_DOWN = [[12, 6, "A"], [13, 6, "A"], [14, 6, "A"], [15, 6, "S"]]

  def on_create
    @w = @user_area_width
    @h = @user_area_height
    @ox = @user_area_x0
    @oy = @user_area_y0
    # Laid out for 426x240 (the hardware's screen); a larger screen (the
    # browser build offers up to 1280x720) scales by whole steps. Text
    # stops growing at the largest text size (4).
    @s = @h / 240
    @s = 1 if @s < 1
    @art = 2 * @s         # scenery: one art pixel is @art x @art screen pixels
    @cs = 3 * @s          # the walker: 16x24 art pixels at @cs
    @text_h = 124 * @s
    @top_y = 6 * @s
    @aw = (@w + @art - 1) / @art
    @lw = @aw * @art      # layer width: whole art pixels, at least the screen
    @ground_y = @h - 50 * @s
    @far_y = 30 * @s
    @far_h = @ground_y - @far_y
    @mid_h = 84 * @s
    @mid_y = @ground_y - @mid_h
    @gnd_h = @h - @ground_y
    @char_x = @w / 4 - 8 * @s
    @char_y = @ground_y + 8 * @s - 24 * @cs

    @seed = 1
    @count = 0
    @pos = 0.0
    @target = 0.0
    @speed = 0.0
    @last_step_at = 0
    @last_manual_at = 0
    @frame = 4
    @shown_frame = -1
    @shown_mark = -2
    @scene = -1
    @text_y = @top_y.to_f
    @effect = ""
    @particles = []
    @next_burst = 0
    @status_on = true
    @timing_on = false
    @fps = 0
    @fps_n = 0
    @fps_ms = 0
    @fps_max = 0
    @fps_at = Machine.board_millis + 1000
    @status_text = ""
    @keys = []
    @last_steps = {}
    @seen_node = ""
    @seen_at = 0
    @ack = nil
    @dirty = true

    @net = :down
    @reconnecting = false
    @net_note = ""
    @retry_at = 0
    @sub = nil
    st = ::FmrbAsterism.settings
    @node = st.node
    @locator = st.router
    @creds = st.credentials
    @creds_error = st.error

    @frames_n = 0
    @render_sum = 0
    @render_max = 0
    @stats_at = Machine.board_millis + STATS_MS
    @last_tick = Machine.board_millis

    load_script(nil)
    make_sprites
    apply_mark(true)
    render(Machine.board_millis)
    Log.info("walk_scroller: #{@w}x#{@h}, script #{@script_path} (#{@marks.size} marks), node #{@node}, router #{@locator}")
  end

  def on_destroy
    begin
      ::Asterism.close
    rescue
    end
  end

  # ---- script ----

  # Keys of a mark: step, text, sub, size, sub_size, color, sub_color,
  # scene, effect. Top level: node, step_px, stop_ms.
  def load_script(path)
    text = nil
    if path
      text = read_file(path)
    else
      i = 0
      while text.nil? && i < HOME_SCRIPTS.size
        path = HOME_SCRIPTS[i]
        text = read_file(path)
        i += 1
      end
      if text.nil?
        path = SAMPLE_SCRIPT
        text = read_file(path)
      end
    end
    conf = nil
    if text
      begin
        conf = path.end_with?(".json") ? ::JSON.parse(text) : parse_toml(text)
      rescue => e
        Log.warn("walk_scroller: #{path}: #{e.message}")
        conf = nil
      end
    end
    conf = { "mark" => [{ "step" => 0, "text" => "Walk Scroller", "sub" => "no script" }] } unless conf.is_a?(Hash)
    @script_path = path || "-"
    @follow = conf["node"].to_s
    @step_px = conf["step_px"].to_i
    @step_px = 32 if @step_px <= 0
    @step_px *= @s # the script's pixels are 426x240 ones
    @stop_ms = conf["stop_ms"].to_i
    @stop_ms = 800 if @stop_ms <= 0
    marks = conf["mark"]
    marks = [] unless marks.is_a?(Array)
    @marks = []
    i = 0
    while i < marks.size
      m = marks[i]
      @marks << m if m.is_a?(Hash)
      i += 1
    end
    @marks << { "step" => 0 } if @marks.empty?
    # insertion sort by step (stable)
    i = 1
    while i < @marks.size
      m = @marks[i]
      j = i - 1
      while j >= 0 && @marks[j]["step"].to_i > m["step"].to_i
        @marks[j + 1] = @marks[j]
        j -= 1
      end
      @marks[j + 1] = m
      i += 1
    end
    @shown_mark = -2
  end

  def read_file(path)
    File.open(path, "r") { |f| f.read }
  rescue
    nil
  end

  # A small part of TOML: key = value lines at the top and in [[mark]]
  # tables; strings ("..." with \n \t \" \\), integers (also 0x..),
  # true / false; # comments.
  def parse_toml(text)
    top = {}
    marks = []
    cur = top
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = toml_strip_comment(lines[i]).strip
      i += 1
      next if line.empty?
      if line == "[[mark]]"
        cur = {}
        marks << cur
        next
      end
      if line.start_with?("[")
        cur = {} # another table: not ours
        next
      end
      eq = line.index("=")
      next unless eq
      key = line[0, eq].strip
      key = key[1, key.length - 2] if key.start_with?("\"") && key.end_with?("\"")
      cur[key] = toml_value(line[eq + 1, line.length].to_s.strip)
    end
    top["mark"] = marks
    top
  end

  def toml_strip_comment(line)
    in_str = false
    j = 0
    n = line.bytesize
    while j < n
      b = line.getbyte(j)
      if in_str
        if b == 92 # backslash
          j += 1
        elsif b == 34
          in_str = false
        end
      elsif b == 34
        in_str = true
      elsif b == 35 # #
        return line.byteslice(0, j)
      end
      j += 1
    end
    line
  end

  def toml_value(s)
    return "" if s.empty?
    if s.getbyte(0) == 34
      out = ""
      j = 1
      seg = 1
      n = s.bytesize
      while j < n
        b = s.getbyte(j)
        if b == 34
          out << s.byteslice(seg, j - seg)
          return out
        elsif b == 92 && j + 1 < n
          out << s.byteslice(seg, j - seg)
          c = s.getbyte(j + 1)
          if c == 110
            out << "\n"
          elsif c == 116
            out << "\t"
          else
            out << s.byteslice(j + 1, 1)
          end
          j += 2
          seg = j
          next
        end
        j += 1
      end
      out << s.byteslice(seg, n - seg)
      return out
    end
    return true if s == "true"
    return false if s == "false"
    if s.start_with?("0x") || s.start_with?("0X")
      v = 0
      j = 2
      while j < s.bytesize
        b = s.getbyte(j)
        d = if b >= 48 && b <= 57 then b - 48
            elsif b >= 97 && b <= 102 then b - 87
            elsif b >= 65 && b <= 70 then b - 55
            else -1
            end
        break if d < 0
        v = v * 16 + d
        j += 1
      end
      return v
    end
    s.to_i
  end

  # The mark in effect at the current count, and the scene of the last
  # mark up to it that names one.
  def current_mark
    k = -1
    i = 0
    while i < @marks.size && @marks[i]["step"].to_i <= @count
      k = i
      i += 1
    end
    k
  end

  def scene_at(k)
    i = k
    while i >= 0
      s = @marks[i]["scene"]
      return s.to_i % SCENES if s.is_a?(Integer)
      i -= 1
    end
    0
  end

  def apply_mark(force)
    k = current_mark
    return if k == @shown_mark && !force
    @shown_mark = k
    m = k >= 0 ? @marks[k] : {}
    sc = scene_at(k)
    build_scene(sc) if sc != @scene
    @effect = m["effect"].to_s
    @particles = [] unless @effect == "fireworks"
    draw_text_sprite(m)
    @text_y = -40.0 * @s
    @dirty = true
    Log.info("walk_scroller: count #{@count} mark #{k} scene #{sc} #{m["text"].to_s.split("\n").join(" / ")}")
  end

  # ---- sprites ----

  def make_sprites
    @far_img = SpriteImage.new(@gfx, width: @lw, height: @far_h, transparent_color: TKEY, use_transparent: true)
    @mid_img = SpriteImage.new(@gfx, width: @lw, height: @mid_h, transparent_color: TKEY, use_transparent: true)
    @gnd_img = SpriteImage.new(@gfx, width: @lw, height: @gnd_h)
    frames = []
    frames << char_image(LEGS_STRIDE, false, ARM_BACK)
    frames << char_image(LEGS_PASS, false, ARM_DOWN)
    frames << char_image(LEGS_STRIDE, true, ARM_FRONT)
    frames << char_image(LEGS_PASS, true, ARM_DOWN)
    frames << char_image(LEGS_STAND, false, ARM_DOWN)
    @char_frames = frames
    @walker = SpriteInstance.new(@gfx, frames, x: @ox + @char_x, y: @oy + @char_y, z: 2)
    @walker.frame = 4
    @text_img = SpriteImage.new(@gfx, width: @w, height: @text_h, transparent_color: TKEY, use_transparent: true)
    @text_sp = SpriteInstance.new(@gfx, @text_img, x: @ox, y: @oy + @top_y, z: 3)
    @status_w = 220
    @status_img = SpriteImage.new(@gfx, width: @status_w, height: 10, transparent_color: TKEY, use_transparent: true)
    @status_sp = SpriteInstance.new(@gfx, @status_img, x: @ox + @w - @status_w - 2, y: @oy + @h - 11, z: 4)
    draw_status_sprite
  end

  def char_color(ch)
    case ch
    when "K" then INK
    when "H" then 0xE0
    when "S" then 0xF6
    when "E" then INK
    when "B" then 0xF8
    when "A" then 0xD0
    when "P" then 0x2B
    when "Q" then 0x06
    when "O" then 0x88
    else TKEY
    end
  end

  def swap_legs(r)
    out = ""
    j = 0
    while j < r.length
      ch = r[j]
      out << (ch == "P" ? "Q" : (ch == "Q" ? "P" : ch))
      j += 1
    end
    out
  end

  # One frame of the walker; swap exchanges the near and the far leg.
  def char_image(legs, swap, arm)
    rows = []
    i = 0
    while i < CHAR_BODY.size
      rows << CHAR_BODY[i].dup
      i += 1
    end
    i = 0
    while i < legs.size
      r = legs[i]
      r = swap_legs(r) if swap
      rows << r
      i += 1
    end
    i = 0
    while i < arm.size
      a = arm[i]
      row = rows[a[0]]
      col = a[1]
      px = a[2]
      rows[a[0]] = row[0, col] + px + row[col + px.length, row.length].to_s
      i += 1
    end
    s = @cs
    img = SpriteImage.new(@gfx, width: 16 * s, height: rows.size * s, transparent_color: TKEY, use_transparent: true)
    img.set_target
    @gfx.fill_rect(0, 0, 16 * s, rows.size * s, TKEY)
    y = 0
    while y < rows.size
      row = rows[y]
      x = 0
      while x < 16
        ch = row[x]
        x2 = x
        x2 += 1 while x2 + 1 < 16 && row[x2 + 1] == ch
        c = char_color(ch)
        @gfx.fill_rect(x * s, y * s, (x2 - x + 1) * s, s, c) if c != TKEY
        x = x2 + 1
      end
      y += 1
    end
    img.reset_target
    img
  end

  def draw_text_sprite(m)
    @text_img.set_target
    @gfx.fill_rect(0, 0, @w, @text_h, TKEY)
    lines = []
    sizes = []
    colors = []
    main = m["text"].to_s
    unless main.empty?
      sz = clamp_size(m["size"], 3, @s)
      parts = main.split("\n")
      i = 0
      while i < parts.size
        lines << parts[i]
        sizes << sz
        colors << (m["color"].is_a?(Integer) ? m["color"] : 0xFF)
        i += 1
      end
    end
    sub = m["sub"].to_s
    unless sub.empty?
      sz = clamp_size(m["sub_size"], 2, @s)
      parts = sub.split("\n")
      i = 0
      while i < parts.size
        lines << parts[i]
        sizes << sz
        colors << (m["sub_color"].is_a?(Integer) ? m["sub_color"] : 0xFC)
        i += 1
      end
    end
    total = 0
    i = 0
    while i < lines.size
      total += 16 * sizes[i] + 4
      i += 1
    end
    y = (@text_h - total) / 2
    y = 0 if y < 0
    @gfx.set_font(:ja, 16)
    i = 0
    while i < lines.size
      sz = sizes[i]
      @gfx.set_text_size(sz)
      tw = @gfx.text_width(lines[i], :ja, 16) # already at the text size
      x = (@w - tw) / 2
      x = 0 if x < 0
      o = sz >= 3 ? 2 : 1
      dy = -o
      while dy <= o
        dx = -o
        while dx <= o
          @gfx.draw_text(x + dx, y + dy, lines[i], INK) unless dx == 0 && dy == 0
          dx += o
        end
        dy += o
      end
      @gfx.draw_text(x, y, lines[i], colors[i])
      y += 16 * sz + 4
      i += 1
    end
    @gfx.set_text_size(1)
    @gfx.set_font(:default)
    @text_img.reset_target
  end

  def clamp_size(v, dflt, scale)
    n = v.is_a?(Integer) ? v : dflt
    n = 1 if n < 1
    n *= scale
    n = 4 if n > 4
    n
  end

  def draw_status_sprite
    @status_img.set_target
    @gfx.fill_rect(0, 0, @status_w, 10, TKEY)
    if @status_on
      now = Machine.board_millis
      c = if @net != :up || @reconnecting then 0xE0
          elsif @seen_at > 0 && now - @seen_at < 5000 then 0x1C
          else 0xFC
          end
      @gfx.fill_rect(@status_w - 7, 2, 5, 5, c)
      t = @status_text
      tw = t.length * 6
      @gfx.draw_text(@status_w - 10 - tw, 1, t, 0x92)
    end
    @status_img.reset_target
  end

  def update_status(now)
    t = if @net == :up && @reconnecting
          "reconnecting #{@count}"
        elsif @net == :up
          src = @seen_node.empty? ? "waiting" : @seen_node
          "#{src} #{@count}"
        else
          "offline #{@count}"
        end
    t = "#{@fps_text}#{t}" if @timing_on
    t = t[t.length - 34, 34] if t.length > 34
    key = "#{t}|#{@net}|#{@reconnecting}|#{@seen_at > 0 && now - @seen_at < 5000}|#{@status_on}"
    return false if key == @status_key
    @status_key = key
    @status_text = t
    draw_status_sprite
    true
  end

  # ---- scenery ----

  def rnd(n)
    @seed = (@seed * 1103515245 + 12345) % 2147483648
    (@seed / 65536) % n
  end

  # A rectangle in art pixels, wrapped at the image's ends.
  def ar(x, y, w, h, c)
    x = x % @aw
    @gfx.fill_rect(x * @art, y * @art, w * @art, h * @art, c)
    @gfx.fill_rect((x - @aw) * @art, y * @art, w * @art, h * @art, c) if x + w > @aw
  end

  def ac(x, y, r, c)
    x = x % @aw
    @gfx.fill_circle(x * @art, y * @art, r * @art, c)
    @gfx.fill_circle((x - @aw) * @art, y * @art, r * @art, c) if x + r > @aw
    @gfx.fill_circle((x + @aw) * @art, y * @art, r * @art, c) if x - r < 0
  end

  def atri(x0, y0, x1, y1, x2, y2, c)
    sh = 0
    while sh <= 1
      d = sh * @aw
      @gfx.fill_triangle((x0 - d) * @art, y0 * @art, (x1 - d) * @art, y1 * @art, (x2 - d) * @art, y2 * @art, c)
      sh += 1
    end
    if x0 < 0 || x1 < 0 || x2 < 0
      d = @aw
      @gfx.fill_triangle((x0 + d) * @art, y0 * @art, (x1 + d) * @art, y1 * @art, (x2 + d) * @art, y2 * @art, c)
    end
  end

  # Sky bands (top to bottom), the sun or moon [x, y, r, colour] in screen
  # pixels.
  def sky_of(n)
    k = @s
    case n
    when 1 then [[0x4F, 0x53, 0x77, 0x9B], [@w - 70 * k, 50 * k, 14 * k, 0xFE]]
    when 2 then [[0x57, 0x5B, 0x7B, 0xBF], [@w - 110 * k, 40 * k, 12 * k, 0xFD]]
    when 3 then [[0x66, 0xA6, 0xE9, 0xF0], [@w / 2 + 60 * k, @ground_y - 70 * k, 30 * k, 0xFC]]
    when 4 then [[0x00, 0x02, 0x06, 0x0A], [@w - 80 * k, 44 * k, 13 * k, 0xFE]]
    else [[0x53, 0x57, 0x7B, 0x9F], [@w - 90 * k, 36 * k, 14 * k, 0xFC]]
    end
  end

  def build_scene(n)
    t0 = Machine.board_millis
    @scene = n
    sk = sky_of(n)
    @sky = sk[0]
    @sun = sk[1]
    fh = @far_h / @art
    mh = @mid_h / @art
    gh = @gnd_h / @art

    @seed = 7 + n * 31
    @far_img.set_target
    @gfx.fill_rect(0, 0, @lw, @far_h, TKEY)
    build_far(n, fh)
    @far_img.reset_target

    @seed = 101 + n * 17
    @mid_img.set_target
    @gfx.fill_rect(0, 0, @lw, @mid_h, TKEY)
    build_mid(n, mh)
    @mid_img.reset_target

    @seed = 211 + n * 13
    @gnd_img.set_target
    build_ground(n, gh)
    @gnd_img.reset_target
    Log.info("walk_scroller: scene #{n} built in #{Machine.board_millis - t0} ms")
  end

  def build_far(n, fh)
    case n
    when 1 # mountains with snow
      x = 0
      while x < @aw
        hgt = 34 + rnd(26)
        half = 22 + rnd(14)
        top = fh - hgt
        atri(x - half, fh, x, top, x + half, fh, 0x6E)
        atri(x - half / 4, top + hgt / 4, x, top, x + half / 4, top + hgt / 4, 0xFF)
        x += 30 + rnd(20)
      end
      x = 10
      while x < @aw
        ac(x, fh + 6, 16 + rnd(8), 0x4D)
        x += 26 + rnd(10)
      end
    when 2 # far buildings
      x = 0
      while x < @aw
        bw = 8 + rnd(10)
        bh = 18 + rnd(30)
        ar(x, fh - bh, bw, bh, 0x92)
        yy = fh - bh + 3
        while yy < fh - 2
          ar(x + 2, yy, bw - 4, 1, 0xB6)
          yy += 4
        end
        x += bw + 1 + rnd(4)
      end
      cloud(20, 8)
      cloud(120, 14)
    when 3 # the sea at sunset
      ar(0, fh - 22, @aw, 22, 0x4A)
      i = 0
      while i < 40
        ar(rnd(@aw), fh - 21 + rnd(20), 3 + rnd(6), 1, i % 3 == 0 ? 0xF4 : 0x6E)
        i += 1
      end
      ar(40, fh - 26, 30, 4, 0x45)
      ar(46, fh - 29, 16, 3, 0x45)
    when 4 # night: stars and towers
      i = 0
      while i < 50
        ar(rnd(@aw), rnd(fh - 30), 1, 1, i % 4 == 0 ? 0xB6 : 0xFF)
        i += 1
      end
      x = 0
      while x < @aw
        bw = 10 + rnd(12)
        bh = 24 + rnd(36)
        ar(x, fh - bh, bw, bh, 0x25)
        yy = fh - bh + 2
        while yy < fh - 2
          xx = x + 2
          while xx < x + bw - 2
            ar(xx, yy, 1, 2, 0xF8) if rnd(3) == 0
            xx += 3
          end
          yy += 4
        end
        x += bw + rnd(3)
      end
    else # meadow: hills and clouds
      cloud(10, 6)
      cloud(80, 14)
      cloud(150, 4)
      x = 0
      while x < @aw
        ac(x, fh + 14, 28 + rnd(14), 0x71)
        x += 36 + rnd(16)
      end
      x = 18
      while x < @aw
        ac(x, fh + 12, 18 + rnd(8), 0x51)
        x += 30 + rnd(14)
      end
    end
  end

  def cloud(x, y)
    ac(x, y + 4, 5, 0xFF)
    ac(x + 6, y + 2, 7, 0xFF)
    ac(x + 13, y + 4, 5, 0xFF)
    ar(x, y + 4, 14, 5, 0xFF)
  end

  def build_mid(n, mh)
    case n
    when 1 # pines
      x = 4
      while x < @aw
        hgt = 22 + rnd(12)
        ar(x - 1, mh - 6, 3, 6, 0x44)
        k = 0
        while k < 3
          ty = mh - 6 - hgt + k * (hgt / 3)
          wd = 5 + k * 3
          atri(x - wd, ty + hgt / 3 + 3, x, ty, x + wd, ty + hgt / 3 + 3, k == 0 ? 0x30 : 0x2C)
          k += 1
        end
        x += 16 + rnd(14)
      end
    when 2 # houses
      walls = [0xF5, 0xFA, 0xD6, 0xB9]
      x = 0
      i = 0
      while x < @aw - 10
        hw = 18 + rnd(8)
        hh = 14 + rnd(8)
        top = mh - hh
        ar(x, top, hw, hh, walls[i % 4])
        atri(x - 2, top, x + hw / 2, top - 9, x + hw + 2, top, 0xC4)
        ar(x + 3, top + 4, 4, 4, 0x4F)
        ar(x + hw - 7, top + 4, 4, 4, 0x4F)
        ar(x + hw / 2 - 2, mh - 7, 4, 7, 0x88)
        x += hw + 6 + rnd(6)
        i += 1
      end
    when 3 # palms
      x = 8
      while x < @aw
        h = 18 + rnd(10)
        k = 0
        while k < h
          ar(x + k / 6, mh - k, 2, 1, 0x20)
          k += 1
        end
        tx = x + h / 6
        ty = mh - h
        ar(tx - 8, ty, 18, 2, 0x20)
        ar(tx - 6, ty - 2, 4, 2, 0x20)
        ar(tx + 4, ty - 2, 4, 2, 0x20)
        ar(tx - 9, ty + 2, 3, 3, 0x20)
        ar(tx + 8, ty + 2, 3, 3, 0x20)
        x += 34 + rnd(26)
      end
    when 4 # street: near buildings and lamps
      x = 0
      while x < @aw
        bw = 14 + rnd(10)
        bh = 16 + rnd(16)
        ar(x, mh - bh, bw, bh, 0x49)
        yy = mh - bh + 3
        while yy < mh - 3
          ar(x + 3, yy, 2, 2, rnd(2) == 0 ? 0xFC : 0x6D)
          ar(x + bw - 5, yy, 2, 2, rnd(2) == 0 ? 0xFC : 0x6D)
          yy += 5
        end
        x += bw + 4 + rnd(8)
      end
      x = 10
      while x < @aw
        ar(x, mh - 22, 1, 22, 0x6D)
        ar(x - 1, mh - 24, 3, 2, 0xFE)
        x += 40
      end
    else # meadow: round trees and bushes
      x = 6
      while x < @aw
        r = 7 + rnd(4)
        ar(x - 1, mh - 12, 3, 12, 0x64)
        ac(x, mh - 12 - r, r, 0x30)
        ac(x - r / 2 - 1, mh - 10 - r / 2, r - 3, 0x30)
        ac(x + r / 2 + 1, mh - 10 - r / 2, r - 3, 0x30)
        ac(x - 2, mh - 14 - r - r / 3, r / 3 + 1, 0x54)
        x += 26 + rnd(22)
      end
      x = 0
      while x < @aw
        ac(x, mh, 4 + rnd(3), 0x34)
        x += 14 + rnd(10)
      end
    end
  end

  def build_ground(n, gh)
    case n
    when 2, 4 # pavement and a road
      side = n == 2 ? 0xB6 : 0x6D
      line = n == 2 ? 0x92 : 0x49
      road = n == 2 ? 0x49 : 0x24
      ar(0, 0, @aw, 9, side)
      x = 0
      while x < @aw
        ar(x, 0, 1, 9, line)
        x += 12
      end
      ar(0, 9, @aw, 2, line)
      ar(0, 11, @aw, gh - 11, road)
      x = 0
      while x < @aw
        ar(x, 17, 10, 2, n == 2 ? 0xFF : 0xF8)
        x += 20
      end
    when 3 # sand
      ar(0, 0, @aw, gh, 0xF9)
      i = 0
      while i < 50
        ar(rnd(@aw), rnd(gh), 2 + rnd(4), 1, 0xD4)
        i += 1
      end
      i = 0
      while i < 8
        ar(rnd(@aw), 2 + rnd(gh - 4), 2, 2, 0xE4)
        i += 1
      end
    else # grass and a path
      grass = n == 1 ? 0x30 : 0x34
      ar(0, 0, @aw, gh, grass)
      ar(0, 4, @aw, 9, n == 1 ? 0xB5 : 0xD5)
      i = 0
      while i < 30
        ar(rnd(@aw), 5 + rnd(7), 2, 1, 0x8C)
        i += 1
      end
      i = 0
      while i < 40
        x = rnd(@aw)
        y = 14 + rnd(gh - 15)
        ar(x, y, 1, 2, 0x2C)
        ar(x + 2, y + 1, 1, 1, 0x2C)
        i += 1
      end
      i = 0
      while i < 14
        c = i % 2 == 0 ? 0xFF : 0xE3
        c = 0xFC if i % 3 == 0
        ar(rnd(@aw), 14 + rnd(gh - 15), 1, 1, c)
        i += 1
      end
      i = 0
      while i < @aw
        ar(i, 0, 1, 1, 0x54)
        i += 3
      end
    end
  end

  # ---- motion ----

  # n steps taken; iv is the time between steps in ms (0 if unknown).
  def take_steps(n, iv, now)
    @count += n
    @target += n * @step_px
    @last_step_at = now
    dur = iv > 0 ? iv : 450
    dur = 250 if dur < 250
    dur = 650 if dur > 650
    left = @target - @pos
    # a backlog of more than two steps is caught up faster
    dur = dur * 2 * @step_px / left if left > 2 * @step_px
    @speed = left / dur.to_f
    apply_mark(false)
  end

  def manual_step(now)
    iv = now - @last_manual_at
    iv = 0 if iv > 1500
    @last_manual_at = now
    take_steps(1, iv, now)
  end

  def jump_to(count, now)
    @count = count
    @count = 0 if @count < 0
    apply_mark(false)
    @last_step_at = now
  end

  def jump_mark(dir, now)
    k = current_mark
    if dir > 0
      j = k + 1
      while j < @marks.size && @marks[j]["step"].to_i <= @count
        j += 1
      end
      return if j >= @marks.size
      jump_to(@marks[j]["step"].to_i, now)
    else
      return if k <= 0
      jump_to(@marks[k - 1]["step"].to_i, now)
    end
    @target += @step_px
    @speed = @step_px / 450.0
  end

  def restart(now)
    @count = 0
    @pos = 0.0
    @target = 0.0
    @last_steps = {}
    apply_mark(true)
    @frame = 4
    @last_step_at = 0
    @dirty = true
  end

  # Moves things by dt ms; true when the screen needs a new frame.
  def advance(dt, now)
    moved = false
    if @pos < @target
      @pos += @speed * dt
      @pos = @target if @pos > @target
      moved = true
    end
    if moved || now - @last_step_at < @stop_ms
      @frame = ((@pos * 2 / @step_px).to_i) % 4
    else
      @frame = 4
    end
    if @text_y < @top_y
      @text_y += dt * 0.25 * @s
      @text_y = @top_y.to_f if @text_y > @top_y
      moved = true
    end
    if @effect == "fireworks"
      step_particles(dt, now)
      moved = true
    end
    moved = true if @frame != @shown_frame
    moved
  end

  def step_particles(dt, now)
    if now >= @next_burst
      @next_burst = now + 500 + rnd(500)
      cx = 30 * @s + rnd(@w - 60 * @s)
      cy = 30 * @s + rnd(@ground_y / 2)
      cols = [0xE0, 0xFC, 0x1F, 0xE3, 0xFF, 0x1C, 0xF0]
      c = cols[rnd(cols.size)]
      i = 0
      while i < 14
        a = i * 6.2832 / 14
        sp = (0.06 + rnd(30) / 1000.0) * @s
        @particles << [cx.to_f, cy.to_f, ::Math.cos(a) * sp, ::Math.sin(a) * sp, 1100, c]
        i += 1
      end
    end
    keep = []
    i = 0
    while i < @particles.size
      p = @particles[i]
      p[0] += p[2] * dt
      p[1] += p[3] * dt
      p[3] += 0.00006 * dt * @s
      p[4] -= dt
      keep << p if p[4] > 0
      i += 1
    end
    @particles = keep
  end

  # ---- frame ----

  def tile_layer(img, h, y, speed)
    off = (@pos * speed).to_i % @lw
    first = @lw - off
    first = @w if first > @w
    @gfx.draw_tile(img.id, off, 0, first, h, dst_x: @ox, dst_y: @oy + y)
    @gfx.draw_tile(img.id, 0, 0, @w - first, h, dst_x: @ox + first, dst_y: @oy + y) if first < @w
  end

  def render(now)
    t0 = Machine.board_millis
    band = @ground_y / 4
    i = 0
    while i < 4
      hh = i == 3 ? @ground_y - band * 3 : band
      @gfx.fill_rect(@ox, @oy + band * i, @w, hh, @sky[i])
      i += 1
    end
    @gfx.fill_circle(@ox + @sun[0], @oy + @sun[1], @sun[2], @sun[3])
    tile_layer(@far_img, @far_h, @far_y, 0.2)
    if @effect == "fireworks"
      i = 0
      while i < @particles.size
        p = @particles[i]
        sz = (p[4] > 500 ? 3 : 2) * @s
        @gfx.fill_rect(@ox + p[0].to_i, @oy + p[1].to_i, sz, sz, p[5])
        i += 1
      end
    end
    tile_layer(@mid_img, @mid_h, @mid_y, 0.5)
    tile_layer(@gnd_img, @gnd_h, @ground_y, 1.0)
    if @frame != @shown_frame
      @walker.frame = @frame
      bob = (@frame == 1 || @frame == 3) ? -@cs : 0
      @walker.move(@ox + @char_x, @oy + @char_y + bob)
      @shown_frame = @frame
    end
    @text_sp.move(@ox, @oy + @text_y.to_i)
    @gfx.present
    @dirty = false
    took = Machine.board_millis - t0
    @frames_n += 1
    @render_sum += took
    @render_max = took if took > @render_max
    @fps_n += 1
    @fps_ms += took
    @fps_max = took if took > @fps_max
  end

  # ---- network ----

  def connect(now)
    begin
      begin
        ::Asterism.close
      rescue
      end
      raise ::Asterism::Error, @creds_error if @creds_error
      ::Asterism.connect(@locator, node: @node, app: APP, credentials: @creds)
      who = @follow.empty? ? "*" : @follow
      @sub = ::Asterism.session.subscribe("asterism/#{who}/step/pedometer/step", depth: 16)
      @progress_key = "asterism/#{@node}/#{APP}/scroller/progress"
      @net = :up
      @net_note = ""
    rescue => e
      @net = :down
      @net_note = e.message.to_s
      @retry_at = now + RETRY_MS
      @sub = nil
    end
    Log.info("walk_scroller: #{@node} #{@net} (#{@locator}) #{@net_note}")
  end

  def lost(why, now)
    Log.info("walk_scroller: lost: #{why}")
    @net = :down
    @reconnecting = false
    @net_note = why
    @retry_at = now + RETRY_MS
    @sub = nil
    begin
      ::Asterism.close
    rescue
    end
  end

  def network(now)
    if @net != :up
      connect(now) if now >= @retry_at
      return
    end
    begin
      return lost("asterism: #{::Asterism.lost_reason}", now) unless ::Asterism.poll
      @reconnecting = ::Asterism.reconnecting?
      @sub.each_pending { |key, payload, att| step_value(key, payload, now) } if @sub
    rescue => e
      lost("#{e.class}: #{e.message}", now)
    end
  end

  def step_value(key, payload, now)
    k = key.to_s
    parts = k.split("/")
    src = parts.size > 1 ? parts[1] : "?"
    return if !@follow.empty? && src != @follow
    v = ::Asterism::Codec.unpack(payload)
    return unless v.is_a?(Hash)
    steps = v["steps"]
    return unless steps.is_a?(Integer)
    prev = @last_steps[src]
    @last_steps[src] = steps
    @seen_at = now
    @seen_node = src
    d = 0
    if prev.nil?
      d = 1 if v["event"] == "step"
    elsif steps > prev
      d = steps - prev
    end
    return if d <= 0
    d = MAX_BURST if d > MAX_BURST
    iv = v["interval_ms"].to_i
    cad = v["cadence"].to_i
    iv = 60000 / cad if iv <= 0 && cad > 0
    take_steps(d, iv, now)
    @ack = [src, steps]
  end

  def put_progress(now)
    a = @ack
    @ack = nil
    return unless a && @net == :up
    v = { "count" => @count, "mark" => @shown_mark, "src" => a[0], "src_steps" => a[1], "ms" => now }
    ::Asterism.session.put(@progress_key, ::Asterism::Codec.pack(v))
  rescue => e
    Log.warn("walk_scroller: progress put: #{e.message}")
  end

  # ---- input and the loop ----

  def on_control(msg)
    return unless msg["cmd"] == "file_selected"
    load_script(msg["path"].to_s)
    apply_mark(true)
  end

  # Keys are noted here and acted on in on_update (one place drives the
  # motion and the network).
  def on_event(ev)
    return unless ev[:type] == :key_down
    @keys << ev[:scancode]
  end

  def run_keys(now)
    while @keys.size > 0
      sc = @keys.shift
      case sc
      when FmrbConst::KEY_SPACE, FmrbConst::KEY_RIGHT then manual_step(now)
      when 0x11 then jump_mark(1, now)   # n
      when 0x05 then jump_mark(-1, now)  # b
      when 0x15 then restart(now)        # r
      when 0x0F                          # l
        load_script(nil)
        apply_mark(true)
      when 0x07                          # d
        @timing_on = !@timing_on
        @status_key = nil
      when 0x0B                          # h
        @status_on = !@status_on
        @status_key = nil
      end
    end
  end

  def on_update
    now = Machine.board_millis
    dt = now - @last_tick
    dt = 100 if dt > 100
    @last_tick = now
    network(now)
    run_keys(now)
    changed = advance(dt, now)
    changed = true if update_status(now)
    render(now) if changed || @dirty
    put_progress(now) if @ack
    if now >= @fps_at
      @fps_text = "#{@fps_n}fps #{@fps_n > 0 ? @fps_ms / @fps_n : 0}/#{@fps_max}ms "
      @fps_n = 0
      @fps_ms = 0
      @fps_max = 0
      @fps_at = now + 1000
    end
    if now >= @stats_at
      avg = @frames_n > 0 ? @render_sum * 10 / @frames_n : 0
      Log.info("walk_scroller: #{@frames_n} frames in #{STATS_MS / 1000} s, render avg #{avg / 10}.#{avg % 10} ms max #{@render_max} ms, count #{@count}, #{@net} #{@net_note}")
      @frames_n = 0
      @render_sum = 0
      @render_max = 0
      @stats_at = now + STATS_MS
    end
    (@pos < @target || @text_y < @top_y || @effect == "fireworks") ? FRAME_MS : IDLE_MS
  end
end

begin
  app = WalkScrollerApp.new
  app.start
rescue => e
  Log.error("walk_scroller: #{e.message}")
end
