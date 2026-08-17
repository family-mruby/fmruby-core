# PicoRuby demo - one tour of what an app written in Ruby can do.
#
# Six sections, each a few pages:
#   Shapes  @gfx primitives (rect / ellipse / triangle / arc / text size)
#   Fonts   @gfx text: the built in 6x8 font, the Japanese fonts, and the
#           mixed / hybrid modes that put both in one call
#   Sprite  BMP sprites, tiles out of a sheet, an image drawn from Ruby, and
#           a decoded picture
#   Sound   the internal sound chip: a scale, a tune, effects
#   System  the clock, the language, a file, a random number, pub/sub with
#           another app, a timer, and a required second file
#   P5      the P5 wrapper layer (doc/p5.md): matrix, bezier, alignment,
#           blend, pixel readback, masked images
#
# Click the picture for the next page (it walks every page in order), or use
# the nav bar at the bottom: < > step a page, the tabs jump to a section.
# Tab does the same from the keyboard, and so do the arrow keys except on the
# Sprite page, where they move the sprite. Each page names its own keys.
#
# The page is white and the palette is chosen for contrast against it
# (see the colour constants below); only the ADD blend page keeps a black
# stage, because adding light to white saturates.
#
# The twin of flash/app/python/python.app.py: the middle four sections are the
# same five topics in the same order, so the two languages can be put side by
# side. Replaces the separate graphics / shapes / ja_text / p5_test / mruby
# demos.

require "/app/demo/picoruby_status"

class PicoRubyDemoApp < FmrbApp
  SECTION_NAMES = ["Shapes", "Fonts", "Sprite", "Sound", "System", "P5"]
  SECTION_PAGES = [
    [:shapes, :arcs, :text_size],
    [:font_ascii, :font_ja, :font_mixed, :font_hybrid, :font_scaled],
    [:sprites, :picture],
    [:sound],
    [:system],
    [:p5_basics, :p5_transform, :p5_bezier, :p5_text, :p5_arc,
     :p5_blend, :p5_pixel, :p5_mask],
  ]
  PAGE_TITLES = {
    shapes:       "round_rect / ellipse / triangle",
    arcs:         "fill_arc / draw_arc",
    text_size:    "set_text_size 1-4",
    font_ascii:   "default font (6x8, ASCII)",
    font_ja:      "set_font(:ja, 8) and (:ja, 12)",
    font_mixed:   "Japanese and ASCII in one font",
    font_hybrid:  "draw_text(mixed: true)",
    font_scaled:  "Japanese font x text size",
    sprites:      "sprites and tiles",
    picture:      "create_image / draw_image",
    sound:        "the internal sound chip",
    system:       "clock, files, pub/sub, timer",
    p5_basics:    "rect / circle / line / point",
    p5_transform: "push_matrix / rotate / scale",
    p5_bezier:    "bezier / curve",
    p5_text:      "text_align",
    p5_arc:       "arc",
    p5_blend:     "blend_mode(ADD)",
    p5_pixel:     "get_pixel readback",
    p5_mask:      "draw_image_masked",
  }
  # Pages that animate; everything else is drawn once when the page opens.
  DYNAMIC_PAGES = [:p5_transform]

  # Assets, borrowed from the Ruby RPG demo rather than added again.
  RPG_DIR   = "/app/game/rpg_demo"
  CACHE     = "/cache/app/picoruby"
  TUNE_SRC  = "/usr/share/music/korobeiniki.fmsq"
  TUNE_SLOT = 4
  MY_TOML   = "/app/demo/picoruby.app.toml"
  TOPIC     = "demo"
  PUBLISHER = "/app/demo/pub_demo.app.rb"
  SCALE     = [262, 294, 330, 349, 392, 440, 494, 523]

  NAV_H = 12
  CHAR_W = 6
  CHAR_H = 8
  HEADER_H = 14
  ARROW_W = 12
  TAB_PAD = 3

  # The page is white, so the colours are picked for contrast against it
  # rather than against black. RGB332 is RRRGGGBB: the saturated constants
  # in FmrbGfx (YELLOW 0xFC, CYAN 0x1F, GREEN 0x1C) are near white in
  # luminance and read badly here, so mid and dark tones are used instead.
  PAPER     = 0xFF          # page background (white)
  INK       = 0x00          # body text and outlines (black)
  INK_SUB   = 0x49          # captions (R=2 G=2 B=1, dark gray)
  C_RED     = 0xC0          # R=6
  C_GREEN   = 0x0C          # G=3
  C_BLUE    = 0x02          # B=2
  C_ORANGE  = 0xE8          # R=7 G=2 (stands in for yellow)
  C_TEAL    = 0x0F          # G=3 B=3 (stands in for cyan)
  C_PURPLE  = 0x82          # R=4 B=2 (stands in for magenta)
  C_PANEL   = 0xB6          # light gray fill, for panels under dark shapes
  BAND1     = 0xAF          # mask page backdrop bands: light, but distinct
  BAND2     = 0xEB
  BAND3     = 0xBE
  COLOR_NAV_BG = 0xB6       # nav bar, a shade darker than the page
  COLOR_NAV_FG = 0x00
  COLOR_TAB_ON = 0x00       # active tab: inverted
  COLOR_TAB_ON_FG = 0xB6

  def initialize
    super()
    @section = 0
    @page = 0
    @t = 0
    @needs_redraw = true
  end

  def on_create
    @p5 = P5.new(@gfx)

    # Sprite / sound / system state. The pages that use them are drawn from
    # here, so the state lives with the app rather than with a page.
    @sprites_ready = false
    @sprite_x = 0
    @sprite_y = 0
    @frame = 0
    @image = nil
    @audio = FmrbAudio.new(self)
    @note_off_at = {}
    @note_i = -1
    @tune_on = false
    @blink = false
    @received = nil
    @pub_pid = nil
    @sent = 0
    @started_at = Machine.board_millis
    @uptime = 0
    @lang = FmrbApp.language
    @toml_bytes = read_own_toml

    subscribe(TOPIC)
    tick_blink
    Log.info("PicoRuby demo started on #{FmrbConst::PLATFORM}")
  end

  def on_update
    tick_notes
    tick_scale
    if @needs_redraw || DYNAMIC_PAGES.include?(page_key)
      @t += 0.05 if DYNAMIC_PAGES.include?(page_key)
      draw_all
      @needs_redraw = false
    end
    66
  end

  def on_resume
    super
    @needs_redraw = true
  end

  def on_event(ev)
    super(ev)
    case ev[:type]
    when :mouse_up
      return unless ev[:button] == 1
      # The close button is handled by super; do not treat it as a page turn.
      close_btn_x = @window_width - 10
      return if ev[:x] >= close_btn_x && ev[:y] >= 2 && ev[:y] < 10
      if ev[:y] >= nav_y
        _nav_click(ev[:x])
      else
        _step_page(1)
      end
    when :key_down
      # Arrow keys carry no character, so they are read as scancodes
      # (HID usage ids, the same numbers FmrbConst::KEY_* holds).
      sc = ev[:scancode]
      return if handle_page_key(sc)
      case sc
      when FmrbConst::KEY_RIGHT then _step_page(1)
      when FmrbConst::KEY_LEFT  then _step_page(-1)
      when FmrbConst::KEY_TAB   then _select_section((@section + 1) % SECTION_NAMES.size)
      end
    end
  end

  # Keys a page claims for itself. Returns true when the page took the key, so
  # the page-stepping below does not also act on it -- the sprite page needs
  # the arrows, and nothing else can then use them to turn the page.
  def handle_page_key(sc)
    case page_key
    when :sprites then sprite_key(sc)
    when :picture then picture_key(sc)
    when :sound   then sound_key(sc)
    when :system  then system_key(sc)
    else false
    end
  end

  def on_destroy
    unsubscribe(TOPIC)
    silence
    @audio.stop
    if @gfx
      @gfx.set_text_size(1)
      @gfx.set_font(:default)
    end
    Log.info("PicoRuby demo destroyed")
  end

  # --- Navigation --------------------------------------------------------

  def pages; SECTION_PAGES[@section]; end
  def page_key; pages[@page]; end

  # Steps through every page of every section in order, so clicking the
  # picture repeatedly is a full tour.
  def _step_page(dir)
    np = @page + dir
    if np >= pages.size
      @section = (@section + 1) % SECTION_NAMES.size
      @page = 0
    elsif np < 0
      @section = (@section - 1) % SECTION_NAMES.size
      @page = pages.size - 1
    else
      @page = np
    end
    @needs_redraw = true
  end

  def _select_section(idx)
    return if idx == @section
    @section = idx
    @page = 0
    @needs_redraw = true
  end

  def _nav_click(x)
    bx = @user_area_x0
    return _step_page(-1) if x < bx + ARROW_W
    return _step_page(1) if x >= bx + ARROW_W + 4 * CHAR_W && x < bx + 2 * ARROW_W + 4 * CHAR_W

    tx = _tabs_x0
    i = 0
    while i < SECTION_NAMES.size
      w = _tab_w(i)
      return _select_section(i) if x >= tx && x < tx + w
      tx += w + 2
      i += 1
    end
  end

  # --- Layout ------------------------------------------------------------

  def uw; @user_area_width; end
  def uh; @user_area_height - NAV_H; end
  def nav_y; @user_area_y0 + @user_area_height - NAV_H; end
  def body_y; @user_area_y0 + HEADER_H; end
  def body_h; uh - HEADER_H; end

  def _tab_w(i); SECTION_NAMES[i].length * CHAR_W + TAB_PAD * 2; end

  def _tabs_x0
    total = 0
    i = 0
    while i < SECTION_NAMES.size
      total += _tab_w(i) + 2
      i += 1
    end
    x = @user_area_x0 + uw - total
    min_x = @user_area_x0 + 2 * ARROW_W + 4 * CHAR_W + 4
    x < min_x ? min_x : x
  end

  # --- Frame -------------------------------------------------------------

  def draw_all
    # Sprites are composited over whatever the page draws, so they are hidden
    # everywhere except on the page that owns them.
    @player.visible = false if @sprites_ready && page_key != :sprites
    @gfx.fill_rect(@user_area_x0, @user_area_y0, uw, uh, PAPER)
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 3,
                   PAGE_TITLES[page_key], INK, PAPER)
    draw_page
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    draw_nav
    draw_window_frame
    @gfx.present
  end

  def draw_nav
    ny = nav_y
    @gfx.fill_rect(@user_area_x0, ny, uw, NAV_H, COLOR_NAV_BG)
    ty = ny + (NAV_H - CHAR_H) / 2
    bx = @user_area_x0

    @gfx.draw_text(bx + 3, ty, "<", COLOR_NAV_FG, COLOR_NAV_BG)
    @gfx.draw_text(bx + ARROW_W, ty, "#{@page + 1}/#{pages.size}",
                   COLOR_NAV_FG, COLOR_NAV_BG)
    @gfx.draw_text(bx + ARROW_W + 4 * CHAR_W + 3, ty, ">", COLOR_NAV_FG, COLOR_NAV_BG)

    tx = _tabs_x0
    i = 0
    while i < SECTION_NAMES.size
      w = _tab_w(i)
      if i == @section
        @gfx.fill_rect(tx, ny + 1, w, NAV_H - 2, COLOR_TAB_ON)
        @gfx.draw_text(tx + TAB_PAD, ty, SECTION_NAMES[i], COLOR_TAB_ON_FG, COLOR_TAB_ON)
      else
        @gfx.draw_text(tx + TAB_PAD, ty, SECTION_NAMES[i], COLOR_NAV_FG, COLOR_NAV_BG)
      end
      tx += w + 2
      i += 1
    end
  end

  def draw_page
    case page_key
    when :shapes       then draw_shapes
    when :arcs         then draw_arcs
    when :text_size    then draw_text_size
    when :font_ascii   then draw_font_ascii
    when :font_ja      then draw_font_ja
    when :font_mixed   then draw_font_mixed
    when :font_hybrid  then draw_font_hybrid
    when :font_scaled  then draw_font_scaled
    when :sprites      then draw_sprites_page
    when :picture      then draw_picture_page
    when :sound        then draw_sound_page
    when :system       then draw_system_page
    else draw_p5_page
    end
  end

  # --- Section 3: sprites, tiles and pictures ----------------------------
  #
  # Sprites are composited on the graphics side, so moving one costs a single
  # command and nothing here is redrawn. That is why anything that moves in a
  # game should be a sprite (see flash/app/game/breakout for the Python one).

  def load_sprites
    return if @sprites_ready
    ["player_down_0.bmp", "player_down_1.bmp", "world.bmp"].each do |name|
      @gfx.sync_file("#{RPG_DIR}/#{name}", dest: "#{CACHE}/#{name}")
    end

    @frames = ["player_down_0.bmp", "player_down_1.bmp"].map do |name|
      img = SpriteImage.new(@gfx, width: 16, height: 16,
                            transparent_color: 0, use_transparent: true)
      img.load_bmp("#{CACHE}/#{name}")
      img
    end
    @player = SpriteInstance.new(@gfx, @frames, x: 0, y: 0, z: 1)

    # The tile sheet stays an image: draw_tile stamps rectangles out of it
    # without an instance per tile.
    @sheet = SpriteImage.new(@gfx, width: 128, height: 128, transparent_color: 0)
    @sheet.load_bmp("#{CACHE}/world.bmp")

    # An image drawn from Ruby rather than loaded. The block form sets this
    # image as the drawing target and puts the canvas back afterwards.
    @badge = SpriteImage.new(@gfx, width: 20, height: 12)
    @badge.draw do |g|
      g.fill_rect(0, 0, 20, 12, C_BLUE)
      g.draw_rect(0, 0, 20, 12, PAPER)
      g.draw_text(2, 2, "RB", PAPER)
    end

    @sprites_ready = true
  end

  def draw_sprites_page
    load_sprites
    x = @user_area_x0 + 6
    y = body_y

    6.times do |i|
      @gfx.draw_tile(@sheet.id, (i % 4) * 16, 0, 16, 16,
                     dst_x: x + i * 16, dst_y: y)
    end
    @gfx.draw_tile(@badge.id, 0, 0, 20, 12,
                   dst_x: @user_area_x0 + uw - 26, dst_y: y + 2)

    @gfx.draw_text(x, y + 24, "SpriteInstance: move and frame", INK, PAPER)
    @gfx.draw_text(x, y + 36, "arrows move it, SPACE steps the frame", INK_SUB, PAPER)
    @gfx.draw_text(x, y + 48, "draw_tile stamps the sheet above", INK_SUB, PAPER)

    if @sprite_x == 0
      @sprite_x = x + 20
      @sprite_y = y + 64
    end
    @player.move(@sprite_x, @sprite_y)
    @player.visible = true
  end

  def sprite_key(sc)
    case sc
    when FmrbConst::KEY_SPACE
      @frame = 1 - @frame
      @player.frame = @frame
      @gfx.present
      true
    when FmrbConst::KEY_RIGHT, FmrbConst::KEY_LEFT,
         FmrbConst::KEY_DOWN, FmrbConst::KEY_UP
      @sprite_x += 6 if sc == FmrbConst::KEY_RIGHT
      @sprite_x -= 6 if sc == FmrbConst::KEY_LEFT
      @sprite_y += 6 if sc == FmrbConst::KEY_DOWN
      @sprite_y -= 6 if sc == FmrbConst::KEY_UP
      @player.move(@sprite_x, @sprite_y)
      @gfx.present
      true
    else
      false
    end
  end

  # create_image decodes a picture file (a PNG); a sprite BMP belongs in
  # load_bmp instead. Handing this one a BMP gives an empty image, silently.
  def draw_picture_page
    x = @user_area_x0 + 6
    y = body_y
    @gfx.draw_text(x, y, "I: load and draw, again to drop it", INK, PAPER)
    if @image
      @gfx.draw_text(x, y + 12,
                     "image #{@image[:id]}: #{@image[:width]}x#{@image[:height]} at 0.5",
                     INK_SUB, PAPER)
      @gfx.draw_image(@image[:id], x: x, y: y + 26, scale_x: 0.5)
    else
      @gfx.draw_text(x, y + 12, "(no image loaded)", INK_SUB, PAPER)
    end
  end

  def picture_key(sc)
    return false unless sc == FmrbConst::KEY_I
    if @image
      @gfx.delete_image(@image[:id])
      @image = nil
    else
      @image = @gfx.create_image("/data/bg_426x240.png")
      Log.warn("PicoRuby demo: create_image failed") unless @image
    end
    @needs_redraw = true
    true
  end

  # --- Section 4: sound ---------------------------------------------------
  #
  # A tune plays on the main sound chip and the effects on the sub one, so an
  # effect never cuts the music. note_on / note_off go straight to C: a note
  # allocates nothing, which is what keeps a stream of them from stuttering.

  def draw_sound_page
    x = @user_area_x0 + 6
    y = body_y
    rows = [
      "1: a scale on pulse1",
      "2: the tune on MAIN #{@tune_on ? '(playing)' : ''}",
      "3: an effect on SUB",
      "0: stop everything",
    ]
    rows.each_with_index { |t, i| @gfx.draw_text(x, y + i * 12, t, INK, PAPER) }
    @gfx.draw_text(x, y + 56, "the note off time is booked by the clock,", INK_SUB, PAPER)
    @gfx.draw_text(x, y + 68, "not counted in frames", INK_SUB, PAPER)
  end

  def sound_key(sc)
    case sc
    when FmrbConst::KEY_1
      @note_i = 0
      @scale_at = Machine.board_millis
      true
    when FmrbConst::KEY_2
      toggle_tune
      true
    when FmrbConst::KEY_3
      note(FmrbAudio::CH_PULSE2, 988, 90, 14)
      true
    when FmrbConst::KEY_0
      silence
      @audio.stop
      @tune_on = false
      @note_i = -1
      @needs_redraw = true
      true
    else
      false
    end
  end

  def toggle_tune
    if @tune_on
      @audio.stop
      @tune_on = false
    else
      @gfx.sync_file(TUNE_SRC, dest: "#{CACHE}/tune.fmsq")
      @audio.load_fmsq_file(TUNE_SLOT, "#{CACHE}/tune.fmsq")
      @audio.play_slot(TUNE_SLOT, instance: FmrbAudio::MAIN)
      @tune_on = true
    end
    @needs_redraw = true
  end

  def note(channel, freq, ms, volume = 12, duty = 2)
    @audio.note_on(channel, freq, volume, duty, 0)
    @note_off_at[channel] = Machine.board_millis + ms
  end

  def tick_scale
    return if @note_i < 0
    return if Machine.board_millis < @scale_at
    if @note_i >= SCALE.length
      @note_i = -1
      return
    end
    note(FmrbAudio::CH_PULSE1, SCALE[@note_i], 150, 12)
    @note_i += 1
    @scale_at = Machine.board_millis + 200
  end

  def tick_notes
    return if @note_off_at.empty?
    now = Machine.board_millis
    due = []
    @note_off_at.each { |ch, at| due << ch if at <= now }
    return if due.empty?
    due.each do |ch|
      @audio.note_off(ch)
      @note_off_at.delete(ch)
    end
  end

  def silence
    @note_off_at.each_key { |ch| @audio.note_off(ch) }
    @note_off_at = {}
  end

  # --- Section 5: the system side ----------------------------------------

  def draw_system_page
    x = @user_area_x0 + 6
    y = body_y
    @uptime = Machine.board_millis - @started_at
    # The rows come from picoruby_status.rb, required at the top of this file:
    # an app can be split across files, and the required one shares this
    # namespace (where a Python module would not -- see python.app.py).
    PicoRubyStatus.rows(self).each_with_index do |entry, i|
      @gfx.draw_text(x, y + i * 11, entry[0], INK_SUB, PAPER)
      @gfx.draw_text(x + 92, y + i * 11, entry[1], INK, PAPER)
    end
    @gfx.fill_circle(@user_area_x0 + uw - 10, y + 4, 3, C_GREEN) if @blink
  end

  def system_key(sc)
    case sc
    when FmrbConst::KEY_R
      # An app cannot spawn another app, so this asks the kernel to.
      request_run(PUBLISHER, @pub_pid)
      true
    when FmrbConst::KEY_P
      @sent += 1
      publish(TOPIC, { "msg" => "picoruby", "n" => @sent, "list" => [1, 2, 3] })
      @needs_redraw = true
      true
    else
      false
    end
  end

  # One-shot timers: the callback arms the next one. They run inside the wait
  # as well, which is why the dot keeps flashing while nothing else happens.
  def tick_blink
    @blink = !@blink
    set_timer(500) { tick_blink }
    @needs_redraw = true if page_key == :system
  end

  def on_control(msg)
    if msg["cmd"] == "topic_data" && msg["topic"] == TOPIC
      @received = msg["data"]
      @needs_redraw = true if page_key == :system
    elsif msg["cmd"] == "run_result"
      @pub_pid = msg["pid"]
    end
  end

  # File.binread does not exist here, so the file is read and its size taken
  # in bytes (String#length would count characters).
  def read_own_toml
    File.open(MY_TOML, "r") { |f| f.read.bytesize }
  rescue => e
    Log.warn("PicoRuby demo: #{e.message}")
    0
  end

  attr_reader :uptime, :lang, :toml_bytes, :sent, :received

  # --- Section 1: @gfx primitives ----------------------------------------

  def draw_shapes
    x0 = @user_area_x0
    y = body_y

    @gfx.draw_round_rect(x0 + 8, y, 60, 30, 6, C_TEAL)
    @gfx.fill_round_rect(x0 + 78, y, 60, 30, 6, C_GREEN)
    @gfx.draw_text(x0 + 14, y + 32, "round_rect", INK_SUB, PAPER)

    @gfx.draw_ellipse(x0 + 38, y + 64, 30, 18, C_ORANGE)
    @gfx.fill_ellipse(x0 + 108, y + 64, 30, 18, C_RED)
    @gfx.draw_text(x0 + 30, y + 86, "ellipse", INK_SUB, PAPER)

    @gfx.draw_triangle(x0 + 8, y + 134, x0 + 58, y + 99, x0 + 68, y + 134, C_PURPLE)
    @gfx.fill_triangle(x0 + 78, y + 134, x0 + 128, y + 99, x0 + 138, y + 134, C_BLUE)
    @gfx.draw_text(x0 + 20, y + 139, "triangle", INK_SUB, PAPER)

    # The panel is a light fill, so the shapes on it stay the dark tones.
    @gfx.fill_round_rect(x0 + 150, y, 80, 134, 10, C_PANEL)
    @gfx.fill_circle(x0 + 190, y + 34, 20, C_RED)
    @gfx.fill_ellipse(x0 + 190, y + 79, 30, 15, C_TEAL)
    @gfx.draw_triangle(x0 + 165, y + 134, x0 + 190, y + 104, x0 + 215, y + 134, C_ORANGE)
    @gfx.draw_text(x0 + 158, y + 139, "composed", INK_SUB, PAPER)
  end

  def draw_arcs
    x0 = @user_area_x0
    cy = body_y + body_h / 2

    @gfx.fill_arc(x0 + 70, cy, 0, 50, 0, 120, C_RED)
    @gfx.fill_arc(x0 + 70, cy, 0, 50, 120, 220, C_GREEN)
    @gfx.fill_arc(x0 + 70, cy, 0, 50, 220, 360, C_BLUE)
    @gfx.draw_text(x0 + 42, cy + 56, "pie chart", INK_SUB, PAPER)

    cx2 = x0 + 180
    @gfx.draw_arc(cx2, cy, 35, 45, 0, 360, C_PANEL)
    @gfx.fill_arc(cx2, cy, 35, 45, 270, 270 + 252, C_TEAL)  # 70%
    # The ring is hollow, so the label sits on the page and stays black.
    @gfx.draw_text(cx2 - 12, cy - 4, "70%", INK, PAPER)
    @gfx.draw_text(cx2 - 24, cy + 56, "progress", INK_SUB, PAPER)
  end

  def draw_text_size
    x0 = @user_area_x0
    y = body_y

    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, y, "Size 1: Hello!", C_TEAL, PAPER)
    y += 14
    @gfx.set_text_size(2)
    @gfx.draw_text(x0 + 8, y, "Size 2: Hi!", C_GREEN, PAPER)
    y += 24
    @gfx.set_text_size(3)
    @gfx.draw_text(x0 + 8, y, "Size 3", C_ORANGE, PAPER)
    y += 34
    @gfx.set_text_size(4)
    @gfx.draw_text(x0 + 8, y, "Sz 4", C_RED, PAPER)
    @gfx.set_text_size(1)
  end

  # --- Section 2: fonts --------------------------------------------------

  def draw_font_ascii
    x0 = @user_area_x0
    y = body_y
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, y, "ASCII only (Font0 6x8)", C_TEAL, PAPER)
    @gfx.draw_text(x0 + 8, y + 16, "Hello, Family mruby!", C_ORANGE, PAPER)
    @gfx.draw_text(x0 + 8, y + 32, "0123456789 !?@\#$%", C_GREEN, PAPER)
    @gfx.draw_text(x0 + 8, y + 48, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", INK, PAPER)
    @gfx.draw_text(x0 + 8, y + 64, "abcdefghijklmnopqrstuvwxyz", INK, PAPER)
  end

  def draw_font_ja
    x0 = @user_area_x0
    y = body_y
    @gfx.set_text_size(1)
    @gfx.set_font(:ja, 8)
    @gfx.draw_text(x0 + 8, y, "8px (misaki): こんにちは、世界！", INK, PAPER)
    @gfx.draw_text(x0 + 8, y + 12, "日本語ひらがなカタカナ漢字", C_TEAL, PAPER)
    @gfx.draw_text(x0 + 8, y + 24, "1234567890!\"@*,./_-~=(){}[]", INK_SUB, PAPER)
    @gfx.set_font(:ja, 12)
    @gfx.draw_text(x0 + 8, y + 44, "12px (efont): こんにちは、世界！", INK, PAPER)
    @gfx.draw_text(x0 + 8, y + 60, "日本語ひらがなカタカナ漢字", C_RED, PAPER)
    @gfx.draw_text(x0 + 8, y + 76, "abcdefghijklmnopqrstuvwxyz", INK_SUB, PAPER)
  end

  def draw_font_mixed
    x0 = @user_area_x0
    y = body_y
    @gfx.set_font(:ja, 8)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, y, "One Japanese font draws both:", INK_SUB, PAPER)
    @gfx.draw_text(x0 + 8, y + 18, "Score: 1234 点", INK, PAPER)
    @gfx.draw_text(x0 + 8, y + 32, "ABC あいう 123", C_GREEN, PAPER)
    @gfx.draw_text(x0 + 8, y + 46, "残り時間: 60秒", C_RED, PAPER)
  end

  # draw_text(..., mixed: true) keeps ASCII on the 6x8 Font0 and falls back
  # to misaki_8 for multi byte UTF-8 inside the same call.
  def draw_font_hybrid
    x0 = @user_area_x0
    y = body_y
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, y, "ASCII -> Font0, UTF-8 -> misaki_8", INK_SUB, PAPER)
    @gfx.draw_text(x0 + 8, y + 18, "Score: 1234 点", INK, PAPER, mixed: true)
    @gfx.draw_text(x0 + 8, y + 32, "残り時間 60秒", C_ORANGE, PAPER, mixed: true)
    @gfx.draw_text(x0 + 8, y + 46, "puts 'こんにちは'", C_GREEN, PAPER, mixed: true)
    @gfx.draw_text(x0 + 8, y + 62, "ABCdef基本的な漢字ＡＢＣ", C_TEAL, PAPER, mixed: true)
  end

  def draw_font_scaled
    x0 = @user_area_x0
    y = body_y
    @gfx.set_font(:ja, 8)
    @gfx.set_text_size(1)
    @gfx.draw_text(x0 + 8, y, "8px x1: あいう漢字", C_TEAL, PAPER)
    @gfx.set_font(:ja, 12)
    @gfx.draw_text(x0 + 8, y + 18, "12px x1: あいう漢字", C_ORANGE, PAPER)
    @gfx.set_font(:ja, 8)
    @gfx.set_text_size(2)
    @gfx.draw_text(x0 + 8, y + 40, "8px x2: あ", C_GREEN, PAPER)
    @gfx.set_text_size(1)
  end

  # --- Section 3: the P5 wrapper -----------------------------------------

  # P5 pages work in their own coordinate space: the origin is the top left
  # of the picture area, so uw / ph below are what they may fill.
  def draw_p5_page
    @p5.reset_matrix
    @p5.translate(@user_area_x0, body_y)
    @p5.no_stroke
    @p5.text_font(:default)
    @p5.text_color(INK)
    @p5.text_align(:left, :top)

    case page_key
    when :p5_basics    then draw_p5_basics
    when :p5_transform then draw_p5_transform
    when :p5_bezier    then draw_p5_bezier
    when :p5_text      then draw_p5_text
    when :p5_arc       then draw_p5_arc
    when :p5_blend     then draw_p5_blend
    when :p5_pixel     then draw_p5_pixel
    when :p5_mask      then draw_p5_mask
    end

    @p5.reset_matrix
  end

  def ph; body_h; end

  def draw_p5_basics
    @p5.fill(C_RED); @p5.stroke(INK); @p5.stroke_weight(1)
    @p5.rect(10, 6, 60, 40)
    @p5.fill(C_ORANGE); @p5.no_stroke
    @p5.circle(120, 26, 18)
    @p5.no_fill; @p5.stroke(C_TEAL); @p5.stroke_weight(2)
    @p5.ellipse(200, 26, 36, 18)
    @p5.fill(C_GREEN); @p5.stroke(INK); @p5.stroke_weight(1)
    @p5.triangle(20, ph - 20, 80, ph - 50, 60, ph - 4)
    @p5.stroke(C_PURPLE); @p5.stroke_weight(3)
    @p5.line(100, ph - 50, uw - 50, ph - 30)
    @p5.no_stroke; @p5.fill(C_BLUE)
    i = 0
    while i < 10
      @p5.point(uw - 30 + i, ph - 70 + i * 2)
      i += 1
    end
  end

  def draw_p5_transform
    @p5.push_matrix
    @p5.translate(uw / 2, ph / 2)
    @p5.rotate(@t)
    @p5.fill(C_TEAL); @p5.stroke(INK); @p5.stroke_weight(1)
    @p5.rect(-30, -20, 60, 40)
    @p5.pop_matrix

    @p5.push_matrix
    @p5.translate(60, ph / 2)
    @p5.scale(1.0 + Math.sin(@t) * 0.4, 1.0)
    @p5.fill(C_ORANGE); @p5.no_stroke
    @p5.circle(0, 0, 24)
    @p5.pop_matrix
  end

  def draw_p5_bezier
    @p5.no_fill
    @p5.stroke(C_GREEN); @p5.stroke_weight(2)
    @p5.bezier(10, ph - 10, 80, 20, uw - 100, ph - 4, uw - 20, 40)
    @p5.stroke(C_TEAL); @p5.stroke_weight(1)
    @p5.curve(10, 80, 60, 45, uw - 100, 70, uw - 20, 30)
  end

  def draw_p5_text
    @p5.fill(INK)
    @p5.text_align(:left, :top)
    @p5.text("left/top", 10, 14)
    @p5.text_align(:center, :center)
    @p5.text("center", uw / 2, ph / 2)
    @p5.text_align(:right, :bottom)
    @p5.text("right/bottom", uw - 4, ph - 4)
    @p5.text_font(:ja, 12)
    @p5.text_align(:left, :top)
    @p5.text("日本語", 10, ph - 30)
  end

  def draw_p5_arc
    cy = ph / 2
    @p5.fill(C_ORANGE); @p5.stroke(INK); @p5.stroke_weight(1)
    @p5.arc(60, cy, 40, 0, Math::PI)
    @p5.fill(C_PURPLE); @p5.no_stroke
    @p5.arc(uw / 2, cy, 40, -Math::PI / 4, Math::PI / 2)
    @p5.no_fill; @p5.stroke(C_TEAL)
    @p5.arc(uw - 60, cy, 30, 0, Math::PI * 1.5)
  end

  # ADD saturates against a light background, so this page keeps a black
  # stage to add onto -- the rest of the demo stays on the white page.
  def draw_p5_blend
    @p5.fill(P5::BLACK); @p5.no_stroke
    @p5.rect(10, 10, uw - 20, ph - 34)
    @p5.fill(P5::RED)
    @p5.rect(30, 30, 110, 80)
    @p5.blend_mode(P5::ADD)
    @p5.fill(P5::BLUE)
    @p5.rect(90, 55, 110, 80)
    @p5.blend_mode(P5::REPLACE)
    @p5.fill(INK)
    @p5.text("ADD blend (rect only, on a black stage)", 10, ph - 12)
  end

  # Draws three swatches, then reads their centre pixels back. get_pixel is
  # a sync command and flushes the queued draws on its way down, so no
  # explicit present is needed before the readback.
  def draw_p5_pixel
    swatches = [
      [C_RED,   "red    0xC0", 10],
      [C_GREEN, "green  0x0C", 50],
      [C_TEAL,  "teal   0x0F", 90],
    ]
    swatches.each do |sw|
      @p5.fill(sw[0]); @p5.no_stroke
      @p5.rect(30, sw[2], 60, 30)
    end
    @p5.fill(INK)
    @p5.text_font(:default)
    @p5.text_align(:left, :top)
    swatches.each do |sw|
      value = @gfx.get_pixel(@user_area_x0 + 60, body_y + sw[2] + 15)
      @p5.text(sprintf("%s read=0x%02X", sw[1], value), 110, sw[2] + 11)
    end
  end

  # The sprite and the masks are built once and kept: rebuilding a mask per
  # frame works but uploads it in small chunks every time.
  def draw_p5_mask
    @masked_sprite ||= build_masked_sprite
    @masks ||= {
      circle48: @gfx.create_mask(48, 48, circle_mask(48)),
      star48:   @gfx.create_mask(48, 48, star_mask(48)),
      circle32: @gfx.create_mask(32, 32, circle_mask(32)),
    }

    band = (ph - 14) / 3
    @p5.no_stroke
    @p5.fill(BAND1); @p5.rect(0, 14, uw, band)
    @p5.fill(BAND2); @p5.rect(0, 14 + band, uw, band)
    @p5.fill(BAND3); @p5.rect(0, 14 + 2 * band, uw, band + 4)

    @p5.fill(INK)
    @p5.text_align(:left, :top)
    @p5.text("SpriteImage + 1bpp mask (cached)", 4, 2)

    sid = @masked_sprite.id
    @gfx.draw_image_masked(sid, @masks[:circle48], x: @user_area_x0 + 20, y: body_y + 30)
    @gfx.draw_image_masked(sid, @masks[:star48],   x: @user_area_x0 + 90, y: body_y + 30)
    @gfx.draw_image_masked(sid, @masks[:circle32], x: @user_area_x0 + 170, y: body_y + 40)
  end

  # 1bpp circular mask, MSB first per byte, 1 bit = pixel drawn. while loops
  # because Integer#times block calls are heavy in picoruby and this runs
  # over thousands of pixels.
  def circle_mask(size)
    row_bytes = (size + 7) / 8
    buf = "\x00" * (row_bytes * size)
    r = size / 2
    r2 = r * r
    y = 0
    while y < size
      dy = y - r
      row_off = y * row_bytes
      x = 0
      while x < size
        dx = x - r
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

  # Five pointed star, from a radius against angle test.
  def star_mask(size)
    row_bytes = (size + 7) / 8
    buf = "\x00" * (row_bytes * size)
    c = size / 2
    rmax = size / 2 - 1
    y = 0
    while y < size
      dy = y - c
      row_off = y * row_bytes
      x = 0
      while x < size
        dx = x - c
        d = Math.sqrt(dx * dx + dy * dy)
        if d <= rmax
          limit = rmax * (0.55 + 0.45 * Math.cos(5 * Math.atan2(dy, dx)))
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

  # 48x48 RGB332 gradient, so the mask cutout is easy to see.
  def build_masked_sprite
    sprite = SpriteImage.new(@gfx, width: 48, height: 48)
    sprite.draw do |g|
      g.fill_rect(0, 0, 48, 48, FmrbGfx::WHITE)
      y = 0
      while y < 48
        col_r = (y * 7 / 47) << 5
        x = 0
        while x < 48
          g.set_pixel(x, y, col_r | ((x * 7 / 47) << 2) | 0x02)
          x += 1
        end
        y += 1
      end
    end
    sprite
  end
end

begin
  app = PicoRubyDemoApp.new
  app.start
rescue => e
  Log.error("PicoRuby demo: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
