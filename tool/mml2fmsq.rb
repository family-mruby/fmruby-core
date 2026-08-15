#!/usr/bin/env ruby
# Convert an MML file into an FMSQ sequence for the APU.
#
# The input is the same file FmrbMidi::MmlPlayer reads (see round.mml):
# "bpm N", "velocity N" and "loop on|off" settings, '#' comments, and one
# part per remaining line, each part going to the next channel. Here the
# channels are the APU's own: part 1 -> pulse1, part 2 -> pulse2, part 3 ->
# triangle.
#
# Why both formats: MmlPlayer plays a tune by handing timed commands out of
# the app's update loop, which suits a music app but not a game -- the loop
# then belongs to the music. An FMSQ is handed to the audio task once and
# loops there, so a game can start it and forget it (this is what rpg_demo
# does). Writing the score once as MML and converting keeps a single source
# for both.
#
# The MML understood here is the subset MIDI::MML::Sequence defines: notes
# a-g with '+'/'#'/'-', rests 'r', lengths with dots, 'o' / '>' / '<',
# 'l', 'v'. Anything else aborts rather than being skipped, so this cannot
# quietly drift away from the parser on the device.
#
# FMSQ layout (fmruby-graphics-audio/components/apu_emu/include/fmsq_format.h):
#   Header (12B): "FMSQ" + ver + flags + frame_count + data_size + loop_offset
#   Commands: 0xxxxxxx WAIT (n+1) frames @60Hz / 110aaaaa,val REG_WRITE /
#             0xFE END / 0xFF,lo,hi LOOP to absolute offset
#
# Usage: ruby tool/mml2fmsq.rb in.mml [out.fmsq]

CPU_FREQ = 1_789_773  # NTSC
CLOCKS_PER_WHOLE = 96
CLOCKS_PER_QUARTER = CLOCKS_PER_WHOLE / 4
FRAMES_PER_MINUTE = 60 * 60  # 60 Hz video frames

REG = {
  pulse1_vol: 0x00, pulse1_lo: 0x02, pulse1_hi: 0x03,
  pulse2_vol: 0x04, pulse2_lo: 0x06, pulse2_hi: 0x07,
  tri_linear: 0x08, tri_lo: 0x0A, tri_hi: 0x0B,
  status: 0x15,
}
FMSQ_CMD_LOOP = 0xFF

NOTE_SEMITONE = { "c" => 0, "d" => 2, "e" => 4, "f" => 5, "g" => 7, "a" => 9, "b" => 11 }

# --- MML ------------------------------------------------------------------

# One part -> [{clock:, note: midi_or_nil, dur:, velocity:}]
class Part
  attr_reader :notes, :total_clocks

  def initialize(text, velocity, label)
    @text = text
    @label = label
    @pos = 0
    @octave = 4
    @length = 4
    @velocity = velocity
    @clock = 0
    @notes = []
    parse
    @total_clocks = @clock
  end

  private

  def fail!(msg)
    abort "#{@label}: #{msg} (at character #{@pos} of #{@text.inspect})"
  end

  def parse
    while @pos < @text.length
      ch = @text[@pos]
      @pos += 1
      case ch
      when " ", "\t" then next
      when "c", "d", "e", "f", "g", "a", "b" then read_note(ch)
      when "r" then read_rest
      when "o" then @octave = clamp(read_number(4), 1, 8)
      when ">" then @octave = clamp(@octave + 1, 1, 8)
      when "<" then @octave = clamp(@octave - 1, 1, 8)
      when "l" then @length = read_number(4)
      when "v" then @velocity = clamp(read_number(100), 0, 127)
      else fail!("unsupported MML character #{ch.inspect}")
      end
    end
  end

  def clamp(v, lo, hi)
    v < lo ? lo : (v > hi ? hi : v)
  end

  def read_note(ch)
    semitone = NOTE_SEMITONE[ch]
    case @text[@pos]
    when "+", "#" then semitone += 1; @pos += 1
    when "-"      then semitone -= 1; @pos += 1
    end
    dur = read_duration
    # Same numbering as the device parser: o4 c is middle C (MIDI 60).
    @notes << { clock: @clock, note: (@octave + 1) * 12 + semitone,
                dur: dur, velocity: @velocity }
    @clock += dur
  end

  def read_rest
    @clock += read_duration
  end

  # A length number with any number of dots; ties ('&') are not accepted
  # because nothing here needs them and silently mishandling one would be
  # worse than refusing it.
  def read_duration
    length = read_number(@length)
    dots = 0
    while @text[@pos] == "."
      dots += 1
      @pos += 1
    end
    fail!("length #{length} is not a divisor of a whole note") if length <= 0
    base = CLOCKS_PER_WHOLE / length
    total = base
    step = base / 2
    dots.times do
      total += step
      step /= 2
    end
    fail!("'&' ties are not supported by this converter") if @text[@pos] == "&"
    total
  end

  def read_number(default)
    start = @pos
    @pos += 1 while @pos < @text.length && @text[@pos] >= "0" && @text[@pos] <= "9"
    return default if start == @pos
    @text[start...@pos].to_i
  end
end

def load_mml(path)
  bpm = 120
  velocity = 100
  loop_on = true
  parts = []
  File.read(path).split("\n").each_with_index do |raw, lineno|
    line = raw.strip
    next if line.empty? || line.start_with?("#")
    key, arg = line.split(" ", 2)
    case key.downcase
    when "bpm"      then bpm = arg.to_i
    when "velocity" then velocity = arg.to_i
    when "loop"     then loop_on = %w[on yes true 1].include?(arg.to_s.strip.downcase)
    else
      parts << Part.new(line.downcase, velocity, "#{path}:#{lineno + 1}")
    end
  end
  abort "#{path}: no parts" if parts.empty?
  abort "#{path}: #{parts.size} parts, but the APU has 3 (pulse1, pulse2, triangle)" if parts.size > 3
  [parts, bpm, loop_on]
end

# --- FMSQ -----------------------------------------------------------------

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

def freq_of(midi_note)
  440.0 * (2.0**((midi_note - 69) / 12.0))
end

# The triangle counts at half the pulse rate, so the same note needs a
# different divider: apu_helper.c uses 16 for the pulses and 32 here. Using
# the pulse formula on the triangle sounds an octave low.
def timer_of(freq, channel)
  divider = channel == 2 ? 32.0 : 16.0
  (CPU_FREQ / (divider * freq) - 1).round
end

def note_on_bytes(channel, midi_note, velocity)
  freq = freq_of(midi_note)
  timer = timer_of(freq, channel)
  abort "note #{midi_note} (#{freq.round(1)} Hz) is out of the APU's range" if timer < 8 || timer > 0x7FF
  case channel
  when 0, 1
    vol = (velocity * 15 / 127)
    vol = 1 if vol < 1
    vol = 15 if vol > 15
    base = channel.zero? ? :pulse1_vol : :pulse2_vol
    lo   = channel.zero? ? :pulse1_lo : :pulse2_lo
    hi   = channel.zero? ? :pulse1_hi : :pulse2_hi
    # duty 2 (50%) for the lead, 1 (25%) for the second voice: two identical
    # squares in unison sound like one detuned one.
    duty = channel.zero? ? 2 : 1
    reg_write(REG[base], (duty << 6) | 0x30 | vol) +
      reg_write(REG[lo], timer & 0xFF) +
      reg_write(REG[hi], 0x08 | ((timer >> 8) & 0x07))
  else
    reg_write(REG[:tri_linear], 0xFF) +
      reg_write(REG[:tri_lo], timer & 0xFF) +
      reg_write(REG[:tri_hi], 0x08 | ((timer >> 8) & 0x07))
  end
end

def note_off_bytes(channel)
  case channel
  when 0 then reg_write(REG[:pulse1_vol], 0x30)
  when 1 then reg_write(REG[:pulse2_vol], 0x30)
  else        reg_write(REG[:tri_linear], 0x00)
  end
end

in_path = ARGV[0] or abort "usage: ruby tool/mml2fmsq.rb in.mml [out.fmsq]"
out_path = ARGV[1] || in_path.sub(/\.mml\z/, "") + ".fmsq"

parts, bpm, loop_on = load_mml(in_path)
abort "#{in_path}: bpm must be positive" if bpm <= 0

# Clock -> frame. Computed from the absolute clock every time rather than by
# accumulating per note, so rounding cannot drift over a long tune.
frames_per_whole = FRAMES_PER_MINUTE.to_f * 4 / bpm
to_frame = ->(clock) { (clock * frames_per_whole / CLOCKS_PER_WHOLE).round }

events = Hash.new { |h, k| h[k] = [] }
parts.each_with_index do |part, ch|
  part.notes.each do |n|
    on_frame = to_frame.call(n[:clock])
    off_frame = to_frame.call(n[:clock] + n[:dur])
    # Release one frame early so repeated notes are heard as separate notes
    # rather than one held one.
    off_frame -= 1 if off_frame - on_frame > 1
    events[on_frame] << note_on_bytes(ch, n[:note], n[:velocity])
    events[off_frame] << note_off_bytes(ch)
  end
end

total_frames = parts.map { |p| to_frame.call(p.total_clocks) }.max

data = "".b
# Enable exactly the channels the file uses.
status = 0
status |= 0x01 if parts.size >= 1
status |= 0x02 if parts.size >= 2
status |= 0x04 if parts.size >= 3
data << reg_write(REG[:status], status)

loop_offset = data.bytesize
cursor = 0
events.keys.sort.each do |frame|
  next if frame > total_frames
  data << wait(frame - cursor) if frame > cursor
  cursor = frame
  events[frame].each { |bytes| data << bytes }
end
data << wait(total_frames - cursor) if total_frames > cursor

if loop_on
  data << [FMSQ_CMD_LOOP, loop_offset & 0xFF, (loop_offset >> 8) & 0xFF].pack("C3")
else
  data << [0xFE].pack("C")
end

header = ["FMSQ", 1, 0,
          total_frames & 0xFFFF,
          data.bytesize & 0xFFFF,
          (loop_on ? loop_offset : 0) & 0xFFFF].pack("a4CCvvv")
File.binwrite(out_path, header + data)

puts "#{out_path}: #{parts.size} part(s), bpm #{bpm}, " \
     "#{total_frames} frames (#{(total_frames / 60.0).round(2)} s), " \
     "#{data.bytesize} B data, loop #{loop_on ? "at #{loop_offset}" : 'off'}"
