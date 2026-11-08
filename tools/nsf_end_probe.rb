#!/usr/bin/env ruby
# frozen_string_literal: true
#
# How the NSF player ends each song of some NSF files (doc/nsf_player_v2):
# by silence, by its loop (laps and period) or by the time limit. Plays every
# track on the host with the same player and APU the devices use
# (tools/nsf_end/probe.c, built here with the host cc), as fast as it can,
# and prints one row per track.
#
#   ruby tools/nsf_end_probe.rb [--md] [--max SECONDS] FILE.nsf|DIR ...
#
# With no file, the bundled tunes (flash/usr/share/sounds/nsf). --md prints a
# Markdown table. Needs gcc (or CC).

require "fileutils"
require "tmpdir"

ROOT = File.expand_path("..", __dir__)
APU = File.join(ROOT, "components/apu_emu")
FPS = 60

def build(dir)
  exe = File.join(dir, "nsf_end_probe")
  cc = ENV["CC"] || "gcc"
  ok = system(cc, "-O2", "-w", "-DFMRB_PLATFORM_HOSTED",
              "-I#{File.join(ROOT, 'tools/mahjong_sounds/stub')}",
              "-I#{File.join(ROOT, 'components/fmrb_common/include')}",
              "-I#{APU}/include", "-I#{APU}/src", "-I#{APU}/src/nofrendo",
              File.join(ROOT, "tools/nsf_end/probe.c"),
              "#{APU}/src/apu_if_p4.c", "#{APU}/src/nofrendo/nes_apu.c",
              "#{APU}/src/nes_cpu.c", "#{APU}/src/nsf_player.c", "-lm", "-o", exe)
  abort "nsf_end_probe: the build failed" unless ok
  exe
end

def files_of(args)
  args = [File.join(ROOT, "flash/usr/share/sounds/nsf")] if args.empty?
  args.flat_map do |a|
    File.directory?(a) ? Dir[File.join(a, "*.{nsf,NSF}")].sort : [a]
  end
end

def probe(exe, file, max_s)
  out = IO.popen([exe, file, "all", max_s.to_s], err: File::NULL, &:read)
  out.lines.grep(/^RESULT /).map do |l|
    l.split[1..].to_h { |kv| k, v = kv.split("=", 2); [k.to_sym, v] }
  end
end

def describe(r)
  case r[:reason]
  when "loop"
    period = r[:period].to_f
    laps = period.positive? ? 1 + (r[:run].to_f / period).floor : 0
    format("loop (%d laps, period %.1f s)", laps, period)
  when "silence" then "silence"
  when "limit" then "time limit"
  else "still playing"
  end
end

md = ARGV.delete("--md")
max_s = 600
if (i = ARGV.index("--max"))
  max_s = ARGV[i + 1].to_i
  ARGV.slice!(i, 2)
end

Dir.mktmpdir("nsf_end_probe") do |dir|
  exe = build(dir)
  totals = Hash.new(0)
  if md
    puts "| file | track | ended by | at (s) |"
    puts "|---|---|---|---|"
  end
  files_of(ARGV).each do |f|
    probe(exe, f, max_s).each do |r|
      totals[r[:reason]] += 1
      at = r[:end].to_f
      if md
        puts "| #{File.basename(f)} | #{r[:track]} | #{describe(r)} | #{format('%.1f', at)} |"
      else
        puts format("%-28s %3s  %-34s %7.1f s", File.basename(f), r[:track], describe(r), at)
      end
    end
  end
  puts
  puts "total: " + totals.map { |k, v| "#{k} #{v}" }.join(", ")
end
