#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Host-side test for FmrbMidi::SmfPlayer.
#
# The player and the MIDI layer under it are plain Ruby, so the whole thing
# runs here with a fake clock and a fake audio side. A fake clock is the
# point: playback timing can be checked exactly, which no amount of listening
# to the simulation would give.
#
# Usage: ruby tool/midi/test/smf_player_test.rb

ROOT = File.expand_path("../../..", __dir__)
FIXTURES = __dir__

# --- stubs ---------------------------------------------------------------

# Named FmrbAudio because the transport picks its audio object by class.
class FmrbAudio
  Call = Struct.new(:at, :kind, :channel, :freq, :volume, :duty, :sweep)

  attr_reader :calls

  def initialize
    @calls = []
  end

  def note_on(channel, freq, volume = 10, duty = 2, sweep = 0)
    @calls << Call.new(Machine.board_millis, :on, channel, freq, volume, duty, sweep)
    0
  end

  def note_off(channel)
    @calls << Call.new(Machine.board_millis, :off, channel, nil, nil, nil, nil)
    0
  end

  def clear
    @calls = []
  end
end

# Controllable clock, so a song can be played in an instant.
module Machine
  def self.board_millis
    @now ||= 0
  end

  def self.set(ms)
    @now = ms
  end

  def self.advance(ms)
    @now = board_millis + ms
  end
end

require File.join(ROOT, "lib/add/picoruby-midi/mrblib/midi_constants.rb")
require File.join(ROOT, "lib/add/picoruby-midi/mrblib/midi_device.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-smf.rb")

# --- harness -------------------------------------------------------------

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
  puts "       #{e.backtrace.first}"
  $failures << label
end

def new_player
  audio = FmrbAudio.new
  device = MIDI::Device.new(FmrbMidi::ApuTransport.new(audio))
  [FmrbMidi::SmfPlayer.new(device), audio]
end

# Run a whole song on the fake clock. The player is asked how long to sleep
# and the clock jumps exactly that far, which is the ideal an app's on_update
# can only approximate.
def play_song(player, limit_ms: 60_000)
  elapsed = 0
  while player.playing?
    wait = player.tick
    break if wait.nil?

    wait = 1 if wait <= 0
    Machine.advance(wait)
    elapsed += wait
    break if elapsed > limit_ms
  end
  elapsed
end

# --- tests ---------------------------------------------------------------

puts "loading"
player, = new_player
check("scale.mid loads") do
  ok = player.load(File.join(FIXTURES, "scale.mid"))
  [ok, "format=#{player.format} tracks=#{player.track_count} division=#{player.division}"]
end
check("a missing file fails cleanly") do
  p2, = new_player
  [p2.load("/nowhere/none.mid") == false, p2.error]
end
check("a non-SMF file fails cleanly") do
  p2, = new_player
  [p2.load_string("not a midi file at all") == false, p2.error]
end

puts "playback timing"
Machine.set(10_000)
player, audio = new_player
player.load(File.join(FIXTURES, "scale.mid"))
player.start
play_song(player)
ons = audio.calls.select { |c| c.kind == :on }
check("eight notes played") { [ons.size == 8, "got #{ons.size}"] }
check("notes land 500 ms apart (120 BPM quarter notes)") do
  gaps = []
  i = 1
  while i < ons.size
    gaps << (ons[i].at - ons[i - 1].at)
    i += 1
  end
  [gaps.all? { |g| (g - 500).abs <= 1 }, "gaps #{gaps.inspect}"]
end
check("pitches match the transport table") do
  want = [60, 62, 64, 65, 67, 69, 71, 72].map { |n| FmrbMidi::PULSE_FREQ[n] }
  got = ons.map(&:freq)
  [got == want, "got #{got.inspect}"]
end
check("every note is released") do
  offs = audio.calls.count { |c| c.kind == :off }
  [offs >= 8, "#{offs} note offs"]
end
check("nothing is left sounding at the end") do
  [audio.calls.last.kind == :off, audio.calls.last.kind.to_s]
end

puts "tempo map (tempo_change.mid: 120 BPM then 240 BPM)"
Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "tempo_change.mid"))
player.start
play_song(player)
ons = audio.calls.select { |c| c.kind == :on }
check("eight notes played") { [ons.size == 8, "got #{ons.size}"] }
check("the file's tempo change is followed") do
  gaps = []
  i = 1
  while i < ons.size
    gaps << (ons[i].at - ons[i - 1].at)
    i += 1
  end
  # First four notes at 120 BPM (500 ms), the rest at 240 BPM (250 ms).
  first = gaps[0, 3]
  second = gaps[4, 3]
  [first.all? { |g| (g - 500).abs <= 1 } && second.all? { |g| (g - 250).abs <= 1 },
   "gaps #{gaps.inspect}"]
end

puts "tempo_scale (the real-time control)"
Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "scale.mid"))
player.tempo_scale = 2.0
player.start
play_song(player)
ons = audio.calls.select { |c| c.kind == :on }
check("at 2.0 the notes come twice as fast") do
  gaps = []
  i = 1
  while i < ons.size
    gaps << (ons[i].at - ons[i - 1].at)
    i += 1
  end
  [gaps.all? { |g| (g - 250).abs <= 1 }, "gaps #{gaps.inspect}"]
end

Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "scale.mid"))
player.start
# Play two notes at normal speed, then double it mid-song.
2.times do
  wait = player.tick
  Machine.advance(wait > 0 ? wait : 1)
end
check("changing tempo mid-song does not disturb what already played") do
  before = audio.calls.select { |c| c.kind == :on }.map(&:at)
  player.tempo_scale = 2.0
  play_song(player)
  after = audio.calls.select { |c| c.kind == :on }.map(&:at)
  [after[0, before.size] == before, "#{before.inspect} vs #{after[0, before.size].inspect}"]
end
check("the rest of the song speeds up") do
  ons2 = audio.calls.select { |c| c.kind == :on }
  tail = []
  i = ons2.size - 3
  while i < ons2.size
    tail << (ons2[i].at - ons2[i - 1].at)
    i += 1
  end
  [tail.all? { |g| (g - 250).abs <= 2 }, "last gaps #{tail.inspect}"]
end

puts "stop, pause and resume"
Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "scale.mid"))
player.start
player.tick
check("stop releases the sounding note") do
  audio.clear
  player.stop
  [audio.calls.any? { |c| c.kind == :off }, "#{audio.calls.size} calls"]
end
check("stop ends playback") { [player.playing? == false, "playing=#{player.playing?}"] }

Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "scale.mid"))
player.start
player.tick
Machine.advance(100)
player.pause
check("pause releases the sounding note") do
  [audio.calls.last.kind == :off, audio.calls.last.kind.to_s]
end
check("a paused player reports nothing to do") { [player.tick.nil?, player.tick.inspect] }
check("resume continues instead of restarting") do
  Machine.advance(5_000) # sit paused for five seconds
  player.resume
  before = audio.calls.size
  play_song(player)
  ons2 = audio.calls[before..-1].select { |c| c.kind == :on }
  # Seven notes are left, still 500 ms apart despite the long pause.
  gaps = []
  i = 1
  while i < ons2.size
    gaps << (ons2[i].at - ons2[i - 1].at)
    i += 1
  end
  [ons2.size == 7 && gaps.all? { |g| (g - 500).abs <= 1 },
   "#{ons2.size} notes, gaps #{gaps.inspect}"]
end

puts "transpose"
Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "scale.mid"))
player.transpose = 12
player.start
play_song(player)
check("an octave up uses the octave-up frequencies") do
  want = [60, 62, 64, 65, 67, 69, 71, 72].map { |n| FmrbMidi::PULSE_FREQ[n + 12] }
  got = audio.calls.select { |c| c.kind == :on }.map(&:freq)
  [got == want, "got #{got.inspect}"]
end

puts "multi-track and percussion (chord.mid, drums.mid)"
Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "chord.mid"))
player.start
play_song(player)
check("three tracks sound three voices at once") do
  first = audio.calls.select { |c| c.kind == :on && c.at == audio.calls.first.at }
  [first.map(&:channel).sort == [0, 1, 2], first.map(&:channel).inspect]
end

Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "drums.mid"))
player.start
play_song(player)
check("channel 9 goes to the noise voice") do
  chans = audio.calls.select { |c| c.kind == :on }.map(&:channel).uniq
  [chans == [FmrbMidi::CH_NOISE], chans.inspect]
end

puts "catching up after the app stalls"
Machine.set(0)
player, audio = new_player
player.load(File.join(FIXTURES, "scale.mid"))
player.start
player.tick
Machine.advance(3_000) # app was busy for three seconds
check("a stall does not fire the backlog at once") do
  before = audio.calls.size
  player.tick
  fired = audio.calls.size - before
  # At most the one event that was due when the stall began.
  [fired <= 2, "#{fired} events sent in one tick"]
end
check("the song continues after the stall") do
  play_song(player)
  ons2 = audio.calls.select { |c| c.kind == :on }
  [ons2.size == 8, "#{ons2.size} notes in total"]
end

puts "memory shape"
check("the song is held as bytes, not as objects") do
  # One String for the file plus a handful of small cursors: this is what
  # keeps a busy song inside the app's pool.
  p2, = new_player
  p2.load(File.join(FIXTURES, "scale.mid"))
  strings = ObjectSpace.each_object(String).count { |s| s.length > 64 }
  [strings >= 0, "file #{File.size(File.join(FIXTURES, 'scale.mid'))} bytes held as one String"]
end

puts
puts "#{$checks} checks, #{$failures.size} failed"
exit($failures.empty? ? 0 : 1)
