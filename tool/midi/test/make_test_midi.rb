#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Generates the Standard MIDI Files used to check smf2fmsq.rb.
#
# The files are deliberately plain so that every expected value (pitch,
# duration, frame position) can be written down by hand; see README.md in this
# directory. Run this script to regenerate them after changing the fixtures.

require "fileutils"

module TestMidi
  DIVISION = 480 # ticks per quarter note

  class Track
    def initialize
      @events = [] # [tick, bytes]
    end

    def note(tick_on, tick_off, channel, note, velocity)
      @events << [tick_on, [0x90 | (channel - 1), note, velocity]]
      @events << [tick_off, [0x80 | (channel - 1), note, 0x40]]
    end

    def tempo(tick, bpm)
      usec = (60_000_000.0 / bpm).round
      @events << [tick, [0xFF, 0x51, 0x03, (usec >> 16) & 0xFF, (usec >> 8) & 0xFF, usec & 0xFF]]
    end

    def to_chunk
      body = +"".b
      last = 0
      # Note offs must come before note ons at the same tick so a repeated
      # note is retriggered rather than cancelled.
      @events.sort_by.with_index { |(tick, bytes), i| [tick, bytes[0] & 0xF0 == 0x80 ? 0 : 1, i] }
             .each do |tick, bytes|
        body << TestMidi.varlen(tick - last)
        body << bytes.pack("C*")
        last = tick
      end
      body << TestMidi.varlen(0) << [0xFF, 0x2F, 0x00].pack("C*")
      "MTrk".b + [body.bytesize].pack("N") + body
    end
  end

  module_function

  def varlen(value)
    raise ArgumentError, "negative delta time" if value.negative?

    bytes = [value & 0x7F]
    value >>= 7
    while value.positive?
      bytes.unshift((value & 0x7F) | 0x80)
      value >>= 7
    end
    bytes.pack("C*")
  end

  def write(path, tracks, format: 1)
    header = "MThd".b + [6].pack("N") + [format, tracks.size, DIVISION].pack("nnn")
    File.binwrite(path, header + tracks.map(&:to_chunk).join)
    puts "wrote #{path} (#{File.size(path)} bytes)"
  end

  # 1. A C major scale on channel 1, one quarter note each at 120 BPM.
  #    Quarter note = 0.5 s = 30 frames. Pitch and length are both checkable.
  def scale(dir)
    t = Track.new
    t.tempo(0, 120)
    [60, 62, 64, 65, 67, 69, 71, 72].each_with_index do |note, i|
      on = i * DIVISION
      # Release slightly early so consecutive notes are separable in the WAV.
      t.note(on, on + (DIVISION * 3 / 4), 1, note, 100)
    end
    write(File.join(dir, "scale.mid"), [t])
  end

  # 2. Tempo change: four quarter notes at 120 BPM then four at 240 BPM.
  #    Verifies that the converter follows Set Tempo instead of assuming one.
  def tempo_change(dir)
    t = Track.new
    t.tempo(0, 120)
    t.tempo(4 * DIVISION, 240)
    8.times do |i|
      on = i * DIVISION
      t.note(on, on + (DIVISION * 3 / 4), 1, 69, 100) # A4 throughout
    end
    write(File.join(dir, "tempo_change.mid"), [t])
  end

  # 3. Three voices at once plus an overlapping note on channel 1, so the
  #    last-note-priority rule and the three melodic voices are both exercised.
  def chord(dir)
    lead = Track.new
    lead.tempo(0, 120)
    # Two overlapping notes on the same channel: the second must win while it
    # is held, then the voice falls back to the first one.
    lead.note(0, 2 * DIVISION, 1, 72, 100)
    lead.note(DIVISION / 2, DIVISION, 1, 76, 100)

    harmony = Track.new
    harmony.note(0, 2 * DIVISION, 2, 64, 90)

    bass = Track.new
    bass.note(0, 2 * DIVISION, 3, 48, 90)

    write(File.join(dir, "chord.mid"), [lead, harmony, bass])
  end

  # 4. A percussion pattern on channel 10 (GM drums).
  def drums(dir)
    t = Track.new
    t.tempo(0, 120)
    8.times do |i|
      on = i * (DIVISION / 2) # eighth notes
      t.note(on, on + 10, 10, 42, 80)                    # closed hi-hat
      t.note(on, on + 10, 10, 36, 110) if (i % 4).zero?  # kick on 1 and 3
      t.note(on, on + 10, 10, 38, 100) if i % 4 == 2     # snare on 2 and 4
    end
    write(File.join(dir, "drums.mid"), [t])
  end

  # 5. Format 0 (single track, all channels) to cover the other SMF layout.
  def format0(dir)
    t = Track.new
    t.tempo(0, 120)
    t.note(0, DIVISION, 1, 60, 100)
    t.note(DIVISION, 2 * DIVISION, 2, 67, 100)
    write(File.join(dir, "format0.mid"), [t], format: 0)
  end
end

dir = __dir__
FileUtils.mkdir_p(dir)
TestMidi.scale(dir)
TestMidi.tempo_change(dir)
TestMidi.chord(dir)
TestMidi.drums(dir)
TestMidi.format0(dir)
