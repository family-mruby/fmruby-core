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

puts "one instant at a time, on a real song"

# The APU has one voice per pulse channel, so a chord written on one MIDI
# channel lands on one voice. Before P7.5 the player sent every message as it
# came, and the release of a chord walked back down its inner notes - each
# one sounding for an instant, off the beat (doc/midi/report/p7_5.md). The
# player now groups the messages of one instant so the voice is set once.
#
# This measures it on the song the problem was reported on, by playing it
# twice: once as it is now, once through an output that does not offer the
# grouping, which is what the old code did.
SONG = File.join(ROOT, "flash/data/midi/joplin_entertainer.mid")

# Forwards what the player sends, minus the grouping hook, so voice_group_of
# finds nothing and every message takes effect as it arrives.
class UngroupedDevice
  def initialize(device)
    @device = device
  end

  def send_note_on(channel, note, velocity)
    @device.send_note_on(channel, note, velocity)
  end

  def send_note_off(channel, note)
    @device.send_note_off(channel, note)
  end

  def send_control_change(channel, cc, value)
    @device.send_control_change(channel, cc, value)
  end

  def send_program_change(channel, program)
    @device.send_program_change(channel, program)
  end
end

def play_entertainer(grouped)
  Machine.set(0)
  audio = FmrbAudio.new
  transport = FmrbMidi::ApuTransport.new(audio)
  device = MIDI::Device.new(transport)
  player = FmrbMidi::SmfPlayer.new(grouped ? device : UngroupedDevice.new(device))
  return nil unless player.load(SONG)

  transport.auto_map(player.channel_usage)
  player.start
  play_song(player, limit_ms: 180_000)
  audio.calls
end

if File.exist?(SONG)
  grouped = play_entertainer(true)
  ungrouped = play_entertainer(false)

  # What each voice was left doing at each instant, which is what a listener
  # hears; everything before it inside the same instant was a blip.
  def final_state(calls)
    state = {}
    calls.each { |c| state[[c.at, c.channel]] = [c.kind, c.freq] }
    state
  end

  check("every instant ends on the same note it used to") do
    # The property that makes this safe: grouping drops writes, but never
    # changes what the voice is left sounding.
    g = final_state(grouped)
    u = final_state(ungrouped)
    differing = g.keys.count { |k| g[k] != u[k] }
    [differing.zero?, "#{differing} of #{g.size} instants differ"]
  end

  check("the writes it drops are the ones nobody could hear") do
    saved = ungrouped.size - grouped.size
    [saved > 100,
     "#{ungrouped.size} writes to the APU became #{grouped.size} (-#{saved})"]
  end

  check("almost every instant still writes something") do
    # An instant with no write at all is one whose notes started and ended
    # inside it over a longer note that never stopped - a click, not a note.
    u = final_state(ungrouped)
    silent = u.keys.size - final_state(grouped).keys.size
    [silent * 20 < u.keys.size, "#{silent} of #{u.keys.size} instants write nothing"]
  end

  check("the same voices are used") do
    [grouped.map(&:channel).uniq.sort == ungrouped.map(&:channel).uniq.sort,
     grouped.map(&:channel).uniq.sort.inspect]
  end
else
  puts "  skip  #{SONG} is missing"
end

puts "scheduling ahead (the C queue, faked)"

# With a scheduler present the player stops sending notes and starts filing
# them: it decodes a few hundred milliseconds ahead and hands over commands
# stamped with the microsecond they are due, which a timer sends later
# (doc/midi/report/p7_6.md). The C queue is not here on the host, so this
# stands in for it and records what would have been filed.
#
# What has to hold: the commands are the same ones the immediate path
# produces, in the same order - the P7.5 chord resolution has moved from the
# tick boundary to the moment of filing, and must have survived the move -
# and their due times must not go backwards.
class FakeSched
  class << self
    attr_reader :cmds

    def reset
      @cmds = []
    end

    def _push_apu_note(due, ch, freq, vol, duty, sweep)
      @cmds << [due, :on, ch, freq, vol, duty, sweep]
      true
    end

    def _push_apu_off(due, ch)
      @cmds << [due, :off, ch]
      true
    end

    def _push_serial(due, len, b1, b2, b3)
      @cmds << [due, :serial, len, b1, b2, b3]
      true
    end

    def _clear
      @cmds = []
    end

    def _reset_stats
      nil
    end

    def _depth
      0
    end

    def _free
      128
    end

    def _now_us
      Machine.board_millis * 1000
    end

    def _stats
      { fired: 0, pushed: @cmds.size, dropped: 0, send_failed: 0,
        late_sum_us: 0, late_max_us: 0, depth_max: 0 }
    end
  end
end

# The player asks FmrbMidi for the scheduler once and remembers the answer;
# clear that so the fake is picked up.
def with_scheduler(sched)
  FmrbMidi.instance_variable_set(:@scheduler_checked, false)
  FmrbMidi.instance_variable_set(:@scheduler, sched)
  FmrbMidi.instance_variable_set(:@scheduler_checked, true)
  yield
ensure
  FmrbMidi.instance_variable_set(:@scheduler_checked, false)
  FmrbMidi.instance_variable_set(:@scheduler, nil)
end

if File.exist?(SONG)
  # What the immediate path plays, as a plain sequence of voice writes.
  immediate = grouped.map { |c| c.kind == :on ? [:on, c.channel, c.freq] : [:off, c.channel] }

  FakeSched.reset
  scheduled = nil
  with_scheduler(FakeSched) do
    Machine.set(0)
    audio = FmrbAudio.new
    transport = FmrbMidi::ApuTransport.new(audio)
    device = MIDI::Device.new(transport)
    player = FmrbMidi::SmfPlayer.new(device)
    player.load(SONG)
    transport.auto_map(player.channel_usage)
    player.start
    play_song(player, limit_ms: 180_000)
    scheduled = FakeSched.cmds
  end

  check("the player files commands instead of sending them") do
    sent_now = 0
    [scheduled.size > 400, "#{scheduled.size} filed, #{sent_now} sent directly"]
  end

  check("the filed commands are the ones the immediate path plays") do
    filed = scheduled.map do |c|
      c[1] == :on ? [:on, c[2], c[3]] : [:off, c[2]]
    end
    # When the clock stops the scheduled run is ahead by up to one lookahead,
    # so it has filed a few commands the immediate run never reached. Compare
    # what both got to, and check the excess is only that.
    n = [filed.size, immediate.size].min
    ahead = filed.size - immediate.size
    [filed[0, n] == immediate && ahead >= 0 && ahead <= 8,
     "#{n} commands identical, scheduled is #{ahead} ahead at the cut"]
  end

  check("due times never go backwards") do
    back = 0
    i = 1
    while i < scheduled.size
      back += 1 if scheduled[i][0] < scheduled[i - 1][0]
      i += 1
    end
    [back.zero?, "#{back} out of order"]
  end

  check("the song is filed ahead of itself, not on the beat") do
    # The point of the exercise: by the time the first second of music has
    # sounded, the player is already several hundred milliseconds ahead.
    span_us = scheduled.last[0] - scheduled.first[0]
    [span_us > 10_000_000, "#{span_us / 1_000_000} s of music filed"]
  end
else
  puts "  skip  #{SONG} is missing"
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
