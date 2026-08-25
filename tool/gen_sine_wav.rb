#!/usr/bin/env ruby
# Generate the test tones play_wav is checked against.
#
# A sine of a known pitch is what makes the resampler verifiable without
# listening to anything: the clip is recorded at one rate, played back at the
# APU's 15720 Hz, and an FFT of what came out has to peak at the same
# frequency it went in at. Get the rate conversion wrong and the pitch moves,
# which a spectrum shows immediately (tools/fmrb_audio_probe.rb --dump).
#
# Two rates on purpose: 16000 is what a speech synthesiser usually produces,
# and 8000 is the other end of the range play_wav accepts. Both are far from
# 15720, so neither can pass by accident.
#
#   ruby tool/gen_sine_wav.rb                 # write the shipped pair
#   ruby tool/gen_sine_wav.rb out.wav 440 16000 1.0
#
# Output is PCM 16-bit mono, the only shape play_wav takes.

require "fileutils"

FADE_MS = 5  # a hard start or stop is a click, and clicks are broadband

def sine_wav(freq_hz, rate, seconds, amplitude = 0.6)
  frames = (rate * seconds).to_i
  fade = (rate * FADE_MS / 1000.0).to_i
  fade = frames / 2 if fade > frames / 2
  samples = Array.new(frames) do |i|
    gain = 1.0
    gain = i.to_f / fade if fade > 0 && i < fade
    gain = (frames - 1 - i).to_f / fade if fade > 0 && i >= frames - fade
    v = Math.sin(2.0 * Math::PI * freq_hz * i / rate) * amplitude * gain
    (v * 32767).round.clamp(-32768, 32767)
  end
  pcm = samples.pack("s<*")

  # Canonical 44-byte header: RIFF / fmt (PCM, mono, 16-bit) / data.
  fmt = ["fmt ", 16, 1, 1, rate, rate * 2, 2, 16].pack("a4Vv2V2v2")
  data = ["data", pcm.bytesize].pack("a4V") + pcm
  ["RIFF", 4 + fmt.bytesize + data.bytesize, "WAVE"].pack("a4Va4") + fmt + data
end

def write(path, bytes)
  FileUtils.mkdir_p(File.dirname(path))
  File.binwrite(path, bytes)
  puts "#{path} (#{bytes.bytesize} bytes)"
end

if ARGV.empty?
  root = File.expand_path("..", __dir__)
  dir = File.join(root, "flash/usr/share/sounds")
  write(File.join(dir, "sine440_16k.wav"), sine_wav(440, 16000, 1.0))
  write(File.join(dir, "sine440_8k.wav"),  sine_wav(440, 8000, 1.0))
else
  out  = ARGV[0]
  freq = (ARGV[1] || 440).to_i
  rate = (ARGV[2] || 16000).to_i
  secs = (ARGV[3] || 1.0).to_f
  write(out, sine_wav(freq, rate, secs))
end
