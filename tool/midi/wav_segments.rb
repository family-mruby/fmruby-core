#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Counts how many times the pitch of a rendered APU capture changes.
#
# The APU voices are periodic, so a note is a run of equal periods and a
# change of period is a note change. Counting them counts the notes the chip
# actually played - including any nobody wrote, which is what this exists
# for: sending a chord's messages one at a time makes the voice step through
# the chord's inner notes (doc/midi/report/p7_5.md).
#
# The period is measured between successive upward zero crossings rather
# than over a window, so the resolution is one period of the note itself
# (about 4 ms at middle C) and a note lasting two periods still shows up.
#
# Reads the 16-bit WAV that tools/fmrb_audio_probe.rb --dump writes.
#
# Usage:
#   ruby tool/midi/wav_segments.rb before.wav after.wav
#   ruby tool/midi/wav_segments.rb capture.wav

# Minimal RIFF reader: the probe writes one fixed shape, so this only has to
# find the fmt and data chunks rather than handle WAV in general.
def read_wav(path)
  raw = File.binread(path)
  raise "#{path}: not a RIFF file" unless raw[0, 4] == "RIFF" && raw[8, 4] == "WAVE"

  pos = 12
  rate = nil
  channels = 1
  bits = 16
  samples = nil
  while pos + 8 <= raw.bytesize
    id = raw[pos, 4]
    size = raw[pos + 4, 4].unpack1("V")
    body = raw[pos + 8, size]
    case id
    when "fmt "
      channels = body[2, 2].unpack1("v")
      rate = body[4, 4].unpack1("V")
      bits = body[14, 2].unpack1("v")
    when "data"
      samples = body.unpack("s<*")
    end
    pos += 8 + size + (size.odd? ? 1 : 0)
  end
  raise "#{path}: no data chunk" unless samples
  raise "#{path}: expected 16-bit samples" unless bits == 16

  # The probe dumps the APU's stereo output; both sides carry the same mix,
  # so one of them is the signal.
  samples = samples.each_slice(channels).map(&:first) if channels > 1
  [samples, rate]
end

MIN_LEVEL = 200      # below this the capture is silence, not a note
TOLERANCE = 0.06     # a semitone; anything larger is a different note
MAX_PERIOD_MS = 40   # longer gaps are silence between notes, not a period

def analyse(path)
  samples, rate = read_wav(path)
  max_period = rate * MAX_PERIOD_MS / 1000

  # Upward zero crossings of the parts that are actually sounding.
  crossings = []
  i = 1
  while i < samples.size
    crossings << i if samples[i - 1] < 0 && samples[i] >= MIN_LEVEL
    i += 1
  end

  changes = 0
  sounding = 0
  previous = nil
  j = 1
  while j < crossings.size
    period = crossings[j] - crossings[j - 1]
    j += 1
    if period > max_period
      previous = nil # a gap: the next period starts a new note either way
      next
    end

    sounding += period
    if previous.nil? || ((period - previous).abs.to_f / previous) > TOLERANCE
      changes += 1
    end
    previous = period
  end

  { file: File.basename(path), seconds: samples.size.to_f / rate,
    sounding_s: sounding.to_f / rate, changes: changes }
end

abort "usage: #{$PROGRAM_NAME} <capture.wav> [other.wav]" if ARGV.empty?

results = ARGV.map { |path| analyse(path) }
results.each do |r|
  printf("%-24s %5.1f s  sounding %5.1f s  pitch changes %5d  (%.1f/s sounding)\n",
         r[:file], r[:seconds], r[:sounding_s], r[:changes],
         r[:changes] / r[:sounding_s])
end

if results.size == 2
  a, b = results
  rate_a = a[:changes] / a[:sounding_s]
  rate_b = b[:changes] / b[:sounding_s]
  printf("\n%s changes pitch %.2fx as often as %s\n",
         b[:file], rate_b / rate_a, a[:file])
end
