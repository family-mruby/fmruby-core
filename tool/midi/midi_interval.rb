#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Measures how evenly notes arrive on the wire.
#
# Reads what tools/fmrb_midi_monitor.rb --log writes (one record per MIDI
# message, with the arrival time in seconds) and reports, per run, how far
# each gap between note ons strays from the gap the score asked for. The
# fixture to play through it is scale.mid: eight notes exactly half a second
# apart, so every deviation belongs to the machine.
#
# A run is a group of notes separated from the next by a long silence, which
# is how flash/app/debug/midi_time.app.rb marks its two passes: the first
# with the notes filed ahead for the C scheduler, the second sent from the
# app's own task the way the player did before P7.6.
#
# Usage:
#   ruby tool/midi/midi_interval.rb /tmp/p76.jsonl [expected_ms]

LOG = ARGV[0] or abort "usage: #{$PROGRAM_NAME} <monitor.jsonl> [expected_ms]"
EXPECTED_MS = (ARGV[1] || 500).to_f

# The monitor writes Ruby-ish JSON (symbol keys, no quotes); only two fields
# are needed, so pull them out rather than depend on a parser.
notes = []
File.foreach(LOG) do |line|
  next unless line.include?("kind:")

  at = line[/at:\s*([0-9.]+)/, 1]
  status = line[/status:\s*(\d+)/, 1]
  data = line[/data:\s*\[([^\]]*)\]/, 1]
  next if at.nil? || status.nil?

  velocity = data.to_s.split(",").map(&:strip)[1].to_i
  # Note on with a velocity: the moment a note starts is the only thing an
  # ear can time.
  next unless (status.to_i & 0xF0) == 0x90 && velocity > 0

  notes << at.to_f
end

abort "no note ons in #{LOG}" if notes.empty?

# Split into runs wherever the wire goes quiet for more than three expected
# gaps.
runs = []
current = [notes.first]
notes.each_cons(2) do |a, b|
  if (b - a) * 1000.0 > EXPECTED_MS * 3
    runs << current
    current = []
  end
  current << b
end
runs << current

runs.each_with_index do |run, index|
  next if run.size < 3

  gaps = run.each_cons(2).map { |a, b| (b - a) * 1000.0 }
  errors = gaps.map { |g| g - EXPECTED_MS }
  mean = errors.sum / errors.size
  spread = Math.sqrt(errors.map { |e| (e - mean)**2 }.sum / errors.size)

  printf("run %d: %2d notes  gap %.1f ms  error mean %+.2f ms  sd %.2f ms  worst %+.2f ms\n",
         index + 1, run.size, gaps.sum / gaps.size, mean, spread,
         errors.max_by(&:abs))
end

if runs.count { |r| r.size >= 3 } == 2
  a, b = runs.select { |r| r.size >= 3 }
  sd = lambda do |run|
    gaps = run.each_cons(2).map { |x, y| (y - x) * 1000.0 }
    errors = gaps.map { |g| g - EXPECTED_MS }
    mean = errors.sum / errors.size
    Math.sqrt(errors.map { |e| (e - mean)**2 }.sum / errors.size)
  end
  printf("\nrun 2 wobbles %.1fx as much as run 1\n", sd.call(b) / sd.call(a))
end
