#!/usr/bin/env ruby
# frozen_string_literal: true
#
# mahjong_voice.rb - the mahjong table's declarations (リーチ, ポン, チー,
# カン, ツモ, ロン) as WAVs made with your own VOICEVOX
# (doc/commentary/report/c4.md).
#
#   ruby tools/mahjong_voice.rb                      # Zundamon (ノーマル) from 127.0.0.1:50021
#   rake voice:mahjong                               # the same
#   VOICEVOX_URL=http://127.0.0.1:50021 SPEAKER=ずんだもん STYLE=ノーマル rake voice:mahjong
#   ruby tools/mahjong_voice.rb --url URL --speaker NAME --style NAME --dir DIR
#
# The voices are not distributed: each user makes them on their own PC
# (the VOICEVOX terms, doc/reference/voicevox_terms.md). They go to
# flash_local/ (gitignored), which the device image lays over flash/, so a
# build of this machine carries them and nothing tracked by git (or the
# browser build's bundle) ever does. A Tab5 takes them without a reflash in
# /home/voice/mahjong/ (rake voice:mahjong_push IP=...).
#
# The WAVs are 16-bit mono 16000 Hz (FmrbAudio#play_wav takes 8000-48000 Hz
# mono; 16 kHz keeps them small), each scaled so its peak is at -1 dBFS
# (normalize_wav, C4b: VOICEVOX's own level was far below the BGM and the
# effects). The character's number is looked up by
# name in /speakers, never assumed. CREDITS.txt goes next to them; its first
# line ("VOICEVOX:<name>") is what the app shows as the credit.
#
# Standard library only. The pure parts (the words, the lookup, the query's
# rate, the WAV header, the normalisation, the guide) are tested by test/mahjong_voice/run.rb.

require "json"
require "net/http"
require "uri"
require "fileutils"

module MahjongVoice
  CORE = File.expand_path("..", __dir__)
  # file name (without .wav) => what the synthesiser reads. The app's
  # mahjong_sound.rb names the same six (VOICE_NAMES).
  WORDS = [["riichi", "リーチ！"], ["pon", "ポン！"], ["chi", "チー！"], ["kan", "カン！"],
           ["tsumo", "ツモ！"], ["ron", "ロン！"]].freeze
  DEFAULT_URL = "http://127.0.0.1:50021"
  DEFAULT_SPEAKER = "ずんだもん"
  DEFAULT_STYLE = "ノーマル"
  RATE = 16_000
  # Where the user's own voice goes (the device's /usr/share/sounds/voice/mahjong/).
  USER_DIR = File.join(CORE, "flash_local", "usr", "share", "sounds", "voice", "mahjong")
  # Where a Tab5 takes them without a reflash (looked at first by the app).
  DEVICE_HOME_DIR = "/home/voice/mahjong"
  VOICEVOX_HOME = "https://voicevox.hiroshiba.jp/"

  class Error < StandardError; end

  module_function

  # The style's number from /speakers' list ([{"name", "styles" => [{"name", "id"}]}]).
  def style_id(list, speaker, style)
    s = list.find { |x| x["name"] == speaker }
    raise Error, "VOICEVOX has no character #{speaker.inspect} (there are: #{list.map { |x| x['name'] }.join(', ')})" unless s
    st = Array(s["styles"]).find { |x| x["name"] == style }
    unless st
      names = Array(s["styles"]).map { |x| x["name"] }
      raise Error, "#{speaker} has no style #{style.inspect} (there are: #{names.join(', ')})"
    end
    st["id"]
  end

  # audio_query's answer with the output the device wants: 16 kHz, mono.
  def tune_query(json)
    q = JSON.parse(json)
    raise Error, "audio_query: not an object" unless q.is_a?(Hash)
    q["outputSamplingRate"] = RATE
    q["outputStereo"] = false
    JSON.generate(q)
  end

  # A RIFF WAV's format: {rate:, channels:, bits:, format:, bytes:, seconds:}.
  def wav_info(data)
    data = data.b
    raise Error, "not a WAV (no RIFF/WAVE header)" unless data.size >= 12 && data[0, 4] == "RIFF" && data[8, 4] == "WAVE"
    pos = 12
    fmt = nil
    while pos + 8 <= data.size
      id = data[pos, 4]
      len = data[pos + 4, 4].unpack1("V")
      if id == "fmt "
        f, ch, rate, _br, _al, bits = data[pos + 8, 16].unpack("vvVVvv")
        fmt = { format: f, channels: ch, rate: rate, bits: bits }
      elsif id == "data"
        raise Error, "WAV: data before fmt" unless fmt
        len = [len, data.size - pos - 8].min
        frames = len / (fmt[:channels] * fmt[:bits] / 8)
        return fmt.merge(bytes: len, seconds: frames.to_f / fmt[:rate])
      end
      pos += 8 + len + (len & 1)
    end
    raise Error, "WAV: no data chunk"
  end

  # What the device can play and this tool promises: PCM 16-bit mono at RATE.
  def check_wav!(data, name = "WAV")
    i = wav_info(data)
    unless i[:format] == 1 && i[:bits] == 16 && i[:channels] == 1 && i[:rate] == RATE
      raise Error, "#{name}: #{i[:bits]}-bit #{i[:channels]}ch #{i[:rate]} Hz (format #{i[:format]}), " \
                   "want 16-bit mono #{RATE} Hz PCM"
    end
    raise Error, "#{name}: #{data.bytesize} bytes, over play_wav's 2 MB" if data.bytesize > 2 * 1024 * 1024
    i
  end

  # Peak normalisation (C4b): every word's loudest sample is brought to
  # PEAK_DBFS. Full scale is 32768 (as ffmpeg's volumedetect counts it),
  # so the target sample is round(32768 * 10^(PEAK_DBFS / 20)).
  PEAK_DBFS = -1.0
  FULL_SCALE = 32_768

  # The sample value PEAK_DBFS stands for (29205 for -1 dBFS).
  def peak_target(dbfs = PEAK_DBFS)
    (FULL_SCALE * (10.0**(dbfs / 20.0))).round
  end

  # [offset, length] of a WAV's data chunk (its bytes inside data).
  def data_chunk(data)
    data = data.b
    raise Error, "not a WAV (no RIFF/WAVE header)" unless data.size >= 12 && data[0, 4] == "RIFF" && data[8, 4] == "WAVE"
    pos = 12
    while pos + 8 <= data.size
      len = data[pos + 4, 4].unpack1("V")
      return [pos + 8, [len, data.size - pos - 8].min] if data[pos, 4] == "data"
      pos += 8 + len + (len & 1)
    end
    raise Error, "WAV: no data chunk"
  end

  # The largest |sample| of 16-bit little-endian PCM (-32768 counts as 32768).
  def peak(pcm)
    pcm.unpack("s<*").map(&:abs).max || 0
  end

  # One sample scaled by target/pk in integers (rounded half away from
  # zero, kept in 16 bits), so the same input always gives the same bytes.
  def scale_sample(s, target, pk)
    v = ((s.abs * target * 2) + pk) / (pk * 2)
    v = -v if s.negative?
    v.clamp(-FULL_SCALE, FULL_SCALE - 1)
  end

  # A 16-bit mono WAV with its peak at dbfs. Returns [wav, gain, peak
  # before]; the header is kept as it is. Silence is left alone (gain 1).
  def normalize_wav(data, dbfs = PEAK_DBFS)
    data = data.b
    off, len = data_chunk(data)
    len -= len & 1
    pcm = data[off, len]
    pk = peak(pcm)
    return [data, 1.0, 0] if pk.zero?
    target = peak_target(dbfs)
    out = pcm.unpack("s<*").map { |s| scale_sample(s, target, pk) }.pack("s<*")
    res = data.dup
    res[off, len] = out
    [res, target.to_f / pk, pk]
  end

  # A gain as dB (for the tools' output and the report).
  def gain_db(gain) = 20.0 * Math.log10(gain)

  # CREDITS.txt next to the WAVs. The first line is the app's credit.
  def credits_text(speaker, style)
    <<~TXT
      VOICEVOX:#{speaker}

      麻雀アプリの宣言の声 (#{WORDS.map { |w| w[1].delete('！') }.join('・')})。
      この PC の VOICEVOX (#{speaker}、#{style}) で tools/mahjong_voice.rb が作った。
      - VOICEVOX の利用規約と、#{speaker} の音声ライブラリの規約に従う。
        使うときはクレジット「VOICEVOX:#{speaker}」が要る (アプリは画面に出す)。
      - 各自が自分の VOICEVOX で作るもので、配らない (git に入れない、
        ほかの人に渡さない)。flash_local/ は git の外にある。
    TXT
  end

  # What to say when VOICEVOX does not answer.
  def guide(url, err)
    <<~TXT
      VOICEVOX が #{url} で答えない (#{err})。

      声を作るには、この PC で VOICEVOX のエンジンを動かす:
        - VOICEVOX のアプリ (#{VOICEVOX_HOME}) を起動する (エンジンは 127.0.0.1:50021), または
        - docker: docker run -d --name voicevox -p 127.0.0.1:50021:50021 voicevox/voicevox_engine:cpu-0.25.0
      WSL から Windows のアプリを使うときは、WSL の網の設定 (localhost の共有) か、
      Windows 側の住所を VOICEVOX_URL=http://<住所>:50021 で渡す。
      そのあと: rake voice:mahjong
    TXT
  end

  # The engine's HTTP: GET /version, /speakers; POST /audio_query, /synthesis.
  class Client
    def initialize(url)
      @url = url.to_s.empty? ? DEFAULT_URL : url.to_s.chomp("/")
      @uri = URI(@url)
      raise Error, "#{@url}: not an http:// URL" unless @uri.is_a?(URI::HTTP) && @uri.host
    end

    attr_reader :url

    def version = JSON.parse("[#{request(:get, '/version').force_encoding('UTF-8')}]")[0].to_s
    def speakers = JSON.parse(request(:get, "/speakers").force_encoding("UTF-8"))

    def synthesize(text, id)
      q = request(:post, "/audio_query?#{URI.encode_www_form(text: text, speaker: id)}", "")
      q = MahjongVoice.tune_query(q.force_encoding("UTF-8"))
      request(:post, "/synthesis?#{URI.encode_www_form(speaker: id)}", q, "Content-Type" => "application/json")
    end

    private

    def request(method, path, body = nil, headers = {})
      http = Net::HTTP.new(@uri.host, @uri.port)
      http.open_timeout = 3
      http.read_timeout = 120
      res = http.start { |h| method == :get ? h.get(path) : h.post(path, body.to_s, headers) }
      raise Error, "#{@url}#{path[/\A[^?]*/]}: HTTP #{res.code}" unless res.is_a?(Net::HTTPSuccess)
      res.body.to_s.b
    rescue Error
      raise
    rescue StandardError => e
      raise Error, "#{e.class}: #{e.message}"
    end
  end

  # Makes the six WAVs and CREDITS.txt in dir. Returns [[name, info], ...].
  def generate(url: DEFAULT_URL, speaker: DEFAULT_SPEAKER, style: DEFAULT_STYLE, dir: USER_DIR, out: $stdout)
    vv = Client.new(url)
    ver = vv.version
    id = style_id(vv.speakers, speaker, style)
    out.puts "VOICEVOX #{ver} at #{vv.url}: #{speaker} (#{style}) is style #{id}"
    FileUtils.mkdir_p(dir)
    made = WORDS.map do |name, text|
      wav = vv.synthesize(text, id)
      info = check_wav!(wav, "#{name}.wav")
      wav, gain, = normalize_wav(wav)
      File.binwrite(File.join(dir, "#{name}.wav"), wav)
      out.puts format("  %-12s %-8s %6d bytes  %.2f s  %d Hz  x%.3f (%+.1f dB)", "#{name}.wav", text, wav.bytesize,
                      info[:seconds], info[:rate], gain, gain_db(gain))
      [name, info]
    end
    File.write(File.join(dir, "CREDITS.txt"), credits_text(speaker, style))
    out.puts "Wrote #{dir} (not tracked by git). Build the device image to carry them, or"
    out.puts "push them to a Tab5: rake voice:mahjong_push IP=<the Tab5's address>"
    made
  end
end

if $PROGRAM_NAME == __FILE__
  url = ENV["VOICEVOX_URL"] || MahjongVoice::DEFAULT_URL
  speaker = ENV["SPEAKER"] || MahjongVoice::DEFAULT_SPEAKER
  style = ENV["STYLE"] || MahjongVoice::DEFAULT_STYLE
  dir = MahjongVoice::USER_DIR
  args = ARGV.dup
  until args.empty?
    a = args.shift
    case a
    when "--url" then url = args.shift
    when "--speaker" then speaker = args.shift
    when "--style" then style = args.shift
    when "--dir" then dir = File.expand_path(args.shift)
    when "-h", "--help"
      puts File.read(__FILE__)[/^# mahjong_voice.rb.*?(?=^$)/m].gsub(/^# ?/, "")
      exit 0
    else
      warn "unknown argument: #{a}"
      exit 2
    end
  end
  begin
    vv = MahjongVoice::Client.new(url)
    begin
      vv.version
    rescue MahjongVoice::Error => e
      warn MahjongVoice.guide(vv.url, e.message)
      exit 1
    end
    MahjongVoice.generate(url: url, speaker: speaker, style: style, dir: dir)
  rescue MahjongVoice::Error => e
    warn "mahjong_voice: #{e.message}"
    exit 1
  end
end
