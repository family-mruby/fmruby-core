#!/usr/bin/env ruby
# Generates bgm.fmsq for the Python breakout demo: a short driving loop,
# pulse2 for the arpeggio and triangle for the bass. Pulse1 is left free
# because the game's effects play there.
#
# FMSQ format (fmruby-graphics-audio components/apu_emu/include/fmsq_format.h):
#   Header (12B): "FMSQ" + ver + flags + frame_count + data_size + loop_offset
#   Commands:
#     0xxxxxxx        WAIT (n+1) frames at 60 Hz, n in 0..127
#     110aaaaa, val   REG_WRITE APU register at offset aaaaa
#     0xFE            END
#     0xFF, lo, hi    LOOP back to absolute byte offset in the data

CPU_FREQ = 1_789_773  # NTSC

REG_PULSE2_VOL = 0x04
REG_PULSE2_LO  = 0x06
REG_PULSE2_HI  = 0x07
REG_TRI_LINEAR = 0x08
REG_TRI_LO     = 0x0A
REG_TRI_HI     = 0x0B
REG_STATUS     = 0x15

FMSQ_CMD_LOOP = 0xFF

def freq_to_timer(freq)     = (CPU_FREQ / (16.0 * freq) - 1).round
# The triangle's timer counts at half the pulse rate, hence its own divider.
def freq_to_tri_timer(freq) = (CPU_FREQ / (32.0 * freq) - 1).round

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

def note_pulse2(freq, volume: 6, duty: 1)
  timer = freq_to_timer(freq)
  reg_write(REG_PULSE2_VOL, (duty << 6) | 0x30 | (volume & 0x0F)) +
    reg_write(REG_PULSE2_LO, timer & 0xFF) +
    reg_write(REG_PULSE2_HI, 0x08 | ((timer >> 8) & 0x07))
end

def note_tri(freq)
  timer = freq_to_tri_timer(freq)
  reg_write(REG_TRI_LINEAR, 0xFF) +
    reg_write(REG_TRI_LO, timer & 0xFF) +
    reg_write(REG_TRI_HI, 0x08 | ((timer >> 8) & 0x07))
end

N = {
  "A2" => 110.00, "C3" => 130.81, "D3" => 146.83, "E3" => 164.81,
  "F3" => 174.61, "G3" => 196.00, "A3" => 220.00,
  "C4" => 261.63, "D4" => 293.66, "E4" => 329.63, "F4" => 349.23,
  "G4" => 392.00, "A4" => 440.00, "B4" => 493.88,
  "C5" => 523.25, "E5" => 659.26, "G5" => 783.99, "A5" => 880.00,
}

# 32 steps at 8 frames each: about 4.3 seconds, fast enough to push the
# game along without demanding attention. Am - F - C - G, the arpeggio
# climbing over a walking bass.
STEP_FRAMES = 8
SEQUENCE = [
  ["A4", "A2"], ["C5", nil], ["E5", nil], ["C5", nil],
  ["A4", "A2"], ["C5", nil], ["E5", nil], ["A5", nil],
  ["F4", "F3"], ["A4", nil], ["C5", nil], ["A4", nil],
  ["F4", "F3"], ["A4", nil], ["C5", nil], ["E5", nil],
  ["C4", "C3"], ["E4", nil], ["G4", nil], ["E4", nil],
  ["C4", "C3"], ["E4", nil], ["G4", nil], ["C5", nil],
  ["G4", "G3"], ["B4", nil], ["D4", nil], ["B4", nil],
  ["G4", "G3"], ["B4", nil], ["D4", nil], ["G4", nil],
].freeze

data = "".b
# Pulse2 and triangle only: pulse1 belongs to the game's effects.
data << reg_write(REG_STATUS, 0x06)
loop_offset = data.bytesize

frame_count = 0
SEQUENCE.each do |lead, bass|
  data << note_pulse2(N.fetch(lead)) if lead
  data << note_tri(N.fetch(bass)) if bass
  data << wait(STEP_FRAMES)
  frame_count += STEP_FRAMES
end

data << [FMSQ_CMD_LOOP, loop_offset & 0xFF, (loop_offset >> 8) & 0xFF].pack("C3")

header = ["FMSQ", 1, 0,
          frame_count & 0xFFFF,
          data.bytesize & 0xFFFF,
          loop_offset & 0xFFFF].pack("a4CCvvv")

out_path = File.join(__dir__, "bgm.fmsq")
File.binwrite(out_path, header + data)

puts "Wrote #{out_path}: #{frame_count} frames " \
     "(#{(frame_count / 60.0).round(2)} s), #{data.bytesize} bytes data"
