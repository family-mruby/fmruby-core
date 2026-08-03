#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Host-side test for the APU transport.
#
# lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb and the imported
# picoruby-midi Ruby layer are plain Ruby, so they can be loaded here with a
# stub in place of FmrbAudio. That gives a fast check of the mapping - which
# voice a channel lands on, what a chord does, how notes are stolen and given
# back - without building the firmware or booting the simulation.
#
# It also pins the pitch agreement with the offline converter: the frequency
# the transport sends must drive the APU to the same timer that
# tool/midi/smf2fmsq.rb writes into an FMSQ file, or the same tune would play
# at different pitches depending on the path.
#
# Usage: ruby tool/midi/test/transport_test.rb

ROOT = File.expand_path("../../..", __dir__)

# --- stubs ---------------------------------------------------------------

# Stands in for FmrbAudio: records what the transport asked the audio side
# to do instead of sending messages.
# Named FmrbAudio because the transport picks its audio object by class,
# not by duck typing (an app can have a note_on of its own).
class FmrbAudio
  Call = Struct.new(:kind, :channel, :freq, :volume, :duty, :sweep)

  attr_reader :calls

  def initialize
    @calls = []
  end

  def note_on(channel, freq, volume = 10, duty = 2, sweep = 0)
    @calls << Call.new(:on, channel, freq, volume, duty, sweep)
    0
  end

  def note_off(channel)
    @calls << Call.new(:off, channel, nil, nil, nil, nil)
    0
  end

  def clear
    @calls = []
  end

  def last
    @calls.last
  end
end

module Machine
  def self.board_millis
    @now ||= 0
  end

  def self.advance(ms)
    @now = board_millis + ms
  end
end

# The gems, in the order the firmware compiles them.
require File.join(ROOT, "lib/add/picoruby-midi/mrblib/midi_constants.rb")
require File.join(ROOT, "lib/add/picoruby-midi/mrblib/midi_device.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb")

# --- harness -------------------------------------------------------------

CPU_CLOCK = 1_789_773

$checks = 0
$failures = []

def check(label)
  $checks += 1
  ok, detail = yield
  if ok
    puts "  ok   #{label}#{detail ? " (#{detail})" : ''}"
  else
    puts "  FAIL #{label}: #{detail}"
    $failures << label
  end
rescue StandardError => e
  puts "  FAIL #{label}: #{e.class}: #{e.message}"
  $failures << label
end

def new_device
  audio = FmrbAudio.new
  [MIDI::Device.new(FmrbMidi::ApuTransport.new(audio)), audio]
end

# What the audio side (apu_helper.c) computes from a frequency argument.
def runtime_timer(freq, divider)
  (CPU_CLOCK / (divider * freq)) - 1
end

# What tool/midi/smf2fmsq.rb writes for the same note.
def offline_timer(note, divider)
  ideal = 440.0 * (2.0**((note - 69) / 12.0))
  (CPU_CLOCK / (divider.to_f * ideal) - 1).round.clamp(divider == 16 ? 8 : 2, 0x7FF)
end

# --- tests ---------------------------------------------------------------

puts "channel mapping"
device, audio = new_device
check("channel 0 goes to pulse 1") do
  device.note_on(60, 100, channel: 0)
  [audio.last.channel == FmrbMidi::CH_PULSE1, "APU channel #{audio.last.channel}"]
end
check("channel 1 goes to pulse 2") do
  device.note_on(64, 100, channel: 1)
  [audio.last.channel == FmrbMidi::CH_PULSE2, "APU channel #{audio.last.channel}"]
end
check("channel 2 goes to the triangle") do
  device.note_on(48, 100, channel: 2)
  [audio.last.channel == FmrbMidi::CH_TRIANGLE, "APU channel #{audio.last.channel}"]
end
check("channel 9 goes to the noise channel") do
  device.note_on(38, 100, channel: 9)
  [audio.last.channel == FmrbMidi::CH_NOISE, "APU channel #{audio.last.channel}"]
end
check("unmapped channels are dropped") do
  audio.clear
  device.note_on(60, 100, channel: 5)
  [audio.calls.empty?, "#{audio.calls.size} calls"]
end
check("map_channel reassigns") do
  device.transport.map_channel(5, :pulse1)
  audio.clear
  device.note_on(60, 100, channel: 5)
  [audio.last && audio.last.channel == FmrbMidi::CH_PULSE1, "#{audio.calls.size} calls"]
end

puts "notes"
device, audio = new_device
check("note off silences the voice") do
  device.note_on(60, 100, channel: 0)
  audio.clear
  device.note_off(60, 0, channel: 0)
  [audio.last.kind == :off && audio.last.channel == FmrbMidi::CH_PULSE1, audio.last.kind.to_s]
end
check("note on with velocity 0 is a note off") do
  device.note_on(60, 100, channel: 0)
  audio.clear
  device.note_on(60, 0, channel: 0)
  [audio.last.kind == :off, audio.last.kind.to_s]
end
check("velocity 100 maps to volume 12") do
  audio.clear
  device.note_on(60, 100, channel: 0)
  [audio.last.volume == 12, "volume #{audio.last.volume}"]
end
check("velocity 127 maps to volume 15") do
  audio.clear
  device.note_on(60, 127, channel: 0)
  [audio.last.volume == 15, "volume #{audio.last.volume}"]
end
check("a barely-touched key still sounds") do
  audio.clear
  device.note_on(60, 1, channel: 0)
  [audio.last.volume == 1, "volume #{audio.last.volume}"]
end

puts "overlapping notes on one channel"
device, audio = new_device
check("the newer note takes the voice") do
  device.note_on(60, 100, channel: 0)
  audio.clear
  device.note_on(64, 100, channel: 0)
  want = FmrbMidi::PULSE_FREQ[64]
  [audio.last.kind == :on && audio.last.freq == want, "freq #{audio.last.freq}, want #{want}"]
end
check("releasing it falls back to the held note") do
  audio.clear
  device.note_off(64, 0, channel: 0)
  want = FmrbMidi::PULSE_FREQ[60]
  [audio.last.kind == :on && audio.last.freq == want, "freq #{audio.last.freq}, want #{want}"]
end
check("releasing the last note stops the voice") do
  audio.clear
  device.note_off(60, 0, channel: 0)
  [audio.last.kind == :off, audio.last.kind.to_s]
end

puts "a chord needs one channel per voice"
device, audio = new_device
check("three channels sound three voices") do
  audio.clear
  device.note_on(60, 100, channel: 0)
  device.note_on(64, 100, channel: 1)
  device.note_on(67, 100, channel: 2)
  voices = audio.calls.map(&:channel).sort
  [voices == [FmrbMidi::CH_PULSE1, FmrbMidi::CH_PULSE2, FmrbMidi::CH_TRIANGLE], voices.inspect]
end

puts "percussion"
device, audio = new_device
check("the kick uses a low noise period") do
  audio.clear
  device.note_on(36, 110, channel: 9)
  [audio.last.freq == 13, "period argument #{audio.last.freq}"]
end
check("the closed hi-hat uses short mode") do
  audio.clear
  device.note_on(42, 110, channel: 9)
  # Bit 7 is the short-mode flag, the low nibble is the period.
  [audio.last.freq == (3 | 0x80), format("0x%02X", audio.last.freq)]
end

puts "control changes"
device, audio = new_device
check("CC 7 scales velocity") do
  device.control_change(7, 64, channel: 0)
  audio.clear
  device.note_on(60, 127, channel: 0)
  [audio.last.volume == 8, "volume #{audio.last.volume}"]
end
check("CC 70 selects the pulse duty") do
  device.control_change(70, 0, channel: 0)
  audio.clear
  device.note_on(60, 100, channel: 0)
  [audio.last.duty == 0, "duty #{audio.last.duty}"]
end
check("all notes off silences a sounding voice") do
  device.note_on(60, 100, channel: 0)
  audio.clear
  device.all_notes_off(channel: 0)
  [audio.last.kind == :off, audio.last.kind.to_s]
end

puts "trigger (scheduled note off)"
device, audio = new_device
check("trigger sounds the note immediately") do
  audio.clear
  device.trigger(60, 100, duration: 100, channel: 0)
  [audio.last.kind == :on, audio.last.kind.to_s]
end
check("the note keeps sounding before its time is up") do
  audio.clear
  Machine.advance(50)
  FmrbMidi.tick
  [audio.calls.empty?, "#{audio.calls.size} calls"]
end
check("pumping after the duration releases it") do
  audio.clear
  Machine.advance(60)
  FmrbMidi.tick
  [audio.last && audio.last.kind == :off, audio.calls.map(&:kind).inspect]
end

puts "pitch agreement with tool/midi/smf2fmsq.rb"

# The APU's pulse timer is 11 bits, so the lowest note it can play is around
# 55 Hz (MIDI 33). Below that both paths sit on the clamped timer and the
# pitch is whatever the hardware can do, so the comparison is only meaningful
# from C2 (36) upwards. At the top the frequency argument is in whole Hz,
# which is coarser than one timer step, leaving a small residual.
def pitch_gap(note, divider, table)
  got = runtime_timer(table[note], divider)
  want = offline_timer(note, divider)
  1200.0 * Math.log2((want + 1.0) / (got + 1))
end

check("pulse pitch matches the offline converter (C2-C7)") do
  gaps = (36..96).map { |n| pitch_gap(n, 16, FmrbMidi::PULSE_FREQ).abs }
  differing = gaps.count { |g| g > 0.01 }
  # A whole-Hz step is worth about 10 cents at C2 and well under 1 cent at
  # C6, so the low end sets the bound.
  [gaps.max < 12.0,
   format("%d of 61 notes differ, worst %.1f cents, mean %.1f",
          differing, gaps.max, gaps.sum / gaps.size)]
end

check("triangle pitch matches the offline converter (C2-C7)") do
  gaps = (36..96).map { |n| pitch_gap(n, 32, FmrbMidi::TRIANGLE_FREQ).abs }
  [gaps.max < 12.0, format("worst %.1f cents", gaps.max)]
end

check("notes below the APU's range clamp instead of wrapping") do
  # The audio side masks the timer to 11 bits, so an out-of-range note must
  # not be allowed to produce a timer that wraps into a high pitch.
  timers = (0..35).map { |n| runtime_timer(FmrbMidi::PULSE_FREQ[n], 16) }
  [timers.all? { |t| t.between?(0x700, 0x7FF) }, "timers #{timers.minmax.inspect}"]
end
check("no pulse timer overflows 11 bits") do
  bad = (0..127).reject { |n| runtime_timer(FmrbMidi::PULSE_FREQ[n], 16).between?(0, 0x7FF) }
  [bad.empty?, "notes #{bad.inspect}"]
end
check("no triangle timer overflows 11 bits") do
  bad = (0..127).reject { |n| runtime_timer(FmrbMidi::TRIANGLE_FREQ[n], 32).between?(0, 0x7FF) }
  [bad.empty?, "notes #{bad.inspect}"]
end

puts
puts "#{$checks} checks, #{$failures.size} failed"
exit($failures.empty? ? 0 : 1)
