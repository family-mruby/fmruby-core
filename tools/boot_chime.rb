#!/usr/bin/env ruby
# frozen_string_literal: true
#
# boot_chime - generate the boot chord as two FMSQ files, one for each APU
# instance (doc/boot_splash).
#
# The desktop loads both inline (FmrbAudio#load_fmsq) and starts them on the
# frame the boot line appears, MAIN then SUB, so the two play as one 8-voice
# sound: 4 pulses, 2 triangles, 2 noises.
#
#   0 ms    G3 (pulse, both instances, slightly apart) over a G2 triangle,
#           a soft noise "hiss"; the G3 brightens while the line stretches
#   267 ms  up a fifth to D4
#   500 ms  G4 A4 D5 with the G2 doubled on the second triangle, a noise
#           sparkle; the pulses then decay
#   900 ms  the second triangle stops (a triangle has no volume, so the bass
#           comes down in two steps), 1100 ms the first one
#   1183 ms silence (all channels off), the pulses having faded to 1
#
# Times are 60 Hz frames, the rate the FMSQ players tick at (the audio
# side's clock, not the desktop's: the Linux sim's runs about 4% fast).
#
# The two triangles both play G2 to double the bass: a triangle has no
# volume, and through the emulator it sounds well under a pulse at volume 10
# or so. They only add up when they are in phase, so the second one starts on
# frame 30, where the first one's phase has come back round (30 frames of
# G2 is 48.97 periods). That holds when both players start on the same audio
# frame, which is what sending the two play_slot back to back gives.
#
# Each file has to fit the inline load: the app message carrying it is at
# most 176 bytes (FMRB_MAX_MSG_PAYLOAD_SIZE) and the Hash around the bytes
# takes 28 of them, so a file may be up to INLINE_MAX bytes. The generator
# refuses to write a larger one.
#
# Usage:
#   ruby tools/boot_chime.rb [OUT_DIR]     (default: flash/boot)
#   ruby tools/midi/fmsq_dump.rb flash/boot/boot_chime_main.fmsq --notes

CPU_CLOCK = 1_789_773
INLINE_MAX = 148

NOTES = {
  "G2" => 98.00, "G3" => 196.00, "D4" => 293.66,
  "G4" => 392.00, "A4" => 440.00, "D5" => 587.33
}.freeze

# Duty: 0 = 12.5%, 1 = 25%, 2 = 50%, 3 = 75%.
DUTY_THIN = 1
DUTY_ROUND = 2

FRAME_FIFTH = 16    # 267 ms: the line spans the width, up to D4
FRAME_CHORD = 30    # 500 ms: fully open, the chord
FRAME_TRI_SUB_OFF = 54   # 900 ms: the second triangle stops (the bass halves)
FRAME_TRI_MAIN_OFF = 66  # 1100 ms: the first one stops, under the pulses' tail
FRAME_END = 71      # 1183 ms: everything off (silence by 1190 ms)

# The lead-in, G3 then D4, as [frame, volume, duty] at full level: a
# one-frame rise, then a step brighter (thinner duty) while the line
# stretches; the fifth up rises the same way.
LEAD_ENV = [
  [0, 6, DUTY_ROUND], [1, 9, DUTY_ROUND], [8, 10, DUTY_THIN],
  [FRAME_FIFTH, 8, DUTY_THIN], [FRAME_FIFTH + 1, 11, DUTY_THIN]
].freeze

# The chord's envelope, as [frame offset from FRAME_CHORD, volume] at full
# level: a one-frame rise, a quick drop to a ringing level and a long tail
# that reaches 1 just before the end.
CHORD_ENV = [
  [0, 10], [1, 14], [3, 13], [6, 11], [10, 9], [15, 8], [20, 7],
  [25, 6], [30, 5], [34, 4], [37, 3], [39, 2]
].freeze

# Each pulse's level against LEAD_ENV / CHORD_ENV, in sixteenths. These are
# what sets the balance against the two triangles, which have no volume of
# their own: chosen from the band RMS of the rendered WAV, not by ear
# (doc/boot_splash/report/b2.md).
LEVEL = {
  lead_main: 7,   # G3 -> D4 (MAIN pulse 1, which then plays A4)
  lead_sub: 4,    # the same, a timer step apart (SUB pulse 1, then D5)
  d5: 8,          # the top, with a shallow vibrato
  g4: 8,          # the root (MAIN pulse 2)
  a4: 6,          # the colour note
  g4b: 6          # G4 again, a few cents apart, for width (SUB pulse 2)
}.freeze

class FmsqWriter
  REG = {
    p_vol: [0x00, 0x04], p_lo: [0x02, 0x06], p_hi: [0x03, 0x07],
    tri_linear: 0x08, tri_lo: 0x0A, tri_hi: 0x0B,
    noise_vol: 0x0C, noise_period: 0x0E, noise_len: 0x0F,
    status: 0x15
  }.freeze

  attr_reader :frames

  # Registers where writing the value already there does nothing, so the
  # write can be left out (a timer high byte or a length load restarts
  # something and is always kept).
  SAME_IS_NOOP = [0x00, 0x04, 0x02, 0x06, 0x0C].freeze

  def initialize
    @events = Hash.new { |h, k| h[k] = [] }
    @frames = 0
  end

  # Writes must be added in time order per register for the de-duplication.
  def write(frame, reg, value)
    @last ||= {}
    v = value & 0xFF
    return if SAME_IS_NOOP.include?(reg) && @last[reg] == v

    @last[reg] = v
    @events[frame] << [reg, v]
  end

  def self.pulse_timer(freq)
    (CPU_CLOCK / (16.0 * freq) - 1).round
  end

  def self.tri_timer(freq)
    (CPU_CLOCK / (32.0 * freq) - 1).round
  end

  # Pulse note: volume, then the timer. Writing the high byte restarts the
  # wave, so it is the attack. `detune` moves the timer by whole steps.
  def pulse(frame, ch, note, vol:, duty:, detune: 0)
    timer = self.class.pulse_timer(NOTES.fetch(note)) + detune
    pulse_vol(frame, ch, vol, duty)
    write(frame, REG[:p_lo][ch], timer & 0xFF)
    write(frame, REG[:p_hi][ch], 0x08 | ((timer >> 8) & 0x07))
  end

  # Constant volume, length counter halted: the note holds until changed.
  def pulse_vol(frame, ch, vol, duty)
    write(frame, REG[:p_vol][ch], (duty << 6) | 0x30 | vol.clamp(0, 15))
  end

  # Only the low timer byte: a pitch bend that does not restart the wave.
  def pulse_lo(frame, ch, timer)
    write(frame, REG[:p_lo][ch], timer & 0xFF)
  end

  def triangle(frame, note)
    timer = self.class.tri_timer(NOTES.fetch(note))
    write(frame, REG[:tri_linear], 0xFF)
    write(frame, REG[:tri_lo], timer & 0xFF)
    write(frame, REG[:tri_hi], 0x08 | ((timer >> 8) & 0x07))
  end

  # period: 0 (highest) .. 15; short: the metallic 93-step mode.
  def noise(frame, period, vol, short: false)
    write(frame, REG[:noise_vol], 0x30 | vol)
    write(frame, REG[:noise_period], (short ? 0x80 : 0) | period)
    write(frame, REG[:noise_len], 0x08)
  end

  def noise_vol(frame, vol)
    write(frame, REG[:noise_vol], 0x30 | vol)
  end

  # Channel enables ($4015). Clearing a bit stops that channel at once; it
  # is the only way to stop a triangle.
  def status(frame, mask)
    write(frame, REG[:status], mask)
  end

  def finish(frame)
    @frames = frame
  end

  def bytes
    data = +"".b
    at = 0
    @events.keys.sort.each do |frame|
      data << wait_bytes(frame - at)
      at = frame
      @events[frame].each { |reg, v| data << [0xC0 | reg, v].pack("CC") }
    end
    data << wait_bytes(@frames - at)
    data << [0xFE].pack("C")
    header = ["FMSQ", 1, 0, @frames, data.bytesize, 0].pack("a4CCvvv")
    header + data
  end

  private

  def wait_bytes(frames)
    out = +"".b
    while frames > 0
      w = frames > 128 ? 128 : frames
      out << [w - 1].pack("C")
      frames -= w
    end
    out
  end
end

# Volume of a pulse at `level` sixteenths of the envelope value.
def scaled(vol, level)
  v = (vol * level + 8) / 16
  v < 1 ? 1 : v
end

# G3, then D4 a fifth up. `detune` moves the timer by whole steps.
def lead(w, level, detune)
  LEAD_ENV.each do |frame, vol, duty|
    v = scaled(vol, level)
    if frame.zero?
      w.pulse(frame, 0, "G3", vol: v, duty: duty, detune: detune)
    elsif frame == FRAME_FIFTH
      w.pulse(frame, 0, "D4", vol: v, duty: duty, detune: detune)
    else
      w.pulse_vol(frame, 0, v, duty)
    end
  end
end

def chord_voice(w, ch, note, level, duty, detune: 0)
  CHORD_ENV.each do |off, vol|
    v = scaled(vol, level)
    if off.zero?
      w.pulse(FRAME_CHORD, ch, note, vol: v, duty: duty, detune: detune)
    else
      w.pulse_vol(FRAME_CHORD + off, ch, v, duty)
    end
  end
end

# A shallow vibrato once the chord rings: the timer moves one step either
# way (about 9 cents at D5), a cycle every 10 frames (6 Hz).
def vibrato(w, ch, note, from, to)
  base = FmsqWriter.pulse_timer(NOTES.fetch(note))
  seq = [1, 0, -1, 0]
  i = 0
  f = from
  while f < to
    w.pulse_lo(f, ch, base + seq[i % 4])
    i += 1
    f += 2 + (i % 2) # 3, 2, 3, 2 frames
  end
end

def build_main
  w = FmsqWriter.new
  # The line lights: G3 over the G2 bass, and a soft hiss.
  lead(w, LEVEL[:lead_main], 0)
  w.triangle(0, "G2")
  w.noise(0, 3, 3)
  w.noise_vol(3, 2)
  w.noise_vol(6, 1)
  w.noise_vol(9, 0)
  # The chord: A4 and G4 here, D5 and the second G4 and G2 on SUB.
  chord_voice(w, 0, "A4", LEVEL[:a4], DUTY_ROUND)
  chord_voice(w, 1, "G4", LEVEL[:g4], DUTY_ROUND)
  # Channel enables: the triangle leaves first, then everything.
  w.status(FRAME_TRI_MAIN_OFF, 0x0B)
  w.status(FRAME_END, 0x00)
  w.finish(FRAME_END + 1)
  w
end

def build_sub
  w = FmsqWriter.new
  # The same G3 and D4 a timer step apart and quieter: the pair beats
  # slowly and sounds wider than one voice.
  lead(w, LEVEL[:lead_sub], 1)
  # The chord: D5 on top, a second G4 a few cents off, and G2 on the
  # second triangle (in phase with the first: see the top of this file).
  chord_voice(w, 0, "D5", LEVEL[:d5], DUTY_THIN)
  chord_voice(w, 1, "G4", LEVEL[:g4b], 0, detune: -1)
  w.triangle(FRAME_CHORD, "G2")
  vibrato(w, 0, "D5", FRAME_CHORD + 12, FRAME_END - 4)
  # A short sparkle of noise as it opens.
  w.noise(FRAME_CHORD, 1, 4, short: true)
  w.noise_vol(FRAME_CHORD + 4, 2)
  w.noise_vol(FRAME_CHORD + 9, 1)
  w.noise_vol(FRAME_CHORD + 14, 0)
  w.status(FRAME_TRI_SUB_OFF, 0x0B)
  w.status(FRAME_END, 0x00)
  w.finish(FRAME_END + 1)
  w
end

if $PROGRAM_NAME == __FILE__
  out_dir = ARGV[0] || File.expand_path("../flash/boot", __dir__)
  { "boot_chime_main.fmsq" => build_main, "boot_chime_sub.fmsq" => build_sub }.each do |name, w|
    bin = w.bytes
    if bin.bytesize > INLINE_MAX
      abort "boot_chime: #{name} is #{bin.bytesize} bytes, over the inline limit of #{INLINE_MAX}"
    end
    path = File.join(out_dir, name)
    File.binwrite(path, bin)
    puts "#{path}: #{w.frames} frames (#{format('%.2f', w.frames / 60.0)} s), #{bin.bytesize} bytes"
  end
end
