#!/usr/bin/env ruby
# Generates bgm.fmsq for the RPG demo - a short, looping village-style
# theme on pulse1 (melody) + pulse2 (harmony) + triangle (bass).
#
# FMSQ format (see fmruby-graphics-audio/components/apu_emu/include/fmsq_format.h):
#   Header (12B): "FMSQ" + ver + flags + frame_count + data_size + loop_offset
#   Commands:
#     0xxxxxxx        WAIT (n+1) frames @ 60 Hz, n in 0..127
#     110aaaaa, val   REG_WRITE APU register at offset aaaaa
#     0xFE            END
#     0xFF, lo, hi    LOOP back to absolute byte offset (lo|hi<<8) in data
#
# loop_offset in header is also set so the player auto-loops without
# needing the 0xFF tail trigger, but we set both to be safe.

CPU_FREQ = 1_789_773  # NTSC

REG_PULSE1_VOL  = 0x00
REG_PULSE1_LO   = 0x02
REG_PULSE1_HI   = 0x03
REG_PULSE2_VOL  = 0x04
REG_PULSE2_LO   = 0x06
REG_PULSE2_HI   = 0x07
REG_TRI_LINEAR  = 0x08
REG_TRI_LO      = 0x0A
REG_TRI_HI      = 0x0B
REG_STATUS      = 0x15

FMSQ_CMD_END  = 0xFE
FMSQ_CMD_LOOP = 0xFF

def freq_to_timer(freq)
  (CPU_FREQ / (16.0 * freq) - 1).round
end

# The triangle's timer counts at half the pulse rate, so it needs its own
# divider: apu_helper.c uses 16 for the pulses and 32 here. This used to
# reuse freq_to_timer, which sounded the whole bass line an octave below
# the score.
def freq_to_tri_timer(freq)
  (CPU_FREQ / (32.0 * freq) - 1).round
end

def reg_write(offset, value)
  [0xC0 | (offset & 0x1F), value & 0xFF].pack("CC")
end

def wait(frames)
  out = "".b
  while frames > 0
    w = [frames, 128].min
    out << [(w - 1) & 0x7F].pack("C")
    frames -= w
  end
  out
end

def note_pulse(ch, freq, volume: 8, duty: 2)
  timer = freq_to_timer(freq)
  vol_reg = ch == 1 ? REG_PULSE1_VOL : REG_PULSE2_VOL
  lo_reg  = ch == 1 ? REG_PULSE1_LO  : REG_PULSE2_LO
  hi_reg  = ch == 1 ? REG_PULSE1_HI  : REG_PULSE2_HI
  vol_byte = (duty << 6) | 0x30 | (volume & 0x0F)
  reg_write(vol_reg, vol_byte) +
    reg_write(lo_reg, timer & 0xFF) +
    reg_write(hi_reg, 0x08 | ((timer >> 8) & 0x07))
end

def note_tri(freq)
  timer = freq_to_tri_timer(freq)
  reg_write(REG_TRI_LINEAR, 0xFF) +
    reg_write(REG_TRI_LO, timer & 0xFF) +
    reg_write(REG_TRI_HI, 0x08 | ((timer >> 8) & 0x07))
end

def silence_pulse(ch)
  reg_write(ch == 1 ? REG_PULSE1_VOL : REG_PULSE2_VOL, 0x30)
end

# Note table (just intonation 12-EDO).
N = {
  "C3" => 130.81, "D3" => 146.83, "E3" => 164.81, "F3" => 174.61,
  "G3" => 196.00, "A3" => 220.00, "B3" => 246.94,
  "C4" => 261.63, "D4" => 293.66, "E4" => 329.63, "F4" => 349.23,
  "G4" => 392.00, "A4" => 440.00, "B4" => 493.88,
  "C5" => 523.25, "D5" => 587.33, "E5" => 659.26, "F5" => 698.46,
  "G5" => 783.99, "A5" => 880.00,
}

# 16-step loop, ~3.3 s at 60 Hz (12 frames per step). Calm village pad
# made of pulse2 (harmony pad) + triangle (bass). Pulse1 melody is left
# out on purpose: the high square-wave lead was too shrill ("piropiro")
# under the rpg_demo context, so we keep only the soft accompaniment.
STEP_FRAMES = 12
SEQUENCE = [
  # bar 1: I (C major) - harmony chord-tone, triangle root
  ["E4", "C3"],
  ["G4", nil],
  ["C5", nil],
  ["G4", nil],
  # bar 2: IV (F major)
  ["A4", "F3"],
  ["C5", nil],
  ["C5", nil],
  ["A4", nil],
  # bar 3: V (G major)
  ["G4", "G3"],
  ["B4", nil],
  ["G4", nil],
  ["D4", nil],
  # bar 4: I again
  ["E4", "C3"],
  ["G4", nil],
  ["F4", nil],
  ["E4", nil],
].freeze

data = "".b

# Enable pulse2 + triangle only (no pulse1 lead).
data << reg_write(REG_STATUS, 0x06)

loop_offset = data.bytesize

frame_count = 0
SEQUENCE.each do |harm, bass|
  # duty 0 (12.5%) gives a soft flute-ish pad; volume 5 keeps it in the
  # background. triangle is naturally mellow so no level tweak there.
  data << note_pulse(2, N.fetch(harm), volume: 5, duty: 0) if harm
  data << note_tri(N.fetch(bass)) if bass
  data << wait(STEP_FRAMES)
  frame_count += STEP_FRAMES
end

# Explicit LOOP back to loop_offset (player also reads loop_offset in
# header, but emitting the tail trigger is harmless and self-documenting).
data << [FMSQ_CMD_LOOP, loop_offset & 0xFF, (loop_offset >> 8) & 0xFF].pack("C3")

header = ["FMSQ", 1, 0,
          frame_count & 0xFFFF,
          data.bytesize & 0xFFFF,
          loop_offset & 0xFFFF].pack("a4CCvvv")

out_path = File.join(__dir__, "bgm.fmsq")
File.binwrite(out_path, header + data)

puts "Wrote #{out_path}: #{frame_count} frames " \
     "(#{(frame_count / 60.0).round(2)} s), " \
     "#{data.bytesize} bytes data, loop_offset=#{loop_offset}"
