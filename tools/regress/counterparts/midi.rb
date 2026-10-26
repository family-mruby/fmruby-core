# frozen_string_literal: true
#
# midi.rb - the receiving end of the sim's serial MIDI port: the root
# tools/fmrb_midi_monitor.rb on the FIFO the core writes to
# (fmruby-core/midi_out.fifo, bind-mounted into the container), its decoded
# lines kept in a log. The monitor's own time stamps (arrival on this PC)
# give the notes' lengths and spacing.
#
#   m = s.counterpart :midi
#   ... the app plays to "out: serial" ...
#   m.messages   # [{at:, bytes: [..], kind:, text:}, ...]
#   m.notes      # [[name, on_s, off_s], ...] (note on to its note off)
#   m.stop / m.resume   # take the monitor away / start it again
module Regress
  module Counterparts
    class Midi < Base
      MONITOR = File.join(ROOT, "tools", "fmrb_midi_monitor.rb")
      LINE = /^\s+(\d+\.\d+)\s+(.+?)\s+\[([0-9A-F ]+)\]\s*$/

      def start
        resume
      end

      def resume
        raise Error, "midi: the monitor is already running" if @pid
        @from = File.exist?(log_path) ? File.size(log_path) : 0
        @pid, = spawn_logged("monitor", {}, [Gem.ruby, MONITOR], ROOT)
        Counterparts.wait_for("the MIDI monitor", 10) do
          raise Error, "midi: the monitor exited (#{log_since(log_path, @from).strip})" if Process.wait(@pid, Process::WNOHANG)
          log_since(log_path, @from).include?("reading ")
        end
        self
      end

      def stop
        kill_group(@pid, "INT", wait: 5)
        @pid = nil
      end

      def close
        stop if @pid
      end

      def log_path = File.join(@dir, "monitor.log")

      # What the monitor decoded since it was (last) started.
      def messages
        log_since(log_path, @from || 0).lines.filter_map do |l|
          m = LINE.match(l) or next
          bytes = m[3].split.map { |h| h.to_i(16) }
          { at: m[1].to_f, bytes: bytes, text: m[2].strip, kind: kind_of(bytes) }
        end
      end

      def kind_of(bytes)
        case bytes[0] & 0xF0
        when 0x90 then bytes[2].to_i.zero? ? :note_off : :note_on
        when 0x80 then :note_off
        when 0xB0 then :control
        when 0xC0 then :program
        when 0xF0 then :system
        else :other
        end
      end

      NAMES = %w[C C# D D# E F F# G G# A A# B].freeze

      def note_name(n) = "#{NAMES[n % 12]}#{(n / 12) - 1}"

      # Each note on with the note off of the same channel and key after it.
      def notes
        open = {}
        out = []
        messages.each do |m|
          ch = m[:bytes][0] & 0x0F
          key = [ch, m[:bytes][1]]
          case m[:kind]
          when :note_on
            open[key] = out.size
            out << [note_name(m[:bytes][1]), m[:at], nil, ch + 1]
          when :note_off
            i = open.delete(key)
            out[i][2] = m[:at] if i
          end
        end
        out
      end

      def state = { "messages" => messages.size, "notes" => notes.size }
    end

    register :midi, Midi
  end
end
