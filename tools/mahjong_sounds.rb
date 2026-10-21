#!/usr/bin/env ruby
# frozen_string_literal: true
#
# mahjong_sounds.rb - the mahjong app's sound as files, made with Family
# mruby's own APU (for asterism-games' commentary page, which plays them).
#
#   ruby tools/mahjong_sounds.rb OUT_DIR            render into OUT_DIR
#   ruby tools/mahjong_sounds.rb --check OUT_DIR    render elsewhere and say
#                                                   which files of OUT_DIR differ
#
# Needs gcc and ffmpeg (with libmp3lame) on the host. Nothing is played by
# ear or written again by hand:
#
# - The effects and jingles are read from flash/app/game/mahjong/mahjong_sound.rb
#   (its SFX table, parsed as data, never evaluated) and played as the app
#   plays them (snd_begin: each voice's notes back to back from 0 ms, a
#   rest as a note off, a note off after the last note) through
#   components/apu_emu built for the host (tools/mahjong_sounds/apu_render.c:
#   the Tab5's apu_if_p4.c, the APU helpers, the nofrendo APU), on SUB as
#   audio_p4_engine_note_on does. The app's notes land between two audio
#   frames, so they are put on the frame at or after their time (1/60 s).
#   Each is a WAV, mono 16-bit at the APU's 15720 Hz.
# - The BGM is the app's scene tunes (its BGM table: the NSF and its track)
#   played by the same build's NSF player on MAIN at unity. The tune's loop
#   is found in the APU register writes (the frames repeat from S with a
#   period of P frames, checked over at least two periods to the end of the
#   render). The seam goes where the two ends sound most alike (the
#   oscillators keep their own phase across a loop, so frames that repeat
#   need not join cleanly; a quiet spot or matching phases does): the frame
#   S + k (k in 0.5 .. 10.5 s) whose half-frame on each side differs least
#   from the same at S + k + P. The file holds up to S + k + P and a
#   second more, and sounds.json gives the loop inside it (loop_start ..
#   loop_end, P frames long, at least half a second after S so a decoder's
#   start delay cannot push it out of the repeating part). An MP3 (24 kHz
#   mono, so a frame is a whole number of samples), which every browser
#   decodes.
# - sounds.json: each file's SHA-256 and size, the loop points, the app's
#   BGM levels and the scene -> tune table, and the SHA-256 of what they
#   were made from (the SFX table's text, the NSF files), so a later render
#   tells whether anything changed (--check).
#
# The license of the result: the effects are the app's author's (MIT in
# asterism-games by his decision); the BGM is shiru8bit's (CC BY 3.0,
# flash/usr/share/sounds/nsf/CREDITS.md) and keeps that license.

require "digest"
require "fileutils"
require "json"
require "open3"
require "tmpdir"

ROOT = File.expand_path("..", __dir__)
SOUND_RB = "flash/app/game/mahjong/mahjong_sound.rb"
NSF_DIR = "flash/usr/share/sounds/nsf"
APU = "components/apu_emu"
RATE = 15_720 # the APU's rate (apu_if_p4.c); 262 samples a frame
FPS = 60
MP3_RATE = 24_000
MP3_BITRATE = "64k"
RENDER_S = 720 # how long the tunes are played to find their loop (the longest is 154 s)
MIN_LOOP_S = 4
MARGIN_FRAMES = 60 # after the loop's end, in the file
SEAM_FRAMES = (30...630).freeze # where the loop may start, after S
# The tunes' titles (CREDITS.md of the NSF directory).
TITLES = {
  "after_the_rain.nsf" => "8-bit Chiptune - After The Rain",
  "spring_thing.nsf" => "8-bit Chiptune - Spring Thing",
  "encounter.nsf" => "8-bit Chiptune - Encounter"
}.freeze

def die(msg)
  warn "mahjong_sounds: #{msg}"
  exit 1
end

def run!(*cmd, stdin: nil)
  out, err, st = Open3.capture3(*cmd, stdin_data: stdin.to_s, binmode: true)
  die("#{cmd.first} failed: #{err.lines.last(5).join}") unless st.success?
  out
end

# ------------------------------------------------------------ reading the app

# The text of `NAME = ... .freeze` in mahjong_sound.rb (a Hash or Array literal).
def constant_text(src, name)
  start = src.index(/^\s*#{name} = /) or die("#{name} not found in #{SOUND_RB}")
  open_at = src.index(/[\[{]/, start)
  depth = 0
  i = open_at
  while i < src.size
    c = src[i]
    depth += 1 if c == "[" || c == "{"
    depth -= 1 if c == "]" || c == "}"
    break if depth.zero?
    i += 1
  end
  src[open_at..i]
end

# A literal of symbols' keys, integers, strings and nil as data (JSON),
# never evaluated: `name: [..]` -> `"name": [..]`.
def literal(text)
  json = text.gsub(/#[^\n"]*$/, "").gsub(/([a-z_][a-z0-9_]*):(?=\s)/, '"\1":')
  JSON.parse(json)
rescue JSON::ParserError => e
  die("cannot read a table of #{SOUND_RB} as data: #{e.message}")
end

def app_tables
  src = File.read(File.join(ROOT, SOUND_RB))
  sfx_text = constant_text(src, "SFX")
  sfx = literal(sfx_text)
  bgm = literal(constant_text(src, "BGM"))
  levels = literal(constant_text(src, "BGM_LEVELS"))
  names = literal(constant_text(src, "BGM_NAMES"))
  tense = src[/TENSE_LEFT = (\d+)/, 1] or die("TENSE_LEFT not found")
  default = src[/BGM_DEFAULT = (\d+)/, 1] or die("BGM_DEFAULT not found")
  [sfx, sfx_text, bgm, { "levels" => levels, "names" => names, "default" => default.to_i, "tense" => tense.to_i }]
end

# ------------------------------------------------------------ the renderer

def build_renderer(dir)
  exe = File.join(dir, "apu_render")
  a = File.join(ROOT, APU)
  run!("gcc", "-O2", "-w", "-I#{File.join(__dir__, 'mahjong_sounds', 'stub')}", "-I#{a}/include", "-I#{a}/src",
       "-I#{a}/src/nofrendo", "-Wl,--wrap=apuif_write_reg", File.join(__dir__, "mahjong_sounds", "apu_render.c"),
       "#{a}/src/apu_if_p4.c", "#{a}/src/apu_helper.c", "#{a}/src/nofrendo/nes_apu.c", "#{a}/src/nes_cpu.c",
       "#{a}/src/nsf_player.c", "-lm", "-o", exe)
  exe
end

def wav(samples_le)
  n = samples_le.bytesize
  ["RIFF", 36 + n, "WAVE", "fmt ", 16, 1, 1, RATE, RATE * 2, 2, 16, "data", n].pack("a4Va4a4VvvVVvva4V") + samples_le
end

# One effect: the events of snd_begin for this sound alone, by frame.
def effect_events(voices)
  evs = []
  voices.each do |v|
    ch = v[0]
    duty = v[1]
    t = 0
    v[2..].each_slice(3) do |freq, ms, vol|
      break unless ms
      evs << [t, ch, freq, vol, duty]
      t += ms
    end
    evs << [t, ch, -1, 0, 0]
  end
  # The order snd_tick keeps: by time, voices in table order.
  evs.each_with_index.sort_by { |e, i| [e[0], i] }.map do |(t, ch, freq, vol, duty), _|
    f = (t * FPS / 1000.0).ceil
    freq >= 0 ? "#{f} on #{ch} #{freq} #{vol} #{duty}" : "#{f} off #{ch}"
  end
end

def render_effect(exe, dir, name, voices)
  lines = effect_events(voices)
  last = lines.map { |l| l.to_i }.max
  raw = File.join(dir, "#{name}.raw")
  run!(exe, "notes", (last + 6).to_s, raw, stdin: lines.join("\n") + "\n")
  data = File.binread(raw)
  s = data.unpack("s<*")
  s.pop while s.size > RATE / 50 && s.last.zero?
  wav(s.pack("s<*"))
end

# [S, P] in frames: the shortest period P (at least MIN_LOOP_S) such that
# the frames' register writes repeat from S to the end, over 2 periods or more.
def find_loop(hashes)
  n = hashes.size
  (MIN_LOOP_S * FPS..n / 3).each do |pd|
    i = n - 1 - pd
    i -= 1 while i >= 0 && hashes[i] == hashes[i + pd]
    s = i + 1
    return [s, pd] if n - pd - s >= 2 * pd
  end
  nil
end

# The loop's start after s (SEAM_FRAMES): where half a frame on each side
# of it and of the same point a period later differ least.
def seam(smp, s, pd)
  per = RATE / FPS
  SEAM_FRAMES.min_by do |k|
    a = (s + k) * per
    b = a + pd * per
    d = 0
    (-per / 2...per / 2).each { |i| d += (smp[a + i] - smp[b + i])**2 }
    [d, k]
  end
end

def render_tune(exe, dir, file, track)
  path = File.join(ROOT, NSF_DIR, file)
  base = File.basename(file, ".nsf")
  raw = File.join(dir, "#{base}.raw")
  hs = File.join(dir, "#{base}.hash")
  run!(exe, "nsf", path, track.to_s, (RENDER_S * FPS).to_s, raw, hs)
  s, pd = find_loop(File.readlines(hs, chomp: true)) || die("no loop found in #{file} (#{RENDER_S} s)")
  k = seam(File.binread(raw).unpack("s<*"), s, pd)
  keep = s + k + pd + MARGIN_FRAMES
  part = File.join(dir, "#{base}.part.raw")
  File.binwrite(part, File.binread(raw, keep * RATE / FPS * 2))
  mp3 = File.join(dir, "#{base}.mp3")
  run!("ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-f", "s16le", "-ar", RATE.to_s, "-ac", "1", "-i", part,
       "-ar", MP3_RATE.to_s, "-c:a", "libmp3lame", "-b:a", MP3_BITRATE, "-fflags", "+bitexact", "-flags:a", "+bitexact",
       "-map_metadata", "-1", "-id3v2_version", "0", "-write_id3v1", "0", mp3)
  ls = (s + k).fdiv(FPS)
  [File.binread(mp3), { "intro_s" => s.fdiv(FPS).round(4), "loop_s" => pd.fdiv(FPS).round(4),
                        "loop_start" => ls.round(6), "loop_end" => (ls + pd.fdiv(FPS)).round(6),
                        "length_s" => keep.fdiv(FPS).round(4) }]
end

# ------------------------------------------------------------ the files

def render(out)
  sfx, sfx_text, bgm, app = app_tables
  files = {}
  manifest = {
    "made_by" => "fmruby-core tools/mahjong_sounds.rb (Family mruby's APU, components/apu_emu)",
    "from" => { SOUND_RB => { "SFX" => Digest::SHA256.hexdigest(sfx_text) } },
    "rate" => RATE,
    "bgm_levels" => app["levels"].map { |x| x / 100.0 },
    "bgm_names" => app["names"],
    "bgm_default" => app["default"],
    "tense_left" => app["tense"],
    "scenes" => {},
    "tunes" => {},
    "effects" => {}
  }
  Dir.mktmpdir("mahjong_sounds") do |dir|
    exe = build_renderer(dir)
    sfx.each do |name, d|
      body = render_effect(exe, dir, name, d[1..])
      f = "#{name}.wav"
      files[f] = body
      manifest["effects"][name] = { "file" => f, "priority" => d[0], "channels" => d[1..].map(&:first).uniq,
                                    "length_s" => ((body.bytesize - 44) / 2).fdiv(RATE).round(3) }
    end
    bgm.each do |scene, (path, track)|
      file = File.basename(path)
      key = "#{File.basename(file, '.nsf')}#{track.to_i.zero? ? '' : "_#{track}"}"
      manifest["scenes"][scene] = key
      next if manifest["tunes"][key]
      body, loop = render_tune(exe, dir, file, track.to_i)
      f = "#{key}.mp3"
      files[f] = body
      nsf = File.join(ROOT, NSF_DIR, file)
      manifest["from"]["#{NSF_DIR}/#{file}"] = Digest::SHA256.file(nsf).hexdigest
      manifest["tunes"][key] = { "file" => f, "nsf" => file, "track" => track.to_i, "title" => TITLES[file],
                                 "author" => "shiru8bit", "license" => "CC BY 3.0" }.merge(loop)
    end
  end
  manifest["files"] = files.sort.to_h { |f, b| [f, { "sha256" => Digest::SHA256.hexdigest(b), "bytes" => b.bytesize }] }
  FileUtils.mkdir_p(out)
  files.each { |f, b| File.binwrite(File.join(out, f), b) }
  File.write(File.join(out, "sounds.json"), "#{JSON.pretty_generate(manifest)}\n")
  manifest
end

def report(manifest, old)
  total = 0
  manifest["files"].each do |f, m|
    total += m["bytes"]
    was = old && old.dig("files", f, "sha256")
    tag = old.nil? ? "" : (was.nil? ? " new" : (was == m["sha256"] ? " same" : " CHANGED"))
    puts format("  %-24s %8d bytes  %s%s", f, m["bytes"], m["sha256"][0, 16], tag)
  end
  (old ? old["files"].keys - manifest["files"].keys : []).each { |f| puts "  #{f} GONE" }
  manifest["tunes"].each do |k, t|
    puts format("  %-24s intro %.2f s, loop %.2f s (%.2f .. %.2f in the file)", k, t["intro_s"], t["loop_s"],
                t["loop_start"], t["loop_end"])
  end
  puts "  total #{total} bytes"
end

check = ARGV.delete("--check")
out = ARGV.shift or die("usage: ruby tools/mahjong_sounds.rb [--check] OUT_DIR")
old_path = File.join(out, "sounds.json")
old = File.exist?(old_path) ? JSON.parse(File.read(old_path)) : nil
if check
  die("#{old_path} not found") unless old
  Dir.mktmpdir("mahjong_sounds_check") do |tmp|
    m = render(tmp)
    report(m, old)
    same = m["files"] == old["files"]
    puts same ? "mahjong_sounds: the files are the same as #{out}" : "mahjong_sounds: the files differ from #{out}"
    exit(same ? 0 : 1)
  end
else
  m = render(out)
  puts "mahjong_sounds: #{out}"
  report(m, old)
end
