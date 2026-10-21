# Host tests for the mahjong table's declaration voices (C4,
# doc/commentary/report/c4.md).
#
# The tools' pure parts (tools/mahjong_voice.rb: the words, the character's
# number by name, the query's rate, the WAV header, the credits, the guide
# when VOICEVOX is not there; tools/mahjong_voice_default.rb's credits and
# the shipped default WAVs), and the app's side (mahjong_sound.rb, loaded
# as it ships into a stand-in MahjongApp): which declaration plays which
# file, where the files are looked for and in which order, the credit, and
# which of two close declarations is heard.
#
#   ruby test/mahjong_voice/run.rb

require "tmpdir"
require "fileutils"
require "json"

CORE = File.expand_path("../..", __dir__)
require File.join(CORE, "tools", "mahjong_voice")
require File.join(CORE, "tools", "mahjong_voice_default")

$failures = 0

def check(label)
  ok = yield
  if ok
    puts "ok   #{label}"
  else
    puts "FAIL #{label}"
    $failures += 1
  end
rescue => e
  puts "FAIL #{label} (#{e.class}: #{e.message})"
  $failures += 1
end

def eq(label, actual, expected)
  check("#{label} (#{actual.inspect})") { actual == expected }
end

def raises(label, pattern)
  yield
  puts "FAIL #{label} (no error)"
  $failures += 1
rescue MahjongVoice::Error => e
  check("#{label} (#{e.message[0, 70]})") { e.message =~ pattern }
end

# A PCM WAV of n frames.
def wav(rate: 16_000, ch: 1, bits: 16, frames: 1600)
  data = "\0".b * (frames * ch * bits / 8)
  fmt = [1, ch, rate, rate * ch * bits / 8, ch * bits / 8, bits].pack("vvVVvv")
  body = "WAVE" + "fmt " + [16].pack("V") + fmt + "data" + [data.bytesize].pack("V") + data
  "RIFF".b + [body.bytesize].pack("V") + body
end

# ---- the VOICEVOX tool -------------------------------------------------------

puts "== tools/mahjong_voice.rb"
eq "the six words' files", MahjongVoice::WORDS.map(&:first), %w[riichi pon chi kan tsumo ron]
eq "what is read", MahjongVoice::WORDS.map(&:last), %w[リーチ！ ポン！ チー！ カン！ ツモ！ ロン！]
eq "defaults", [MahjongVoice::DEFAULT_URL, MahjongVoice::DEFAULT_SPEAKER, MahjongVoice::DEFAULT_STYLE],
   ["http://127.0.0.1:50021", "ずんだもん", "ノーマル"]

speakers = [{ "name" => "四国めたん", "styles" => [{ "name" => "ノーマル", "id" => 2 }] },
            { "name" => "ずんだもん", "styles" => [{ "name" => "あまあま", "id" => 1 }, { "name" => "ノーマル", "id" => 3 }] }]
eq "the number by name", MahjongVoice.style_id(speakers, "ずんだもん", "ノーマル"), 3
eq "another style", MahjongVoice.style_id(speakers, "ずんだもん", "あまあま"), 1
raises("no such character lists them", /四国めたん, ずんだもん/) { MahjongVoice.style_id(speakers, "春日部つむぎ", "ノーマル") }
raises("no such style lists them", /あまあま, ノーマル/) { MahjongVoice.style_id(speakers, "ずんだもん", "ささやき") }

q = JSON.parse(MahjongVoice.tune_query(JSON.generate("accent_phrases" => [], "outputSamplingRate" => 24_000,
                                                    "outputStereo" => true, "speedScale" => 1.0)))
eq "the query asks for 16 kHz", q["outputSamplingRate"], 16_000
eq "and mono", q["outputStereo"], false
eq "the rest is kept", q["speedScale"], 1.0

i = MahjongVoice.wav_info(wav)
eq "the WAV header", [i[:format], i[:channels], i[:rate], i[:bits], i[:bytes]], [1, 1, 16_000, 16, 3200]
eq "its length", i[:seconds], 0.1
check("16-bit mono 16 kHz passes") { MahjongVoice.check_wav!(wav) }
raises("24 kHz is refused", /24000 Hz/) { MahjongVoice.check_wav!(wav(rate: 24_000)) }
raises("stereo is refused", /2ch/) { MahjongVoice.check_wav!(wav(ch: 2)) }
raises("not a WAV", /RIFF/) { MahjongVoice.wav_info("hello world!") }

eq "the user's voices go to flash_local", MahjongVoice::USER_DIR.sub(CORE + "/", ""),
   "flash_local/usr/share/sounds/voice/mahjong"
check("flash_local is ignored by git") do
  system("git", "-C", CORE, "check-ignore", "-q", File.join(MahjongVoice::USER_DIR, "riichi.wav"))
end
eq "a Tab5 takes them in", MahjongVoice::DEVICE_HOME_DIR, "/home/voice/mahjong"
c = MahjongVoice.credits_text("ずんだもん", "ノーマル")
eq "the credit is CREDITS.txt's first line", c.lines.first.chomp, "VOICEVOX:ずんだもん"
check("CREDITS.txt says not to hand them out") { c.include?("配らない") }
g = MahjongVoice.guide("http://127.0.0.1:50021", "connection refused")
check("the guide names the address, the app and the docker image") do
  g.include?("127.0.0.1:50021") && g.include?(MahjongVoice::VOICEVOX_HOME) && g.include?("voicevox/voicevox_engine")
end
check("and says what to run next") { g.include?("rake voice:mahjong") }
raises("an engine that is not there", /Errno::ECONNREFUSED|Connection refused|OpenTimeout/) do
  MahjongVoice::Client.new("http://127.0.0.1:9").version
end
raises("not an http URL", /not an http/) { MahjongVoice::Client.new("ftp://x") }

# ---- the bundled default voice -----------------------------------------------

puts "== tools/mahjong_voice_default.rb and its WAVs"
eq "rendered into flash/ (shipped)", MahjongVoiceDefault::DIR.sub(CORE + "/", ""),
   "flash/usr/share/sounds/voice/mahjong/default"
md = MahjongVoiceDefault.credits
check("CREDITS.md: the voice, its licence, its holder") do
  md.include?("Mei") && md.include?("Creative Commons Attribution 3.0") && md.include?("Nagoya Institute of Technology")
end
check("CREDITS.md: the source's digests") { md.include?(MahjongVoiceDefault::ZIP_SHA256) && md.include?(MahjongVoiceDefault::VOICE_SHA256) }
check("CREDITS.md: generated, not edited") { md.include?("Generated, not edited") }
check("the shipped CREDITS.md is the tool's") { File.read(File.join(MahjongVoiceDefault::DIR, "CREDITS.md")) == md }
MahjongVoice::WORDS.each do |n, _|
  check("default/#{n}.wav is 16-bit mono 16 kHz, under 1 s") do
    i = MahjongVoice.check_wav!(File.binread(File.join(MahjongVoiceDefault::DIR, "#{n}.wav")), n)
    i[:seconds] > 0.3 && i[:seconds] < 1.0
  end
end

# ---- the app (mahjong_sound.rb) ----------------------------------------------

puts "== mahjong_sound.rb"

module Machine
  @now = 0
  class << self
    attr_accessor :now
  end
  def self.board_millis = @now
end

module Log
  @lines = []
  class << self
    attr_reader :lines
  end
  def self.info(s) = @lines << s
end

class FmrbApp; end

# What mahjong_sound.rb wraps and reads (mahjong.app.rb's).
class MahjongApp < FmrbApp
  A_RIICHI = 2
  A_TSUMO = 3
  A_ANKAN = 4
  A_KAKAN = 5
  A_PON = 6
  A_CHI = 7
  A_MINKAN = 8
  A_RON = 9
  A_KYUUSHU = 11
  ASSIST_FILE = "/nonexistent/mahjong.toml"
  CACHE_DIR = "/nonexistent"
  %i[take_view round_tick leave_table on_update lobby_key list_key run_button stop on_destroy save_assist].each do |m|
    define_method(m) { |*| nil }
  end
end
eval(File.read(File.join(CORE, "flash", "app", "game", "mahjong", "mahjong_sound.rb")), TOPLEVEL_BINDING, "mahjong_sound.rb")

# play_wav as the device answers: true (Modern) or false (Retro).
class FakeAudio
  attr_reader :played
  attr_accessor :answer

  def initialize
    @played = []
    @answer = true
  end

  def play_wav(p)
    @played << p
    @answer
  end

  def stop_wav = true
end

app = MahjongApp.new
eq "the app's voices are the tool's words", MahjongApp::VOICE_NAMES.map(&:to_s), MahjongVoice::WORDS.map(&:first)
eq "the lookup order", MahjongApp::VOICE_DIRS.map(&:first),
   ["/home/voice/mahjong", "/usr/share/sounds/voice/mahjong", "/usr/share/sounds/voice/mahjong/default"]
eq "the default is where the tool renders it", "flash" + MahjongApp::VOICE_DIRS[2][0], MahjongVoiceDefault::DIR.sub(CORE + "/", "")
eq "the user's is where flash_local puts it", "flash_local" + MahjongApp::VOICE_DIRS[1][0], MahjongVoice::USER_DIR.sub(CORE + "/", "")

{ MahjongApp::A_RON => :ron, MahjongApp::A_TSUMO => :tsumo, MahjongApp::A_RIICHI => :riichi,
  MahjongApp::A_PON => :pon, MahjongApp::A_CHI => :chi, MahjongApp::A_ANKAN => :kan,
  MahjongApp::A_KAKAN => :kan, MahjongApp::A_MINKAN => :kan, MahjongApp::A_KYUUSHU => :kyuushu,
  13 => :ryuukyoku }.each do |type, name|
  eq "declaration #{type} plays #{name}", app.snd_jingle(type), name
end

check("ron outranks tsumo, kan, pon, chi") do
  pr = MahjongApp::VOICE_PRI
  pr[:ron] > pr[:tsumo] && pr[:tsumo] > pr[:kan] && pr[:kan] > pr[:pon] && pr[:pon] > pr[:chi]
end
eq "a higher one replaces a voice", app.snd_voice_ok?(6, [3, 1000], 500), true
eq "an equal one replaces it (the newest)", app.snd_voice_ok?(3, [3, 1000], 500), true
eq "a lower one waits", app.snd_voice_ok?(3, [6, 1000], 500), false
eq "until it is over", app.snd_voice_ok?(3, [6, 1000], 1000), true

Dir.mktmpdir do |tmp|
  home = File.join(tmp, "home")
  user = File.join(tmp, "user")
  dflt = File.join(tmp, "default")
  FileUtils.mkdir_p([home, user, dflt])
  dirs = [[home, nil], [user, nil], [dflt, "Open JTalk (Mei, CC BY 3.0)"]]
  %w[riichi pon chi kan tsumo ron].each { |n| File.binwrite(File.join(dflt, "#{n}.wav"), wav) }

  f = app.snd_find_voices(dirs)
  eq "only the default: every word from it", f[0].values.map { |p| File.dirname(p) }.uniq, [dflt]
  eq "and its credit", f[1], "Open JTalk (Mei, CC BY 3.0)"

  %w[riichi pon chi kan tsumo ron].each { |n| File.binwrite(File.join(user, "#{n}.wav"), wav) }
  File.write(File.join(user, "CREDITS.txt"), MahjongVoice.credits_text("ずんだもん", "ノーマル"))
  f = app.snd_find_voices(dirs)
  eq "the user's flash_local voice before the default", f[0][:ron], File.join(user, "ron.wav")
  eq "its credit from CREDITS.txt", f[1], "VOICEVOX:ずんだもん"

  File.binwrite(File.join(home, "ron.wav"), wav)
  f = app.snd_find_voices(dirs)
  eq "/home first, word by word", [f[0][:ron], f[0][:pon]], [File.join(home, "ron.wav"), File.join(user, "pon.wav")]
  eq "/home without CREDITS.txt: the users' voice (ずんだもん)", f[1], "VOICEVOX:ずんだもん"

  File.write(File.join(user, "CREDITS.txt"), MahjongVoice.credits_text("四国めたん", "ノーマル"))
  FileUtils.rm(File.join(user, "chi.wav"))
  f = app.snd_find_voices(dirs)
  eq "a mixed set shows every credit", f[1], "VOICEVOX:ずんだもん / VOICEVOX:四国めたん / Open JTalk (Mei, CC BY 3.0)"
  eq "and the missing word from the default", f[0][:chi], File.join(dflt, "chi.wav")

  f = app.snd_find_voices([[File.join(tmp, "none"), nil]])
  eq "no files: no voices, no credit", f, [{}, nil]
end

# The events: a declaration's voice through snd_voice.
au = FakeAudio.new
app.instance_variable_set(:@audio, au)
app.instance_variable_set(:@snd_voices, { pon: "/v/pon.wav", ron: "/v/ron.wav", chi: "/v/chi.wav" })
app.instance_variable_set(:@snd_vo_on, true)
app.instance_variable_set(:@snd_vo, [0, 0])
Machine.now = 10_000
app.snd_voice(:pon)
Machine.now = 10_200
app.snd_voice(:ron)
eq "a pon then a ron: both start, the ron last (it replaces the pon)", au.played, ["/v/pon.wav", "/v/ron.wav"]
Machine.now = 10_400
app.snd_voice(:chi)
eq "a chi right after the ron is not heard", au.played.size, 2
Machine.now = 11_300
app.snd_voice(:chi)
eq "after the ron's time it is", au.played.last, "/v/chi.wav"
app.snd_voice(:kan)
eq "a word without a file: nothing", au.played.size, 3
app.instance_variable_set(:@snd_vo_on, false)
Machine.now = 20_000
app.snd_voice(:ron)
eq "the switch off: nothing", au.played.size, 3
app.instance_variable_set(:@snd_vo_on, true)
app.instance_variable_set(:@snd_vo, [0, 0])
au.answer = false
app.snd_voice(:pon)
eq "play_wav false (Retro): nothing held", app.instance_variable_get(:@snd_vo), [0, 0]

# The switch's label and the saved line.
app.instance_variable_set(:@snd_bgm, 1)
app.instance_variable_set(:@snd_se, true)
app.instance_variable_set(:@snd_conf, {})
eq "the voices' switch", app.snd_labels.last, "声 入[V]"
app.instance_variable_set(:@snd_vo_on, false)
eq "off", app.snd_labels.last, "声 切[V]"
check("saved as voice = false") { app.snd_lines.include?("voice = false\n") }
app.instance_variable_set(:@snd_voices, {})
eq "no voices: no switch", app.snd_labels.size, 2

puts
if $failures.zero?
  puts "mahjong voice tests: all passed"
else
  puts "mahjong voice tests: #{$failures} failed"
  exit 1
end
