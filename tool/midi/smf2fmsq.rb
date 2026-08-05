#!/usr/bin/env ruby
# frozen_string_literal: true
#
# smf2fmsq - convert a Standard MIDI File into an FMSQ sequence.
#
# FMSQ is the frame indexed command stream the Family mruby audio side plays
# (see fmrb-audio-tools/doc/fmsq_format.md and
# components/apu_emu/include/fmsq_format.h). This converter emits REG_WRITE
# commands only: they carry the full APU state (duty, volume, sweep, noise
# period) and both FMSQ players implement them, so the result is unambiguous.
#
# The NES APU has four voices (two pulses, one triangle, one noise) while MIDI
# has sixteen channels, so the conversion is lossy by construction. Every
# reduction is a decision, and each one is spelled out in doc/midi/report/p1.md.
#
# Usage:
#   ruby smf2fmsq.rb input.mid [-o out.fmsq] [options]
#
# Standard library only (no gems), per the tooling policy in CLAUDE.md.

require "optparse"

module Smf2Fmsq
  # --- APU / FMSQ constants -------------------------------------------------

  CPU_CLOCK = 1_789_773 # NTSC 2A03
  FRAME_RATE = 60.0     # FMSQ ticks at 60 Hz

  REG_PULSE1_VOL   = 0x00
  REG_PULSE1_SWEEP = 0x01
  REG_PULSE1_LO    = 0x02
  REG_PULSE1_HI    = 0x03
  REG_PULSE2_VOL   = 0x04
  REG_PULSE2_SWEEP = 0x05
  REG_PULSE2_LO    = 0x06
  REG_PULSE2_HI    = 0x07
  REG_TRI_LINEAR   = 0x08
  REG_TRI_LO       = 0x0A
  REG_TRI_HI       = 0x0B
  REG_NOISE_VOL    = 0x0C
  REG_NOISE_PERIOD = 0x0E
  REG_NOISE_LEN    = 0x0F
  REG_STATUS       = 0x15

  STATUS_PULSE1   = 0x01
  STATUS_PULSE2   = 0x02
  STATUS_TRIANGLE = 0x04
  STATUS_NOISE    = 0x08

  CMD_END  = 0xFE
  CMD_LOOP = 0xFF
  MAX_WAIT = 128
  HEADER_SIZE = 12
  MAX_DATA_SIZE = 0xFFFF   # data_size is uint16
  MAX_FRAME_COUNT = 0xFFFF # frame_count is uint16

  VOICES = %i[pulse1 pulse2 triangle noise].freeze

  VOICE_ALIASES = {
    "p1" => :pulse1, "pulse1" => :pulse1,
    "p2" => :pulse2, "pulse2" => :pulse2,
    "tri" => :triangle, "triangle" => :triangle,
    "noise" => :noise, "drums" => :noise, "drum" => :noise,
    "-" => nil, "off" => nil, "none" => nil
  }.freeze

  # MIDI channel (1-based) -> APU voice. The default follows doc/midi/README.md.
  DEFAULT_MAP = { 1 => :pulse1, 2 => :pulse2, 3 => :triangle, 10 => :noise }.freeze

  # General MIDI percussion note -> noise settings.
  # period: 0 (highest pitch) .. 15 (lowest rumble), short: 93-step mode.
  # decay: how many frames the hit takes to fade out.
  # Anything not listed falls back to DRUM_DEFAULT.
  DRUM_MAP = {
    35 => { period: 13, short: false, decay: 8 },  # Acoustic Bass Drum
    36 => { period: 13, short: false, decay: 8 },  # Bass Drum 1
    37 => { period: 6,  short: true,  decay: 3 },  # Side Stick
    38 => { period: 8,  short: false, decay: 6 },  # Acoustic Snare
    39 => { period: 7,  short: false, decay: 6 },  # Hand Clap
    40 => { period: 8,  short: false, decay: 6 },  # Electric Snare
    41 => { period: 11, short: false, decay: 7 },  # Low Floor Tom
    43 => { period: 10, short: false, decay: 7 },  # High Floor Tom
    45 => { period: 10, short: false, decay: 6 },  # Low Tom
    47 => { period: 9,  short: false, decay: 6 },  # Low-Mid Tom
    48 => { period: 9,  short: false, decay: 6 },  # Hi-Mid Tom
    50 => { period: 8,  short: false, decay: 6 },  # High Tom
    42 => { period: 3,  short: true,  decay: 2 },  # Closed Hi-Hat
    44 => { period: 3,  short: true,  decay: 3 },  # Pedal Hi-Hat
    46 => { period: 2,  short: false, decay: 10 }, # Open Hi-Hat
    49 => { period: 1,  short: false, decay: 16 }, # Crash Cymbal 1
    51 => { period: 2,  short: false, decay: 12 }, # Ride Cymbal 1
    52 => { period: 1,  short: false, decay: 16 }, # Chinese Cymbal
    57 => { period: 1,  short: false, decay: 16 }  # Crash Cymbal 2
  }.freeze

  DRUM_DEFAULT = { period: 6, short: false, decay: 5 }.freeze

  class Error < StandardError; end

  # --- SMF parsing ----------------------------------------------------------

  # One MIDI event placed on the absolute tick timeline.
  Event = Struct.new(:tick, :track, :order, :kind, :channel, :a, :b)

  class SmfReader
    attr_reader :format, :division, :events

    def initialize(bytes)
      @bytes = bytes
      @pos = 0
      @events = []
      parse
    end

    # True when division is in SMPTE units instead of ticks per quarter note.
    def smpte?
      (@division & 0x8000) != 0
    end

    # Seconds per tick for SMPTE timing (tempo events do not apply there).
    def smpte_seconds_per_tick
      frames_per_second = 256 - ((@division >> 8) & 0xFF) # stored as a negative
      frames_per_second = 29.97 if frames_per_second == 29
      ticks_per_frame = @division & 0xFF
      1.0 / (frames_per_second * ticks_per_frame)
    end

    private

    def parse
      raise Error, "not a Standard MIDI File (missing MThd)" unless read(4) == "MThd"

      header_len = read_u32
      raise Error, "unexpected MThd length #{header_len}" if header_len < 6

      @format = read_u16
      track_count = read_u16
      @division = read_u16
      skip(header_len - 6)

      raise Error, "SMF format 2 is not supported (independent tracks)" if @format == 2
      raise Error, "unsupported SMF format #{@format}" if @format > 2

      track_count.times { |i| parse_track(i) }
      # Stable order: absolute tick, then track, then position inside the track.
      @events.sort_by! { |e| [e.tick, e.track, e.order] }
    end

    def parse_track(index)
      # Some files pad between chunks; skip anything that is not MTrk.
      loop do
        break if @pos >= @bytes.bytesize

        id = read(4)
        len = read_u32
        if id == "MTrk"
          parse_track_body(index, len)
          return
        end
        skip(len)
      end
    end

    def parse_track_body(index, len)
      track_end = @pos + len
      tick = 0
      status = nil
      order = 0

      while @pos < track_end
        tick += read_varlen
        byte = peek

        if byte >= 0x80
          current = read_u8
          # Channel messages arm running status; system messages clear it.
          status = current < 0xF0 ? current : nil
        else
          current = status
          raise Error, "running status without a preceding status byte" if current.nil?
        end

        case current
        when 0xFF
          type = read_u8
          data_len = read_varlen
          data = read(data_len)
          if type == 0x51 && data_len == 3
            usec = (data.getbyte(0) << 16) | (data.getbyte(1) << 8) | data.getbyte(2)
            @events << Event.new(tick, index, order, :tempo, nil, usec, nil)
            order += 1
          end
          # 0x2F (end of track) needs no event: the chunk length already bounds us.
        when 0xF0, 0xF7
          skip(read_varlen)
        else
          kind = current & 0xF0
          channel = (current & 0x0F) + 1 # 1-based, as musicians count them
          case kind
          when 0x80, 0x90, 0xA0, 0xB0, 0xE0
            a = read_u8
            b = read_u8
            if kind == 0x90
              # Note On with velocity 0 is the usual way to write Note Off.
              if b.zero?
                @events << Event.new(tick, index, order, :note_off, channel, a, 0)
              else
                @events << Event.new(tick, index, order, :note_on, channel, a, b)
              end
              order += 1
            elsif kind == 0x80
              @events << Event.new(tick, index, order, :note_off, channel, a, b)
              order += 1
            end
          when 0xC0, 0xD0
            read_u8
          else
            raise Error, format("unknown MIDI status 0x%02X at byte %d", current, @pos)
          end
        end
      end

      @pos = track_end
    end

    def peek
      byte = @bytes.getbyte(@pos)
      raise Error, "unexpected end of file" if byte.nil?

      byte
    end

    def read(n)
      raise Error, "unexpected end of file" if @pos + n > @bytes.bytesize

      s = @bytes.byteslice(@pos, n)
      @pos += n
      s
    end

    def read_u8
      byte = @bytes.getbyte(@pos)
      raise Error, "unexpected end of file" if byte.nil?

      @pos += 1
      byte
    end

    def read_u16
      (read_u8 << 8) | read_u8
    end

    def read_u32
      (read_u16 << 16) | read_u16
    end

    def read_varlen
      value = 0
      loop do
        byte = read_u8
        value = (value << 7) | (byte & 0x7F)
        break if (byte & 0x80).zero?
      end
      value
    end

    def skip(n)
      @pos += n
    end
  end

  # Converts absolute ticks into seconds, following Set Tempo events.
  class TempoMap
    def initialize(reader)
      @reader = reader
      if reader.smpte?
        @seconds_per_tick = reader.smpte_seconds_per_tick
        @segments = nil
        return
      end

      @ticks_per_quarter = reader.division
      raise Error, "division of 0 ticks per quarter note" if @ticks_per_quarter.zero?

      # [start_tick, start_seconds, seconds_per_tick]
      @segments = [[0, 0.0, 500_000 / 1e6 / @ticks_per_quarter]] # default 120 BPM
      seconds = 0.0
      last_tick = 0
      spt = @segments[0][2]

      reader.events.each do |e|
        next unless e.kind == :tempo

        seconds += (e.tick - last_tick) * spt
        last_tick = e.tick
        spt = e.a / 1e6 / @ticks_per_quarter
        @segments << [last_tick, seconds, spt]
      end
    end

    def seconds_at(tick)
      return tick * @seconds_per_tick if @segments.nil?

      # Last segment whose start tick is at or before this tick.
      index = @segments.bsearch_index { |s| s[0] > tick } || @segments.size
      seg = @segments[index - 1]
      seg[1] + (tick - seg[0]) * seg[2]
    end

    def frame_at(tick)
      (seconds_at(tick) * FRAME_RATE).round
    end
  end

  # --- Arrangement ----------------------------------------------------------

  # Collects APU register writes per frame.
  class FrameWriter
    attr_reader :frames

    def initialize
      @frames = Hash.new { |h, k| h[k] = [] }
    end

    def write(frame, offset, value)
      @frames[frame] << [offset, value & 0xFF]
    end

    def last_frame
      @frames.empty? ? 0 : @frames.keys.max
    end
  end

  # One monophonic APU voice fed by one MIDI channel.
  #
  # A voice can only sound one note, so overlapping notes are resolved with
  # last-note priority: the newest note takes the voice, and when it is
  # released the voice falls back to the most recent note still held. That is
  # what monophonic synthesizers do, and it keeps melodies intact when a file
  # has slight note overlaps (legato) instead of dropping every second note.
  class MonoVoice
    attr_reader :stolen

    def initialize(name, writer, options)
      @name = name
      @writer = writer
      @options = options
      @held = [] # [note, velocity], oldest first
      @current = nil
      @stolen = 0
    end

    def note_on(frame, note, velocity)
      @held.reject! { |n, _| n == note }
      @stolen += 1 unless @held.empty?
      @held << [note, velocity]
      sound(frame, note, velocity)
    end

    def note_off(frame, note)
      was_current = @current == note
      @held.reject! { |n, _| n == note }
      return unless was_current

      if @held.empty?
        silence(frame)
      else
        note2, velocity = @held.last
        sound(frame, note2, velocity)
      end
    end

    def finish(frame)
      silence(frame) unless @current.nil?
    end

    private

    def sound(frame, note, velocity)
      @current = note
      emit_on(frame, note, velocity)
    end

    def silence(frame)
      @current = nil
      emit_off(frame)
    end
  end

  class PulseVoice < MonoVoice
    def initialize(name, writer, options, index)
      super(name, writer, options)
      @vol_reg = index.zero? ? REG_PULSE1_VOL : REG_PULSE2_VOL
      @lo_reg  = index.zero? ? REG_PULSE1_LO : REG_PULSE2_LO
      @hi_reg  = index.zero? ? REG_PULSE1_HI : REG_PULSE2_HI
      @duty = index.zero? ? options[:duty1] : options[:duty2]
    end

    private

    def emit_on(frame, note, velocity)
      timer = Smf2Fmsq.pulse_timer(note)
      volume = Smf2Fmsq.velocity_to_volume(velocity, @options[:vel_curve])
      # $4000: duty, length halt (0x20), constant volume (0x10), volume.
      @writer.write(frame, @vol_reg, ((@duty & 0x03) << 6) | 0x30 | volume)
      @writer.write(frame, @lo_reg, timer & 0xFF)
      # Writing the high byte reloads the length counter and restarts the phase.
      @writer.write(frame, @hi_reg, 0xF8 | ((timer >> 8) & 0x07))
    end

    def emit_off(frame)
      @writer.write(frame, @vol_reg, 0x30) # constant volume 0
    end
  end

  # The triangle has no volume control, so velocity is ignored and the note is
  # silenced through the status register (immediate, unlike letting the linear
  # counter run down).
  class TriangleVoice < MonoVoice
    def initialize(name, writer, options, status)
      super(name, writer, options)
      @status = status
    end

    private

    def emit_on(frame, note, _velocity)
      timer = Smf2Fmsq.triangle_timer(note)
      @status.set(frame, STATUS_TRIANGLE, true)
      @writer.write(frame, REG_TRI_LINEAR, 0xFF) # halt flag + max linear counter
      @writer.write(frame, REG_TRI_LO, timer & 0xFF)
      @writer.write(frame, REG_TRI_HI, 0xF8 | ((timer >> 8) & 0x07))
    end

    def emit_off(frame)
      @status.set(frame, STATUS_TRIANGLE, false)
    end
  end

  # Percussion voice. Hits are one-shot: MIDI note-off is ignored and the hit
  # fades out over a few frames written by the converter. The APU's own
  # envelope would be cheaper, but it always starts at full volume, which
  # would throw away velocity.
  class NoiseVoice
    attr_reader :stolen

    def initialize(_name, writer, options)
      @writer = writer
      @options = options
      @stolen = 0
      @busy_until = -1
    end

    def note_on(frame, note, velocity)
      drum = DRUM_MAP.fetch(note, DRUM_DEFAULT)
      volume = Smf2Fmsq.velocity_to_volume(velocity, @options[:vel_curve])
      @stolen += 1 if frame < @busy_until

      period = (drum[:period] & 0x0F) | (drum[:short] ? 0x80 : 0x00)
      @writer.write(frame, REG_NOISE_PERIOD, period)
      @writer.write(frame, REG_NOISE_VOL, 0x30 | volume)
      @writer.write(frame, REG_NOISE_LEN, 0xF8)

      decay = [(drum[:decay] * @options[:drum_decay_scale]).round, 1].max
      steps = [volume, decay].min
      if steps.positive?
        (1..steps).each do |i|
          step_volume = volume - ((volume * i) / steps.to_f).round
          step_frame = frame + ((decay * i) / steps.to_f).round
          @writer.write(step_frame, REG_NOISE_VOL, 0x30 | [step_volume, 0].max)
        end
      end
      @busy_until = frame + decay
    end

    def note_off(_frame, _note); end

    def finish(frame)
      @writer.write(frame, REG_NOISE_VOL, 0x30)
    end
  end

  # Shadow of $4015 so channel enables can be toggled without losing the
  # other channels' state.
  class StatusRegister
    def initialize(writer)
      @writer = writer
      @value = STATUS_PULSE1 | STATUS_PULSE2 | STATUS_TRIANGLE | STATUS_NOISE
    end

    attr_reader :value

    def set(frame, bit, on)
      new_value = on ? (@value | bit) : (@value & ~bit)
      return if new_value == @value

      @value = new_value & 0xFF
      @writer.write(frame, REG_STATUS, @value)
    end
  end

  # --- Conversion helpers ---------------------------------------------------

  module_function

  def note_frequency(note)
    440.0 * (2.0**((note - 69) / 12.0))
  end

  # Notes too low for the 11-bit timer, raised by whole octaves until they
  # fit (pulse bottoms out at MIDI 33, triangle at MIDI 21). Clamping instead
  # would play a whole bass line on one pitch. Keep this identical to
  # playable_note in tool/midi/gen_apu_note_table.rb: the real-time path uses
  # that table, and the same tune has to sound the same whichever path plays
  # it (doc/midi/report/p7_5.md).
  def playable_note(note, divider)
    note += 12 while (CPU_CLOCK / (divider.to_f * note_frequency(note)) - 1).round > 0x7FF
    note
  end

  def pulse_timer(note)
    timer = (CPU_CLOCK / (16.0 * note_frequency(playable_note(note, 16))) - 1).round
    timer.clamp(8, 0x7FF) # below 8 the pulse channel is muted by hardware
  end

  def triangle_timer(note)
    timer = (CPU_CLOCK / (32.0 * note_frequency(playable_note(note, 32))) - 1).round
    timer.clamp(2, 0x7FF)
  end

  # MIDI velocity (1-127) -> APU volume (1-15).
  #
  # linear keeps the ratio of the velocity, which is what most chiptune
  # converters do and what makes accents in a drum part survive. log spreads
  # the quiet half of the range over more steps, which suits sustained parts
  # where the linear map makes everything below mezzo-forte inaudible.
  def velocity_to_volume(velocity, curve)
    return 0 if velocity <= 0

    value = case curve
            when :log
              15.0 * (Math.log(1.0 + (velocity / 127.0) * 9.0) / Math.log(10.0))
            else
              15.0 * (velocity / 127.0)
            end
    value.round.clamp(1, 15)
  end

  # --- FMSQ serialization ---------------------------------------------------

  def serialize(frame_writer, loop_frame: nil)
    data = +"".b
    frames = frame_writer.frames.keys.sort
    current = 0
    loop_offset = 0

    frames.each do |frame|
      gap = frame - current
      data << encode_wait(gap) if gap.positive?
      current = frame
      loop_offset = data.bytesize if loop_frame && loop_frame == frame && loop_offset.zero?
      frame_writer.frames[frame].each do |offset, value|
        data << [0xC0 | (offset & 0x1F), value & 0xFF].pack("CC")
      end
    end

    total_frames = current
    if loop_frame
      data << [CMD_LOOP, loop_offset & 0xFF, (loop_offset >> 8) & 0xFF].pack("C3")
    else
      data << [CMD_END].pack("C")
    end

    if data.bytesize > MAX_DATA_SIZE
      raise Error, "command data is #{data.bytesize} bytes, over the FMSQ limit of " \
                   "#{MAX_DATA_SIZE} (use --duration to shorten the piece)"
    end
    if total_frames > MAX_FRAME_COUNT
      raise Error, "sequence is #{total_frames} frames, over the FMSQ limit of #{MAX_FRAME_COUNT}"
    end

    header = ["FMSQ", 1, 0, total_frames & 0xFFFF, data.bytesize & 0xFFFF,
              loop_offset & 0xFFFF].pack("a4CCvvv")
    [header + data, total_frames]
  end

  def encode_wait(frames)
    out = +"".b
    while frames.positive?
      chunk = [frames, MAX_WAIT].min
      out << [(chunk - 1) & 0x7F].pack("C")
      frames -= chunk
    end
    out
  end

  # --- Driver ---------------------------------------------------------------

  class Converter
    attr_reader :stats

    def initialize(reader, options)
      @reader = reader
      @options = options
      @stats = {}
    end

    def channel_usage
      usage = Hash.new { |h, k| h[k] = { count: 0, pitch_sum: 0, min: 127, max: 0 } }
      @reader.events.each do |e|
        next unless e.kind == :note_on

        u = usage[e.channel]
        u[:count] += 1
        u[:pitch_sum] += e.a
        u[:min] = e.a if e.a < u[:min]
        u[:max] = e.a if e.a > u[:max]
      end
      usage
    end

    # Assign the busiest channels to voices, highest average pitch first.
    # Channel 10 is General MIDI percussion and always goes to the noise voice.
    def auto_map
      usage = channel_usage
      map = {}
      map[10] = :noise if usage.key?(10)
      melodic = usage.reject { |ch, _| ch == 10 }
                     .sort_by { |_, u| -u[:count] }
                     .first(3)
                     .sort_by { |_, u| -(u[:pitch_sum].to_f / u[:count]) }
      %i[pulse1 pulse2 triangle].each_with_index do |voice, i|
        ch = melodic[i]&.first
        map[ch] = voice if ch
      end
      map
    end

    def convert
      map = @options[:map] || (@options[:auto] ? auto_map : DEFAULT_MAP)
      writer = FrameWriter.new
      status = StatusRegister.new(writer)

      # Frame 0: put the APU into a known state. Sweep units are disabled once
      # here instead of on every note, which saves two bytes per note.
      writer.write(0, REG_STATUS, status.value)
      writer.write(0, REG_PULSE1_SWEEP, 0x00)
      writer.write(0, REG_PULSE2_SWEEP, 0x00)
      writer.write(0, REG_PULSE1_VOL, 0x30)
      writer.write(0, REG_PULSE2_VOL, 0x30)
      writer.write(0, REG_NOISE_VOL, 0x30)

      voices = {
        pulse1: PulseVoice.new(:pulse1, writer, @options, 0),
        pulse2: PulseVoice.new(:pulse2, writer, @options, 1),
        triangle: TriangleVoice.new(:triangle, writer, @options, status),
        noise: NoiseVoice.new(:noise, writer, @options)
      }

      tempo = TempoMap.new(@reader)
      limit_frame = @options[:duration] ? (@options[:duration] * FRAME_RATE).round : nil

      dropped = Hash.new(0)
      notes = 0
      last_frame = 0

      @reader.events.each do |e|
        next unless %i[note_on note_off].include?(e.kind)

        voice_name = map[e.channel]
        if voice_name.nil?
          dropped[e.channel] += 1 if e.kind == :note_on
          next
        end

        frame = tempo.frame_at(e.tick)
        break if limit_frame && frame > limit_frame

        # Frame 0 is reserved for the init writes above; a note landing there
        # would be emitted before them only by luck of insertion order.
        frame = 1 if frame.zero?
        last_frame = frame if frame > last_frame

        voice = voices.fetch(voice_name)
        if e.kind == :note_on
          voice.note_on(frame, e.a, e.b)
          notes += 1
        else
          voice.note_off(frame, e.a)
        end
      end

      # Release whatever is still sounding, one frame after the last event.
      tail = [last_frame + 1, writer.last_frame + 1].max
      voices.each_value { |v| v.finish(tail) }

      loop_frame = @options[:loop] ? 1 : nil
      binary, total_frames = Smf2Fmsq.serialize(writer, loop_frame: loop_frame)

      @stats = {
        map: map,
        notes: notes,
        dropped: dropped,
        stolen: voices.transform_values(&:stolen),
        frames: total_frames,
        seconds: total_frames / FRAME_RATE,
        bytes: binary.bytesize
      }
      binary
    end
  end

  def parse_map(spec)
    map = {}
    spec.split(",").each do |pair|
      channel, voice = pair.split(":", 2)
      raise Error, "bad --map entry #{pair.inspect} (expected CHANNEL:VOICE)" if voice.nil?

      ch = Integer(channel, exception: false)
      raise Error, "bad MIDI channel #{channel.inspect} in --map (1-16)" if ch.nil? || !(1..16).cover?(ch)

      key = voice.strip.downcase
      raise Error, "unknown voice #{voice.inspect} in --map" unless VOICE_ALIASES.key?(key)

      target = VOICE_ALIASES[key]
      map[ch] = target if target
    end
    map
  end

  def run(argv)
    options = {
      output: nil, map: nil, auto: false, duration: nil, vel_curve: :linear,
      duty1: 2, duty2: 2, drum_decay_scale: 1.0, loop: false, list: false, quiet: false
    }

    parser = OptionParser.new do |o|
      o.banner = "Usage: smf2fmsq.rb INPUT.mid [options]"
      o.on("-o", "--output PATH", "output file (default: INPUT.fmsq)") { |v| options[:output] = v }
      o.on("--map SPEC", "channel to voice map, e.g. 1:p1,2:p2,3:tri,10:drums") do |v|
        options[:map] = Smf2Fmsq.parse_map(v)
      end
      o.on("--auto", "pick channels automatically instead of the default map") { options[:auto] = true }
      o.on("--list", "print channel usage and exit") { options[:list] = true }
      o.on("-d", "--duration SEC", Float, "stop after SEC seconds") { |v| options[:duration] = v }
      o.on("--vel-curve NAME", %w[linear log], "velocity to volume curve (linear, log)") do |v|
        options[:vel_curve] = v.to_sym
      end
      o.on("--duty1 N", Integer, "pulse 1 duty 0-3 (default 2 = 50%)") { |v| options[:duty1] = v & 3 }
      o.on("--duty2 N", Integer, "pulse 2 duty 0-3 (default 2 = 50%)") { |v| options[:duty2] = v & 3 }
      o.on("--drum-decay SCALE", Float, "scale drum decay lengths (default 1.0)") do |v|
        options[:drum_decay_scale] = v
      end
      o.on("--loop", "loop back to the start instead of ending") { options[:loop] = true }
      o.on("-q", "--quiet", "print nothing on success") { options[:quiet] = true }
      o.on("-h", "--help", "show this help") do
        puts o
        return 0
      end
    end
    parser.parse!(argv)

    input = argv.shift
    if input.nil?
      warn parser.help
      return 1
    end

    bytes = File.binread(input)
    reader = SmfReader.new(bytes)
    converter = Converter.new(reader, options)

    if options[:list]
      puts "format=#{reader.format} division=#{reader.division}" \
           "#{reader.smpte? ? ' (SMPTE)' : ' ticks/quarter'}"
      puts "channel  notes  pitch(min-max)  average"
      converter.channel_usage.sort.each do |ch, u|
        printf("%7d  %5d  %3d-%-3d         %6.1f%s\n", ch, u[:count], u[:min], u[:max],
               u[:pitch_sum].to_f / u[:count], ch == 10 ? "  (GM percussion)" : "")
      end
      return 0
    end

    binary = converter.convert
    output = options[:output] || "#{File.basename(input, '.*')}.fmsq"
    File.binwrite(output, binary)

    unless options[:quiet]
      s = converter.stats
      voices = s[:map].sort.map { |ch, v| "ch#{ch}->#{v}" }.join(" ")
      puts "#{output}: #{s[:frames]} frames (#{format('%.2f', s[:seconds])} s), " \
           "#{s[:bytes]} bytes, #{s[:notes]} notes"
      puts "  voices: #{voices}"
      stolen = s[:stolen].reject { |_, n| n.zero? }
      puts "  overlapping notes resolved: #{stolen.map { |v, n| "#{v}=#{n}" }.join(' ')}" unless stolen.empty?
      unless s[:dropped].empty?
        dropped = s[:dropped].sort.map { |ch, n| "ch#{ch}=#{n}" }.join(" ")
        puts "  dropped (unmapped channels): #{dropped}"
      end
    end
    0
  rescue Error, Errno::ENOENT => e
    warn "smf2fmsq: #{e.message}"
    1
  end
end

exit(Smf2Fmsq.run(ARGV)) if $PROGRAM_NAME == __FILE__
