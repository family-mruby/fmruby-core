# PicoRabbit renderer for FmrbGfx
# Full-featured rendering with themes, inline formatting, syntax highlight, timer

module PicoRabbit
  # Theme definition
  class Theme
    attr_reader :name
    attr_accessor :bg, :text, :title, :title_bg, :bullet, :code_bg, :code_text,
                  :quote_bar, :quote_text, :footer_text, :footer_bg,
                  :inline_code_bg, :inline_code_text

    def initialize(name)
      @name = name
    end

    # The default theme follows the system theme for every colour that is
    # defined against the background: the ground itself, the body text, and
    # the quiet text that sits on it (footer, blockquote). The rest are
    # presentation accents and are fixed. A deck that wants the old black
    # ground asks for `theme: dark` in its frontmatter.
    def self.default
      t = Theme.new("default")
      t.bg = ::FmrbConst::THEME_WINDOW_BG
      t.text = ::FmrbConst::THEME_TEXT
      t.title = 0xFC           # Yellow
      t.title_bg = 0x03        # Dark blue
      t.bullet = 0x1C          # Green
      t.code_bg = 0x24         # Dark gray
      t.code_text = 0xFF       # White
      t.quote_bar = 0x60       # Gray
      t.quote_text = ::FmrbConst::THEME_BORDER
      t.footer_text = ::FmrbConst::THEME_BORDER
      t.footer_bg = ::FmrbConst::THEME_WINDOW_BG
      t.inline_code_bg = 0x24  # Dark gray
      t.inline_code_text = 0xFC # Yellow
      t
    end

    # The palette PicoRabbit shipped with before the default followed the
    # system theme. Kept so a talk on a projector can go back to a black
    # ground with one frontmatter line.
    def self.dark
      t = Theme.new("dark")
      t.bg = 0x00              # Black
      t.text = 0xFF            # White
      t.title = 0xFC           # Yellow
      t.title_bg = 0x03        # Dark blue
      t.bullet = 0x1C          # Green
      t.code_bg = 0x24         # Dark gray
      t.code_text = 0xFF       # White
      t.quote_bar = 0x60       # Gray
      t.quote_text = 0xDB      # Light gray
      t.footer_text = 0x6D     # Gray
      t.footer_bg = 0x00       # Black
      t.inline_code_bg = 0x24  # Dark gray
      t.inline_code_text = 0xFC # Yellow
      t
    end

    def self.light
      t = Theme.new("light")
      t.bg = 0xFF
      t.text = 0x00
      t.title = 0x00
      t.title_bg = 0xDB
      t.bullet = 0x1C
      t.code_bg = 0xDB
      t.code_text = 0x00
      t.quote_bar = 0x6D
      t.quote_text = 0x49
      t.footer_text = 0x6D
      t.footer_bg = 0xFF
      t.inline_code_bg = 0xDB
      t.inline_code_text = 0xE0
      t
    end

    def self.for_name(name)
      case name
      when "light" then light
      when "dark" then dark
      else default
      end
    end
  end

  # Syntax highlight colors (same as Editor)
  HL_COLORS = [
    0xFF,  # 0: default    - white
    0xE0,  # 1: keyword    - red
    0x1C,  # 2: string     - green
    0x92,  # 3: comment    - gray
    0xFC,  # 4: number     - yellow
    0xE3,  # 5: symbol     - magenta
    0x1F,  # 6: constant   - cyan
    0xFE,  # 7: variable   - light pink
    0x9F,  # 8: method     - light blue
  ]

  class FmrbRenderer
    # Metrics of the size 1 cell. Everything the body draws is a multiple of
    # these, scaled by the deck's text_size; the footer and the runners stay
    # at size 1 whatever the body does.
    CHAR_W = 6     # Font size 1 character width
    CHAR_H = 8     # Font size 1 character height
    LINE_H = 10    # Line height with spacing
    MARGIN_X = 4
    # The bottom strip: one sprite tall. The runners stand in it with the
    # start and goal flags, and the page number and clock sit at its right
    # end. No rule above it and no track line under the runners -- the strip
    # is told apart from the body by the runners alone.
    FOOTER_H = 16
    FLAG_H = 12        # pole height; the cloth hangs from its top
    FLAG_W = 6
    FLAG_START = 0xE0  # red, as upstream's start flag
    FLAG_GOAL = 0x03   # blue, as upstream's goal flag
    RUBY_H = 8     # a ruby line is one size 1 cell above its base
    MAX_TEXT_SIZE = 3
    CLOCK_CHARS = 7    # "||-MM:SS" at its longest, right-aligned in its box
    INDEX_ROWS = 20    # rows per column in the index; past that it goes to two

    SPRITE_SIZE = 16
    SPRITE_DIR = "/usr/share/picorabbit"
    SPRITE_CACHE = "/cache/app/picorabbit"

    # Frame order inside each SpriteInstance's image list.
    USAGI_FILES = ["usagi_run1.bmp", "usagi_run2.bmp", "usagi_jump.bmp",
                   "usagi_sleep.bmp", "usagi_hurry.bmp"]
    KAME_FILES = ["kame_walk1.bmp", "kame_walk2.bmp", "kame_banzai.bmp"]
    U_RUN1 = 0
    U_RUN2 = 1
    U_JUMP = 2
    U_SLEEP = 3
    U_HURRY = 4
    K_WALK1 = 0
    K_WALK2 = 1
    K_BANZAI = 2

    RUN_MS = 3000     # the rabbit keeps running this long after a page turn
    LEAD = 0.10       # how far ahead/behind before it dozes or hurries

    def initialize(gfx, width, height, metadata = {})
      @gfx = gfx
      @w = width
      @h = height
      @theme = Theme.for_name(metadata["theme"])
      @allotted_ms = parse_allotted_ms(metadata["allotted_time"])
      # Body size from the frontmatter (1 or 2; anything else is 1). Headings
      # go one step above the body, capped, so a size 2 body still has a
      # heading that stands out.
      @ts = metadata["text_size"] == "2" ? 2 : 1
      @hs = @ts + 1
      @hs = MAX_TEXT_SIZE if @hs > MAX_TEXT_SIZE
      @char_w = CHAR_W * @ts
      @char_h = CHAR_H * @ts
      @line_h = LINE_H * @ts
      @head_h = CHAR_H * @hs
      @title_bar_h = @head_h + 4
      # What the canvas is currently scaled to. A layout pass measures at 1
      # and scales by hand, because one line can carry base text at the body
      # size and ruby at size 1.
      @cur_ts = nil
      @start_ms = Machine.board_millis
      @usagi_jump_y = 0
      @usagi_vy = 0
      @last_turn_ms = @start_ms
      # The runners' strip. A sprite's own ground row is 14
      # (tool/gen_picorabbit_sprites.rb), so drawn at @sprite_y = @h - 16 its
      # feet land one row above the bottom edge, level with the flag poles.
      # The track runs pole to pole: from the start flag at the left margin
      # to the goal flag just short of the clock and page number, whose
      # x is fixed in render_footer from the widest the number can get.
      @sprite_y = @h - FOOTER_H
      @track_left = MARGIN_X + 2
      widest = CHAR_W * 2  # placeholder until render_footer measures
      @clock_x = @w - MARGIN_X - widest - 4 - CLOCK_CHARS * CHAR_W
      @track_w = @clock_x - 6 - @track_left
      @usagi = nil
      @kame = nil
      @usagi_x = nil
      @usagi_y = nil
      @usagi_frame = nil
      @kame_x = nil
      @kame_frame = nil
      @paused_ms = nil
      @clock_shown = false
      @clock_hidden = false
      @clock_text = nil
      @clock_negative = false
      @clock_x = @w
      @time_up_fired = false
      @goal_idx = nil
    end

    # Which slide is the finish line. nil means the last one.
    def goal_index=(idx)
      @goal_idx = idx
    end

    # ---- the clock -------------------------------------------------------

    def timer_paused?
      @paused_ms != nil
    end

    def pause_timer
      @paused_ms = Machine.board_millis unless @paused_ms
    end

    # Give back the time that stood still, so the turtle carries on from
    # where it stopped rather than jumping forward.
    def resume_timer
      return unless @paused_ms
      @start_ms += Machine.board_millis - @paused_ms
      @paused_ms = nil
    end

    def clock_visible=(flag)
      @clock_shown = flag
    end

    # Keep the clock off the slide whatever the presenter set, for the one
    # case where the slide is not being presented but photographed: an
    # exported picture should not carry the time left in somebody's talk.
    def clock_hidden=(flag)
      @clock_hidden = flag
    end

    # A paused clock always shows itself: a presenter has to be able to see
    # that it is stopped.
    def clock_visible?
      return false if @clock_hidden
      @clock_shown || timer_paused?
    end

    # True exactly once each time the clock runs out. reset_timer re-arms it.
    def take_time_up
      return false unless @allotted_ms
      unless turtle_progress >= 1.0
        @time_up_fired = false
        return false
      end
      return false if @time_up_fired
      @time_up_fired = true
      true
    end

    # ---- index ----------------------------------------------------------

    # The whole deck as a list of headings, one line each, at size 1. The
    # selected row is a bar; the slide the talk is actually on is marked with
    # a caret, so the two are told apart while the selection moves.
    def render_index(slides, current, selected)
      @gfx.clear(@theme.bg)
      @gfx.fill_rect(0, 0, @w, CHAR_H + 4, @theme.title_bg)
      set_ts(1)
      @gfx.draw_text(MARGIN_X, 2, "Index", @theme.title, @theme.title_bg)

      # Where the talk is and how much time is left, in the header: the index
      # is opened to decide where to jump given the time remaining, and that
      # decision should not need a second screen. Same reading as the footer
      # clock, red once it is over.
      pos = "#{current + 1}/#{slides.length}"
      px = @w - MARGIN_X - pos.length * CHAR_W
      @gfx.draw_text(px, 2, pos, @theme.title, @theme.title_bg)
      if has_timer?
        left = clock_text
        colour = @clock_negative ? 0xE0 : @theme.title
        @gfx.draw_text(px - 4 - left.length * CHAR_W, 2, left, colour,
                       @theme.title_bg)
      end

      n = slides.length
      n = INDEX_ROWS * 2 if n > INDEX_ROWS * 2
      cols = n > INDEX_ROWS ? 2 : 1
      col_w = (@w - MARGIN_X * 2) / cols
      i = 0
      while i < n
        x = MARGIN_X + (i / INDEX_ROWS) * col_w
        y = index_row_y(i)
        if i == selected
          @gfx.fill_rect(x - 1, y - 1, col_w - 2, CHAR_H + 2, @theme.title_bg)
        end
        fg = i == selected ? @theme.title : @theme.text
        bg = i == selected ? @theme.title_bg : @theme.bg
        mark = i == current ? ">" : " "
        @gfx.draw_text_mixed(x, y, mark + index_label(i, slides[i], col_w - 14),
                             fg, bg)
        i += 1
      end
      @gfx.present
    end

    # Top of row i, so a tap can be turned back into a row.
    def index_row_y(i)
      CHAR_H + 8 + (i % INDEX_ROWS) * LINE_H
    end

    # Which row a tap at (x, y) landed on, or nil.
    def index_hit(x, y, count)
      n = count > INDEX_ROWS * 2 ? INDEX_ROWS * 2 : count
      cols = n > INDEX_ROWS ? 2 : 1
      col_w = (@w - MARGIN_X * 2) / cols
      col = (x - MARGIN_X) / col_w
      col = 0 if col < 0
      return nil if col >= cols
      row = (y - (CHAR_H + 8) + 1) / LINE_H
      return nil if row < 0 || row >= INDEX_ROWS
      i = col * INDEX_ROWS + row
      i < n ? i : nil
    end

    # Repaint just the box the remaining time sits in, and only when the text
    # changed. Returns true when it drew, so the caller can fold it into the
    # present the runners already asked for.
    def draw_clock
      text = clock_visible? ? clock_text : ""
      return false if text == @clock_text
      @clock_text = text
      ty = @h - FOOTER_H + 4
      @gfx.fill_rect(@clock_x, ty, CLOCK_CHARS * CHAR_W, CHAR_H,
                     @theme.footer_bg)
      return true if text.length == 0
      set_ts(1)
      colour = @clock_negative ? 0xE0 : @theme.footer_text
      x = @clock_x + (CLOCK_CHARS - text.length) * CHAR_W
      @gfx.draw_text(x, ty, text, colour, @theme.footer_bg)
      true
    end

    def has_timer?
      @allotted_ms != nil
    end

    def rabbit_jumping?
      @usagi_jump_y != 0 || @usagi_vy != 0
    end

    def render_slide(slide, step, slide_idx, total_slides)
      @current_slide_idx = slide_idx
      @last_slide = slide
      @last_turn_ms = Machine.board_millis
      @gfx.clear(@theme.bg)

      if slide.title_slide
        render_title_slide(slide)
      else
        render_content_slide(slide, step)
      end

      # Footer: page number, clock box, and the two flags. The runners are
      # sprites composited on top, so the flags are the only part of the
      # race the canvas carries.
      render_footer(slide, slide_idx, total_slides)
      render_track

      # Place the runners before presenting: the page turn keeps its single
      # present rather than growing a second one for the sprites.
      update_sprites(slide_idx, total_slides)

      @gfx.present
    end

    # Load the eight frames and put the two runners on the start line. Called
    # once, after the presentation is parsed. A deck with no allotted time has
    # no race, so it loads nothing.
    def load_sprites
      return false unless has_timer?
      @usagi_imgs = load_sprite_images(USAGI_FILES)
      @kame_imgs = load_sprite_images(KAME_FILES)
      @kame = ::SpriteInstance.new(@gfx, @kame_imgs,
                                   x: sprite_x(0.0), y: @sprite_y, z: 2)
      @usagi = ::SpriteInstance.new(@gfx, @usagi_imgs,
                                    x: sprite_x(0.0), y: @sprite_y, z: 3)
      ::Log.info("PicoRabbit: #{USAGI_FILES.length + KAME_FILES.length} sprite frames loaded")
      true
    end

    def destroy_sprites
      @usagi.destroy if @usagi
      @kame.destroy if @kame
      @usagi = nil
      @kame = nil
      destroy_sprite_images(@usagi_imgs)
      destroy_sprite_images(@kame_imgs)
      @usagi_imgs = nil
      @kame_imgs = nil
    end

    # Ctrl+Tab parks the presentation. The canvas is hidden by the suspend,
    # but a sprite is composited in its own layer, so it has to be told.
    def sprites_visible=(flag)
      @usagi.visible = flag if @usagi
      @kame.visible = flag if @kame
    end

    # Move the runners to where the page and the clock say they are, and pick
    # the frame each of them should be showing. Returns true only when
    # something actually changed, which is the only time the caller presents.
    def update_sprites(slide_idx, total_slides)
      return false unless @usagi && @kame
      now = Machine.board_millis

      turtle_p = turtle_progress
      kame_x = sprite_x(turtle_p)
      kame_frame = turtle_frame(turtle_p)

      rabbit_p = rabbit_progress(slide_idx, total_slides)
      usagi_x = sprite_x(rabbit_p)
      usagi_y = @sprite_y + @usagi_jump_y
      usagi_frame = rabbit_frame(now, rabbit_p, turtle_p)

      changed = false
      if kame_x != @kame_x
        @kame.move(kame_x, @sprite_y)
        @kame_x = kame_x
        changed = true
      end
      if kame_frame != @kame_frame
        @kame.frame = kame_frame
        @kame_frame = kame_frame
        changed = true
      end
      if usagi_x != @usagi_x || usagi_y != @usagi_y
        @usagi.move(usagi_x, usagi_y)
        @usagi_x = usagi_x
        @usagi_y = usagi_y
        changed = true
      end
      if usagi_frame != @usagi_frame
        @usagi.frame = usagi_frame
        @usagi_frame = usagi_frame
        changed = true
      end
      changed
    end

    def jump_rabbit
      @usagi_vy = -5 if @usagi_jump_y == 0
    end

    # Put the turtle back on the start line (Alt+t upstream, plain "t" here).
    # A stopped clock starts again: the presenter asked for a fresh run.
    def reset_timer
      @start_ms = Machine.board_millis
      @paused_ms = nil
      @time_up_fired = false
    end

    def update_rabbit
      if @usagi_jump_y != 0 || @usagi_vy != 0
        @usagi_jump_y += @usagi_vy
        @usagi_vy += 1  # Gravity
        if @usagi_jump_y >= 0
          @usagi_jump_y = 0
          @usagi_vy = 0
        end
      end
    end

    # Compile every fmrb_code block up front. Call this right after loading a
    # presentation: eval runs the mruby compiler (deep C recursion) on the app
    # task's stack, and the call depth here is far shallower than in the
    # middle of a render. A block that fails to compile is left nil; the
    # render path retries it and draws the error on the slide.
    def precompile(slides)
      slides.each do |slide|
        slide.elements.each do |e|
          next unless e.type == :fmrb_code && e.text.is_a?(Array)
          next if e.compiled_proc
          begin
            e.compiled_proc = compile_fmrb_code(e)
          rescue => err
            ::Log.warn("fmrb_code precompile failed: #{err.message}")
          end
        end
      end
    end

    private

    # Compile one fmrb_code block into a Proc. The code addresses the canvas
    # through the $fmrb_* globals, so wrapping it in a Proc changes nothing
    # about how it runs -- only when it is compiled.
    def compile_fmrb_code(elem)
      eval("::Proc.new do\n" + elem.text.join("\n") + "\nend")
    end

    def render_title_slide(slide)
      lines = slide.title ? slide.title.split("\n") : [""]
      sub_lines = []
      slide.elements.each do |e|
        sub_lines << e.text if e.type == :text && e.text
      end

      total_h = lines.length * (@head_h + 4) + sub_lines.length * @line_h + 8

      y = (@h - total_h) / 2

      # The title sits on a band of title_bg, as the heading of a content
      # slide does: the default theme's title colour is meant for that band
      # and is unreadable straight on the page background (yellow on white
      # on a Tab5). The band spans the width and the title lines' height.
      band_h = lines.length * (@head_h + 4)
      @gfx.fill_rect(0, y - 2, @w, band_h + 4, @theme.title_bg)

      i = 0
      while i < lines.length
        line = lines[i]
        x = (@w - w1(line) * @hs) / 2
        set_ts(@hs)
        @gfx.draw_text_mixed(x, y, line, @theme.title, @theme.title_bg)
        y += @head_h + 4
        i += 1
      end

      y += 8
      i = 0
      while i < sub_lines.length
        line = sub_lines[i]
        x = (@w - w1(line) * @ts) / 2
        set_ts(@ts)
        @gfx.draw_text_mixed(x, y, line, @theme.text, @theme.bg)
        y += @line_h
        i += 1
      end
    end

    def render_content_slide(slide, step)
      # Title bar
      @gfx.fill_rect(0, 0, @w, @title_bar_h, @theme.title_bg)
      title = truncate_to_width(slide.title || "", @w - MARGIN_X * 2, @hs)
      set_ts(@hs)
      @gfx.draw_text_mixed(MARGIN_X, 2, title, @theme.title, @theme.title_bg)

      y = @title_bar_h + 4
      content_x = MARGIN_X
      content_w = @w - MARGIN_X * 2
      max_y = @h - FOOTER_H - 4
      wait_seen = 0

      slide.elements.each do |elem|
        if elem.type == :wait
          wait_seen += 1
          break if step && wait_seen > step
          next
        end

        break if y + @line_h > max_y

        case elem.type
        when :text
          y = draw_rich_text(content_x, y, elem.text, content_w, elem.align)

        when :bullet
          indent = content_x + elem.level * @char_w * 2
          marker = elem.level == 0 ? "- " : "  - "
          y = draw_list_item(indent, y, marker, elem.text,
                             content_x + content_w)

        when :numbered
          indent = content_x + elem.level * @char_w * 2
          # The author's own number, so a list that restarts or repeats one
          # reads as written.
          marker = "#{elem.number || 1}. "
          y = draw_list_item(indent, y, marker, elem.text,
                             content_x + content_w)

        when :code_block
          if elem.text.is_a?(Array)
            code_str = elem.text.join("\n")
            block_h = elem.text.length * @line_h + 4
            @gfx.fill_rect(content_x, y, content_w, block_h, @theme.code_bg)
            set_ts(@ts)

            # Try syntax highlighting
            begin
              hl_map = SyntaxHighlight.tokenize(code_str)
              cy = y + 2
              offset = 0
              elem.text.each do |line|
                draw_highlighted_line(content_x + 2, cy, line, hl_map, offset)
                offset += line.length + 1  # +1 for newline
                cy += @line_h
              end
            rescue
              # Fallback: plain text
              cy = y + 2
              elem.text.each do |line|
                max_c = (content_w - 4) / @char_w
                code_line = line.length > max_c ? line[0, max_c] : line
                @gfx.draw_text(content_x + 2, cy, code_line, @theme.code_text, @theme.code_bg)
                cy += @line_h
              end
            end
            y += block_h
          end

        when :fmrb_code
          if elem.text.is_a?(Array)
            begin
              $fmrb_gfx = @gfx
              $fmrb_x = content_x
              $fmrb_y = y
              $fmrb_w = content_w
              $fmrb_theme = @theme
              elem.compiled_proc ||= compile_fmrb_code(elem)
              # Blocks are written against the size 1 cell (they place text by
              # multiplying out 6px columns), so they get the canvas at 1
              # whatever the deck's body size is.
              set_ts(1)
              elem.compiled_proc.call
              y = $fmrb_y if $fmrb_y > y
            rescue => e
              @gfx.draw_text(content_x, y, "[fmrb_code error: #{e.message}]", 0xE0, @theme.bg)
              y += @line_h
            end
            # The block draws with the raw canvas and may have changed the
            # text size behind our back.
            @cur_ts = nil
          end

        when :blockquote
          qy = y
          y = draw_rich_text(content_x + 6, y, elem.text || "",
                             content_w - 6, nil, @theme.quote_text)
          @gfx.fill_rect(content_x, qy, 2, y - qy - (@line_h - @char_h), @theme.quote_bar)

        when :image
          begin
            path = elem.text
            path = path.sub(".bmp", ".png") if path && path.end_with?(".bmp")
            img = @gfx.create_image(path)
            if img
              ix = calc_align_x_px(elem, img[:width], content_x, content_w)
              @gfx.draw_image(img[:id], x: ix, y: y)
              y += img[:height] + 2
              @gfx.delete_image(img[:id])
            end
          rescue
            set_ts(@ts)
            @gfx.draw_text(content_x, y, "[img: #{elem.text}]", @theme.footer_text, @theme.bg)
            y += @line_h
          end

        when :blank
          y += @line_h / 2
        end
      end
    end

    # One list item: its marker, then its text wrapped to what is left of the
    # line. The text is laid out before the marker is drawn because the marker
    # follows the base line, which ruby on the first line moves.
    def draw_list_item(indent, y, marker, text, right_x)
      tx = indent + marker.length * @char_w
      # Laying out measures at size 1, so the body size goes on after it and
      # not before, or the marker comes out one size too small.
      lines = layout_rich_text(parse_inline(text || ""), right_x - tx)
      set_ts(@ts)
      @gfx.draw_text(indent, base_line_y(y, lines), marker,
                     @theme.bullet, @theme.bg)
      draw_rich_lines(tx, y, lines, right_x - tx, nil, @theme.text)
    end

    # Draw text with inline formatting (**bold** and `code`), wrapping to
    # max_w. The fit is measured in pixels with text_width, not in character
    # cells, so a line of CJK (8px per glyph, drawn with misaki_8) breaks
    # where it actually runs off the slide.
    #
    # ASCII breaks between words. CJK has no spaces to break at, so it breaks
    # between glyphs; no kinsoku (see plan.md P2).
    #
    # Returns the y of the line after the last one drawn.
    def draw_rich_text(x, y, text, max_w, align, color = nil)
      return y + @line_h unless text
      draw_rich_lines(x, y, layout_rich_text(parse_inline(text), max_w),
                      max_w, align, color)
    end

    # Draw lines that are already laid out. Split from draw_rich_text for the
    # callers that have to know about the first line before they draw beside
    # it -- a list marker sits on the base line, and ruby on that line pushes
    # the base down -- so they can lay the text out once and still draw here.
    def draw_rich_lines(x, y, lines, max_w, align, color = nil)
      color = @theme.text unless color
      n = lines.length
      return y + @line_h if n == 0
      i = 0
      while i < n
        line = lines[i]
        lx = x
        if align == :center
          lx = x + (max_w - line[1]) / 2
        elsif align == :right
          lx = x + max_w - line[1]
        end
        # A line carrying ruby takes one size 1 cell of headroom above it.
        extra = line[2] ? RUBY_H : 0
        draw_rich_line(lx, y, line[0], color, line[2])
        y += @line_h + extra
        i += 1
      end
      y
    end

    # Where the base of the first line sits. A line carrying ruby draws its
    # body one size 1 cell lower to leave room for the reading, and anything
    # drawn alongside it (a bullet, a list number) has to drop with it.
    def base_line_y(y, lines)
      first = lines[0]
      (first && first[2]) ? y + RUBY_H : y
    end

    # Flow inline segments into lines that fit max_w.
    # A line is [pieces, width]; a piece is [type, str, width].
    def layout_rich_text(segments, max_w)
      lines = []
      pieces = []
      line_w = 0
      si = 0
      ns = segments.length
      while si < ns
        type = segments[si][0]
        str = segments[si][1]

        # A ruby run never breaks: it is one word as wide as the wider of the
        # base and the reading. The base draws at the body size, the reading
        # always at size 1.
        if type == :ruby
          rb = segments[si][2]
          base_w = w1(str) * @ts
          ruby_w = w1(rb)
          piece_w = base_w > ruby_w ? base_w : ruby_w
          if line_w > 0 && line_w + piece_w > max_w
            push_line(lines, pieces, line_w)
            pieces = []
            line_w = 0
          end
          pieces << [:ruby, str, piece_w, rb, base_w, ruby_w]
          line_w += piece_w
          si += 1
          next
        end

        pad = (type == :code) ? 2 : 0
        i = 0
        len = str.length
        while i < len
          j = token_end(str, i)
          token = str[i, j - i]
          tw = w1(token) * @ts
          if tw > max_w && j - i > 1
            # A word wider than the whole line (a URL, an identifier): break
            # it hard rather than run off the edge, taking as much as still
            # fits on the line in hand.
            avail = max_w - line_w
            k = 1
            while i + k < j
              break if w1(str[i, k + 1]) * @ts > avail
              k += 1
            end
            j = i + k
            token = str[i, k]
            tw = w1(token) * @ts
          end
          i = j
          if line_w > 0 && line_w + tw > max_w
            push_line(lines, pieces, line_w)
            pieces = []
            line_w = 0
            # A space that only exists to separate words is dropped at the
            # break; a CJK glyph or a word is carried to the new line.
            next if token[0] == " "
          end
          last = pieces.length > 0 ? pieces[pieces.length - 1] : nil
          if last && last[0] == type
            last[1] = last[1] + token
            last[2] = last[2] + tw
            line_w += tw
          else
            pieces << [type, token, tw + pad]
            line_w += tw + pad
          end
        end
        si += 1
      end
      push_line(lines, pieces, line_w) if pieces.length > 0
      lines
    end

    # Push one finished line, dropping the trailing spaces so a centred or
    # right-aligned line measures the same width as it draws. A line is
    # [pieces, width, has_ruby]; the flag decides the line's headroom.
    def push_line(lines, pieces, line_w)
      last = pieces[pieces.length - 1]
      if last[0] != :ruby
        str = last[1]
        n = str.length
        while n > 0 && str[n - 1] == " "
          n -= 1
        end
        if n < str.length
          cut = w1(str[n, str.length - n]) * @ts
          last[1] = str[0, n]
          last[2] = last[2] - cut
          line_w -= cut
        end
      end
      has_ruby = false
      i = 0
      while i < pieces.length
        has_ruby = true if pieces[i][0] == :ruby
        i += 1
      end
      lines << [pieces, line_w, has_ruby]
    end

    # End index of the wrap token that starts at i: a run of spaces, a single
    # multi-byte glyph, or an ASCII word.
    def token_end(str, i)
      len = str.length
      c = str[i]
      return i + 1 if c.bytesize > 1
      j = i + 1
      if c == " "
        while j < len && str[j] == " "
          j += 1
        end
        return j
      end
      while j < len
        cj = str[j]
        break if cj == " " || cj.bytesize > 1
        j += 1
      end
      j
    end

    # Two passes over the line: the base at the body size, then the readings
    # at size 1 in the headroom above. Two passes rather than two size
    # switches per piece.
    def draw_rich_line(x, y, pieces, color, has_ruby)
      base_y = has_ruby ? y + RUBY_H : y
      set_ts(@ts)
      bx = x
      i = 0
      n = pieces.length
      while i < n
        piece = pieces[i]
        type = piece[0]
        str = piece[1]
        w = piece[2]
        if type == :ruby
          # The narrower of base and reading is centred under the wider.
          @gfx.draw_text_mixed(bx + (w - piece[4]) / 2, base_y, str, color, @theme.bg)
        elsif type == :bold
          # Bold: draw twice with 1px offset
          @gfx.draw_text_mixed(bx, base_y, str, color, @theme.bg)
          @gfx.draw_text_mixed(bx + 1, base_y, str, color)
        elsif type == :code
          # Inline code: background highlight. ASCII only, so Font0 is enough.
          @gfx.fill_rect(bx, base_y, w, @char_h, @theme.inline_code_bg)
          @gfx.draw_text(bx + 1, base_y, str, @theme.inline_code_text, @theme.inline_code_bg)
        else
          @gfx.draw_text_mixed(bx, base_y, str, color, @theme.bg)
        end
        bx += w
        i += 1
      end
      return unless has_ruby

      set_ts(1)
      bx = x
      i = 0
      while i < n
        piece = pieces[i]
        if piece[0] == :ruby
          @gfx.draw_text_mixed(bx + (piece[2] - piece[5]) / 2, y, piece[3],
                               color, @theme.bg)
        end
        bx += piece[2]
        i += 1
      end
    end

    # Pixel width of str in a size 1 cell. text_width multiplies by whatever
    # the canvas is scaled to, so the canvas is held at 1 while measuring and
    # the caller scales by hand -- a line can carry two sizes at once.
    def w1(str)
      set_ts(1)
      @gfx.text_width(str, :default)
    end

    def set_ts(size)
      return if @cur_ts == size
      @gfx.set_text_size(size)
      @cur_ts = size
    end

    # Drop trailing characters until the string fits w at the current text
    # size. Titles are short, so the repeated measuring costs nothing.
    # "N. heading", cut to fit. A title slide's heading can carry a <br>, so
    # only its first line goes in the list.
    def index_label(i, slide, max_w)
      title = slide.title || ""
      nl = title.index("\n")
      title = title[0, nl] if nl
      truncate_to_width("#{i + 1}. #{title}", max_w, 1)
    end

    def truncate_to_width(str, w, size)
      return str if w1(str) * size <= w
      n = str.length
      while n > 0
        cut = str[0, n]
        return cut if w1(cut) * size <= w
        n -= 1
      end
      ""
    end

    # Parse inline formatting: **bold**, `code` and {base|reading} ruby.
    def parse_inline(text)
      result = []
      buf = ""
      i = 0
      len = text.length

      while i < len
        # Check for {base|reading}. A brace followed by a colon belongs to a
        # block directive ({::wait/}, {:.center}), which the parser has
        # already taken out of the line; anything else that does not close
        # with both a bar and a brace is ordinary text.
        if text[i] == '{' && !(i + 1 < len && text[i + 1] == ':')
          bar = nil
          close = nil
          j = i + 1
          while j < len
            c = text[j]
            if c == '|' && bar.nil?
              bar = j
            elsif c == '}'
              close = j
              break
            end
            j += 1
          end
          if bar && close && bar > i + 1 && close > bar + 1
            result << [:normal, buf] if buf.length > 0
            buf = ""
            result << [:ruby, text[i + 1, bar - i - 1],
                       text[bar + 1, close - bar - 1]]
            i = close + 1
            next
          end
        end

        # Check for **bold**
        if i + 1 < len && text[i] == '*' && text[i + 1] == '*'
          result << [:normal, buf] if buf.length > 0
          buf = ""
          i += 2
          while i < len
            if i + 1 < len && text[i] == '*' && text[i + 1] == '*'
              i += 2
              break
            end
            buf += text[i]
            i += 1
          end
          result << [:bold, buf] if buf.length > 0
          buf = ""
          next
        end

        # Check for `code`
        if text[i] == '`'
          result << [:normal, buf] if buf.length > 0
          buf = ""
          i += 1
          while i < len
            if text[i] == '`'
              i += 1
              break
            end
            buf += text[i]
            i += 1
          end
          result << [:code, buf] if buf.length > 0
          buf = ""
          next
        end

        buf += text[i]
        i += 1
      end

      result << [:normal, buf] if buf.length > 0
      result
    end

    # Draw syntax-highlighted code line
    def draw_highlighted_line(x, y, text, hl_map, offset)
      max_c = (@w - MARGIN_X * 2 - 4) / @char_w
      visible_len = text.length > max_c ? max_c : text.length
      i = 0
      while i < visible_len
        cat = hl_map.getbyte(offset + i) || 0
        color = HL_COLORS[cat] || @theme.code_text

        j = i + 1
        while j < visible_len
          next_cat = hl_map.getbyte(offset + j) || 0
          break if next_cat != cat
          j += 1
        end

        chunk = text[i, j - i]
        @gfx.draw_text(x + i * @char_w, y, chunk, color, @theme.code_bg)
        i = j
      end
    end

    # The strip carries no slide title: the heading is already at the top of
    # the same slide, and upstream's footer shows the number alone.
    def render_footer(slide, slide_idx, total_slides)
      fy = @h - FOOTER_H
      @gfx.fill_rect(0, fy, @w, FOOTER_H, @theme.footer_bg)

      # Page number at the right, centred in the strip. The clock's box is
      # measured against the widest the number can get, so neither it nor
      # the goal flag moves as the pages go by.
      num = "#{slide_idx + 1}/#{total_slides}"
      nx = @w - num.length * CHAR_W - MARGIN_X
      set_ts(1)
      @gfx.draw_text(nx, fy + 4, num, @theme.footer_text, @theme.footer_bg)

      widest = "#{total_slides}/#{total_slides}".length * CHAR_W
      @clock_x = @w - MARGIN_X - widest - 4 - CLOCK_CHARS * CHAR_W
      @track_w = @clock_x - 6 - @track_left
      @clock_text = nil
      draw_clock
    end

    # The start and goal flags, one at each end of the track. A runner at
    # progress 0 or 1 stands on the pole.
    def render_track
      return unless @allotted_ms
      draw_flag(@track_left, FLAG_START)
      draw_flag(@track_left + @track_w, FLAG_GOAL)
    end

    # A pole with the cloth to its right, feet on the same row as the runners.
    def draw_flag(x, colour)
      top = @h - 2 - FLAG_H
      @gfx.fill_rect(x, top, 1, FLAG_H, @theme.footer_text)
      @gfx.fill_rect(x + 1, top, FLAG_W, 4, colour)
    end

    def load_sprite_images(files)
      list = []
      i = 0
      while i < files.length
        name = files[i]
        @gfx.sync_file("#{SPRITE_DIR}/#{name}", dest: "#{SPRITE_CACHE}/#{name}")
        img = ::SpriteImage.new(@gfx, width: SPRITE_SIZE, height: SPRITE_SIZE,
                                transparent_color: 0, use_transparent: true)
        img.load_bmp("#{SPRITE_CACHE}/#{name}")
        list << img
        i += 1
      end
      list
    end

    def destroy_sprite_images(list)
      return unless list
      i = 0
      while i < list.length
        list[i].destroy
        i += 1
      end
      nil
    end

    # Left edge of a 16px sprite whose middle sits at the given point on the
    # track, kept inside the canvas at both ends.
    def sprite_x(progress)
      x = @track_left + (progress * @track_w).to_i - SPRITE_SIZE / 2
      return 0 if x < 0
      limit = @w - SPRITE_SIZE
      x > limit ? limit : x
    end

    # The turtle walks while there is time left and cheers when there is not.
    # Its stride is counted off the clock it carries, not off the wall, so a
    # paused clock stops its legs as well as its position.
    def turtle_frame(turtle_p)
      return K_BANZAI if turtle_p >= 1.0
      ((elapsed_ms / 1000) % 2) == 0 ? K_WALK1 : K_WALK2
    end

    # The rabbit runs for a few seconds after a page turn. After that its pose
    # says how the talk is doing against the clock: dozing when it is well
    # ahead, hurrying when it is well behind.
    def rabbit_frame(now, rabbit_p, turtle_p)
      return U_JUMP if rabbit_jumping?
      since = now - @last_turn_ms
      if since < RUN_MS
        return ((since / 500) % 2) == 0 ? U_RUN1 : U_RUN2
      end
      lead = rabbit_p - turtle_p
      return U_SLEEP if lead >= LEAD
      return U_HURRY if lead <= -LEAD
      U_RUN1
    end

    # How far the talk has come, 0..1. The title slide is not part of the
    # race: the first content slide is the start line, and the finish line is
    # the last slide unless a {::goal/} named an earlier one -- anything past
    # it (questions, an appendix) leaves the rabbit sitting at the goal.
    # A deck with fewer than three slides is always at the goal.
    def rabbit_progress(slide_idx, total_slides)
      goal = @goal_idx ? @goal_idx : total_slides - 1
      return 1.0 if goal < 2
      p = (slide_idx - 1).to_f / (goal - 1)
      return 0.0 if p < 0.0
      return 1.0 if p > 1.0
      p
    end

    # How much of the allotted time is gone, 0..1. Upstream keeps walking
    # past the goal; we stop there, because P1 has the turtle cheer instead.
    def turtle_progress
      p = elapsed_ms.to_f / @allotted_ms
      p > 1.0 ? 1.0 : p
    end

    # Time the talk has taken. A paused clock reads the moment it stopped.
    def elapsed_ms
      (@paused_ms || Machine.board_millis) - @start_ms
    end

    # "MM:SS" left, "-MM:SS" once it is over, "||" in front while stopped.
    def clock_text
      return "" unless @allotted_ms
      left = @allotted_ms - elapsed_ms
      @clock_negative = left < 0
      left = -left if @clock_negative
      secs = left / 1000
      mm = secs / 60
      ss = secs % 60
      out = +""
      out << "||" if timer_paused?
      out << "-" if @clock_negative
      out << "0" if mm < 10
      out << mm.to_s
      out << ":"
      out << "0" if ss < 10
      out << ss.to_s
      out
    end

    # Read the allotted time out of the frontmatter.
    #
    # A bare integer is minutes (the Harucom form, as in demo.md). A value
    # with units follows Rabbit upstream: "90s", "5m", "1h30m". Anything that
    # does not add up to a time at all means "no timer".
    def parse_allotted_ms(str)
      return nil unless str
      s = str.strip
      total = 0
      num = 0
      digits = 0
      units = 0
      i = 0
      len = s.length
      while i < len
        c = s[i]
        if c >= "0" && c <= "9"
          num = num * 10 + (c.ord - 48)
          digits += 1
        elsif c == "h" || c == "m" || c == "s"
          return nil if digits == 0
          total += num * (c == "h" ? 3600 : (c == "m" ? 60 : 1))
          num = 0
          digits = 0
          units += 1
        else
          return nil
        end
        i += 1
      end
      if units == 0
        return nil if digits == 0
        total = num * 60
      elsif digits > 0
        return nil
      end
      total > 0 ? total * 1000 : nil
    end

    def calc_align_x_px(elem, px_w, content_x, content_w)
      return content_x unless elem.align
      case elem.align
      when :center then content_x + (content_w - px_w) / 2
      when :right then content_x + content_w - px_w
      else content_x
      end
    end
  end
end
