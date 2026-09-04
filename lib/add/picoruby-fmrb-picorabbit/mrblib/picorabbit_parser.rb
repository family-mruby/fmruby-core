# PicoRabbit markdown parser
# Compatible with Harucom OS picorabbit format
# Ported from harucom-os/rootfs/lib/picorabbit/parser.rb

module PicoRabbit
  class Parser
    def self.parse_file(path)
      content = File.open(path, "r") { |f| f.read }
      parse(content)
    end

    def self.parse(content)
      slides = []
      current_title = nil
      current_elements = []
      in_code_block = false
      code_block_type = :code_block
      code_lines = []
      metadata = {}

      current_goal = false
      current_bg = nil

      lines = split_lines(content)

      # Parse YAML frontmatter (--- delimited block at the start)
      if lines.length > 0 && lines[0].strip == "---"
        lines.shift
        while lines.length > 0
          line = lines.shift
          break if line.strip == "---"
          idx = line.index(":")
          if idx
            key = line[0, idx].strip
            val = line[idx + 1, line.length - idx - 1].strip
            metadata[key] = val
          end
        end
      end

      # Generate title slide from frontmatter
      if metadata["title"]
        title_text = metadata["title"].gsub("<br>", "\n")
        elems = []
        elems << Element.new(:text, metadata["subtitle"]) if metadata["subtitle"]
        elems << Element.new(:text, metadata["author"]) if metadata["author"]
        s = Slide.new(title_text, elems)
        s.title_slide = true
        slides << s
      end

      lines.each do |line|
        # Code block fence
        if line.strip.start_with?("```")
          if in_code_block
            current_elements << Element.new(code_block_type, code_lines)
            code_lines = []
            code_block_type = :code_block
            in_code_block = false
          else
            lang = line.strip[3, line.strip.length - 3]
            if lang == "fmrb"
              code_block_type = :fmrb_code
            else
              code_block_type = :code_block
            end
            in_code_block = true
          end
          next
        end

        if in_code_block
          code_lines << line
          next
        end

        # Heading: start a new slide
        if line.start_with?("# ")
          if current_title
            sl = Slide.new(current_title, current_elements)
            sl.goal = current_goal
            sl.background = current_bg
            slides << sl
            current_goal = false
            current_bg = nil
          end
          current_title = line[2, line.length - 2].strip.gsub("<br>", "\n")
          current_elements = []
          next
        end

        # Wait marker
        if line.strip == "{::wait/}"
          current_elements << Element.new(:wait)
          next
        end

        # Goal marker: draws nothing, marks the slide it sits on.
        if line.strip == "{::goal/}"
          current_goal = true
          next
        end

        # Background of this slide: a path, or "none" to shut the deck's
        # background off for this one page. Draws nothing itself.
        s = line.strip
        if s.start_with?("{::background ") && s.end_with?("/}")
          current_bg = s[14, s.length - 16].strip
          next
        end

        # Attribute line: {:.center}, {:.large}, or several at once
        # ({:.center .xlarge}). The classes belong to the last element that
        # draws something, so a blank line between the text and the line does
        # not swallow them. With nothing to attach to, the line is dropped.
        if s.start_with?("{:.") && s.end_with?("}")
          ai = current_elements.length - 1
          while ai >= 0 && current_elements[ai].type == :blank
            ai -= 1
          end
          apply_classes(current_elements[ai], s[2, s.length - 3]) if ai >= 0
          next
        end

        # Image
        stripped_line = line.strip
        if stripped_line.start_with?("![")
          paren = stripped_line.index("](")
          if paren
            close = stripped_line.index(")", paren + 2)
            if close
              path = stripped_line[paren + 2, close - paren - 2]
              img = Element.new(:image, path)
              apply_image_options(img, stripped_line[2, paren - 2])
              current_elements << img
              next
            end
          end
        end

        # Blank line
        if line.strip.length == 0
          current_elements << Element.new(:blank)
          next
        end

        # Blockquote
        stripped = line.lstrip
        if stripped.start_with?("> ")
          text = stripped[2, stripped.length - 2]
          current_elements << Element.new(:blockquote, text)
          next
        end

        # Bullet list
        if stripped.start_with?("- ") || stripped.start_with?("* ")
          indent = line.length - stripped.length
          level = indent / 2
          text = stripped[2, stripped.length - 2]
          current_elements << Element.new(:bullet, text, level)
          next
        end

        # Numbered list
        dot_pos = stripped.index(". ")
        if dot_pos && dot_pos > 0 && dot_pos <= 3
          num_str = stripped[0, dot_pos]
          all_digits = true
          i = 0
          while i < num_str.length
            c = num_str[i]
            unless c >= "0" && c <= "9"
              all_digits = false
              break
            end
            i += 1
          end
          if all_digits
            indent = line.length - stripped.length
            level = indent / 2
            text = stripped[dot_pos + 2, stripped.length - dot_pos - 2]
            item = Element.new(:numbered, text, level)
            item.number = num_str.to_i
            current_elements << item
            next
          end
        end

        # Plain text
        current_elements << Element.new(:text, line.strip)
      end

      # Close any unclosed code block
      if in_code_block && code_lines.length > 0
        current_elements << Element.new(code_block_type, code_lines)
      end

      # Save last slide
      if current_title
        sl = Slide.new(current_title, current_elements)
        sl.goal = current_goal
        sl.background = current_bg
        slides << sl
      end

      ParseResult.new(slides, metadata)
    end

    private

    def self.split_lines(str)
      result = []
      start = 0
      i = 0
      len = str.length
      while i < len
        if str[i] == "\n"
          result << str[start, i - start]
          start = i + 1
        end
        i += 1
      end
      result << str[start, len - start] if start < len
      result
    end

    # The alt text of an image is read as words: "w=200" is a width in
    # pixels and "60%" a share of the body width; for a video (.mjpg)
    # "fps=10" sets the rate and "once" stops it at the end ("loop" is the
    # default, spelled out). Any other word is a caption nobody shows, and
    # is dropped.
    def self.apply_image_options(elem, alt)
      return unless alt && alt.length > 0
      words = split_words(alt)
      i = 0
      while i < words.length
        w = words[i]
        if w.end_with?("%")
          pct = w[0, w.length - 1].to_i
          elem.img_pct = pct if pct > 0
        elsif w.start_with?("w=")
          px = w[2, w.length - 2].to_i
          elem.img_w = px if px > 0
        elsif w.start_with?("fps=")
          fps = w[4, w.length - 4].to_i
          elem.video_fps = fps if fps > 0
        elsif w == "once"
          elem.video_loop = false
        elsif w == "loop"
          elem.video_loop = true
        end
        i += 1
      end
    end

    # ".center .xlarge" -> the element's alignment and size. A class this
    # parser does not know is ignored, so a deck written for a fuller
    # renderer still opens.
    def self.apply_classes(elem, classes)
      words = split_words(classes)
      i = 0
      while i < words.length
        case words[i]
        when ".center" then elem.align = :center
        when ".right" then elem.align = :right
        when ".small" then elem.size = :small
        when ".large" then elem.size = :large
        when ".xlarge" then elem.size = :xlarge
        when ".shadow" then elem.shadow = true
        end
        i += 1
      end
    end

    def self.split_words(str)
      words = []
      start = nil
      i = 0
      len = str.length
      while i < len
        if str[i] == " "
          words << str[start, i - start] if start
          start = nil
        elsif start.nil?
          start = i
        end
        i += 1
      end
      words << str[start, len - start] if start
      words
    end
  end
end
