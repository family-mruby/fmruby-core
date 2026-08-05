# MIDI MML (Music Macro Language) Parser
#
# Parses MML strings and generates clock-based MIDI events

module MIDI
  module MML
    # MML Sequence - parses MML and provides events at clock positions
    class Sequence
      # Notes per octave with semitone offsets
      NOTE_MAP = {
        'c' => 0, 'd' => 2, 'e' => 4, 'f' => 5,
        'g' => 7, 'a' => 9, 'b' => 11
      }

      # Clocks per whole note (24 PPQ * 4 = 96)
      CLOCKS_PER_WHOLE = 96

      attr_reader :channel, :total_length, :events

      # @param mml [String] MML string to parse
      # @param channel [Integer] MIDI channel (0-15)
      # @param velocity [Integer] Default velocity (0-127)
      def initialize(mml, channel: 0, velocity: 100)
        @mml = remove_whitespace(mml.downcase)
        @channel = channel
        @default_velocity = velocity
        @events = []
        @total_length = 0
        parse
        # puts "[MML] Sequence parsed: #{@events.length} events, total_length=#{@total_length} clocks"
        @events.each do |e|
          # puts "[MML]   clock=#{e[:clock]} #{e[:type]} note=#{e[:note]}"
        end
      end

      # Get events at a specific clock position
      # @param clock [Integer] Clock position
      # @return [Array<Hash>] Events at this position
      def events_at(clock)
        @events.select { |e| e[:clock] == clock }
      end

      private

      # Remove all whitespace characters (space, tab, newline, etc.)
      def remove_whitespace(str)
        result = ""
        str.each_char do |c|
          ord = c.ord
          # Skip space (32), tab (9), newline (10), carriage return (13)
          next if ord == 32 || ord == 9 || ord == 10 || ord == 13
          result << c
        end
        result
      end

      def parse
        @pos = 0
        @octave = 4
        @default_length = 4  # Quarter note
        @velocity = @default_velocity
        @clock = 0
        @tie_note = nil

        while @pos < @mml.length
          char = @mml[@pos]
          @pos += 1

          case char
          when 'c', 'd', 'e', 'f', 'g', 'a', 'b'
            parse_note(char)
          when 'r'
            parse_rest
          when 'o'
            @octave = parse_number(4)
            @octave = 1 if @octave < 1
            @octave = 8 if @octave > 8
          when '>'
            @octave += 1
            @octave = 8 if @octave > 8
          when '<'
            @octave -= 1
            @octave = 1 if @octave < 1
          when 'l'
            @default_length = parse_number(4)
          when 'v'
            @velocity = parse_number(100)
            @velocity = 0 if @velocity < 0
            @velocity = 127 if @velocity > 127
          when '['
            parse_loop
          when '&'
            # Tie - handled in parse_note
          end
        end

        @total_length = @clock
      end

      def parse_note(note_char)
        semitone = NOTE_MAP[note_char]

        # Check for sharp/flat
        if peek == '+' || peek == '#'
          @pos += 1
          semitone += 1
        elsif peek == '-'
          @pos += 1
          semitone -= 1
        end

        # Calculate MIDI note number
        midi_note = (@octave + 1) * 12 + semitone

        # Get note length
        length, dots = parse_length
        clocks = length_to_clocks(length, dots)

        # Check for tie and handle tied durations (&8 notation).
        # NOTE: use `while true` instead of Kernel#loop. mrubyc defines
        # `loop` in pure Ruby (def loop; while true; yield; end; end)
        # and the yield+break+closure interaction skips the body, so the
        # tied duration was never accumulated on device — the leading
        # note ended up shorter than the MML specifies.
        while true
          is_tied = false
          # Look ahead for &
          save_pos = @pos
          while @pos < @mml.length && @mml[@pos] == ' '
            @pos += 1
          end
          if @pos < @mml.length && @mml[@pos] == '&'
            @pos += 1
            is_tied = true
          else
            @pos = save_pos
          end

          # If tied, check if next char is a duration (digit or '.')
          # This supports shorthand notation like "c4&8" (same as "c4&c8")
          if is_tied
            # Skip whitespace after &
            while @pos < @mml.length && @mml[@pos] == ' '
              @pos += 1
            end

            next_char = peek
            if is_digit?(next_char) || next_char == '.'
              # Parse the tied duration and add to total
              tie_length, tie_dots = parse_length
              tie_clocks = length_to_clocks(tie_length, tie_dots)
              clocks += tie_clocks
              # Continue loop to check for more ties
            else
              # Tie without duration - will continue with next note of same pitch
              break
            end
          else
            # No more ties
            break
          end
        end

        # Final tie check after all durations accumulated
        is_tied = false
        save_pos = @pos
        while @pos < @mml.length && @mml[@pos] == ' '
          @pos += 1
        end
        if @pos < @mml.length && @mml[@pos] == '&'
          @pos += 1
          is_tied = true
        else
          @pos = save_pos
        end

        if @tie_note && @tie_note[:note] == midi_note
          # Continue tied note - extend duration
          @tie_note[:duration] += clocks
          if !is_tied
            # End of tie - emit note off
            @events << {
              type: :note_off,
              clock: @tie_note[:start] + @tie_note[:duration],
              note: midi_note,
              channel: @channel
            }
            @tie_note = nil
          end
        else
          # New note
          if @tie_note
            # Previous tie ended without matching note
            @events << {
              type: :note_off,
              clock: @tie_note[:start] + @tie_note[:duration],
              note: @tie_note[:note],
              channel: @channel
            }
            @tie_note = nil
          end

          # Note on
          @events << {
            type: :note_on,
            clock: @clock,
            note: midi_note,
            velocity: @velocity,
            channel: @channel,
            duration_clocks: clocks  # Duration in clock ticks
          }

          if is_tied
            @tie_note = { note: midi_note, start: @clock, duration: clocks }
          else
            # Note off
            @events << {
              type: :note_off,
              clock: @clock + clocks,
              note: midi_note,
              channel: @channel
            }
          end
        end

        @clock += clocks
      end

      def parse_rest
        length, dots = parse_length
        clocks = length_to_clocks(length, dots)
        @clock += clocks
      end

      def parse_length
        if is_digit?(peek)
          length = parse_number(@default_length)
        else
          length = @default_length
        end

        dots = 0
        while peek == '.'
          @pos += 1
          dots += 1
        end

        [length, dots]
      end

      def parse_number(default)
        return default unless is_digit?(peek)

        num = 0
        while is_digit?(peek)
          num = num * 10 + @mml[@pos].to_i
          @pos += 1
        end
        num
      end

      def is_digit?(char)
        return false if char.nil?
        c = char.ord
        c >= 48 && c <= 57  # '0' = 48, '9' = 57
      end

      def parse_loop
        # Find matching ]
        start_pos = @pos
        depth = 1
        while @pos < @mml.length && depth > 0
          if @mml[@pos] == '['
            depth += 1
          elsif @mml[@pos] == ']'
            depth -= 1
          end
          @pos += 1
        end

        # Extract loop content
        end_pos = @pos - 1
        loop_content = @mml[start_pos...end_pos]

        # Get repeat count
        repeat = parse_number(2)

        # Parse loop content multiple times
        original_pos = @pos
        repeat.times do
          @pos = 0
          temp_mml = @mml
          @mml = loop_content
          while @pos < @mml.length
            char = @mml[@pos]
            @pos += 1
            case char
            when 'c', 'd', 'e', 'f', 'g', 'a', 'b'
              parse_note(char)
            when 'r'
              parse_rest
            when 'o'
              @octave = parse_number(4)
            when '>'
              @octave += 1
            when '<'
              @octave -= 1
            when 'l'
              @default_length = parse_number(4)
            when 'v'
              @velocity = parse_number(100)
            when '['
              parse_loop
            end
          end
          @mml = temp_mml
        end
        @pos = original_pos
      end

      def length_to_clocks(length, dots)
        return CLOCKS_PER_WHOLE if length == 0
        base = CLOCKS_PER_WHOLE / length
        total = base
        dot_value = base / 2
        # NOTE: was `dots.times do total += dot_value; dot_value /= 2; end`.
        # On mrubyc, rebinding (`+=`, `=`) of an outer local inside a block
        # does not write back to the enclosing scope, so `total` and
        # `dot_value` stayed at their initial values and dotted notes
        # silently lost their dot. Use a `while` loop (no closure boundary).
        i = 0
        while i < dots
          total += dot_value
          dot_value /= 2
          i += 1
        end
        total
      end

      def peek
        @pos < @mml.length ? @mml[@pos] : nil
      end
    end
  end
end
