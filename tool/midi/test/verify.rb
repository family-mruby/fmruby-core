#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Checks smf2fmsq.rb against the fixtures in this directory.
#
# Two layers:
#   1. Structural - convert each .mid and compare the decoded FMSQ events
#      (frame, voice, pitch, drum settings) against values worked out by hand
#      from the fixture. This layer always runs.
#   2. Acoustic - render scale.fmsq and tempo_change.fmsq with fmsq_player and
#      measure the WAV. This layer needs fmrb-audio-tools built
#      (rake build in ../../../fmrb-audio-tools) and is skipped when the
#      binary is missing.
#
# Expected pitches are computed from the APU's own timer arithmetic, not from
# smf2fmsq, so a mistake in the converter cannot cancel out in the comparison.
#
# Usage: ruby verify.rb [--keep]

require "tmpdir"
require "fileutils"
require_relative "../fmsq_dump"
require_relative "../wav_pitch"

module Verify
  CPU_CLOCK = 1_789_773
  TOOL_DIR = File.expand_path("..", __dir__)
  FIXTURE_DIR = __dir__
  PLAYER = ENV["FMSQ_PLAYER"] ||
           File.expand_path("../../../fmrb-audio-tools/bin/fmsq_player", TOOL_DIR)

  module_function

  def ideal_frequency(note)
    440.0 * (2.0**((note - 69) / 12.0))
  end

  # What the APU can actually produce for this note, after timer quantization.
  def apu_frequency(note, divider)
    timer = (CPU_CLOCK / (divider * ideal_frequency(note)) - 1).round
    CPU_CLOCK / (divider.to_f * (timer + 1))
  end

  def cents(measured, expected)
    1200.0 * Math.log2(measured / expected)
  end

  class Runner
    def initialize
      @failures = []
      @checks = 0
      @skipped = 0
    end

    attr_reader :failures, :checks, :skipped

    def check(label)
      @checks += 1
      ok, detail = yield
      if ok
        puts "  ok   #{label}#{detail ? " (#{detail})" : ''}"
      else
        puts "  FAIL #{label}: #{detail}"
        @failures << label
      end
    rescue StandardError => e
      puts "  FAIL #{label}: #{e.class}: #{e.message}"
      @failures << label
    end

    def skip(label, reason)
      @skipped += 1
      puts "  skip #{label} (#{reason})"
    end
  end

  def convert(runner, name, out_dir, args = [])
    input = File.join(FIXTURE_DIR, "#{name}.mid")
    output = File.join(out_dir, "#{name}.fmsq")
    cmd = ["ruby", File.join(TOOL_DIR, "smf2fmsq.rb"), input, "-o", output, "-q", *args]
    ok = system(*cmd, out: File::NULL)
    runner.check("#{name}: converts") { [ok && File.exist?(output), ok ? nil : "smf2fmsq failed"] }
    output
  end

  def notes_of(path)
    header, data = FmsqDump.read(path)
    [header, FmsqDump.notes(FmsqDump.writes(data))]
  end

  # --- fixtures -------------------------------------------------------------

  # C major scale, quarter notes at 120 BPM: one note every 30 frames.
  def check_scale(runner, out_dir)
    path = convert(runner, "scale", out_dir)
    _header, notes = notes_of(path)
    expected_notes = [60, 62, 64, 65, 67, 69, 71, 72]

    runner.check("scale: 8 pulse1 notes") do
      pulse = notes.select { |n| n.voice == :pulse1 }
      [pulse.size == 8, "got #{pulse.size}"]
    end

    pulse = notes.select { |n| n.voice == :pulse1 }
    expected_notes.each_with_index do |note, i|
      break if pulse[i].nil?

      expected_frame = i.zero? ? 1 : i * 30
      runner.check("scale: note #{i} frame") do
        [pulse[i].frame == expected_frame, "got #{pulse[i].frame}, want #{expected_frame}"]
      end
      want = Verify.apu_frequency(note, 16)
      runner.check("scale: note #{i} pitch") do
        diff = Verify.cents(pulse[i].frequency, want)
        [diff.abs < 1.0, format("%.2f Hz vs %.2f Hz (%+.2f cents)", pulse[i].frequency, want, diff)]
      end
    end

    runner.check("scale: velocity 100 maps to volume 12") do
      # Linear curve: 15 * 100 / 127 = 11.8 -> 12.
      [pulse.all? { |n| n.volume == 12 }, "got #{pulse.map(&:volume).uniq.inspect}"]
    end

    acoustic_scale(runner, path, expected_notes)
  end

  def acoustic_scale(runner, fmsq_path, expected_notes)
    unless File.executable?(PLAYER)
      runner.skip("scale: WAV pitch", "fmsq_player not built at #{PLAYER}")
      return
    end

    wav_path = fmsq_path.sub(/\.fmsq\z/, ".wav")
    unless system(PLAYER, fmsq_path, "-o", wav_path, out: File::NULL, err: File::NULL)
      runner.check("scale: renders to WAV") { [false, "fmsq_player failed"] }
      return
    end

    measured = WavPitch::Analyzer.new(WavPitch::Wav.new(wav_path)).notes
    runner.check("scale: WAV has 8 notes") { [measured.size == 8, "got #{measured.size}"] }

    measured.each_with_index do |note, i|
      break if i >= expected_notes.size

      want = Verify.apu_frequency(expected_notes[i], 16)
      runner.check("scale: WAV note #{i} pitch") do
        diff = Verify.cents(note.frequency, want)
        [diff.abs < 10.0, format("%.2f Hz vs %.2f Hz (%+.1f cents)", note.frequency, want, diff)]
      end
      runner.check("scale: WAV note #{i} start") do
        want_start = (i.zero? ? 1 : i * 30) / 60.0
        [(note.start - want_start).abs < 0.03,
         format("%.3f s vs %.3f s", note.start, want_start)]
      end
      runner.check("scale: WAV note #{i} length") do
        # Written as a 3/4-length quarter note: 0.375 s.
        [(note.duration - 0.375).abs < 0.04, format("%.3f s", note.duration)]
      end
    end
  end

  # Four quarter notes at 120 BPM then four at 240 BPM. Frame spacing must
  # halve, which only happens if Set Tempo is followed.
  def check_tempo_change(runner, out_dir)
    path = convert(runner, "tempo_change", out_dir)
    _header, notes = notes_of(path)
    pulse = notes.select { |n| n.voice == :pulse1 }
    want_frames = [1, 30, 60, 90, 120, 135, 150, 165]

    runner.check("tempo_change: 8 notes") { [pulse.size == 8, "got #{pulse.size}"] }
    runner.check("tempo_change: frames follow the tempo map") do
      got = pulse.map(&:frame)
      [got == want_frames, "got #{got.inspect}, want #{want_frames.inspect}"]
    end
    want = Verify.apu_frequency(69, 16)
    runner.check("tempo_change: all A4") do
      bad = pulse.reject { |n| Verify.cents(n.frequency, want).abs < 1.0 }
      [bad.empty?, "off pitch: #{bad.map { |n| n.frequency.round(2) }.inspect}"]
    end
  end

  # Overlapping notes on one channel plus two more channels.
  def check_chord(runner, out_dir)
    path = convert(runner, "chord", out_dir)
    _header, notes = notes_of(path)

    runner.check("chord: three voices sound together at frame 1") do
      first = notes.select { |n| n.frame == 1 }.map(&:voice).sort
      [first == %i[pulse1 pulse2 triangle], "got #{first.inspect}"]
    end

    pulse1 = notes.select { |n| n.voice == :pulse1 }
    runner.check("chord: last-note priority then fall back") do
      # C5 at frame 1, E5 takes over at frame 15, C5 returns at frame 30.
      want = [[1, 72], [15, 76], [30, 72]]
      got = pulse1.map { |n| [n.frame, (69 + (12 * Math.log2(n.frequency / 440.0))).round] }
      [got == want, "got #{got.inspect}, want #{want.inspect}"]
    end

    runner.check("chord: triangle plays C3") do
      tri = notes.find { |n| n.voice == :triangle }
      want = Verify.apu_frequency(48, 32)
      [tri && Verify.cents(tri.frequency, want).abs < 5.0,
       tri ? format("%.2f Hz vs %.2f Hz", tri.frequency, want) : "no triangle note"]
    end
  end

  # GM percussion: eighth-note hi-hats with kick and snare.
  def check_drums(runner, out_dir)
    path = convert(runner, "drums", out_dir)
    _header, notes = notes_of(path)
    hits = notes.select { |n| n.voice == :noise }

    runner.check("drums: 12 hits") { [hits.size == 12, "got #{hits.size}"] }
    runner.check("drums: hi-hats every 15 frames") do
      hats = hits.select { |n| n.period == 3 }.map(&:frame)
      want = [1, 15, 30, 45, 60, 75, 90, 105]
      [hats == want, "got #{hats.inspect}"]
    end
    runner.check("drums: kick on beats 1 and 3") do
      kicks = hits.select { |n| n.period == 13 }.map(&:frame)
      [kicks == [1, 60], "got #{kicks.inspect}"]
    end
    runner.check("drums: snare on beats 2 and 4") do
      snares = hits.select { |n| n.period == 8 }.map(&:frame)
      [snares == [30, 90], "got #{snares.inspect}"]
    end
    runner.check("drums: velocity separates kick from hi-hat") do
      kick = hits.find { |n| n.period == 13 }
      hat = hits.find { |n| n.period == 3 }
      [kick.volume > hat.volume, "kick=#{kick.volume} hat=#{hat.volume}"]
    end
  end

  # Format 0 keeps every channel in one track.
  def check_format0(runner, out_dir)
    path = convert(runner, "format0", out_dir)
    _header, notes = notes_of(path)
    runner.check("format0: one note per voice") do
      got = notes.map { |n| [n.frame, n.voice] }
      [got == [[1, :pulse1], [30, :pulse2]], "got #{got.inspect}"]
    end
  end

  # Options that change the output in a way worth pinning down.
  def check_options(runner, out_dir)
    path = convert(runner, "scale", out_dir + "/log", ["--vel-curve", "log"])
    _header, notes = notes_of(path)
    runner.check("--vel-curve log lifts quiet notes") do
      # Velocity 100 is volume 11 on the linear curve, 14 on the log one.
      [notes.first.volume == 14, "got #{notes.first.volume}"]
    end

    path = convert(runner, "chord", out_dir + "/map", ["--map", "1:tri,2:p1,3:p2"])
    _header, notes = notes_of(path)
    runner.check("--map reassigns voices") do
      voices = notes.select { |n| n.frame == 1 }.map(&:voice).sort
      [voices == %i[pulse1 pulse2 triangle], "got #{voices.inspect}"]
    end
    runner.check("--map sends channel 1 to the triangle") do
      tri = notes.find { |n| n.voice == :triangle }
      want = Verify.apu_frequency(72, 32)
      [tri && Verify.cents(tri.frequency, want).abs < 5.0,
       tri ? format("%.2f Hz vs %.2f Hz", tri.frequency, want) : "no triangle note"]
    end

    path = convert(runner, "scale", out_dir + "/loop", ["--loop"])
    header, = notes_of(path)
    runner.check("--loop sets a loop point") { [header.loop_offset.positive?, "got #{header.loop_offset}"] }

    path = convert(runner, "scale", out_dir + "/dur", ["-d", "1.0"])
    _header, notes = notes_of(path)
    runner.check("--duration truncates") do
      # Notes start at 0.0, 0.5 and 1.0 s; the cut is inclusive of 1.0 s.
      [notes.size == 3, "got #{notes.size} notes"]
    end
  end

  def run
    runner = Runner.new
    Dir.mktmpdir("smf2fmsq-verify") do |dir|
      %w[log map loop dur].each { |sub| FileUtils.mkdir_p(File.join(dir, sub)) }
      puts "scale.mid"
      check_scale(runner, dir)
      puts "tempo_change.mid"
      check_tempo_change(runner, dir)
      puts "chord.mid"
      check_chord(runner, dir)
      puts "drums.mid"
      check_drums(runner, dir)
      puts "format0.mid"
      check_format0(runner, dir)
      puts "options"
      check_options(runner, dir)
    end

    puts
    puts "#{runner.checks} checks, #{runner.failures.size} failed, #{runner.skipped} skipped"
    runner.failures.empty? ? 0 : 1
  end
end

exit(Verify.run) if $PROGRAM_NAME == __FILE__
