# Plays the bundled mahjong voices with FmrbAudio#play_wav, one per key
# (1..6: riichi pon chi kan tsumo ron), for the live scene "wav_voice"
# (test/regress/live/20_streams.rb) and for listening to them on a board
# without a game of mahjong (doc/flash_layout/report/f3.md: the voices are
# IMA-ADPCM). Not in any device image (app/test); send it over WiFi.
#
# What to read in the log:
#   WavVoice: ready
#   WavVoice: riichi /usr/share/sounds/voice/mahjong/default/riichi.wav true

class WavVoiceApp < FmrbApp
  DIR = "/usr/share/sounds/voice/mahjong/default"
  NAMES = ["riichi", "pon", "chi", "kan", "tsumo", "ron"].freeze

  def on_create
    @audio = FmrbAudio.new(self)
    @last = "-"
    draw
    Log.info("WavVoice: ready")
  end

  def draw
    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbConst::THEME_WINDOW_BG)
    @gfx.draw_text(4, 4, "1-6: riichi pon chi kan tsumo ron", FmrbConst::THEME_TEXT,
                   FmrbConst::THEME_WINDOW_BG)
    @gfx.draw_text(4, 16, "last: #{@last}", FmrbConst::THEME_TEXT, FmrbConst::THEME_WINDOW_BG)
    @gfx.present
  end

  def play(i)
    name = NAMES[i]
    path = "#{DIR}/#{name}.wav"
    ok = @audio.play_wav(path)
    Log.info("WavVoice: #{name} #{path} #{ok}")
    @last = "#{name} #{ok}"
    draw
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    sc = ev[:scancode]
    # 0x1E..0x23 are the keys 1..6.
    play(sc - 0x1E) if sc >= 0x1E && sc <= 0x23
  end

  def on_destroy
    @audio.stop_wav if @audio
  end
end
