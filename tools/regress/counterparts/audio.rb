# frozen_string_literal: true
#
# audio.rb - what the built-in sound (the APU) puts out, read by the root
# tools/fmrb_audio_probe.rb from the ring graphics-audio mixes into (it
# works headless: the SDL dummy driver plays nothing, the ring is still
# written). One listening window at a time; a scene may make the sound while
# it listens (the block runs while the probe reads).
#
#   a = s.counterpart :audio
#   a.listen(1.0)                    # silence?
#   a.listen(3.0) { s.key "1" }      # the sound of the scale
#   a.state  # the last window: {"windows", "sounding", "peak", "seconds"}
require "open3"

module Regress
  module Counterparts
    class Audio < Base
      PROBE = File.join(ROOT, "tools", "fmrb_audio_probe.rb")
      # The probe's windows (50 ms): a window is sounding past 256.
      WINDOW_S = 0.05
      SUMMARY = /windows=(\d+) samples=(\d+) \(([\d.]+) s\) peak=(\d+) avg_rms=([\d.]+) sounding_windows=(\d+)/

      def listen(seconds)
        result = nil
        th = Thread.new do
          out, st = Open3.capture2e(Gem.ruby, PROBE, "--duration", seconds.to_s, "-q", chdir: ROOT)
          result = [out, st.exitstatus]
        end
        sleep 0.3 # the probe takes its first position before the sound starts
        yield if block_given?
        th.join
        out, code = result
        File.write(File.join(@dir, "probe.log"), out, mode: "a")
        m = SUMMARY.match(out)
        raise Error, "audio: the probe read nothing (exit #{code}: #{out.strip.lines.last})" unless m && code != 1
        @last = { "windows" => m[1].to_i, "seconds" => m[3].to_f, "peak" => m[4].to_i, "sounding" => m[6].to_i }
      end

      def state = @last || {}
    end

    register :audio, Audio
  end
end
