# Walk Scroller: a walker drawn from BMP frames whose every step comes from a step
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
#   second), T the tuning panel (below).
# - Tuning panel (T opens and closes it; it is never shown otherwise, and
#   the walk goes on while it is open): the step sensor's detector as the
#   sensor itself reports it -- the threshold (rise_mg), the shortest gap
#   between steps (min_gap_ms), whether those are saved on the sensor, the
#   sensor's node and how long ago it last answered (or why it does not:
#   no node yet, offline, no answer, an error from the sensor). It calls
#   pedometer.state when it opens and every TUNE_POLL_MS. While it is open,
#   Up / Down change the threshold by 10 mg and Left / Right the gap by
#   10 ms (Right is no longer a step then; Space still is), each press
#   calling pedometer.tune at once; the panel shows the values the sensor
#   answers, not the ones asked for. S calls pedometer.save (the sensor
#   writes /home/step_sensor.toml and keeps the values across a restart),
#   0 asks for the defaults (150 mg / 250 ms; S to keep them). Below, the
#   last few seconds of the sensor's raw values (asterism/<node>/step/
#   pedometer/accel, subscribed only while the panel is open): dyn_peak
#   (the largest dyn of the sensor's samples since its previous put; dyn,
#   the value at the put, from a sender without it) as a line with the
#   threshold across it (a step is a peak that crosses it) and the largest
#   of the window as a number. The sensor tuned is the
#   script's `node`, else the last node that sent a step (else the last
#   node heard from).
# - Network: FmrbAsterism::Link, node ID and router from
#   FmrbAsterism.settings (as asterism_demo; the browser build reaches
#   zenohd over ws/). Tries again every 3 s while it cannot connect or
#   after the connection is lost; link_up subscribes after every connect.
#   After the frame that shows a step it puts its progress on
#   asterism/<ID>/walk/scroller/progress (count, mark, src, src_steps), so
#   a sender can time a step from its put to the screen.
# - Drawing: a frame is the sky (four bands and the sun or moon, drawn in
#   code), three scenery layers each drawn with draw_tile at its own offset
#   (two calls when the layer is as wide as the screen), and the sprites
#   (walker, text, status). Text is drawn into a sprite only when it
#   changes.
# - Pictures: BMP files read with SpriteImage#load_bmp, the defaults in
#   /usr/share/sprites/walk_scroller/. They are 8-bit BMPs whose pixel
#   bytes are RGB332 colours, with the RGB332 palette (what the Family
#   mruby Console sprite editor and tools/fm_asset_editor write). 0x00
#   (black) is transparent everywhere, so draw black as 0x24. Nothing is
#   scaled. A file over 256 KB is refused.
# - Scenery: scene<N>_far.bmp, scene<N>_mid.bmp and scene<N>_gnd.bmp for
#   the scenes 0-4. The ground layer stands on the bottom of the screen,
#   the middle and the far layer stand on the ground layer, the sky fills
#   what is above (the defaults: far 426x160, mid 426x84, gnd 426x50).
#   A layer is any width up to 1024 that tiles: its left and right edges
#   meet, and it repeats when narrower than the screen. The far and middle
#   layers are mostly 0x00 so the sky shows through. They scroll at 20%,
#   50% and 100% of the walking distance (far_speed, mid_speed, gnd_speed
#   in the script). Only the current scene's layers are in memory; a layer
#   of the size already held is read into the same image.
# - Your own pictures: the script's `scenery` key names a folder (e.g.
#   "/home/walk_scroller/scenery") whose scene<N>_<layer>.bmp files are
#   used instead of the defaults, file by file; `walker` names a folder of
#   walker frames used instead of the default walker as a whole. A walker
#   folder holds walker_0.bmp, walker_1.bmp, ... (the walking frames, as
#   many as there are in a row, up to 8; together they are one walking
#   cycle, which is two steps) and walker_stand.bmp (standing; left out,
#   walker_0 is used), at most 160x160 and all the same size. The frame's
#   middle is at a quarter of the screen and its bottom row 8 pixels below
#   the top of the ground layer; the default frames are 48x75 (a 16x24
#   drawing at 3x, the passing frames drawn 3 pixels higher). A bad file
#   and the defaults are used, with the reason in the status line and the
#   log. L reads the script and the pictures again. All the layers are
#   brought to the graphics side at start (the first start after a change
#   copies them, which takes long in the Linux simulator).
#
# From the PC (fmruby-core): ruby tool/imu/step_send.rb --help

class WalkScrollerApp < FmrbApp
  APP = "walk"
  SAMPLE_SCRIPT = "/app/demo/walk_scroller/sample.toml"
  HOME_SCRIPTS = ["/home/walk_scroller.toml", "/home/walk_scroller.json"]
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
  SPRITE_DIR = "/usr/share/sprites/walk_scroller"
  CACHE = "/cache/app/walk_scroller"
  MAX_WALK = 8          # walking frames read from a folder (walker_0..7)
  MAX_ART = 160         # largest walker frame side accepted
  WALKER_SINK = 8
  LAYERS = ["far", "mid", "gnd"]
  SPEEDS = [20, 50, 100] # layer scroll speeds, % of the walking distance
  LAYER_MAX_W = 1024
  LAYER_MAX_H = 240
  BMP_MAX_FILE = 262144 # what the graphics side reads in one go
  NOTE_MS = 6000        # how long "walker: <folder>" stays in the status line
  # Tuning panel (T)
  TUNE_POLL_MS = 3000   # pedometer.state again this often while open
  TUNE_CALL_MS = 2000   # time limit of one call to the sensor
  TUNE_DEFAULT = [150, 250]
  TUNE_STEP = 10        # mg / ms per key press
  PANEL_W = 240
  PANEL_H = 120
  PANEL_CHARS = 38     # 6x8 characters on a line of the panel
  PANEL_BG = 0x25       # not TKEY, so the panel is solid
  GRAPH_BG = 0x24
  GRAPH_N = 80          # raw values kept (20 Hz: 4 s)
  GRAPH_MS = 100        # redraw the graph at most this often

  def on_create
    @w = @user_area_width
    @h = @user_area_height
    @ox = @user_area_x0
    @oy = @user_area_y0
    # Laid out for 426x240 (the hardware's screen). On a larger screen (the
    # browser build offers up to 1280x720) the text and the step scale by
    # whole steps (text stops growing at the largest size, 4); the BMPs are
    # shown as they are, the layers tiled across and the sky filling the top.
    @s = @h / 240
    @s = 1 if @s < 1
    @text_h = 124 * @s
    @top_y = 6 * @s
    # The layers stand on the bottom of the screen; build_scene sets these
    # from the heights of the scene's BMPs.
    @ground_y = @h - 50
    @far_y = 30
    @mid_y = 106
    @layers = [nil, nil, nil]
    @layer_files = nil
    # The walker stands with the middle of its frame here and its bottom
    # row WALKER_SINK pixels into the ground layer (frames of any size line
    # up the same way).
    @char_cx = @w / 4 + 16 * @s

    @seed = 1
    @count = 0
    @pos = 0.0
    @target = 0.0
    @speed = 0.0
    @last_step_at = 0
    @last_manual_at = 0
    @frame = 0
    @walk_n = 1
    @walker = nil
    @walker_note = ""
    @walker_note_until = 0
    @scenery_note = ""
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
    @step_node = ""       # the last node whose value was a step
    @seen_at = 0
    @ack = nil
    @dirty = true
    @panel_on = false
    @panel_img = nil
    @panel_sp = nil
    @panel_dirty = false
    @panel_drawn_at = 0
    @panel_sec = -1
    @tune_node = ""
    @tune_vals = nil      # [rise_mg, min_gap_ms, saved] as the sensor answered
    @tune_ok_at = 0
    @tune_err = ""
    @tune_note = ""
    @tune_call = nil      # [Future, kind] of the call on its way
    @tune_want = nil      # a tune or save waiting for the call on its way
    @tune_poll_at = 0
    @accel_sub = nil
    @accel_node = ""
    @accel = []           # dyn_peak (else dyn) in mg of the last raw values
    @accel_rise = 0
    @accel_new = false

    @sub = nil
    # No wait for WiFi: the first update already tries (a failure retries).
    @link = ::FmrbAsterism::Link.new(self, APP, tag: "walk_scroller", wifi_wait_ms: 0)

    @frames_n = 0
    @render_sum = 0
    @render_max = 0
    @stats_at = Machine.board_millis + STATS_MS
    @last_tick = Machine.board_millis

    load_script(nil)
    make_sprites
    apply_mark(true)
    render(Machine.board_millis)
    Log.info("walk_scroller: #{@w}x#{@h}, script #{@script_path} (#{@marks.size} marks), node #{@link.node}, router #{@link.router}")
  end

  def on_destroy
    close_accel
    @link.close
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
    @walker_dir = conf["walker"].to_s
    @scenery_dir = conf["scenery"].to_s
    @speeds = []
    i = 0
    while i < 3
      v = conf["#{LAYERS[i]}_speed"]
      @speeds << (v.is_a?(Integer) && v >= 0 ? v : SPEEDS[i]) / 100.0
      i += 1
    end
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
    prepare_scenery
    load_walker
    @text_img = SpriteImage.new(@gfx, width: @w, height: @text_h, transparent_color: TKEY, use_transparent: true)
    @text_sp = SpriteInstance.new(@gfx, @text_img, x: @ox, y: @oy + @top_y, z: 3)
    @status_w = 300
    @status_img = SpriteImage.new(@gfx, width: @status_w, height: 10, transparent_color: TKEY, use_transparent: true)
    @status_sp = SpriteInstance.new(@gfx, @status_img, x: @ox + @w - @status_w - 2, y: @oy + @h - 11, z: 4)
    draw_status_sprite
  end

  # The walker's frames: walker_0.bmp, walker_1.bmp, ... (as many as there
  # are in a row, up to MAX_WALK; they make one walking cycle of two steps)
  # and walker_stand.bmp (left out: walker_0 stands in). The script's
  # `walker` folder is tried first; anything wrong with it (no walker_0, a
  # file that is not an 8-bit BMP, frames of different sizes) and the
  # defaults in SPRITE_DIR are used, with the reason in the status line and
  # the log.
  # After the script was read again: its folders may have changed, or the
  # files in them.
  def reload_art
    prepare_scenery
    load_walker
    @scene = -1
    apply_mark(true)
  end

  def load_walker
    old = @walker
    old_imgs = @walker_imgs
    set = nil
    if !@walker_dir.empty?
      set = walker_set(@walker_dir, "user")
      if set.is_a?(String)
        note_walker("#{set} (default walker)", 0)
        Log.warn("walk_scroller: #{@walker_dir}: #{set}; using the default walker")
        set = nil
      else
        note_walker("walker: #{@walker_dir}", NOTE_MS)
      end
    else
      note_walker("", 0)
    end
    if set.nil?
      set = walker_set(SPRITE_DIR, "default")
      if set.is_a?(String)
        Log.error("walk_scroller: #{SPRITE_DIR}: #{set}")
        note_walker("walker: none (#{set})", 0)
        set = nil
      end
    end
    @walker = nil
    old.destroy if old
    if old_imgs
      i = 0
      while i < old_imgs.size
        old_imgs[i].destroy
        i += 1
      end
    end
    @walker_imgs = nil
    @walk_n = 1
    @frame = 0
    @shown_frame = -1
    @dirty = true
    return unless set
    w = set[0]
    h = set[1]
    paths = set[2]
    imgs = []
    begin
      i = 0
      while i < paths.size
        img = SpriteImage.new(@gfx, width: w, height: h, transparent_color: TKEY, use_transparent: true)
        img.load_bmp(paths[i])
        imgs << img
        i += 1
      end
    rescue => e
      Log.warn("walk_scroller: walker #{w}x#{h}: #{e.message}")
      note_walker("walker: no room for #{w}x#{h}", 0)
      i = 0
      while i < imgs.size
        imgs[i].destroy
        i += 1
      end
      return
    end
    # frames: the walking ones, then the stand frame (walker_0 if none)
    @walk_n = set[3] ? imgs.size - 1 : imgs.size
    frames = imgs.dup
    frames << imgs[0] unless set[3]
    @walker_imgs = imgs
    @frame = @walk_n
    @walker_w = w
    @walker_h = h
    @walker = SpriteInstance.new(@gfx, frames, x: @ox + @char_cx - w / 2, y: @oy + @ground_y + WALKER_SINK - h, z: 2)
    @walker.frame = @frame
    @shown_frame = @frame
    Log.info("walk_scroller: walker from #{set[4]} (#{@walk_n} walking frames + stand, #{w}x#{h})")
  end

  # Checks the frames in dir and brings them to the graphics side. Returns
  # [w, h, cache paths (the walking frames, then walker_stand if there is
  # one), has_stand, dir], or a short reason why not.
  def walker_set(dir, tag)
    names = []
    i = 0
    while i < MAX_WALK
      name = "walker_#{i}.bmp"
      break unless ::File.exist?("#{dir}/#{name}")
      names << name
      i += 1
    end
    return "no walker_0.bmp" if names.empty?
    has_stand = ::File.exist?("#{dir}/walker_stand.bmp")
    names << "walker_stand.bmp" if has_stand
    w = 0
    h = 0
    paths = []
    i = 0
    while i < names.size
      name = names[i]
      size = bmp_size("#{dir}/#{name}", MAX_ART, MAX_ART)
      return "#{name} #{size}" if size.is_a?(String)
      if i == 0
        w = size[0]
        h = size[1]
      elsif size[0] != w || size[1] != h
        return "#{name} not #{w}x#{h}"
      end
      dest = "#{CACHE}/#{tag}/#{name}"
      return "#{name} copy failed" unless @gfx.sync_file("#{dir}/#{name}", dest: dest)
      paths << dest
      i += 1
    end
    [w, h, paths, has_stand, dir]
  end

  # [width, height] of an 8-bit uncompressed BMP, or what is wrong with it
  # (what the sprite loader on the graphics side accepts).
  def bmp_size(path, max_w, max_h)
    d = read_file(path)
    return "unreadable" if d.nil?
    return "file over 256 KB" if d.bytesize > BMP_MAX_FILE
    return "not a BMP" if d.bytesize < 54 || d.getbyte(0) != 66 || d.getbyte(1) != 77
    return "not 8-bit" if le(d, 28, 2) != 8
    return "compressed" if le(d, 30, 4) != 0
    w = le(d, 18, 4)
    h = le(d, 22, 4)
    h = 4294967296 - h if h >= 2147483648 # top-down
    return "size #{w}x#{h}" if w <= 0 || h <= 0 || w > max_w || h > max_h
    return "too short" if le(d, 10, 4) + ((w + 3) / 4 * 4) * h > d.bytesize
    [w, h]
  end

  def le(d, at, n)
    v = 0
    i = n - 1
    while i >= 0
      v = v * 256 + d.getbyte(at + i)
      i -= 1
    end
    v
  end

  def note_walker(text, ms)
    @walker_note = text
    @walker_note_until = ms > 0 ? Machine.board_millis + ms : 0
    @status_key = nil
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
      c = if !@link.up? then 0xE0
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
    t = if @link.state == :reconnecting
          "reconnecting #{@count}"
        elsif @link.connected?
          src = @seen_node.empty? ? "waiting" : @seen_node
          "#{src} #{@count}"
        else
          "offline #{@count}"
        end
    t = "#{@fps_text}#{t}" if @timing_on
    t = t[t.length - 34, 34] if t.length > 34
    n = ""
    n = @walker_note if !@walker_note.empty? && (@walker_note_until == 0 || now < @walker_note_until)
    n = n.empty? ? @scenery_note : "#{n}  #{@scenery_note}" unless @scenery_note.empty?
    if !n.empty?
      room = 50 - t.length - 2
      n = n[0, room] if n.length > room
      t = "#{n}  #{t}"
    end
    key = "#{t}|#{@link.state}|#{@seen_at > 0 && now - @seen_at < 5000}|#{@status_on}"
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

  # The layers of every scene, chosen and brought to the graphics side once
  # (at start and with L) so that a scene change only loads three files:
  # @layer_files[scene][layer] = [cache path, width, height]. A layer from
  # the script's `scenery` folder replaces the default when it is there and
  # readable; a bad one is reported and the default used.
  def prepare_scenery
    first = @layer_files.nil?
    @layer_files = []
    bad = ""
    mine = 0
    n = 0
    while n < SCENES
      row = []
      l = 0
      while l < 3
        name = "scene#{n}_#{LAYERS[l]}.bmp"
        loading(name, n * 3 + l + 1) if first
        f = nil
        if !@scenery_dir.empty? && ::File.exist?("#{@scenery_dir}/#{name}")
          f = layer_file(@scenery_dir, name, "user")
          if f.is_a?(String)
            Log.warn("walk_scroller: #{@scenery_dir}/#{name}: #{f}; using the default")
            bad = "#{name} #{f}" if bad.empty?
            f = nil
          else
            mine += 1
          end
        end
        if f.nil?
          f = layer_file(SPRITE_DIR, name, "default")
          if f.is_a?(String)
            Log.error("walk_scroller: #{SPRITE_DIR}/#{name}: #{f}")
            f = nil
          end
        end
        row << f
        l += 1
      end
      @layer_files << row
      n += 1
    end
    @scenery_note = bad.empty? ? "" : "#{bad} (default used)"
    @status_key = nil
    Log.info("walk_scroller: scenery: #{mine} layers from #{@scenery_dir}") unless @scenery_dir.empty?
  end

  # Progress while the layers go to the graphics side at start: the first
  # start after an update copies them all, which takes a while where that
  # side is a separate machine (seconds in the Linux simulator).
  def loading(name, i)
    @gfx.fill_rect(@ox, @oy, @w, @h, 0x53)
    @gfx.draw_text(@ox + 8, @oy + 8, "Walk Scroller: loading #{name} (#{i}/#{SCENES * 3})", 0xFF)
    @gfx.present
  end

  def layer_file(dir, name, tag)
    size = bmp_size("#{dir}/#{name}", LAYER_MAX_W, LAYER_MAX_H)
    return size if size.is_a?(String)
    dest = "#{CACHE}/#{tag}/#{name}"
    return "copy failed" unless @gfx.sync_file("#{dir}/#{name}", dest: dest)
    [dest, size[0], size[1]]
  end

  # Loads the scene's three layers (only the current scene is in memory)
  # and lays them out: the ground layer on the bottom of the screen, the
  # middle and the far layer standing on it, the sky above. A layer of the
  # size already held is read into the same image: freeing a large image
  # and asking for one as large again can fail on the graphics side's pool.
  def build_scene(n)
    t0 = Machine.board_millis
    @scene = n
    l = 0
    while l < 3
      f = @layer_files[n][l]
      img = @layers[l]
      if img && (f.nil? || img.width != f[1] || img.height != f[2])
        img.destroy
        img = nil
      end
      if f && img.nil?
        begin
          img = SpriteImage.new(@gfx, width: f[1], height: f[2], transparent_color: TKEY, use_transparent: true)
        rescue => e
          Log.warn("walk_scroller: #{f[0]} (#{f[1]}x#{f[2]}): #{e.message}")
          @scenery_note = "scenery: no room for #{LAYERS[l]} #{f[1]}x#{f[2]}"
          @status_key = nil
          img = nil
        end
      end
      img.load_bmp(f[0]) if img
      @layers[l] = img
      l += 1
    end
    gh = @layers[2] ? @layers[2].height : 50
    @ground_y = @h - gh
    @mid_y = @ground_y - (@layers[1] ? @layers[1].height : 0)
    @far_y = @ground_y - (@layers[0] ? @layers[0].height : 0)
    sk = sky_of(n)
    @sky = sk[0]
    @sun = sk[1]
    @walker.move(@ox + @char_cx - @walker_w / 2, @oy + @ground_y + WALKER_SINK - @walker_h) if @walker
    Log.info("walk_scroller: scene #{n} loaded in #{Machine.board_millis - t0} ms")
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
    @frame = @walk_n
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
      # one step is half a walking cycle
      @frame = ((@pos * @walk_n / (2 * @step_px)).to_i) % @walk_n
    else
      @frame = @walk_n
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

  # A layer across the screen at its offset: two draw_tile calls when it
  # is at least as wide as the screen, more when it is narrower.
  def tile_layer(img, y, speed)
    return unless img
    lw = img.width
    h = img.height
    off = (@pos * speed).to_i % lw
    x = 0
    while x < @w
      n = lw - off
      n = @w - x if n > @w - x
      @gfx.draw_tile(img.id, off, 0, n, h, dst_x: @ox + x, dst_y: @oy + y)
      x += n
      off = 0
    end
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
    tile_layer(@layers[0], @far_y, @speeds[0])
    if @effect == "fireworks"
      i = 0
      while i < @particles.size
        p = @particles[i]
        sz = (p[4] > 500 ? 3 : 2) * @s
        @gfx.fill_rect(@ox + p[0].to_i, @oy + p[1].to_i, sz, sz, p[5])
        i += 1
      end
    end
    tile_layer(@layers[1], @mid_y, @speeds[1])
    tile_layer(@layers[2], @ground_y, @speeds[2])
    if @frame != @shown_frame
      @walker.frame = @frame if @walker
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

  # After every connect (FmrbAsterism::Link): Asterism.close forgot the
  # subscription.
  def link_up(link)
    who = @follow.empty? ? "*" : @follow
    @sub = ::Asterism.session.subscribe("asterism/#{who}/step/pedometer/step", depth: 16)
    @progress_key = "asterism/#{link.node}/#{APP}/scroller/progress"
  end

  # A connect failed or the connection went away (closed by the Link).
  def link_down(link)
    @sub = nil
    @accel_sub = nil
    @accel_node = ""
    @tune_call = nil
  end

  def network(now)
    return unless @link.update
    begin
      @sub.each_pending { |key, payload, att| step_value(key, payload, now) } if @sub
      tune_network(now) if @panel_on
    rescue => e
      @link.lost("#{e.class}: #{e.message}")
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
    @step_node = src
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
    return unless a && @link.connected?
    v = { "count" => @count, "mark" => @shown_mark, "src" => a[0], "src_steps" => a[1], "ms" => now }
    ::Asterism.session.put(@progress_key, ::Asterism::Codec.pack(v))
  rescue => e
    Log.warn("walk_scroller: progress put: #{e.message}")
  end

  # ---- tuning panel ----

  def toggle_panel(now)
    @panel_on = !@panel_on
    if @panel_on
      unless @panel_img
        @panel_img = SpriteImage.new(@gfx, width: PANEL_W, height: PANEL_H, transparent_color: TKEY, use_transparent: true)
        @panel_sp = SpriteInstance.new(@gfx, @panel_img, x: @ox + 4, y: @oy + 4, z: 5)
      end
      @panel_sp.visible = true
      @tune_poll_at = now # read the state at once
      @tune_note = ""
      @accel = []
      Log.info("walk_scroller: tuning panel open")
    else
      @panel_sp.visible = false if @panel_sp
      close_accel
      @tune_want = nil
      Log.info("walk_scroller: tuning panel closed")
    end
    @panel_dirty = true
    @dirty = true
  end

  # A key while the panel is open; true when the panel took it.
  def panel_key(sc)
    if sc == FmrbConst::KEY_UP
      tune_key(TUNE_STEP, 0)
    elsif sc == FmrbConst::KEY_DOWN
      tune_key(-TUNE_STEP, 0)
    elsif sc == FmrbConst::KEY_RIGHT
      tune_key(0, TUNE_STEP)
    elsif sc == FmrbConst::KEY_LEFT
      tune_key(0, -TUNE_STEP)
    elsif sc == 0x16 # s
      tune_save
    elsif sc == 0x27 # 0: the defaults
      tune_key(nil, nil)
    else
      return false
    end
    true
  end

  # The node whose detector is tuned: the script's, else the last node
  # that sent a step (a sensor's repeated values do not count), else the
  # last node that sent anything.
  def tune_target
    return @follow unless @follow.empty?
    @step_node.empty? ? @seen_node : @step_node
  end

  def close_accel
    s = @accel_sub
    @accel_sub = nil
    @accel_node = ""
    return unless s
    begin
      s.close
    rescue
    end
  end

  # While the panel is open (from network, after the poll): follows the
  # target node with the raw-value subscription, collects the answer of the
  # call on its way, then sends the next call (a waiting tune / save, or
  # the periodic state). One call at a time.
  def tune_network(now)
    node = tune_target
    if node != @tune_node
      @tune_node = node
      @tune_vals = nil
      @tune_ok_at = 0
      @tune_err = ""
      @tune_poll_at = now
      @panel_dirty = true
    end
    if node != @accel_node
      close_accel
      unless node.empty?
        begin
          @accel_sub = ::Asterism.session.subscribe("asterism/#{node}/step/pedometer/accel", depth: 32)
          @accel_node = node
          @accel = []
        rescue => e
          Log.warn("walk_scroller: accel subscribe: #{e.message}")
        end
      end
    end
    @accel_sub.each_pending { |key, payload, att| accel_value(payload) } if @accel_sub
    c = @tune_call
    if c && c[0].done?
      @tune_call = nil
      tune_answer(c[0], c[1], now)
    end
    return if @tune_call || node.empty?
    w = @tune_want
    if w
      @tune_want = nil
      send_tune(node, w[0], w[1], now)
    elsif now >= @tune_poll_at
      send_tune(node, :state, nil, now)
    end
  end

  def send_tune(node, kind, args, now)
    @tune_poll_at = now + TUNE_POLL_MS
    method = kind == :tune ? "tune" : (kind == :save ? "save" : "state")
    begin
      f = ::Asterism.call_async("#{node}/step/pedometer", method, args, nil, TUNE_CALL_MS)
      @tune_call = [f, kind]
    rescue => e
      tune_failed(e, kind)
    end
  end

  def tune_answer(f, kind, now)
    v = nil
    begin
      v = f.value
    rescue => e
      tune_failed(e, kind)
      return
    end
    unless v.is_a?(Hash) && v["rise_mg"].is_a?(Integer)
      @tune_err = "#{kind}: no settings in the answer (old step_sensor?)"
      @panel_dirty = true
      return
    end
    @tune_vals = [v["rise_mg"], v["min_gap_ms"].to_i, v["saved"] == true]
    @tune_ok_at = now
    @tune_err = ""
    if kind == :save
      @tune_note = v["saved"] == true ? "saved on the sensor" : "save failed: #{v["error"]}"
    elsif kind == :tune
      @tune_note = "tuned (S to save)"
    end
    err = v["error"] ? " (#{v["error"]})" : ""
    Log.info("walk_scroller: #{kind} #{@tune_node}: rise #{@tune_vals[0]} gap #{@tune_vals[1]} saved #{@tune_vals[2]}#{err}") if kind != :state
    @panel_dirty = true
  end

  def tune_failed(e, kind)
    @tune_err = if e.is_a?(::Asterism::TimeoutError)
                  e.message.include?("nobody answers") ? "no answer: sensor gone or not allowed" : "no answer within #{TUNE_CALL_MS / 1000} s"
                elsif e.is_a?(::Asterism::RemoteError)
                  "#{e.remote_kind || 'error'}: #{e.remote_message}"
                elsif e.is_a?(::Asterism::Disconnected)
                  "offline: #{e.message}"
                else
                  "#{e.class}: #{e.message}"
                end
    Log.warn("walk_scroller: #{kind} #{@tune_node}: #{@tune_err}")
    @panel_dirty = true
  end

  def accel_value(payload)
    v = ::Asterism::Codec.unpack(payload)
    return unless v.is_a?(Hash)
    # dyn_peak catches the impacts dyn at 20 Hz misses; older senders lack it.
    d = v["dyn_peak"]
    d = v["dyn"] unless d.is_a?(Integer)
    return unless d.is_a?(Integer)
    @accel << d
    @accel.shift while @accel.size > GRAPH_N
    r = v["rise"]
    @accel_rise = r if r.is_a?(Integer)
    @accel_new = true
  end

  # A key on the panel: a change of the values in view (nil: the defaults).
  # It waits behind the call on its way; the last one asked wins.
  def tune_key(d_rise, d_gap)
    if @tune_node.empty?
      @tune_note = "no sensor to tune yet"
      @panel_dirty = true
      return
    end
    base = @tune_want && @tune_want[0] == :tune ? @tune_want[1] : @tune_vals
    if d_rise.nil?
      args = [TUNE_DEFAULT[0], TUNE_DEFAULT[1]]
    elsif base.nil?
      @tune_note = "waiting for the sensor's values"
      @panel_dirty = true
      return
    else
      args = [base[0] + d_rise, base[1] + d_gap]
    end
    @tune_want = [:tune, args]
    @tune_note = "asking #{args[0]} mg / #{args[1]} ms"
    @panel_dirty = true
  end

  def tune_save
    if @tune_node.empty?
      @tune_note = "no sensor to save on"
    else
      @tune_want = [:save, nil]
      @tune_note = "saving..."
    end
    @panel_dirty = true
  end

  def fit(t, n)
    t.length > n ? t[0, n] : t
  end

  # Redraws the panel's sprite when something in it changed (the graph at
  # most every GRAPH_MS, the age of the last answer once a second). True
  # when it was redrawn.
  def update_panel(now)
    return false unless @panel_on
    sec = @tune_ok_at > 0 ? (now - @tune_ok_at) / 1000 : -1
    if sec != @panel_sec
      @panel_sec = sec
      @panel_dirty = true
    end
    @panel_dirty = true if @accel_new && now - @panel_drawn_at >= GRAPH_MS
    return false unless @panel_dirty
    @panel_dirty = false
    @accel_new = false
    @panel_drawn_at = now
    draw_panel(sec)
    true
  end

  def draw_panel(sec)
    w = PANEL_W
    h = PANEL_H
    @panel_img.set_target
    @gfx.fill_rect(0, 0, w, h, PANEL_BG)
    @gfx.draw_rect(0, 0, w, h, 0x92)
    node = @tune_node.empty? ? "(no sensor yet)" : @tune_node
    @gfx.draw_text(4, 3, fit("TUNE #{node}", PANEL_CHARS), 0xFF)
    v = @tune_vals
    if v
      @gfx.draw_text(4, 13, "rise #{v[0]} mg  gap #{v[1]} ms", 0xFF)
      @gfx.draw_text(4, 23, v[2] ? "saved" : "not saved", v[2] ? 0x1C : 0xFC)
    else
      @gfx.draw_text(4, 13, "rise -- mg  gap -- ms", 0xB6)
    end
    age = sec >= 0 ? "read #{sec} s ago" : "not read yet"
    @gfx.draw_text(w - 4 - age.length * 6, 23, age, 0xB6)
    bad = !@tune_err.empty? || !@link.connected?
    line = if !@tune_err.empty? then @tune_err
           elsif !@link.connected? then "offline: #{@link.reason}"
           elsif @tune_node.empty? then "waiting for a step or script node"
           else @tune_note
           end
    @gfx.draw_text(4, 33, fit(line, PANEL_CHARS), bad ? 0xEC : 0xB6)
    # The graph: dyn_peak (or dyn) of the last GRAPH_N raw values, the
    # threshold across.
    gx = 4
    gy = 45
    gw = GRAPH_N * 2
    gh = h - gy - 14
    rise = @accel_rise > 0 ? @accel_rise : (v ? v[0] : TUNE_DEFAULT[0])
    top = rise * 2
    top = 400 if top < 400
    low = -top / 4
    span = top - low
    @gfx.fill_rect(gx, gy, gw, gh, GRAPH_BG)
    ty = gy + gh - 1 - (rise - low) * (gh - 1) / span
    @gfx.draw_line(gx, ty, gx + gw - 1, ty, 0xE0)
    zy = gy + gh - 1 - (0 - low) * (gh - 1) / span
    @gfx.draw_line(gx, zy, gx + gw - 1, zy, 0x49)
    a = @accel
    peak = nil
    px = 0
    py = 0
    x0 = gx + gw - a.size * 2
    i = 0
    while i < a.size
      d = a[i]
      peak = d if peak.nil? || d > peak
      d = top if d > top
      d = low if d < low
      y = gy + gh - 1 - (d - low) * (gh - 1) / span
      x = x0 + i * 2
      @gfx.draw_line(px, py, x, y, 0x1F) if i > 0
      px = x
      py = y
      i += 1
    end
    lx = gx + gw + 4
    @gfx.draw_text(lx, ty - 4, "#{rise}", 0xE0)
    @gfx.draw_text(lx, gy + gh - 18, "peak", 0xB6)
    @gfx.draw_text(lx, gy + gh - 8, peak ? "#{peak}" : "--", 0x1F)
    @gfx.draw_text(4, h - 11, "Up/Dn rise Lt/Rt gap S save 0 dflt", 0x92)
    @panel_img.reset_target
  end

  # ---- input and the loop ----

  def on_control(msg)
    return unless msg["cmd"] == "file_selected"
    load_script(msg["path"].to_s)
    reload_art
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
      next if @panel_on && panel_key(sc)
      case sc
      when 0x17 then toggle_panel(now)   # t
      when FmrbConst::KEY_SPACE, FmrbConst::KEY_RIGHT then manual_step(now)
      when 0x11 then jump_mark(1, now)   # n
      when 0x05 then jump_mark(-1, now)  # b
      when 0x15 then restart(now)        # r
      when 0x0F                          # l
        load_script(nil)
        reload_art
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
    panel = update_panel(now)
    if changed || @dirty
      render(now)
    elsif panel
      @gfx.present # only the panel's sprite changed
    end
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
      Log.info("walk_scroller: #{@frames_n} frames in #{STATS_MS / 1000} s, render avg #{avg / 10}.#{avg % 10} ms max #{@render_max} ms, count #{@count}, #{@link.state} #{@link.reason}")
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
