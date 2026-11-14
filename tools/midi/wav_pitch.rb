#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wav_pitch - measure note pitches and lengths in a PCM WAV file.
#
# Used to check FMSQ output numerically without listening to it: render with
# fmrb-audio-tools (bin/fmsq_player x.fmsq -o x.wav) and run this on the
# result. Segmentation is by loudness, pitch is the autocorrelation peak with
# parabolic interpolation, which is accurate to a few cents on the APU's
# square and triangle waves.
#
# Only one note at a time can be measured this way: on a chord the strongest
# partial wins, so render voices separately when the exact pitch matters.
#
# Usage:
#   ruby wav_pitch.rb file.wav [--threshold 0.05] [--min-note 0.03] [--json]

require "optparse"

module WavPitch
  NOTE_NAMES = %w[C C# D D# E F F# G G# A A# B].freeze

  class Error < StandardError; end

  Note = Struct.new(:start, :duration, :frequency, :rms, keyword_init: true) do
    def midi_note
      return nil if frequency.nil? || frequency <= 0

      69 + (12 * Math.log2(frequency / 440.0))
    end

    def name
      n = midi_note
      return "-" if n.nil?

      nearest = n.round
      "#{NOTE_NAMES[nearest % 12]}#{(nearest / 12) - 1}"
    end

    def cents
      n = midi_note
      return nil if n.nil?

      ((n - n.round) * 100.0)
    end
  end

  # Minimal PCM WAV reader (16-bit integer or 32-bit float, any channel count).
  class Wav
    attr_reader :sample_rate, :channels, :samples

    def initialize(path)
      data = File.binread(path)
      raise Error, "not a RIFF/WAVE file" unless data[0, 4] == "RIFF" && data[8, 4] == "WAVE"

      pos = 12
      fmt = nil
      pcm = nil
      while pos + 8 <= data.bytesize
        id = data[pos, 4]
        size = data[pos + 4, 4].unpack1("V")
        body = data.byteslice(pos + 8, size)
        case id
        when "fmt " then fmt = body
        when "data" then pcm = body
        end
        pos += 8 + size + (size.odd? ? 1 : 0)
      end
      raise Error, "missing fmt chunk" if fmt.nil?
      raise Error, "missing data chunk" if pcm.nil?

      format_tag, @channels, @sample_rate, _byte_rate, _align, bits = fmt.unpack("vvVVvv")
      @samples = decode(pcm, format_tag, bits)
    end

    # Mono mix, normalized to -1.0 .. 1.0.
    def mono
      return @samples if @channels == 1

      out = Array.new(@samples.size / @channels)
      out.each_index do |i|
        sum = 0.0
        @channels.times { |c| sum += @samples[(i * @channels) + c] }
        out[i] = sum / @channels
      end
      out
    end

    private

    def decode(pcm, format_tag, bits)
      case [format_tag, bits]
      when [1, 16]
        pcm.unpack("s<*").map { |v| v / 32_768.0 }
      when [1, 8]
        pcm.unpack("C*").map { |v| (v - 128) / 128.0 }
      when [3, 32]
        pcm.unpack("e*")
      else
        raise Error, "unsupported WAV encoding (format=#{format_tag} bits=#{bits})"
      end
    end
  end

  class Analyzer
    def initialize(wav, threshold: 0.05, min_note: 0.03, window: 0.010)
      @wav = wav
      @rate = wav.sample_rate
      @signal = wav.mono
      @threshold = threshold
      @min_note = min_note
      @window = (window * @rate).to_i
    end

    # Split into sounding segments and measure each one.
    def notes
      envelope = rms_envelope
      peak = envelope.max || 0.0
      return [] if peak <= 0.0

      level = peak * @threshold
      segments = []
      start = nil
      envelope.each_with_index do |value, i|
        if value >= level && start.nil?
          start = i
        elsif value < level && start
          segments << [start, i]
          start = nil
        end
      end
      segments << [start, envelope.size] if start

      segments.filter_map do |from, to|
        duration = (to - from) * @window / @rate.to_f
        next if duration < @min_note

        offset = from * @window
        length = (to - from) * @window
        Note.new(start: offset / @rate.to_f, duration: duration,
                 frequency: pitch(offset, length),
                 rms: envelope[from...to].max)
      end
    end

    private

    def rms_envelope
      out = []
      i = 0
      while i + @window <= @signal.size
        sum = 0.0
        @window.times { |k| sum += @signal[i + k]**2 }
        out << Math.sqrt(sum / @window)
        i += @window
      end
      out
    end

    # Autocorrelation over the steady middle of the segment.
    def pitch(offset, length, fmin: 40.0, fmax: 4000.0)
      # Skip the attack, and cap the analysis window for speed.
      skip = [length / 8, (0.005 * @rate).to_i].max
      from = offset + skip
      size = [length - skip, (0.08 * @rate).to_i].min
      return nil if size < (@rate / fmin).to_i

      frame = @signal[from, size]
      mean = frame.sum / frame.size
      frame = frame.map { |v| v - mean }

      min_lag = (@rate / fmax).to_i.clamp(2, size - 1)
      max_lag = [(@rate / fmin).to_i, size - 1].min
      return nil if max_lag <= min_lag

      best = 0.0
      values = Array.new(max_lag + 1, 0.0)
      (min_lag..max_lag).each do |lag|
        sum = 0.0
        limit = size - lag
        k = 0
        while k < limit
          sum += frame[k] * frame[k + lag]
          k += 1
        end
        sum /= limit
        values[lag] = sum
        best = sum if sum > best
      end
      return nil if best <= 0

      # Any integer multiple of the true period correlates just as well, so
      # taking the global maximum reports an octave (or twelfth) too low.
      # Take the earliest peak that is nearly as strong instead.
      accept = best * 0.85
      best_lag = nil
      (min_lag + 1...max_lag).each do |lag|
        next unless values[lag] >= accept && values[lag] >= values[lag - 1] && values[lag] >= values[lag + 1]

        best_lag = lag
        break
      end
      best_lag ||= values.each_index.max_by { |i| values[i] }
      return nil if best_lag.nil? || best_lag < min_lag

      rough = @rate / best_lag.to_f
      refine(frame, best_lag) || rough
    end

    # Count rising zero crossings over the whole steady part of the note.
    # The autocorrelation peak alone is only good to about half a sample
    # (tens of cents at these rates) because the correlation peak of a square
    # wave is a triangle, not a parabola. Averaging over ~100 periods instead
    # brings the error down to well under a cent.
    def refine(frame, rough_lag)
      crossings = []
      guard = rough_lag * 0.5
      i = 0
      while i < frame.size - 1
        a = frame[i]
        b = frame[i + 1]
        if a < 0 && b >= 0
          t = i + (a.abs / (b - a).abs)
          crossings << t if crossings.empty? || (t - crossings.last) >= guard
        end
        i += 1
      end
      return nil if crossings.size < 3

      span = crossings.last - crossings.first
      return nil if span <= 0

      @rate * (crossings.size - 1) / span
    end
  end

  module_function

  def run(argv)
    options = { threshold: 0.05, min_note: 0.03 }
    parser = OptionParser.new do |o|
      o.banner = "Usage: wav_pitch.rb FILE.wav [options]"
      o.on("--threshold F", Float, "loudness gate as a fraction of peak (default 0.05)") do |v|
        options[:threshold] = v
      end
      o.on("--min-note SEC", Float, "ignore segments shorter than SEC (default 0.03)") do |v|
        options[:min_note] = v
      end
      o.on("-h", "--help") do
        puts o
        return 0
      end
    end
    parser.parse!(argv)

    path = argv.shift
    if path.nil?
      warn parser.help
      return 1
    end

    wav = Wav.new(path)
    notes = Analyzer.new(wav, **options).notes
    puts "#{path}: #{wav.sample_rate} Hz, #{wav.channels} ch, " \
         "#{format('%.2f', wav.mono.size / wav.sample_rate.to_f)} s, #{notes.size} notes"
    puts "  #  start(s)  dur(s)   freq(Hz)  note   cents"
    notes.each_with_index do |n, i|
      printf("%3d  %8.3f  %6.3f  %9.2f  %-5s %+6.1f\n",
             i, n.start, n.duration, n.frequency || 0.0, n.name, n.cents || 0.0)
    end
    0
  rescue Error, Errno::ENOENT => e
    warn "wav_pitch: #{e.message}"
    1
  end
end

exit(WavPitch.run(ARGV)) if $PROGRAM_NAME == __FILE__
