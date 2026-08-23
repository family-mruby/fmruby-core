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
            slides << sl
            current_goal = false
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

        # Alignment directive. It belongs to the last element that draws
        # something, so a blank line between the text and the directive does
        # not swallow it. With nothing to align, the directive is dropped.
        s = line.strip
        if s == "{:.center}" || s == "{:.right}"
          align = s == "{:.center}" ? :center : :right
          ai = current_elements.length - 1
          while ai >= 0 && current_elements[ai].type == :blank
            ai -= 1
          end
          current_elements[ai].align = align if ai >= 0
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
              current_elements << Element.new(:image, path)
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
  end
end
