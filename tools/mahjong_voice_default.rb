#!/usr/bin/env ruby
# frozen_string_literal: true
#
# mahjong_voice_default.rb - the mahjong table's bundled declarations
# (リーチ, ポン, チー, カン, ツモ, ロン): Open JTalk with the HTS voice
# "Mei" (MMDAgent, CC BY 3.0), rendered in a container
# (doc/commentary/report/c4.md).
#
#   ruby tools/mahjong_voice_default.rb               # renders into flash/ (tracked: shipped)
#   rake voice:mahjong_default                        # the same
#   ruby tools/mahjong_voice_default.rb --zip MMDAgent_Example-1.8.zip   # no download
#   ruby tools/mahjong_voice_default.rb --dir DIR     # somewhere else (to compare)
#
# Every board and the browser build get these; a user's own voice
# (tools/mahjong_voice.rb, VOICEVOX into flash_local/, or /home/voice/mahjong/
# on a Tab5) is played in their place when present.
#
# Reproducible: the voice comes from the MMDAgent sample script's official
# zip (its SHA-256 and the voice file's are checked), Open JTalk and its
# dictionary are Debian bookworm's packages at a fixed version, and the
# options are fixed here. open_jtalk renders at Mei's own 48000 Hz (no -s
# or -p: its 240-sample frame), lifted by 6 dB (no sample clips; any that
# did would stop the tool). Mei's frequency warping (all-pass constant
# 0.55) is made for 48 kHz: rendered directly at 16 kHz its spectrum is
# bent and リーチ sounds like ぱーぱ (doc/commentary/report/c4c.md). Ruby
# then brings each word to 16-bit mono 16000 Hz (MahjongVoice.decimate:
# a 7200 Hz windowed-sinc low-pass, every 3rd sample) and scales it so its
# peak is at -1 dBFS (MahjongVoice.normalize_wav), both in integer
# arithmetic: the same bytes every time. CREDITS.md is written next to
# them. Needs docker and the network (the zip from SourceForge, the
# packages from Debian); nothing is left running.

require "digest"
require "fileutils"
require "net/http"
require "tmpdir"
require "uri"
require_relative "mahjong_voice"

module MahjongVoiceDefault
  CORE = MahjongVoice::CORE
  DIR = File.join(CORE, "flash", "usr", "share", "sounds", "voice", "mahjong", "default")
  ZIP_NAME = "MMDAgent_Example-1.8.zip"
  ZIP_URL = "https://sourceforge.net/projects/mmdagent/files/MMDAgent_Example/MMDAgent_Example-1.8/#{ZIP_NAME}/download"
  ZIP_SHA256 = "f702f2109a07dca103c7b9a5123a25c6dda038f0d7fcc899ff0281d07e873a63"
  VOICE_PATH = "MMDAgent_Example-1.8/Voice/mei/mei_normal.htsvoice"
  VOICE_SHA256 = "b19e8e5ddef4d9d9559d654f395818e2bb3c0bbdbbca533f96086de21ac5db2b"
  IMAGE = "debian:bookworm-slim"
  PACKAGES = ["open-jtalk=1.11-3", "open-jtalk-mecab-naist-jdic=1.11-3", "unzip"].freeze
  # open_jtalk's options besides the files: the gain in dB. The rate and
  # the frame are the voice's own (48000 Hz, 240 samples).
  OPTIONS = "-g 6"

  module_function

  def download(url, dest, limit = 8)
    raise MahjongVoice::Error, "#{url}: too many redirects" if limit.zero?
    uri = URI(url)
    Net::HTTP.start(uri.host, uri.port, use_ssl: uri.scheme == "https", open_timeout: 10, read_timeout: 120) do |h|
      h.request(Net::HTTP::Get.new(uri, "User-Agent" => "curl/8")) do |res|
        if res.is_a?(Net::HTTPRedirection)
          return download(URI.join(url, res["location"]).to_s, dest, limit - 1)
        end
        raise MahjongVoice::Error, "#{url}: HTTP #{res.code}" unless res.is_a?(Net::HTTPSuccess)
        File.open(dest, "wb") { |f| res.read_body { |c| f.write(c) } }
      end
    end
  end

  # The shell run in the container: install, take the voice out of the
  # zip and check it, render each word.
  def script(uid, gid)
    words = MahjongVoice::WORDS.map { |n, t| "#{n}:#{t}" }.join(" ")
    <<~SH
      set -e
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq >/dev/null
      apt-get install -y -qq --no-install-recommends #{PACKAGES.join(' ')} >/dev/null 2>&1
      dpkg-query -W -f '${Package} ${Version}\\n' open-jtalk open-jtalk-mecab-naist-jdic
      unzip -p /in/#{ZIP_NAME} '#{VOICE_PATH}' > /tmp/mei_normal.htsvoice
      echo "#{VOICE_SHA256}  /tmp/mei_normal.htsvoice" | sha256sum -c --quiet
      for w in #{words}; do
        n=${w%%:*}; t=${w#*:}
        echo "$t" | open_jtalk -x /var/lib/mecab/dic/open-jtalk/naist-jdic -m /tmp/mei_normal.htsvoice #{OPTIONS} -ow /out/$n.wav
      done
      chown -R #{uid}:#{gid} /out
    SH
  end

  def credits
    <<~MD
      # The mahjong table's declarations (bundled voice)

      `riichi.wav` `pon.wav` `chi.wav` `kan.wav` `tsumo.wav` `ron.wav`: リーチ・ポン・チー・カン・ツモ・ロン,
      played by `flash/app/game/mahjong/mahjong_sound.rb` when someone declares.

      - **Voice**: HTS Voice "Mei" (`mei_normal.htsvoice`, version 1.5), released by the
        MMDAgent Project Team (http://www.mmdagent.jp/).
        Copyright (c) 2009-2018 Nagoya Institute of Technology, Department of Computer Science.
        Licensed under the Creative Commons Attribution 3.0 license
        (https://creativecommons.org/licenses/by/3.0/).
      - **Source**: #{ZIP_NAME} (MMDAgent sample script 1.8, 2018-12-25),
        https://sourceforge.net/projects/mmdagent/files/MMDAgent_Example/MMDAgent_Example-1.8/
        (zip SHA-256 `#{ZIP_SHA256}`,
        `#{VOICE_PATH}` SHA-256 `#{VOICE_SHA256}`).
      - **Synthesis**: Open JTalk 1.11 (BSD-3-Clause, Nagoya Institute of Technology) with the
        dictionary open-jtalk-mecab-naist-jdic 1.11 (BSD-3-Clause, Nara Institute of Science and
        Technology and Nagoya Institute of Technology), Debian bookworm packages `1.11-3`.
        The program and the dictionary are not shipped, only the audio they made.
      - **Generated, not edited**: rendered by `tools/mahjong_voice_default.rb`
        (open_jtalk `#{OPTIONS}` at the voice's own 48000 Hz, +6 dB; then brought to 16-bit mono
        16000 Hz by a #{MahjongVoice::LOWPASS_CUTOFF.round} Hz windowed-sinc low-pass (#{MahjongVoice::LOWPASS_TAPS} taps, Blackman) keeping
        every 3rd sample; then each word scaled so its peak is at #{MahjongVoice::PEAK_DBFS.round} dBFS). Mei is a
        48 kHz voice: rendered directly at 16 kHz its frequency warping is wrong and the words
        blur. Run it again to reproduce them.

      The app shows the credit 「声: Open JTalk (Mei, CC BY 3.0)」 while these are played.
    MD
  end

  def render(dir: DIR, zip: nil, out: $stdout)
    Dir.mktmpdir("mahjong_voice_default") do |tmp|
      indir = File.join(tmp, "in")
      outdir = File.join(tmp, "out")
      FileUtils.mkdir_p([indir, outdir])
      zpath = File.join(indir, ZIP_NAME)
      if zip
        FileUtils.cp(zip, zpath)
      else
        out.puts "Downloading #{ZIP_NAME} (about 30 MB) from SourceForge"
        download(ZIP_URL, zpath)
      end
      got = Digest::SHA256.file(zpath).hexdigest
      raise MahjongVoice::Error, "#{ZIP_NAME}: SHA-256 #{got}, want #{ZIP_SHA256}" unless got == ZIP_SHA256
      name = "fmrb-mahjong-voice-#{Process.pid}"
      ok = system("docker", "run", "--rm", "--name", name, "-v", "#{indir}:/in:ro", "-v", "#{outdir}:/out",
                  IMAGE, "bash", "-c", script(Process.uid, Process.gid))
      raise MahjongVoice::Error, "the container failed (docker run #{IMAGE})" unless ok
      FileUtils.mkdir_p(dir)
      MahjongVoice::WORDS.each do |n, t|
        wav48 = File.binread(File.join(outdir, "#{n}.wav"))
        off, len = MahjongVoice.data_chunk(wav48)
        clipped = MahjongVoice.clipped_samples(wav48.b[off, len - (len & 1)])
        raise MahjongVoice::Error, "#{n}.wav: #{clipped} samples clipped at 48 kHz (lower -g)" if clipped.positive?
        wav = MahjongVoice.decimate_wav(wav48, "#{n}.wav")
        info = MahjongVoice.check_wav!(wav, "#{n}.wav")
        wav, gain, = MahjongVoice.normalize_wav(wav)
        File.binwrite(File.join(dir, "#{n}.wav"), wav)
        out.puts format("  %-12s %-8s %6d bytes  %.2f s  %d Hz  x%.3f (%+.1f dB)", "#{n}.wav", t, wav.bytesize,
                        info[:seconds], info[:rate], gain, MahjongVoice.gain_db(gain))
      end
      File.write(File.join(dir, "CREDITS.md"), credits)
      out.puts "Wrote #{dir}"
    end
  end
end

if $PROGRAM_NAME == __FILE__
  dir = MahjongVoiceDefault::DIR
  zip = nil
  args = ARGV.dup
  until args.empty?
    a = args.shift
    case a
    when "--dir" then dir = File.expand_path(args.shift)
    when "--zip" then zip = File.expand_path(args.shift)
    when "-h", "--help"
      puts File.read(__FILE__)[/^# mahjong_voice_default.rb.*?(?=^$)/m].gsub(/^# ?/, "")
      exit 0
    else
      warn "unknown argument: #{a}"
      exit 2
    end
  end
  begin
    MahjongVoiceDefault.render(dir: dir, zip: zip)
  rescue MahjongVoice::Error => e
    warn "mahjong_voice_default: #{e.message}"
    exit 1
  end
end
