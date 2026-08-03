#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Generates the MIDI note -> APU frequency argument tables used by
# FmrbMidi::ApuTransport (lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb).
#
# Why a table instead of computing 440 * 2**((n-69)/12.0) at run time:
#
# The real-time path hands a frequency in Hz to FmrbAudio#note_on, and the
# audio side turns it into an APU timer with *integer* division
# (apu_helper.c: `timer = APU_CPU_CLOCK / (16 * freq) - 1`). The offline
# path (tool/midi/smf2fmsq.rb) computes the timer directly with rounding.
# Feeding the rounded ideal frequency into the truncating divider lands on a
# different timer for many notes - about 8 cents apart - so the same tune
# would sound different depending on which path played it.
#
# This generator picks, for each note, the integer Hz value that makes the
# audio side arrive at exactly the timer the offline converter would have
# written. Where no integer Hz can reach that timer (high notes, where one
# Hz step moves the timer by more than one), it picks the value with the
# smallest pitch error and reports it.
#
# Usage:
#   ruby tool/midi/gen_apu_note_table.rb          # print the Ruby tables
#   ruby tool/midi/gen_apu_note_table.rb --check  # report mismatches only

CPU_CLOCK = 1_789_773

def ideal_frequency(note)
  440.0 * (2.0**((note - 69) / 12.0))
end

# What tool/midi/smf2fmsq.rb writes into the FMSQ stream.
def offline_timer(note, divider)
  timer = (CPU_CLOCK / (divider.to_f * ideal_frequency(note)) - 1).round
  timer.clamp(divider == 16 ? 8 : 2, 0x7FF)
end

# What apu_helper.c computes from a frequency argument (integer division).
def runtime_timer(freq, divider)
  return nil if freq <= 0

  t = (CPU_CLOCK / (divider * freq)) - 1
  t.clamp(0, 0xFFFF)
end

def frequency_of_timer(timer, divider)
  CPU_CLOCK / (divider.to_f * (timer + 1))
end

# Integer Hz that makes the audio side produce `target`, or the nearest
# reachable timer when no integer Hz hits it exactly.
#
# Timers above 0x7FF must never be produced: the audio side masks the high
# bits (`0xF8 | ((timer >> 8) & 0x07)`), so an overflowing timer wraps to a
# wildly wrong pitch instead of just being out of range. Below the lowest
# note the APU can play, both paths therefore sit on the same clamped timer.
def pick_frequency(note, divider)
  target = offline_timer(note, divider)
  ideal = ideal_frequency(note)
  base = (CPU_CLOCK / (divider.to_f * (target + 1))).round

  best = nil
  best_distance = nil
  ((base - 8)..(base + 8)).each do |freq|
    next if freq <= 0 || freq > 0xFFFF

    t = runtime_timer(freq, divider)
    next if t > 0x7FF # would wrap in the register write

    distance = (t - target).abs
    # Prefer the timer closest to what the offline converter writes; among
    # equals prefer the frequency closest to the ideal pitch.
    next unless best_distance.nil? || distance < best_distance ||
                (distance == best_distance && (freq - ideal).abs < (best - ideal).abs)

    best = freq
    best_distance = distance
  end

  cents = if best_distance.zero?
            0.0
          else
            got = frequency_of_timer(runtime_timer(best, divider), divider)
            1200.0 * Math.log2(got / frequency_of_timer(target, divider))
          end
  [best, target, cents]
end

def build(divider)
  rows = []
  mismatches = []
  (0..127).each do |note|
    freq, target, error = pick_frequency(note, divider)
    rows << freq
    mismatches << [note, freq, target, error] unless error.zero?
  end
  [rows, mismatches]
end

def format_table(name, rows, comment)
  out = +"    # #{comment}\n"
  out << "    #{name} = [\n"
  rows.each_slice(12).with_index do |slice, i|
    out << "      #{slice.map { |v| v.to_s.rjust(5) }.join(', ')},"
    out << "  # #{i * 12}\n"
  end
  out.sub!(/,(\s+# \d+\n)\z/, '\1')
  out << "    ].freeze\n"
  out
end

pulse, pulse_bad = build(16)
triangle, triangle_bad = build(32)

if ARGV.include?("--check")
  puts "pulse: #{pulse_bad.size} notes cannot reach the offline timer"
  pulse_bad.each { |n, f, t, e| printf("  note %3d freq=%5d want_timer=%4d diff=%+.2f cents vs offline path\n", n, f, t, e) }
  puts "triangle: #{triangle_bad.size} notes cannot reach the offline timer"
  triangle_bad.each { |n, f, t, e| printf("  note %3d freq=%5d want_timer=%4d diff=%+.2f cents vs offline path\n", n, f, t, e) }
  exit 0
end

puts format_table("PULSE_FREQ",
                  pulse,
                  "MIDI note -> Hz for the pulse channels (16x divider).")
puts
puts format_table("TRIANGLE_FREQ",
                  triangle,
                  "MIDI note -> Hz for the triangle channel (32x divider).")
