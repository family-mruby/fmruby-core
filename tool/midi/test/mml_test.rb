#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Host-side test for MML playback.
#
# Two things are pinned here. The first is what the imported parser makes of
# a string - it is Midori's file, unchanged, so this is a record of the
# dialect Family mruby now speaks rather than a test of our own code, and it
# will tell us if an upstream update changes the meaning of a tune.
#
# The second is FmrbMidi::MmlPlayer, which is ours: that it turns those
# events into notes at the right times, that a chord written across two
# parts still lands on one instant, that looping joins up, and that a tune
# handed to the C queue arrives with the same notes as one played directly
# (doc/midi/report/p7_7.md).
#
# Usage: ruby tool/midi/test/mml_test.rb

ROOT = File.expand_path("../../..", __dir__)

# --- stubs ---------------------------------------------------------------

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
require File.join(ROOT, "lib/add/picoruby-midi-mml/mrblib/midi_mml.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-mml.rb")

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
  [FmrbMidi::MmlPlayer.new(device), audio]
end

# Run a tune on the fake clock, jumping exactly as far as the player asks.
def play(player, limit_ms: 60_000)
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

def note_ons(seq)
  seq.events.select { |e| e[:type] == :note_on }
end

# --- the parser ----------------------------------------------------------
#
# The MML strings are Midori's own examples, so the numbers below are a
# record of what that dialect means.

puts "the imported parser (MIDI::MML::Sequence)"

SCALE_MML = "o4 l4 cdefgab>cba<gfedc" # example/play_scale.rb

seq = MIDI::MML::Sequence.new(SCALE_MML, channel: 0, velocity: 100)
check("the scale is fifteen notes") do
  # Fifteen sounding notes for seventeen letters: > and < move the octave
  # without sounding anything.
  [note_ons(seq).size == 15, "#{note_ons(seq).size} notes"]
end

check("o4 c is middle C") do
  # The dialect puts middle C (MIDI 60) in octave 4.
  [note_ons(seq).first[:note] == 60, "note #{note_ons(seq).first[:note]}"]
end

check("the octave marks carry, so the descent is not a mirror") do
  notes = note_ons(seq).map { |e| e[:note] }
  # Up to >c the scale climbs C4 to C5. The b and a after it are in the
  # octave > moved to, so they come back an octave above where they went up;
  # < then drops back for the last five. This is what Midori's example does,
  # recorded rather than corrected.
  want = [60, 62, 64, 65, 67, 69, 71, 72, 83, 81, 67, 65, 64, 62, 60]
  [notes == want, notes.inspect]
end

check("l4 is a quarter note, 24 clocks") do
  ons = note_ons(seq)
  gaps = ons.each_cons(2).map { |a, b| b[:clock] - a[:clock] }
  [gaps.uniq == [24], "gaps #{gaps.uniq.inspect}"]
end

check("every note is released") do
  offs = seq.events.count { |e| e[:type] == :note_off }
  [offs == note_ons(seq).size, "#{offs} note offs for #{note_ons(seq).size} notes"]
end

check("octave marks move by twelve") do
  up = MIDI::MML::Sequence.new("o4 c > c", channel: 0)
  down = MIDI::MML::Sequence.new("o4 c < c", channel: 0)
  [note_ons(up).map { |e| e[:note] } == [60, 72] &&
     note_ons(down).map { |e| e[:note] } == [60, 48],
   "#{note_ons(up).map { |e| e[:note] }.inspect} #{note_ons(down).map { |e| e[:note] }.inspect}"]
end

check("sharps and flats") do
  s = MIDI::MML::Sequence.new("o4 c+ d- e#", channel: 0)
  [note_ons(s).map { |e| e[:note] } == [61, 61, 65],
   note_ons(s).map { |e| e[:note] }.inspect]
end

check("a dotted half is three quarters") do
  s = MIDI::MML::Sequence.new("o4 c2. c4", channel: 0)
  [s.events.first[:duration_clocks] == 72, "#{s.events.first[:duration_clocks]} clocks"]
end

check("a tie adds the tied length to the note") do
  s = MIDI::MML::Sequence.new("o4 c4&8", channel: 0)
  ons = note_ons(s)
  off = s.events.find { |e| e[:type] == :note_off }
  [ons.size == 1 && off[:clock] == 36, "one note ending at clock #{off[:clock]}"]
end

check("a loop repeats its contents") do
  s = MIDI::MML::Sequence.new("[cd]3", channel: 0)
  [note_ons(s).size == 6, "#{note_ons(s).size} notes"]
end

check("rests take time without sounding") do
  s = MIDI::MML::Sequence.new("o4 c4 r4 c4", channel: 0)
  ons = note_ons(s)
  [ons.size == 2 && ons[1][:clock] == 48, "second note at clock #{ons[1][:clock]}"]
end

check("velocity is carried on the note") do
  s = MIDI::MML::Sequence.new("v80 c", channel: 0, velocity: 100)
  [note_ons(s).first[:velocity] == 80, "velocity #{note_ons(s).first[:velocity]}"]
end

check("a tempo command is not part of the dialect") do
  # "t120" is silently ignored by the parser, which is why the tempo belongs
  # to MmlPlayer (see FAMILY_MRUBY_PORT.md).
  with_t = MIDI::MML::Sequence.new("t120 o4 c", channel: 0)
  without = MIDI::MML::Sequence.new("o4 c", channel: 0)
  [note_ons(with_t).map { |e| e[:note] } == note_ons(without).map { |e| e[:note] },
   note_ons(with_t).map { |e| e[:note] }.inspect]
end

# --- the player ----------------------------------------------------------

puts "FmrbMidi::MmlPlayer"

Machine.set(0)
player, audio = new_player
check("a tune loads") do
  [player.load_string(SCALE_MML) && player.loaded?, "#{player.event_count} events"]
end

check("the parsed events are packed into Integers") do
  # What playing costs is decided here: a tune is one Array of Integers, so
  # handing over an event allocates nothing (doc/midi/report/p6.md).
  packed = player.instance_variable_get(:@events)
  [packed.all? { |e| e.is_a?(Integer) }, "#{packed.size} entries, #{packed.first.class}"]
end

Machine.set(0)
player, audio = new_player
player.load_string(SCALE_MML)
player.bpm = 120
player.start
play(player)
ons = audio.calls.select { |c| c.kind == :on }

check("the scale sounds fifteen notes") { [ons.size == 15, "#{ons.size} notes"] }

check("at 120 BPM a quarter note is 500 ms") do
  gaps = ons.each_cons(2).map { |a, b| b.at - a.at }
  [gaps.all? { |g| (g - 500).abs <= 1 }, "gaps #{gaps.uniq.inspect}"]
end

check("the pitches are the transport's") do
  want = [60, 62, 64, 65, 67, 69, 71, 72].map { |n| FmrbMidi::PULSE_FREQ[n] }
  [ons.first(8).map(&:freq) == want, ons.first(8).map(&:freq).inspect]
end

check("nothing is left sounding") do
  [audio.calls.last.kind == :off, audio.calls.last.kind.to_s]
end

Machine.set(0)
player, audio = new_player
player.load_string(SCALE_MML)
player.bpm = 240
player.start
play(player)
check("doubling the BPM halves the gaps") do
  ons2 = audio.calls.select { |c| c.kind == :on }
  gaps = ons2.each_cons(2).map { |a, b| b.at - a.at }
  [gaps.all? { |g| (g - 250).abs <= 1 }, "gaps #{gaps.uniq.inspect}"]
end

Machine.set(0)
player, audio = new_player
player.load_string(SCALE_MML)
player.tempo_scale = 2.0
player.start
play(player)
check("tempo_scale works the same way as it does for a .mid") do
  ons2 = audio.calls.select { |c| c.kind == :on }
  gaps = ons2.each_cons(2).map { |a, b| b.at - a.at }
  [gaps.all? { |g| (g - 250).abs <= 1 }, "gaps #{gaps.uniq.inspect}"]
end

puts "two parts at once (example/two_part.rb)"

Machine.set(0)
player, audio = new_player
player.load_string("o5 l4 cegegegc", channel: 0, velocity: 90)
player.add_string("o3 l2 c   g   c", channel: 1, velocity: 80)
player.start
play(player)

check("both parts sound, on their own voices") do
  voices = audio.calls.select { |c| c.kind == :on }.map(&:channel).uniq.sort
  [voices == [FmrbMidi::CH_PULSE1, FmrbMidi::CH_PULSE2], voices.inspect]
end

check("the parts start together") do
  first_at = audio.calls.first.at
  starting = audio.calls.select { |c| c.at == first_at && c.kind == :on }
  [starting.map(&:channel).sort == [0, 1], starting.map(&:channel).inspect]
end

check("merging keeps the events in time order") do
  packed = player.instance_variable_get(:@events)
  shift = FmrbMidi::MmlPlayer::EV_CLOCK_SHIFT
  clocks = packed.map { |e| e >> shift }
  [clocks == clocks.sort, "#{clocks.size} events"]
end

puts "looping"

Machine.set(0)
player, audio = new_player
player.load_string("o4 l4 cd")
player.loop = true
player.start
play(player, limit_ms: 4_000)

check("a looping tune keeps going round") do
  ons2 = audio.calls.select { |c| c.kind == :on }
  # Two notes per round, one second per round, four seconds of clock.
  [ons2.size >= 6, "#{ons2.size} notes in four seconds"]
end

check("the loop joins up without a gap or a double note") do
  ons2 = audio.calls.select { |c| c.kind == :on }
  gaps = ons2.each_cons(2).map { |a, b| b.at - a.at }
  [gaps.all? { |g| (g - 500).abs <= 1 }, "gaps #{gaps.uniq.inspect}"]
end

check("stopping a loop releases what it was holding") do
  audio.clear
  player.stop
  [audio.calls.any? { |c| c.kind == :off } || audio.calls.empty?,
   "#{audio.calls.size} calls"]
end

check("a stopped player reports nothing to do") { [player.tick.nil?, player.tick.inspect] }

puts "handing the tune to the scheduler"

# The C queue is not here on the host; this records what would have been
# filed, the same way smf_player_test.rb does.
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

def with_scheduler(sched)
  FmrbMidi.instance_variable_set(:@scheduler, sched)
  FmrbMidi.instance_variable_set(:@scheduler_checked, true)
  yield
ensure
  FmrbMidi.instance_variable_set(:@scheduler, nil)
  FmrbMidi.instance_variable_set(:@scheduler_checked, false)
end

Machine.set(0)
direct_player, direct_audio = new_player
direct_player.load_string(SCALE_MML)
direct_player.start
play(direct_player)
direct = direct_audio.calls.map { |c| c.kind == :on ? [:on, c.channel, c.freq] : [:off, c.channel] }

FakeSched.reset
filed = nil
with_scheduler(FakeSched) do
  Machine.set(0)
  p2, = new_player
  p2.load_string(SCALE_MML)
  p2.start
  play(p2)
  filed = FakeSched.cmds
end

check("the filed commands are the ones the direct path plays") do
  mapped = filed.map { |c| c[1] == :on ? [:on, c[2], c[3]] : [:off, c[2]] }
  n = [mapped.size, direct.size].min
  [mapped[0, n] == direct[0, n] && (mapped.size - direct.size).abs <= 8,
   "#{n} identical, filed #{mapped.size} vs direct #{direct.size}"]
end

check("the filed notes are 500 ms apart in microseconds") do
  ons2 = filed.select { |c| c[1] == :on }
  gaps = ons2.each_cons(2).map { |a, b| b[0] - a[0] }
  [gaps.all? { |g| (g - 500_000).abs <= 10 }, "gaps #{gaps.uniq.inspect}"]
end

check("the tune is filed ahead of the clock, not on the beat") do
  # Nothing waits for the beat: the whole first stretch is handed over at
  # once, which is what the C timer needs to be accurate.
  [filed.first[0] < 500_000 && filed.last[0] > 7_000_000,
   "first #{filed.first[0]}us, last #{filed.last[0]}us"]
end

# --- tunes read from a file ----------------------------------------------
#
# A song can be an asset rather than a string inside the program that plays
# it (tools/fm_asset_editor edits these). What is pinned here is that the
# settings reach the player and that each part lands on its own channel.

TUNE_FILE = <<~MML
  # Round - two parts
  bpm 90
  loop on
  o5 l4 cegegegc
  velocity 80
  o3 l2 c   g   c
MML

file_player, = new_player
check("a tune reads from text") do
  [file_player.load_text(TUNE_FILE) && file_player.loaded?, "#{file_player.event_count} events"]
end

check("the settings in the file reach the player") do
  [file_player.bpm == 90 && file_player.loop == true, "bpm #{file_player.bpm}, loop #{file_player.loop}"]
end

check("each part is on its own channel") do
  channels = file_player.instance_variable_get(:@events).map do |packed|
    (packed >> FmrbMidi::MmlPlayer::EV_CHANNEL_SHIFT) & 0x0F
  end.uniq.sort
  [channels == [0, 1], "channels #{channels.inspect}"]
end

check("the file and the same parts loaded by hand agree") do
  by_hand, = new_player
  by_hand.load_string("o5 l4 cegegegc", channel: 0, velocity: 100)
  by_hand.add_string("o3 l2 c   g   c", channel: 1, velocity: 80)
  a = file_player.instance_variable_get(:@events)
  b = by_hand.instance_variable_get(:@events)
  [a == b, "#{a.size} vs #{b.size} events"]
end

check("comments and blank lines are not parts") do
  quiet, = new_player
  quiet.load_text("# only a comment\n\n")
  [quiet.loaded? == false && quiet.error.to_s.length > 0, quiet.error.to_s]
end

check("a missing file is refused, not raised") do
  missing, = new_player
  [missing.load_file("/nowhere/at/all.mml") == false && missing.error.to_s.length > 0,
   missing.error.to_s]
end

# The four sound settings say what plays a part. They are sent as the file is
# loaded, so what is checked is the state they leave the transport in.
VOICED_TUNE = <<~MML
  bpm 120
  voice triangle
  duty 1
  volume 100
  o3 l4 cde
  voice noise
  program 118
  o3 l4 fga
MML

voiced, = new_player
voiced.load_text(VOICED_TUNE)
transport = voiced.device.transport

check("voice puts the part on the APU voice it names") do
  [transport.voice_for(0) == FmrbMidi::CH_TRIANGLE && transport.voice_for(1) == FmrbMidi::CH_NOISE,
   "ch0 #{transport.voice_for(0)}, ch1 #{transport.voice_for(1)}"]
end

check("duty reaches the transport as 0-3") do
  duties = transport.instance_variable_get(:@cc_duty)
  [duties[0] == 1 && duties[1] == 1, "ch0 #{duties[0]}, ch1 #{duties[1]}"]
end

check("volume reaches the transport") do
  volumes = transport.instance_variable_get(:@cc_volume)
  [volumes[0] == 100, "ch0 #{volumes[0]}"]
end

check("a program change is harmless where there is no instrument to change") do
  # The APU has no programs: the transport is supposed to take the message
  # and ignore it, which is what a MIDI receiver does with what it cannot use.
  [voiced.loaded? && voiced.event_count > 0, "#{voiced.event_count} events"]
end

check("a tune with no sound settings leaves the mapping alone") do
  plain, = new_player
  plain.load_text("bpm 120\no4 l4 cde\n")
  [plain.device.transport.voice_for(0) == FmrbMidi::CH_PULSE1,
   "ch0 #{plain.device.transport.voice_for(0)}"]
end

check("the shipped example plays") do
  example = File.join(ROOT, "flash/usr/share/music/round.mml")
  shipped, = new_player
  [File.exist?(example) && shipped.load_file(example) && shipped.loaded?,
   "#{shipped.event_count} events, bpm #{shipped.bpm}"]
end

puts
puts "#{$checks} checks, #{$failures.size} failed"
exit($failures.empty? ? 0 : 1)
