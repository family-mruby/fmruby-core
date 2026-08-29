# PicoRabbit - Fullscreen presentation tool
# Supports PicoRabbit-compatible markdown format
#
# The app starts on a menu of every deck it can find: your own under
# /home/slides, the samples under /usr/share/samples/slides, and the SD card. From there a deck is presented, or written out as one
# picture per slide.
#
# Keys follow Rabbit upstream (doc/picorabbit/rabbit_behavior.md), which
# separates "next" (walk one wait step) from "next slide" (skip the rest of
# the steps):
#   next        Space / Enter / PgDn / Tab / n f j l
#   next slide  Right / Down
#   previous    PgUp / BackSpace / p b h k
#   prev slide  Left / Up
#   first/last  Home, a / End, e
#   back to the menu  Esc
#   quit        q
#   rabbit jump u      (not in upstream; Up is "previous slide" there)
#   reset timer t      (upstream Alt+t; Alt does not reach us in the sim)
#
# Presenter aids sit on the shift layer, because the plain letters were
# already spoken for above:
#   black / white screen   Shift+b / Shift+w   (upstream spells these the same)
#   remaining time on/off  Shift+t
#   pause / resume clock   Shift+p
#   index of every slide   i          (Esc or i again to leave it)
#     in the index: Up/Down/PgUp/PgDn/Home/End move the selection,
#     Enter / Space / Right jumps to it, and a tap jumps outright
#
# In the menu: Up/Down/PgUp/PgDn/Home/End move the selection and a tap picks a
# row; Enter starts, e exports, q or Esc quits.
#
# A one-finger tap (left button) goes forward; a two-finger tap (right
# button) or the middle button goes back. Where the tap lands is not looked
# at -- the Tab5 touch is a trackpad, so a tap clicks at the cursor, not at
# the finger. Like upstream's click, a tap skips the wait steps rather than
# walking them.

class SlideShowApp < FmrbApp
  # Where decks live. The second is the card, and is simply absent on a
  # machine without one.
  SLIDE_DIRS = ["/home/slides", "/usr/share/samples/slides", "/mnt/sd/slides"]
  SLIDE_TAGS = ["home", "sd"]
  SD_ROOT = "/mnt/sd"
  EXPORT_ROOT = "/mnt/sd/picorabbit"

  MENU_ROW_H = 10
  MENU_HEAD_H = 12
  MENU_BTN_H = 16
  MENU_BTN_W = 64
  MENU_CHAR_W = 6

  # How long to wait for one exported picture to appear. On the device the
  # file is the signal; ten seconds is far past a card write of a 426x240
  # JPEG and still short enough to report a failure rather than hang.
  EXPORT_WAIT_MS = 10000
  # In the simulator the file lands on the graphics side, which the core
  # cannot see, so the wait is a fixed one.
  EXPORT_SIM_WAIT_MS = 200

  def initialize
    super()
    @mode = :menu
    @slide_index = 0
    @step = 0
    @max_step = 0
    @result = nil
    @renderer = nil
    @loaded_path = nil
    @overlay = nil        # nil, :black or :white while the screen is blanked
    @index_mode = false
    @index_sel = 0
    @show_time = false
    @audio = nil
    @chime = false
    @chime_off_at = nil
    @deck_labels = []
    @deck_paths = []
    @menu_sel = 0
    @menu_scroll = 0
    @status = nil
    @sd_ready = false
    # Core and display share one filesystem on the device and not in the
    # simulator, which decides both how an export reports it is done and what
    # the pictures are called (the display side writes JPEG there, BMP here).
    @on_device = FmrbConst::PLATFORM == "esp32"
  end

  def on_create
    Log.info("PicoRabbit started")

    @menu_btn_y = @window_height - MENU_BTN_H - 4
    @menu_status_y = @menu_btn_y - 12
    @menu_list_y = MENU_HEAD_H + 4
    @menu_rows = (@menu_status_y - 2 - @menu_list_y) / MENU_ROW_H
    @menu_rows = 1 if @menu_rows < 1

    scan_decks
    @audio = FmrbAudio.new(self)
    @sd_ready = prepare_sd

    @ui = FmrbUI.new(self)
    @ui.button(:start, 4, @menu_btn_y, MENU_BTN_W, MENU_BTN_H, "Start")
    @ui.button(:export, 8 + MENU_BTN_W, @menu_btn_y, MENU_BTN_W, MENU_BTN_H,
               "Export")
    @ui.button(:quit, 12 + MENU_BTN_W * 2, @menu_btn_y, MENU_BTN_W, MENU_BTN_H,
               "Quit")

    unless @sd_ready
      @ui.set_enabled(:export, false)
      @status = "no card at #{SD_ROOT} - nothing to export to"
    end
    if @deck_labels.length == 0
      @ui.set_enabled(:start, false)
      @ui.set_enabled(:export, false)
      @status = "no .md deck in #{SLIDE_DIRS[0]} or #{SLIDE_DIRS[1]}"
    end

    draw_menu
  end

  # ---- decks ------------------------------------------------------------

  # Every .md under the two deck directories, named by where it came from so
  # two decks of the same name stay apart. A directory that is not there (no
  # card, or a card without a slides folder) simply contributes nothing.
  def scan_decks
    d = 0
    while d < SLIDE_DIRS.length
      names = list_md(SLIDE_DIRS[d])
      i = 0
      while i < names.length
        @deck_labels << "#{SLIDE_TAGS[d]}/#{names[i]}"
        @deck_paths << "#{SLIDE_DIRS[d]}/#{names[i]}"
        i += 1
      end
      d += 1
    end
  end

  def list_md(dir)
    files = []
    begin
      handle = Dir.open(dir)
      while (entry = handle.read)
        next if entry == "." || entry == ".."
        files << entry if entry.end_with?(".md")
      end
      handle.close
    rescue => e
      Log.info("PicoRabbit: no decks under #{dir} (#{e.message})")
    end
    files.sort!
    files
  end

  # Is there somewhere to export to? On the device /mnt/sd is the card's
  # mount point and its absence means there is no card. The simulator has no
  # card and no mount for one -- there /mnt/sd is an ordinary directory, so
  # make it and export there, which is what makes the export path testable
  # without hardware.
  def prepare_sd
    return Dir.exist?(SD_ROOT) if @on_device
    mkdir_p(SD_ROOT)
  end

  def mkdir_p(path)
    begin
      i = 1
      while i < path.length
        if path[i] == "/"
          part = path[0, i]
          Dir.mkdir(part) unless Dir.exist?(part)
        end
        i += 1
      end
      Dir.mkdir(path) unless Dir.exist?(path)
    rescue => e
      Log.warn("PicoRabbit: mkdir #{path}: #{e.message}")
    end
    Dir.exist?(path)
  end

  # "/home/slides/intro_ja.md" -> "intro_ja"
  def deck_name(path)
    i = path.length - 1
    while i >= 0 && path[i] != "/"
      i -= 1
    end
    base = path[i + 1, path.length - i - 1]
    base.end_with?(".md") ? base[0, base.length - 3] : base
  end

  # Parse and prepare a deck, unless it is already the one in hand: the parse
  # and the eight sprite frames cost more than going back to the menu should.
  def ensure_deck(path)
    return true if @loaded_path == path && @renderer
    @renderer.destroy_sprites if @renderer
    @renderer = nil
    @loaded_path = nil
    load_presentation(path)
    return false unless @renderer
    @renderer.load_sprites
    @loaded_path = path
    true
  end

  def load_presentation(path)
    begin
      @result = PicoRabbit::Parser.parse_file(path)
      @renderer = PicoRabbit::FmrbRenderer.new(
        @gfx, @window_width, @window_height, @result.metadata)
      @renderer.precompile(@result.slides)
      @renderer.goal_index = find_goal_index
      # Images in a deck are named relative to the deck itself.
      @renderer.deck_path = path
      @chime = @result.metadata["chime"] == "true"
      @slide_index = 0
      update_step
      Log.info("Loaded #{path}: #{@result.slides.length} slides")
    rescue => e
      Log.error("Failed to load #{path}: #{e.message}")
      @renderer = nil
    end
  end

  # The first slide carrying {::goal/}, or nil for "the last one".
  def find_goal_index
    slides = @result.slides
    i = 0
    while i < slides.length
      return i if slides[i].goal
      i += 1
    end
    nil
  end

  # ---- the menu ---------------------------------------------------------

  def draw_menu
    @mode = :menu
    @gfx.clear(FmrbConst::THEME_WINDOW_BG)
    @gfx.fill_rect(0, 0, @window_width, MENU_HEAD_H, FmrbConst::THEME_MENU_BG)
    @gfx.set_text_size(1)
    @gfx.draw_text(4, 2, "PicoRabbit", FmrbConst::THEME_TEXT,
                   FmrbConst::THEME_MENU_BG)
    n = @deck_labels.length
    right = n == 1 ? "1 deck" : "#{n} decks"
    @gfx.draw_text(@window_width - 4 - right.length * MENU_CHAR_W, 2, right,
                   FmrbConst::THEME_TEXT, FmrbConst::THEME_MENU_BG)

    i = @menu_scroll
    last = @menu_scroll + @menu_rows
    last = n if last > n
    while i < last
      draw_menu_row(i)
      i += 1
    end

    draw_menu_status
    @ui.invalidate_all
    @ui.flush
  end

  def draw_menu_row(i)
    row = i - @menu_scroll
    return if row < 0 || row >= @menu_rows
    y = @menu_list_y + row * MENU_ROW_H
    sel = i == @menu_sel
    bg = sel ? FmrbConst::THEME_HIGHLIGHT : FmrbConst::THEME_WINDOW_BG
    fg = sel ? FmrbConst::THEME_TEXT_LIGHT : FmrbConst::THEME_TEXT
    @gfx.fill_rect(2, y - 1, @window_width - 4, MENU_ROW_H, bg)
    @gfx.set_text_size(1)
    @gfx.draw_text(6, y, @deck_labels[i], fg, bg)
  end

  # One line above the buttons: why Export is off, or where the pictures went.
  # With nothing to report it carries the keys instead, so the menu says how
  # to work it without a manual.
  MENU_HINT = "Enter start   e export   q quit"

  def draw_menu_status
    @gfx.fill_rect(0, @menu_status_y, @window_width, 10,
                   FmrbConst::THEME_WINDOW_BG)
    @gfx.draw_line(2, @menu_status_y, @window_width - 2, @menu_status_y,
                   FmrbConst::THEME_BORDER)
    @gfx.set_text_size(1)
    @gfx.draw_text(4, @menu_status_y + 2, @status || MENU_HINT,
                   FmrbConst::THEME_BORDER, FmrbConst::THEME_WINDOW_BG)
  end

  def move_menu(delta)
    move_menu_to(@menu_sel + delta)
  end

  def move_menu_to(idx)
    n = @deck_labels.length
    return if n == 0
    idx = 0 if idx < 0
    idx = n - 1 if idx >= n
    return if idx == @menu_sel
    prev = @menu_sel
    @menu_sel = idx
    if scroll_menu_into_view
      draw_menu
      return
    end
    draw_menu_row(prev)
    draw_menu_row(@menu_sel)
    @gfx.present
  end

  # Returns true when the window of visible rows had to move, which is the
  # only case that needs the whole list drawn again.
  def scroll_menu_into_view
    top = @menu_scroll
    if @menu_sel < top
      @menu_scroll = @menu_sel
    elsif @menu_sel >= top + @menu_rows
      @menu_scroll = @menu_sel - @menu_rows + 1
    end
    @menu_scroll != top
  end

  # Which row a tap landed on, or nil. A tap picks the row and stops there:
  # starting a talk is what the Start button is for.
  def menu_hit(x, y)
    return nil if y < @menu_list_y
    row = (y - @menu_list_y) / MENU_ROW_H
    return nil if row < 0 || row >= @menu_rows
    i = @menu_scroll + row
    i < @deck_labels.length ? i : nil
  end

  def on_menu_event(ev)
    id = @ui.handle(ev)
    @ui.flush
    case id
    when :start
      return start_show
    when :export
      return run_export
    when :quit
      return stop
    end

    if ev[:type] == :mouse_up
      row = menu_hit((ev[:x] || 0).to_i, (ev[:y] || 0).to_i)
      move_menu_to(row) if row
      return
    end
    return unless ev[:type] == :key_down

    case ev[:scancode] || 0
    when FmrbConst::KEY_UP
      move_menu(-1)
    when FmrbConst::KEY_DOWN
      move_menu(1)
    when FmrbConst::KEY_PGUP
      move_menu(-@menu_rows)
    when FmrbConst::KEY_PGDN
      move_menu(@menu_rows)
    when FmrbConst::KEY_HOME
      move_menu_to(0)
    when FmrbConst::KEY_END
      move_menu_to(@deck_labels.length - 1)
    when FmrbConst::KEY_ENTER, FmrbConst::KEY_SPACE
      start_show
    when FmrbConst::KEY_E
      run_export if @sd_ready
    when FmrbConst::KEY_ESC, FmrbConst::KEY_Q
      stop
    end
  end

  # ---- presenting -------------------------------------------------------

  # Opened from somewhere else -- the file manager's double click on a .md,
  # or `open` in the shell. The kernel sends this once the app can receive it
  # (doc/user_extension/assoc).
  #
  # It goes straight into the deck rather than to the menu: someone who
  # double-clicked a deck has already chosen which one, and making them pick
  # it again from a list would be asking twice. Esc still comes back to the
  # menu, so nothing is lost.
  def on_control(msg)
    return nil unless msg["cmd"] == "file_selected"
    path = msg["path"].to_s
    return nil if path.empty?
    Log.info("PicoRabbit: opening #{path}")
    start_show(path)
    nil
  end

  # Every start is a fresh run: the deck opens at its first slide with the
  # clock back at the top, whether or not it is the deck just presented.
  #
  # +path+ names the deck when something else chose it (on_control above);
  # without it the menu selection is used.
  def start_show(path = nil)
    path = @deck_paths[@menu_sel] if path.nil?
    return if path.nil?
    unless ensure_deck(path)
      @status = "cannot read #{path}"
      draw_menu
      return
    end
    @mode = :show
    @overlay = nil
    @index_mode = false
    @show_time = false
    @renderer.clock_hidden = false
    @renderer.clock_visible = false
    @renderer.sprites_visible = true
    @renderer.reset_timer
    @slide_index = 0
    update_step
    draw_current
  end

  # Esc during a talk. The runners belong to the talk, not to the menu.
  def back_to_menu
    @renderer.sprites_visible = false if @renderer
    @overlay = nil
    @index_mode = false
    @status = nil
    draw_menu
  end

  def update_step
    return unless @result
    slide = @result.slides[@slide_index]
    @max_step = slide ? slide.wait_count : 0
    @step = 0
  end

  def draw_current
    return unless @result && @renderer
    slide = @result.slides[@slide_index]
    return unless slide
    step_val = @max_step > 0 ? @step : nil
    @renderer.render_slide(slide, step_val, @slide_index, @result.slides.length)
  end

  def advance
    return unless @result
    if @step < @max_step
      @step += 1
      draw_current
      return
    end
    if @slide_index < @result.slides.length - 1
      @slide_index += 1
      update_step
      draw_current
    end
  end

  def go_back
    return unless @result
    if @step > 0
      @step -= 1
      draw_current
      return
    end
    if @slide_index > 0
      @slide_index -= 1
      update_step
      @step = @max_step
      draw_current
    end
  end

  # Skip the remaining wait steps of this slide and show the next one from
  # its own first step ("next slide" upstream).
  def next_slide
    return unless @result
    return if @slide_index >= @result.slides.length - 1
    @slide_index += 1
    update_step
    draw_current
  end

  def prev_slide
    return unless @result
    return if @slide_index <= 0
    @slide_index -= 1
    update_step
    draw_current
  end

  def go_first
    return unless @result
    @slide_index = 0
    update_step
    draw_current
  end

  def go_last
    return unless @result
    @slide_index = @result.slides.length - 1
    update_step
    @step = @max_step
    draw_current
  end

  # ---- exporting --------------------------------------------------------
  #
  # One picture per slide, into a directory named after the deck. Each slide
  # is drawn the way it ends up -- every wait open -- with the runners off the
  # track and the clock out of the footer, and then written by the display
  # side through the GFX command that follows the present.
  #
  # Nothing is accepted while this runs. It is a handful of seconds and the
  # screen is busy showing the slides being written.

  def run_export
    return unless @sd_ready
    return if @deck_labels.length == 0
    path = @deck_paths[@menu_sel]
    unless ensure_deck(path)
      @status = "cannot read #{path}"
      draw_menu
      return
    end

    dir = "#{EXPORT_ROOT}/#{deck_name(path)}"
    unless mkdir_p(dir)
      @status = "cannot make #{dir}"
      draw_menu
      return
    end

    total = @result.slides.length
    width = total >= 100 ? 3 : 2
    keep_index = @slide_index
    keep_step = @step
    keep_clock = @show_time
    @renderer.sprites_visible = false
    @renderer.clock_hidden = true

    done = 0
    i = 0
    while i < total
      @slide_index = i
      update_step
      @step = @max_step
      draw_current
      file = "#{dir}/#{export_file_name(i + 1, width)}"
      # A picture left over from an earlier export would answer the wait
      # below the instant it is asked, so the loop would run ahead of the
      # writer and read a failed write as a success. Take it away first: on
      # the device the file appearing is the only signal there is.
      remove_stale(file)
      @gfx.export_frame(file)
      break unless wait_export(file)
      done += 1
      draw_export_progress(done, total)
      i += 1
    end

    @renderer.clock_hidden = false
    @renderer.clock_visible = keep_clock
    @renderer.sprites_visible = @mode == :show
    @slide_index = keep_index
    update_step
    @step = keep_step > @max_step ? @max_step : keep_step

    @status = if done == total
                "#{done} files -> #{dir}/"
              else
                "stopped after #{done}/#{total} in #{dir}/"
              end
    Log.info("PicoRabbit export: #{@status}")
    draw_menu
  end

  # Clear the way for one picture. Only on the device: in the simulator the
  # pictures land on the graphics side's filesystem, which the core cannot
  # reach, and the fixed wait there does not depend on the file anyway.
  def remove_stale(path)
    return unless @on_device
    begin
      File.delete(path) if File.exist?(path)
    rescue => e
      Log.warn("PicoRabbit export: cannot remove #{path}: #{e.message}")
    end
  end

  # "01.jpg" .. "99.jpg", three digits past a hundred slides. The display
  # side writes a JPEG on the device and a BMP in the simulator, so the name
  # says which.
  def export_file_name(num, width)
    s = num.to_s
    while s.length < width
      s = "0#{s}"
    end
    @on_device ? "#{s}.jpg" : "#{s}.bmp"
  end

  # Wait for one picture to be written. On the device the core and the
  # display share a filesystem, so the file appearing is the write finishing.
  # In the simulator they do not share one we can see, and all we can do is
  # give the display side time.
  def wait_export(path)
    unless @on_device
      sleep_ms EXPORT_SIM_WAIT_MS
      return true
    end
    t0 = Machine.board_millis
    while Machine.board_millis - t0 < EXPORT_WAIT_MS
      return true if File.exist?(path)
      sleep_ms 20
    end
    Log.error("PicoRabbit export: #{path} never appeared")
    false
  end

  # The slide has to be on screen for its picture to be taken, so the count
  # goes over the slide that was just written rather than over the menu. The
  # next slide wipes it.
  def draw_export_progress(done, total)
    y = @window_height - 10
    @gfx.fill_rect(0, y, @window_width, 10, FmrbConst::THEME_MENU_BG)
    @gfx.set_text_size(1)
    @gfx.draw_text(4, y + 1, "Exporting #{done}/#{total} ...",
                   FmrbConst::THEME_TEXT, FmrbConst::THEME_MENU_BG)
    @gfx.present
  end

  # ---- blank screen ----
  #
  # Shift+b / Shift+w paint the screen over. Any key or tap brings the slide
  # back and does nothing else: a presenter who blanked the screen to talk
  # should not find the deck three pages on when they bring it back.

  def show_overlay(kind)
    @overlay = kind
    @renderer.sprites_visible = false if @renderer
    @gfx.clear(kind == :black ? 0x00 : 0xFF)
    @gfx.present
  end

  def clear_overlay
    @overlay = nil
    @renderer.sprites_visible = true if @renderer
    draw_current
  end

  # Bring the runners and the clock up to date and present once if anything
  # moved. The keys below change the clock out of band, and the runners are
  # only placed once a second: without this the turtle would creep to its new
  # place on the next tick, after the clock had already stopped.
  def refresh_race
    return unless @renderer && @result
    dirty = @renderer.update_sprites(@slide_index, @result.slides.length)
    dirty = true if @renderer.draw_clock
    @gfx.present if dirty
  end

  def toggle_clock
    return unless @renderer
    @show_time = !@show_time
    @renderer.clock_visible = @show_time
    refresh_race
  end

  def toggle_pause
    return unless @renderer
    if @renderer.timer_paused?
      @renderer.resume_timer
    else
      @renderer.pause_timer
    end
    refresh_race
  end

  def reset_timer
    return unless @renderer
    @renderer.reset_timer
    refresh_race
  end

  # ---- index ----
  #
  # A list of every heading, to jump straight to a slide. The runners are
  # hidden while it is up, but the clock keeps running: the talk does not
  # stop because the presenter is looking for a page.

  def open_index
    return unless @result
    @index_mode = true
    @index_sel = @slide_index
    @renderer.sprites_visible = false if @renderer
    draw_index
  end

  def close_index
    @index_mode = false
    @renderer.sprites_visible = true if @renderer
    draw_current
  end

  def draw_index
    @renderer.render_index(@result.slides, @slide_index, @index_sel)
  end

  def on_event(ev)
    return on_menu_event(ev) if @mode == :menu
    return on_index_event(ev) if @index_mode
    if ev[:type] == :mouse_up
      return clear_overlay if @overlay
      # Upstream's click is "next slide", not a wait step, so a tap skips
      # the steps too. The position is deliberately not looked at: the Tab5
      # touch is a trackpad (a tap clicks wherever the cursor already is),
      # so a left/right split reads the cursor, not the finger. One finger
      # forward; two fingers (a right click) or a middle button back.
      if (ev[:button] || 1) == 1
        next_slide
      else
        prev_slide
      end
      return
    end
    return unless ev[:type] == :key_down
    return clear_overlay if @overlay

    sc = ev[:scancode] || 0
    if ev_shift?(ev)
      case sc
      when FmrbConst::KEY_B
        show_overlay(:black)
      when FmrbConst::KEY_W
        show_overlay(:white)
      when FmrbConst::KEY_T
        toggle_clock
      when FmrbConst::KEY_P
        toggle_pause
      end
      return
    end

    # Scancodes are USB HID Usage IDs on the device and in the simulator
    # alike; ev[:keycode] is an SDL keysym on Linux and would not match.
    # Letters are read as scancodes too, so kana mode cannot shift them.
    case sc
    when FmrbConst::KEY_SPACE, FmrbConst::KEY_ENTER, FmrbConst::KEY_PGDN,
         FmrbConst::KEY_TAB, FmrbConst::KEY_N, FmrbConst::KEY_F,
         FmrbConst::KEY_J, FmrbConst::KEY_L
      advance
    when FmrbConst::KEY_RIGHT, FmrbConst::KEY_DOWN
      next_slide
    when FmrbConst::KEY_PGUP, FmrbConst::KEY_BACKSPACE, FmrbConst::KEY_P,
         FmrbConst::KEY_B, FmrbConst::KEY_H, FmrbConst::KEY_K
      go_back
    when FmrbConst::KEY_LEFT, FmrbConst::KEY_UP
      prev_slide
    when FmrbConst::KEY_HOME, FmrbConst::KEY_A
      go_first
    when FmrbConst::KEY_END, FmrbConst::KEY_E
      go_last
    when FmrbConst::KEY_ESC
      back_to_menu
    when FmrbConst::KEY_Q
      stop
    when FmrbConst::KEY_U
      @renderer.jump_rabbit if @renderer
    when FmrbConst::KEY_T
      reset_timer
    when FmrbConst::KEY_I
      open_index
    end
  end

  # A tap picks a slide outright rather than selecting it first: upstream
  # wants a double click here, which is awkward on a touch screen.
  def on_index_event(ev)
    if ev[:type] == :mouse_up
      row = @renderer.index_hit((ev[:x] || 0).to_i, (ev[:y] || 0).to_i,
                                @result.slides.length)
      if row
        @index_sel = row
        jump_to_index
      end
      return
    end
    return unless ev[:type] == :key_down

    case ev[:scancode] || 0
    when FmrbConst::KEY_UP
      move_index(-1)
    when FmrbConst::KEY_DOWN
      move_index(1)
    when FmrbConst::KEY_PGUP
      move_index(-PicoRabbit::FmrbRenderer::INDEX_ROWS)
    when FmrbConst::KEY_PGDN
      move_index(PicoRabbit::FmrbRenderer::INDEX_ROWS)
    when FmrbConst::KEY_HOME
      move_index_to(0)
    when FmrbConst::KEY_END
      move_index_to(@result.slides.length - 1)
    when FmrbConst::KEY_ENTER, FmrbConst::KEY_SPACE, FmrbConst::KEY_RIGHT
      jump_to_index
    when FmrbConst::KEY_I, FmrbConst::KEY_ESC
      close_index
    end
  end

  def move_index(delta)
    move_index_to(@index_sel + delta)
  end

  def move_index_to(idx)
    n = @result.slides.length
    idx = 0 if idx < 0
    idx = n - 1 if idx >= n
    return if idx == @index_sel
    @index_sel = idx
    draw_index
  end

  # Jumping shows the chosen slide from its own first step, like every other
  # move that skips the wait steps.
  def jump_to_index
    @slide_index = @index_sel
    update_step
    @index_mode = false
    @renderer.sprites_visible = true if @renderer
    draw_current
  end

  # The race is two sprites composited over the slide, so a tick that changes
  # nothing costs nothing: once a second is enough to walk the turtle. A jump
  # is the exception -- at one frame a second it would not read as a hop, so
  # the physics runs at ten while the rabbit is off the ground.
  #
  # The clock rides on the same tick and the same present: showing it does
  # not cost a second one.
  def on_update
    return 1000 unless @renderer && @result
    # Nothing on screen belongs to the slide while the menu is up or the
    # screen is blanked, and the runners are hidden, so there is nothing to
    # draw -- but the clock keeps running underneath.
    return 1000 if @mode == :menu || @overlay || @index_mode

    was_jumping = @renderer.rabbit_jumping?
    @renderer.update_rabbit if was_jumping
    dirty = @renderer.update_sprites(@slide_index, @result.slides.length)
    dirty = true if @renderer.draw_clock
    ring_chime if @chime && @renderer.take_time_up
    @gfx.present if dirty

    return 200 if release_chime
    was_jumping ? 100 : 1000
  end

  # One short note as the turtle reaches the goal, when the deck asked for it
  # with `chime: true`. The note is let go on the next tick rather than held
  # over a sleep, so the update path never blocks.
  def ring_chime
    return unless @audio
    @audio.note_on(FmrbAudio::CH_PULSE1, 880, 12)
    @chime_off_at = Machine.board_millis + 200
  end

  # True while a note is still sounding, so the caller comes back sooner.
  def release_chime
    return false unless @chime_off_at
    if Machine.board_millis >= @chime_off_at
      @audio.note_off(FmrbAudio::CH_PULSE1)
      @chime_off_at = nil
      return false
    end
    true
  end

  # Ctrl+Tab. The suspend hides the canvas, but a sprite lives in its own
  # layer and would otherwise stay on top of the desktop.
  def on_suspend
    return unless @renderer
    @renderer.sprites_visible = false
    @gfx.present
  end

  def on_resume
    if @mode == :menu
      draw_menu
      return
    end
    return unless @renderer
    if @overlay
      @gfx.clear(@overlay == :black ? 0x00 : 0xFF)
      @gfx.present
      return
    end
    @renderer.sprites_visible = true
    draw_current
  end

  def on_destroy
    @renderer.destroy_sprites if @renderer
    Log.info("PicoRabbit destroyed")
  end
end

Log.info("SlideShowApp.new")
begin
  app = SlideShowApp.new
  Log.info("SlideShowApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
