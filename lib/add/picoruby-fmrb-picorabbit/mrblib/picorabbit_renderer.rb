# PicoRabbit renderer for FmrbGfx
# Full-featured rendering with themes, inline formatting, syntax highlight, timer

module PicoRabbit
  # Theme definition
  class Theme
    attr_reader :name
    attr_accessor :bg, :text, :title, :title_bg, :bullet, :code_bg, :code_text,
                  :quote_bar, :quote_text, :footer_text, :footer_bg,
                  :inline_code_bg, :inline_code_text, :timer_track, :timer_turtle, :timer_rabbit

    def initialize(name)
      @name = name
    end

    def self.default
      t = Theme.new("default")
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
      t.timer_track = 0x24     # Dark gray
      t.timer_turtle = 0x1C    # Green
      t.timer_rabbit = 0xE0    # Red
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
      t.timer_track = 0xDB
      t.timer_turtle = 0x1C
      t.timer_rabbit = 0xE0
      t
    end

    def self.for_name(name)
      case name
      when "light" then light
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
    CHAR_W = 6     # Font size 1 character width
    CHAR_H = 8     # Font size 1 character height
    LINE_H = 10    # Line height with spacing
    TITLE_CHAR_W = 12  # Font size 2 character width
    TITLE_CHAR_H = 16  # Font size 2 character height
    MARGIN_X = 4
    MARGIN_Y = 2
    FOOTER_H = 10
    TIMER_H = 4
    TITLE_BAR_H = 20

    def initialize(gfx, width, height, metadata = {})
      @gfx = gfx
      @w = width
      @h = height
      @theme = Theme.for_name(metadata["theme"])
      @allotted_ms = nil
      if metadata["allotted_time"]
        mins = metadata["allotted_time"].to_i
        @allotted_ms = mins * 60 * 1000 if mins > 0
      end
      @start_ms = Machine.board_millis
      @usagi_jump_y = 0
      @usagi_vy = 0
    end

    def has_timer?
      @allotted_ms != nil
    end

    def rabbit_jumping?
      @usagi_jump_y != 0 || @usagi_vy != 0
    end

    # Partial redraw: only footer + timer area (no clear, no content re-render)
    def redraw_timer_area(slide_idx, total_slides)
      # Clear footer + timer + jump headroom area
      jump_margin = 10
      fy = @h - FOOTER_H - TIMER_H - jump_margin
      @gfx.fill_rect(0, fy, @w, FOOTER_H + TIMER_H + jump_margin, @theme.bg)
      render_footer(@last_slide, slide_idx, total_slides)
      render_timer(slide_idx, total_slides)
      @gfx.present
    end

    def render_slide(slide, step, slide_idx, total_slides)
      @current_slide_idx = slide_idx
      @last_slide = slide
      @gfx.clear(@theme.bg)

      if slide.title_slide
        render_title_slide(slide)
      else
        render_content_slide(slide, step)
      end

      # Footer
      render_footer(slide, slide_idx, total_slides)

      # Timer (rabbit/turtle race)
      render_timer(slide_idx, total_slides)

      @gfx.present
    end

    def jump_rabbit
      @usagi_vy = -5 if @usagi_jump_y == 0
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

    private

    def render_title_slide(slide)
      lines = slide.title ? slide.title.split("\n") : [""]
      sub_lines = []
      slide.elements.each do |e|
        sub_lines << e.text if e.type == :text && e.text
      end

      total_h = lines.length * (TITLE_CHAR_H + 4) + sub_lines.length * LINE_H + 8

      y = (@h - total_h) / 2

      @gfx.set_text_size(2)
      lines.each do |line|
        x = (@w - line.length * TITLE_CHAR_W) / 2
        @gfx.draw_text(x, y, line, @theme.title, @theme.bg)
        y += TITLE_CHAR_H + 4
      end

      @gfx.set_text_size(1)
      y += 8
      sub_lines.each do |line|
        x = (@w - line.length * CHAR_W) / 2
        @gfx.draw_text(x, y, line, @theme.text, @theme.bg)
        y += LINE_H
      end
    end

    def render_content_slide(slide, step)
      # Title bar
      @gfx.fill_rect(0, 0, @w, TITLE_BAR_H, @theme.title_bg)
      @gfx.set_text_size(2)
      title = slide.title || ""
      max_chars = (@w - MARGIN_X * 2) / TITLE_CHAR_W
      title = title[0, max_chars] if title.length > max_chars
      @gfx.draw_text(MARGIN_X, 2, title, @theme.title, @theme.title_bg)
      @gfx.set_text_size(1)

      y = TITLE_BAR_H + 4
      content_x = MARGIN_X
      content_w = @w - MARGIN_X * 2
      max_y = @h - FOOTER_H - TIMER_H - 4
      wait_seen = 0

      slide.elements.each do |elem|
        if elem.type == :wait
          wait_seen += 1
          break if step && wait_seen > step
          next
        end

        break if y + LINE_H > max_y

        case elem.type
        when :text
          y = draw_rich_text(content_x, y, elem.text, content_w, elem.align)

        when :bullet
          indent = content_x + elem.level * CHAR_W * 2
          marker = elem.level == 0 ? "- " : "  - "
          @gfx.draw_text(indent, y, marker, @theme.bullet, @theme.bg)
          draw_rich_text_at(indent + marker.length * CHAR_W, y, elem.text || "", @theme.text)
          y += LINE_H

        when :numbered
          indent = content_x + elem.level * CHAR_W * 2
          # Simple numbered prefix
          @gfx.draw_text(indent, y, "  ", @theme.text, @theme.bg)
          draw_rich_text_at(indent + CHAR_W * 2, y, elem.text || "", @theme.text)
          y += LINE_H

        when :code_block
          if elem.text.is_a?(Array)
            code_str = elem.text.join("\n")
            block_h = elem.text.length * LINE_H + 4
            @gfx.fill_rect(content_x, y, content_w, block_h, @theme.code_bg)

            # Try syntax highlighting
            begin
              hl_map = SyntaxHighlight.tokenize(code_str)
              cy = y + 2
              offset = 0
              elem.text.each do |line|
                draw_highlighted_line(content_x + 2, cy, line, hl_map, offset)
                offset += line.length + 1  # +1 for newline
                cy += LINE_H
              end
            rescue
              # Fallback: plain text
              cy = y + 2
              elem.text.each do |line|
                max_c = (content_w - 4) / CHAR_W
                code_line = line.length > max_c ? line[0, max_c] : line
                @gfx.draw_text(content_x + 2, cy, code_line, @theme.code_text, @theme.code_bg)
                cy += LINE_H
              end
            end
            y += block_h
          end

        when :fmrb_code
          if elem.text.is_a?(Array)
            code_str = elem.text.join("\n")
            begin
              $fmrb_gfx = @gfx
              $fmrb_x = content_x
              $fmrb_y = y
              $fmrb_w = content_w
              $fmrb_theme = @theme
              eval(code_str)
              y = $fmrb_y if $fmrb_y > y
            rescue => e
              @gfx.draw_text(content_x, y, "[fmrb_code error: #{e.message}]", 0xE0, @theme.bg)
              y += LINE_H
            end
          end

        when :blockquote
          @gfx.fill_rect(content_x, y, 2, CHAR_H, @theme.quote_bar)
          @gfx.draw_text(content_x + 6, y, elem.text || "", @theme.quote_text, @theme.bg)
          y += LINE_H

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
            @gfx.draw_text(content_x, y, "[img: #{elem.text}]", @theme.footer_text, @theme.bg)
            y += LINE_H
          end

        when :blank
          y += LINE_H / 2
        end
      end
    end

    # Draw text with inline formatting (**bold** and `code`)
    def draw_rich_text(x, y, text, content_w, align)
      return y + LINE_H unless text
      segments = parse_inline(text)

      if align == :center || align == :right
        total_w = 0
        segments.each { |seg| total_w += seg[1].length * CHAR_W }
        if align == :center
          x = x + (content_w - total_w) / 2
        else
          x = x + content_w - total_w
        end
      end

      segments.each do |seg|
        type = seg[0]
        str = seg[1]
        case type
        when :bold
          # Bold: draw twice with 1px offset
          @gfx.draw_text(x, y, str, @theme.text, @theme.bg)
          @gfx.draw_text(x + 1, y, str, @theme.text)
          x += str.length * CHAR_W
        when :code
          # Inline code: background highlight
          cw = str.length * CHAR_W + 2
          @gfx.fill_rect(x, y, cw, CHAR_H, @theme.inline_code_bg)
          @gfx.draw_text(x + 1, y, str, @theme.inline_code_text, @theme.inline_code_bg)
          x += cw
        else
          @gfx.draw_text(x, y, str, @theme.text, @theme.bg)
          x += str.length * CHAR_W
        end
      end
      y + LINE_H
    end

    # Draw rich text at fixed position (for bullets etc)
    def draw_rich_text_at(x, y, text, default_color)
      segments = parse_inline(text)
      segments.each do |seg|
        type = seg[0]
        str = seg[1]
        case type
        when :bold
          @gfx.draw_text(x, y, str, default_color, @theme.bg)
          @gfx.draw_text(x + 1, y, str, default_color)
          x += str.length * CHAR_W
        when :code
          cw = str.length * CHAR_W + 2
          @gfx.fill_rect(x, y, cw, CHAR_H, @theme.inline_code_bg)
          @gfx.draw_text(x + 1, y, str, @theme.inline_code_text, @theme.inline_code_bg)
          x += cw
        else
          @gfx.draw_text(x, y, str, default_color, @theme.bg)
          x += str.length * CHAR_W
        end
      end
    end

    # Parse inline formatting: **bold** and `code`
    def parse_inline(text)
      result = []
      buf = ""
      i = 0
      len = text.length

      while i < len
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
      max_c = (@w - MARGIN_X * 2 - 4) / CHAR_W
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
        @gfx.draw_text(x + i * CHAR_W, y, chunk, color, @theme.code_bg)
        i = j
      end
    end

    def render_footer(slide, slide_idx, total_slides)
      fy = @h - FOOTER_H - TIMER_H
      @gfx.fill_rect(0, fy, @w, FOOTER_H, @theme.footer_bg)
      @gfx.draw_line(0, fy, @w, fy, @theme.footer_text)

      # Slide title on left
      if slide && slide.title && !slide.title_slide
        ft = slide.title
        max_c = (@w / 2) / CHAR_W
        ft = ft[0, max_c] if ft.length > max_c
        @gfx.draw_text(MARGIN_X, fy + 1, ft, @theme.footer_text, @theme.footer_bg)
      end

      # Page number on right
      num = "#{slide_idx + 1}/#{total_slides}"
      nx = @w - num.length * CHAR_W - MARGIN_X
      @gfx.draw_text(nx, fy + 1, num, @theme.footer_text, @theme.footer_bg)
    end

    def render_timer(slide_idx, total_slides)
      return unless @allotted_ms
      ty = @h - TIMER_H
      track_left = MARGIN_X + 8
      track_right = @w - MARGIN_X - 8
      track_w = track_right - track_left

      # Track line
      @gfx.fill_rect(track_left, ty + 1, track_w, 2, @theme.timer_track)

      # Turtle position (slide progress)
      turtle_progress = total_slides > 1 ? slide_idx.to_f / (total_slides - 1) : 0
      turtle_x = track_left + (turtle_progress * track_w).to_i

      # Rabbit position (time progress)
      elapsed = Machine.board_millis - @start_ms
      rabbit_progress = elapsed.to_f / @allotted_ms
      rabbit_progress = 1.0 if rabbit_progress > 1.0
      rabbit_x = track_left + (rabbit_progress * track_w).to_i

      update_rabbit

      # Draw turtle as solid circle (green)
      @gfx.draw_text(turtle_x - 2, ty - 2 + @usagi_jump_y, "*", @theme.timer_turtle, @theme.bg)
      # Draw rabbit as solid square (red)
      @gfx.fill_rect(rabbit_x - 2, ty - 1, 4, 4, @theme.timer_rabbit)
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
