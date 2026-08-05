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

# What tool/midi/smf2fmsq.rb writes for the same note, including its octave
# fold for notes below what the 11-bit timer can reach.
def raw_timer(note, divider)
  ideal = 440.0 * (2.0**((note - 69) / 12.0))
  (CPU_CLOCK / (divider.to_f * ideal) - 1).round
end

def offline_timer(note, divider)
  note += 12 while raw_timer(note, divider) > 0x7FF
  raw_timer(note, divider).clamp(divider == 16 ? 8 : 2, 0x7FF)
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

puts "one instant at a time (defer_voices / flush_voices)"

# A chord lands on one voice because a voice is monophonic. Sending each
# message as it arrives makes the release of C-E-G play E and then C for an
# instant each - notes that are not in the score, off the beat. Grouping the
# messages of one musical instant fixes that without touching the fallback
# that joins overlapping notes across instants (doc/midi/report/p7_5.md).
def chord_on(device, notes, channel: 0)
  notes.each { |n| device.note_on(n, 100, channel: channel) }
end

def chord_off(device, notes, channel: 0)
  notes.each { |n| device.note_off(n, 0, channel: channel) }
end

device, audio = new_device
transport = device.transport
check("a chord struck in one instant sounds once, on the last note") do
  transport.defer_voices
  chord_on(device, [60, 64, 67])
  transport.flush_voices
  ons = audio.calls.select { |c| c.kind == :on }
  [audio.calls.size == 1 && ons.first.freq == FmrbMidi::PULSE_FREQ[67],
   "#{audio.calls.size} calls, #{ons.map(&:freq).inspect}"]
end

check("releasing it in one instant stops the voice without playing the rest") do
  audio.clear
  transport.defer_voices
  chord_off(device, [67, 64, 60])
  transport.flush_voices
  # The bug: an :on for 64 and another for 60 before the :off.
  [audio.calls.size == 1 && audio.calls.first.kind == :off,
   audio.calls.map { |c| "#{c.kind}#{c.freq}" }.inspect]
end

check("releasing part of a chord hands the voice to what is still held") do
  audio.clear
  transport.defer_voices
  chord_on(device, [60, 64, 67])
  transport.flush_voices
  audio.clear
  transport.defer_voices
  device.note_off(67, 0, channel: 0)
  transport.flush_voices
  [audio.calls.size == 1 && audio.calls.first.kind == :on &&
     audio.calls.first.freq == FmrbMidi::PULSE_FREQ[64],
   audio.calls.map { |c| "#{c.kind}#{c.freq}" }.inspect]
end

device, audio = new_device
transport = device.transport
check("legato across two instants still falls back to the held note") do
  transport.defer_voices
  device.note_on(60, 100, channel: 0)
  transport.flush_voices
  transport.defer_voices
  device.note_on(64, 100, channel: 0)
  transport.flush_voices
  audio.clear
  transport.defer_voices
  device.note_off(64, 0, channel: 0)   # a later instant: 60 is still down
  transport.flush_voices
  [audio.calls.size == 1 && audio.calls.first.kind == :on &&
     audio.calls.first.freq == FmrbMidi::PULSE_FREQ[60],
   audio.calls.map { |c| "#{c.kind}#{c.freq}" }.inspect]
end

device, audio = new_device
transport = device.transport
check("a note struck and released inside one instant leaves a longer one alone") do
  device.note_on(60, 100, channel: 0)  # sounding before the group
  audio.clear
  transport.defer_voices
  device.note_on(64, 100, channel: 0)
  device.note_off(64, 0, channel: 0)
  transport.flush_voices
  # 60 never stopped, so it must not be struck again.
  [audio.calls.empty?, audio.calls.map { |c| "#{c.kind}#{c.freq}" }.inspect]
end

check("a note repeated inside one instant is struck again") do
  audio.clear
  transport.defer_voices
  device.note_off(60, 0, channel: 0)
  device.note_on(60, 100, channel: 0)
  transport.flush_voices
  [audio.calls.size == 1 && audio.calls.first.kind == :on &&
     audio.calls.first.freq == FmrbMidi::PULSE_FREQ[60],
   audio.calls.map { |c| "#{c.kind}#{c.freq}" }.inspect]
end

device, audio = new_device
check("outside a group a note sounds immediately") do
  # An app playing by hand (a key press, a drum pad) must not wait for a
  # boundary that never comes.
  device.note_on(60, 100, channel: 0)
  sounded = audio.calls.size == 1 && audio.calls.first.kind == :on
  device.note_off(60, 0, channel: 0)
  [sounded && audio.calls.size == 2 && audio.calls.last.kind == :off,
   audio.calls.map { |c| "#{c.kind}#{c.freq}" }.inspect]
end

check("chords on separate channels are untouched by grouping") do
  device, audio = new_device
  transport = device.transport
  transport.defer_voices
  device.note_on(60, 100, channel: 0)
  device.note_on(64, 100, channel: 1)
  device.note_on(48, 100, channel: 2)
  transport.flush_voices
  voices = audio.calls.map(&:channel).sort
  [voices == [FmrbMidi::CH_PULSE1, FmrbMidi::CH_PULSE2, FmrbMidi::CH_TRIANGLE],
   voices.inspect]
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

puts "notes below the APU's range"

# The pulse timer is 11 bits, so the lowest note the channel can play is A1
# (33); the triangle reaches A0 (21). Below that the note is raised by whole
# octaves until it fits, which keeps its pitch class - a bass line still
# spells out the harmony instead of collapsing onto one held pitch, which is
# what clamping to the lowest timer used to do (doc/midi/report/p7_5.md).
PULSE_LOWEST = 33
TRIANGLE_LOWEST = 21

def folded_note(note, lowest)
  note += 12 while note < lowest
  note
end

check("the pulse channel plays down to A1 unchanged") do
  # 55 Hz is A1 itself; the note below it must not be given the same pitch.
  [FmrbMidi::PULSE_FREQ[PULSE_LOWEST] == 55 &&
     FmrbMidi::PULSE_FREQ[PULSE_LOWEST - 1] != FmrbMidi::PULSE_FREQ[PULSE_LOWEST],
   "A1 #{FmrbMidi::PULSE_FREQ[PULSE_LOWEST]} Hz, G#1 #{FmrbMidi::PULSE_FREQ[PULSE_LOWEST - 1]} Hz"]
end

check("notes below A1 fold up by octaves on the pulse channels") do
  bad = (0...PULSE_LOWEST).reject do |n|
    FmrbMidi::PULSE_FREQ[n] == FmrbMidi::PULSE_FREQ[folded_note(n, PULSE_LOWEST)]
  end
  [bad.empty?, "notes #{bad.inspect}"]
end

check("notes below A0 fold up by octaves on the triangle") do
  bad = (0...TRIANGLE_LOWEST).reject do |n|
    FmrbMidi::TRIANGLE_FREQ[n] == FmrbMidi::TRIANGLE_FREQ[folded_note(n, TRIANGLE_LOWEST)]
  end
  [bad.empty?, "notes #{bad.inspect}"]
end

check("the fold keeps the pitch class") do
  # Every folded note is a whole number of octaves from the note it plays,
  # so its frequency ratio to that note is a power of two.
  worst = 0.0
  (0...PULSE_LOWEST).each do |n|
    played = FmrbMidi::PULSE_FREQ[n]
    ratio = played / (440.0 * (2.0**((n - 69) / 12.0)))
    octaves = Math.log2(ratio)
    worst = [worst, (octaves - octaves.round).abs].max
  end
  # 1/100 of an octave is 12 cents, the whole-Hz resolution of the low end.
  [worst < 0.01, format("worst %.4f octave off a whole octave", worst)]
end

check("the offline converter folds the same way") do
  # Same tune, either path, same pitch: this is the P5-sim cross-check
  # extended to the notes that used to be clamped.
  gaps = (0...PULSE_LOWEST).map { |n| pitch_gap(n, 16, FmrbMidi::PULSE_FREQ).abs }
  [gaps.max < 12.0, format("worst %.1f cents", gaps.max)]
end

check("no note below the fold is left on the old clamped pitch") do
  # The bug this replaced: notes 0..32 all played 55 Hz.
  same = (0...PULSE_LOWEST).count { |n| FmrbMidi::PULSE_FREQ[n] == 55 }
  # Only the A naturals fold onto A1 itself.
  [same == (0...PULSE_LOWEST).count { |n| (n % 12) == 9 }, "#{same} notes still on 55 Hz"]
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
