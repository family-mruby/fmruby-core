# Graphics demo - one tour of the drawing APIs.
#
# Three sections, each a few pages:
#   Shapes  @gfx primitives (rect / ellipse / triangle / arc / text size)
#   Fonts   @gfx text: the built in 6x8 font, the Japanese fonts, and the
#           mixed / hybrid modes that put both in one call
#   P5      the P5 wrapper layer (doc/p5.md): matrix, bezier, alignment,
#           blend, pixel readback, masked images
#
# Click the picture for the next page (it walks every page in order), or use
# the nav bar at the bottom: < > step a page, the tabs jump to a section.
# The arrow keys and Tab do the same from the keyboard.
#
# The page is white and the palette is chosen for contrast against it
# (see the colour constants below); only the ADD blend page keeps a black
# stage, because adding light to white saturates.
#
# Replaces the separate shapes / ja_text / p5_test demos.

class GraphicsDemoApp < FmrbApp
  SECTION_NAMES = ["Shapes", "Fonts", "P5"]
  SECTION_PAGES = [
    [:shapes, :arcs, :text_size],
    [:font_ascii, :font_ja, :font_mixed, :font_hybrid, :font_scaled],
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
    Log.info("Graphics demo started")
  end

  def on_update
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
      case ev[:scancode]
      when FmrbConst::KEY_RIGHT then _step_page(1)
      when FmrbConst::KEY_LEFT  then _step_page(-1)
      when FmrbConst::KEY_TAB   then _select_section((@section + 1) % SECTION_NAMES.size)
      end
    end
  end

  def on_destroy
    if @gfx
      @gfx.set_text_size(1)
      @gfx.set_font(:default)
    end
    Log.info("Graphics demo destroyed")
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
    else draw_p5_page
    end
  end

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
  app = GraphicsDemoApp.new
  app.start
rescue => e
  Log.error("Graphics: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
