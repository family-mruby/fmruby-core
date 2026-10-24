# The table's sound (MJ11, doc/ruby_asterism/report/mj11.md): effects,
# short jingles in place of the spoken calls, the end-of-game fanfares and
# a BGM hook. Read by on_create (after the class body, since it wraps some
# of its methods); a Retro screen never reads it.
#
# - Effects and jingles are note_on / note_off on the APU's SUB instance
#   (two pulses, the triangle, the noise). A sound is started only when
#   something happens (a view that differs, a step of the result's count);
#   its later notes are deadlines in wall-clock ms that snd_tick runs at the
#   end of on_update, after the drawing (so a long repaint does not stretch
#   the first note). Nothing pending: snd_tick returns at once.
# - Overlap: a sound claims the channels it uses unless a sound of a higher
#   priority still holds one (that voice is then left out). The newest of
#   equal priority wins; its generation number keeps the older sound's
#   pending notes off the channel.
# - The BGM hook: an NSF (FmrbAudio#play) or an FMSQ (load_fmsq_file +
#   play_slot on MAIN) per scene, :lobby (the list and the lobby), :play
#   (the game and its results), :tense (optional: a riichi on the table or
#   TENSE_LEFT tiles left; without a tune it is :play). The end of a game
#   is silent (the fanfare plays alone). No tune: no BGM. FmrbMidi's
#   MmlPlayer is not used: it plays on SUB, with the effects.
# - The declarations' voices (C4, doc/commentary/report/c4.md): a WAV per
#   declaration, played with FmrbAudio#play_wav in place of its jingle (the
#   jingle plays when no voice starts: the switch off, no file, a higher
#   voice still on, or Retro's play_wav false). Each of VOICE_NAMES is looked
#   for in VOICE_DIRS in order: the user's /home/voice/mahjong/ (a Tab5
#   takes it without a reflash), the user's own voice the device image
#   carries from flash_local/ (tools/mahjong_voice.rb, VOICEVOX), then the
#   bundled Open JTalk voice (tools/mahjong_voice_default.rb). The credit of
#   each directory used is shown in the lobby. Declarations close together
#   (a pon and a ron) keep the higher of VOICE_PRI for VOICE_HOLD_MS.
# - Tunes (and a voice of one's own per jingle) come from BGM below, and
#   /home/mahjong.toml over them:
#     bgm_lobby = "/home/music/lobby.fmsq"   bgm_play_track = 2 (an NSF's track)
#     voice_riichi = "/home/voice/riichi.wav"   (voice_kyuushu, voice_ryuukyoku too)
# - The player's switches are in /home/mahjong.toml (save_assist writes
#   them): "bgm" is the BGM's level (MJ12) 0 off, 1 low (the default),
#   2 mid, 3 high -- an older file's true reads as low and false as off --
#   "se" the effects (the jingles are effects) on or off, on by default,
#   and "voice" the declarations' voices on or off, on by default.
#   The level is FmrbAudio#main_volume (the MAIN instance only, so the
#   effects on SUB keep theirs); the kernel puts it back to 100 when the
#   app ends.

class MahjongApp < FmrbApp
  B_BGM = 130
  B_SE = 131
  B_VOICE = 132
  SND_CH = 4              # pulse1, pulse2, triangle, noise (FmrbAudio::CH_*)
  TENSE_LEFT = 10         # :tense from this many tiles left (or a riichi)
  BGM_SLOT = { lobby: 12, play: 13, tense: 14 }.freeze # FMSQ slots (machine-wide)
  # The BGM's levels (MJ12): off, low, mid, high as MAIN volume percents,
  # and their names on the button. Tentative values (2026-10-19).
  BGM_LEVELS = [0, 30, 55, 100].freeze
  BGM_NAMES = ["切", "小", "中", "大"].freeze
  BGM_DEFAULT = 1
  # scene => [path, NSF track] or nil (no tune: silent). The defaults are
  # shiru8bit's tunes shipped next to lost_in_hyperspace.nsf (CC BY 3.0,
  # ASSETS.md and that directory's CREDITS.md); /home/mahjong.toml's
  # bgm_lobby / bgm_play / bgm_tense lines replace them (chosen 2026-10-19).
  BGM = { lobby: ["/usr/share/sounds/nsf/after_the_rain.nsf", 0],
          play: ["/usr/share/sounds/nsf/spring_thing.nsf", 0],
          tense: ["/usr/share/sounds/nsf/encounter.nsf", 0] }.freeze
  # The declarations with a voice (NAME.wav), and where they are looked for:
  # [directory, its credit] (nil: the first line of its CREDITS.txt when that
  # names VOICEVOX, else VOICE_CREDIT -- the voice users make, chosen
  # 2026-10-21).
  VOICE_NAMES = [:riichi, :pon, :chi, :kan, :tsumo, :ron].freeze
  VOICE_DIRS = [["/home/voice/mahjong", nil], ["/usr/share/sounds/voice/mahjong", nil],
                ["/usr/share/sounds/voice/mahjong/default", "Open JTalk (Mei, CC BY 3.0)"]].freeze
  VOICE_CREDIT = "VOICEVOX:ずんだもん"
  VOICE_PRI = { riichi: 1, kyuushu: 1, chi: 2, pon: 3, kan: 4, tsumo: 5, ron: 6, ryuukyoku: 6 }.freeze
  VOICE_HOLD_MS = 1000
  SND_KEYS = ["bgm_lobby", "bgm_play", "bgm_tense", "voice_riichi", "voice_pon", "voice_chi", "voice_kan",
              "voice_ron", "voice_tsumo", "voice_kyuushu", "voice_ryuukyoku"].freeze

  # The effects' gain (C4b, 2026-10-22: the effects felt a little loud next
  # to the voices): every note_on volume of SFX is scaled by
  # SE_GAIN_NUM / SE_GAIN_DEN, rounded, with 0 kept at 0 and anything above
  # 0 kept at 1 or more (no note goes silent). 3/4 is about -2.5 dB (the
  # APU's 0-15 is linear in amplitude). The BGM (MAIN) is not touched.
  SE_GAIN_NUM = 3
  SE_GAIN_DEN = 4

  # name => [priority, voice, ...]; a voice is [channel, duty, then per note
  # freq (Hz; the noise: its period 0-15), ms, volume 0-15]; freq -1 rests.
  # The triangle has no volume (0). Priorities: 1 the frequent clicks, 2 the
  # turn and the count, 3 the jingles, 4 the fanfares.
  SFX = {
    discard: [1, [3, 0, 3, 20, 12, 3, 25, 6, 5, 30, 3]],
    discard_other: [1, [3, 0, 5, 20, 8, 6, 30, 4]],
    draw: [1, [1, 1, 1319, 30, 5, 1760, 40, 4]],
    turn: [2, [0, 2, 1047, 70, 8, 1568, 120, 7]],
    round: [3, [0, 2, 523, 90, 10, 659, 90, 10, 784, 90, 10, 1047, 240, 9], [2, 0, 131, 270, 0, 262, 240, 0]],
    yaku: [2, [0, 1, 1568, 40, 9, 2093, 70, 7]],
    points: [3, [0, 2, 784, 60, 11, 1047, 60, 11, 1319, 60, 11, 1568, 260, 10], [1, 1, 523, 180, 7, 784, 260, 6],
             [2, 0, 131, 180, 0, 196, 260, 0]],
    riichi: [3, [0, 2, 988, 70, 12, 1319, 70, 12, 1976, 200, 11], [1, 1, 494, 140, 7, 659, 200, 7]],
    pon: [3, [0, 1, 1568, 60, 12, -1, 30, 0, 1568, 90, 11], [3, 0, 4, 30, 8]],
    chi: [3, [0, 1, 1319, 60, 11, 1047, 100, 10]],
    kan: [3, [0, 2, 784, 80, 12, 1047, 80, 12, 1568, 180, 11], [2, 0, 98, 340, 0]],
    ron: [3, [3, 0, 2, 60, 12, 4, 60, 6], [0, 2, 1047, 80, 13, 1319, 80, 13, 1568, 80, 13, 2093, 320, 12],
          [1, 2, 523, 240, 9, 784, 320, 8], [2, 0, 131, 240, 0, 262, 320, 0]],
    tsumo: [3, [0, 2, 1568, 70, 12, 2093, 70, 12, 1568, 70, 12, 2637, 280, 11], [1, 1, 784, 210, 8, 1047, 280, 7],
            [2, 0, 196, 210, 0, 262, 280, 0]],
    kyuushu: [3, [0, 2, 1175, 100, 10, 988, 100, 10, 784, 240, 9], [2, 0, 98, 440, 0]],
    ryuukyoku: [3, [0, 2, 784, 150, 9, 698, 150, 9, 659, 150, 9, 523, 360, 8], [2, 0, 131, 450, 0, 98, 360, 0]],
    fanfare1: [4, [0, 2, 523, 110, 12, 659, 110, 12, 784, 110, 12, 1047, 220, 12, 784, 110, 11, 1047, 500, 12],
               [1, 1, 330, 110, 8, 392, 110, 8, 523, 110, 8, 659, 220, 8, 523, 110, 8, 659, 500, 8],
               [2, 0, 131, 330, 0, 175, 220, 0, 196, 110, 0, 131, 500, 0]],
    fanfare2: [4, [0, 2, 784, 100, 11, 880, 100, 11, 988, 100, 11, 1047, 450, 11], [1, 1, 523, 300, 7, 659, 450, 7],
               [2, 0, 196, 300, 0, 131, 450, 0]],
    fanfare3: [4, [0, 2, 659, 140, 10, 587, 140, 10, 659, 140, 10, 784, 420, 10], [2, 0, 131, 420, 0, 196, 420, 0]],
    fanfare4: [4, [0, 2, 659, 180, 9, 523, 180, 9, 440, 180, 9, 415, 500, 8], [2, 0, 110, 540, 0, 104, 500, 0]]
  }.freeze

  # ------------------------------------------------------------ start, stop, the switches

  def snd_start
    @snd_req = []
    @snd_q = []
    @snd_gen = [0, 0, 0, 0]
    @snd_busy = [0, 0, 0, 0]
    @snd_pri = [0, 0, 0, 0]
    @snd_scene = :lobby
    @snd_tune = nil         # the tune playing now ([path, track]) or nil
    @snd_loaded = {}        # FMSQ slot => the path loaded into it
    @snd_conf = {}
    @snd_bgm = BGM_DEFAULT
    @snd_se = true
    @snd_vo_on = true
    @snd_vo = [0, 0]        # the voice playing: [its VOICE_PRI, until (ms)]
    snd_load_conf
    found = snd_find_voices(VOICE_DIRS)
    @snd_voices = found[0]
    @snd_credit = found[1]
    VOICE_PRI.each do |n, _|
      w = @snd_conf["voice_#{n}"]
      @snd_voices[n] = w if w && !w.empty?
    end
    @audio = nil
    @audio = FmrbAudio.new(self)
    snd_bgm_volume
    snd_bgm_to(:lobby)
  rescue => e
    Log.info("mahjong: no sound (#{e.message})")
    @audio = nil
  end

  # The sound's settings out of /home/mahjong.toml (app_settings;
  # load_assist reads the rest).
  def snd_load_conf
    conf = app_settings
    v = conf["bgm"]
    @snd_bgm = snd_bgm_level(v) unless v.nil?
    v = conf["se"]
    @snd_se = v != false && v != 0 unless v.nil?
    v = conf["voice"]
    @snd_vo_on = v != false && v != 0 unless v.nil?
    conf.each do |k, x|
      if x.is_a?(String)
        @snd_conf[k] = x if SND_KEYS.index(k)
      elsif x.is_a?(Integer) && k.size > 6 && k[k.size - 6, 6] == "_track"
        @snd_conf[k] = x
      end
    end
  end

  # What save_assist writes for the sound: the switches and the tunes and
  # voices the player named there.
  def snd_settings(v)
    v["bgm"] = @snd_bgm
    v["se"] = @snd_se
    v["voice"] = @snd_vo_on != false
    @snd_conf.each { |k, x| v[k] = x }
    v
  end

  # A saved "bgm" as a level: 0-3, or an older file's true / false.
  def snd_bgm_level(v)
    return 0 if v == false
    return BGM_DEFAULT if v == true
    v.is_a?(Integer) && v >= 0 && v < BGM_LEVELS.size ? v : BGM_DEFAULT
  end

  # The button and B: off -> low -> mid -> high -> off.
  def snd_toggle_bgm
    @snd_bgm = (@snd_bgm + 1) % BGM_LEVELS.size
    if @snd_bgm > 0
      snd_bgm_volume
      snd_bgm_to(@snd_scene)
    else
      snd_bgm_stop
    end
    save_assist
    @dirty = true
    Log.info("mahjong: bgm #{BGM_LEVELS[@snd_bgm]}%")
  end

  # The level as the MAIN volume (off leaves it: nothing plays on MAIN).
  def snd_bgm_volume
    return unless @audio && @snd_bgm > 0
    @audio.main_volume = BGM_LEVELS[@snd_bgm]
  rescue => e
    Log.info("mahjong: bgm volume: #{e.message}")
  end

  def snd_toggle_se
    @snd_se = !@snd_se
    snd_quiet unless @snd_se
    save_assist
    @dirty = true
    Log.info("mahjong: se #{@snd_se}")
  end

  def snd_toggle_voice
    @snd_vo_on = !@snd_vo_on
    unless @snd_vo_on
      @audio.stop_wav if @audio
      @snd_vo = [0, 0]
    end
    save_assist
    @dirty = true
    Log.info("mahjong: voice #{@snd_vo_on}")
  rescue => e
    Log.info("mahjong: voice: #{e.message}")
  end

  # Every effect stops (the pending notes, the four channels, a WAV).
  def snd_quiet
    return unless @audio
    @snd_req = []
    @snd_q = []
    ch = 0
    while ch < SND_CH
      @snd_gen[ch] += 1
      @snd_busy[ch] = 0
      @audio.note_off(ch)
      ch += 1
    end
    @audio.stop_wav
  rescue => e
    Log.info("mahjong: sound stop: #{e.message}")
  end

  # The app closes: nothing may go on sounding after it.
  def snd_stop
    return unless @audio
    snd_bgm_stop
    snd_quiet
    @audio = nil
  end

  # ------------------------------------------------------------ the buttons (the list and the lobby)

  # The switches' labels; the voice's only while there is one.
  def snd_labels
    l = ["BGM #{BGM_NAMES[@snd_bgm || BGM_DEFAULT]}[B]", "効果音 #{@snd_se == false ? '切' : '入'}[E]"]
    l << "声 #{@snd_vo_on == false ? '切' : '入'}[V]" if @snd_voices && !@snd_voices.empty?
    l
  end

  # The switches from x; returns the x after them.
  def snd_buttons(x, y)
    l = snd_labels
    x += button(B_BGM, [], x, y, l[0]) + u(6)
    x += button(B_SE, [], x, y, l[1]) + u(6)
    x += button(B_VOICE, [], x, y, l[2]) + u(6) if l.size > 2
    x
  end

  def snd_buttons_w
    w = 0
    snd_labels.each { |s| w += tw(s) + u(18) }
    w
  end

  # The voices' credit (the VOICEVOX terms, the voice's licence) right of
  # the lobby's switches at (x, y), while a voice file is used.
  def snd_credit(x, y)
    return unless @snd_credit && @snd_vo_on != false
    text(x + u(2), y + u(4), fit("声: #{@snd_credit}", @x0 + @w - x - u(10)), C_DIM)
  end

  # B, E or V (with voices) in the list and the lobby (not while a name is typed).
  def snd_key(k)
    return false if @name_edit
    ch = k[1]
    ch += 32 if ch >= 65 && ch <= 90
    if ch == 98
      snd_toggle_bgm
    elsif ch == 101
      snd_toggle_se
    elsif ch == 118 && @snd_voices && !@snd_voices.empty?
      snd_toggle_voice
    else
      return false
    end
    true
  end

  # ------------------------------------------------------------ effects

  # Starts a sound (at the next snd_tick, plus delay ms).
  def snd_play(name, delay = 0)
    return unless @audio && @snd_se
    @snd_req << [name, delay]
    Log.info("mahjong: sfx #{name}")
  rescue => e
    Log.info("mahjong: sfx #{name}: #{e.message}")
  end

  # From on_update, after the drawing: starts what was asked for, plays the
  # notes that are due; returns the wait until the next (at most `wait`).
  def snd_tick(wait)
    return wait if @audio.nil? || (@snd_req.empty? && @snd_q.empty?)
    now = Machine.board_millis
    snd_begin(@snd_req.shift, now) until @snd_req.empty?
    nxt = nil
    i = 0
    while i < @snd_q.size
      e = @snd_q[i]
      if e[0] <= now
        if @snd_gen[e[1]] == e[5]
          e[2] >= 0 ? @audio.note_on(e[1], e[2], snd_se_vol(e[3]), e[4], 0) : @audio.note_off(e[1])
        end
        @snd_q.delete_at(i)
      else
        nxt = e[0] if nxt.nil? || e[0] < nxt
        i += 1
      end
    end
    return wait if nxt.nil?
    d = nxt - now
    d = 5 if d < 5
    d < wait ? d : wait
  end

  # A note's volume (0-15) with the effects' gain: 0 stays 0, above 0 is
  # at least 1.
  def snd_se_vol(v)
    return 0 if v <= 0
    g = (v * SE_GAIN_NUM * 2 + SE_GAIN_DEN) / (SE_GAIN_DEN * 2)
    g < 1 ? 1 : g
  end

  # Lays a sound's notes out as deadlines from now + its delay.
  def snd_begin(req, now)
    d = SFX[req[0]]
    return unless d
    pri = d[0]
    k = 1
    while k < d.size
      v = d[k]
      ch = v[0]
      if pri >= @snd_pri[ch] || now >= @snd_busy[ch]
        g = @snd_gen[ch] + 1
        @snd_gen[ch] = g
        t = now + req[1]
        j = 2
        while j + 2 < v.size
          @snd_q << [t, ch, v[j], v[j + 2], v[1], g]
          t += v[j + 1]
          j += 3
        end
        @snd_q << [t, ch, -1, 0, 0, g]
        @snd_busy[ch] = t
        @snd_pri[ch] = pri
      end
      k += 1
    end
  end

  # ------------------------------------------------------------ the events

  def snd_river_n(v)
    kw = v["kw"]
    return 0 unless kw.is_a?(Array)
    n = 0
    i = 0
    while i < kw.size
      n += kw[i].to_s.bytesize
      i += 1
    end
    n
  end

  # This seat's move (a draw or a discard to make), not during the round's name.
  def snd_my_turn?(v)
    r = v["r"]
    return false unless @seat && v["st"] == "play" && v["in"].nil? && r.is_a?(Array) && r.size > 7
    r[6] == @seat && r[7].to_i <= 1
  end

  # What a new view says happened (old: the view before; shout and intro:
  # @shout and @intro_until before take_view).
  def snd_view(v, old, shout, intro)
    return if @rp || @audio.nil?
    snd_scene_of(v)
    return if old.nil?
    st = v["st"]
    snd_play(:round) if @intro_until && intro.nil?
    if @shout && !@shout.equal?(shout)
      j = snd_jingle(@shout[0])
      snd_play(j) unless snd_voice(j)
    end
    if st == "play" && old["st"] == "play"
      if snd_river_n(v) > snd_river_n(old)
        ls = v["ls"]
        snd_play(@seat && ls.is_a?(Array) && ls[0] == @seat ? :discard : :discard_other)
      end
      d = v["dt"]
      r = v["r"]
      orr = old["r"]
      if @seat && v["in"].nil? && d.is_a?(Integer) && d >= 0 && r.is_a?(Array) && orr.is_a?(Array)
        od = old["dt"]
        snd_play(:draw) if !od.is_a?(Integer) || od < 0 || r[5] != orr[5]
      end
    end
    snd_play(:turn, 70) if snd_my_turn?(v) && !snd_my_turn?(old)
    if st == "result" && old["st"] != "result"
      res = v["res"]
      k = res.is_a?(Hash) ? res["k"].to_s : ""
      snd_play(:ryuukyoku) if k == "ryuukyoku" || k == "suufon_renda" || k == "suukaikan" || k == "suucha_riichi"
    end
    snd_fanfare(v) if st == "over" && old["st"] != "over"
  end

  def snd_jingle(type)
    return :ron if type == A_RON
    return :ryuukyoku if type == 13
    return :tsumo if type == A_TSUMO
    return :riichi if type == A_RIICHI
    return :kyuushu if type == A_KYUUSHU
    return :pon if type == A_PON
    type == A_CHI ? :chi : :kan
  end

  # ------------------------------------------------------------ the voices (C4)

  # [{name => path}, the credits joined or nil]: each of VOICE_NAMES from
  # the first of dirs that has it.
  def snd_find_voices(dirs)
    found = {}
    used = []
    VOICE_NAMES.each do |n|
      i = 0
      while i < dirs.size
        p = "#{dirs[i][0]}/#{n}.wav"
        if File.exist?(p)
          found[n] = p
          used << i unless used.index(i)
          break
        end
        i += 1
      end
    end
    credits = []
    used.sort.each do |i|
      c = dirs[i][1] || snd_credit_file(dirs[i][0]) || VOICE_CREDIT
      credits << c unless credits.index(c)
    end
    [found, credits.empty? ? nil : credits.join(" / ")]
  end

  # The first line of dir/CREDITS.txt when it is a VOICEVOX credit
  # (tools/mahjong_voice.rb writes "VOICEVOX:<character>").
  def snd_credit_file(dir)
    l = nil
    File.open("#{dir}/CREDITS.txt", "r") { |f| l = f.read.to_s.split("\n")[0].to_s.strip }
    l && l.size > 9 && l[0, 9] == "VOICEVOX:" ? l : nil
  rescue
    nil
  end

  # Whether a voice of priority pri may start now over the one in cur
  # ([priority, until]): a higher or equal one replaces it, a lower one
  # waits until it is over.
  def snd_voice_ok?(pri, cur, now)
    now >= cur[1] || pri >= cur[0]
  end

  # The declaration's voice, if there is one and the switch is on; true
  # when it started (the jingle is then left out).
  def snd_voice(name)
    return false unless @audio && @snd_vo_on && @snd_voices
    path = @snd_voices[name]
    return false unless path
    now = Machine.board_millis
    pri = VOICE_PRI[name] || 1
    return false unless snd_voice_ok?(pri, @snd_vo, now)
    ok = @audio.play_wav(path)
    Log.info("mahjong: voice #{name} #{path} #{ok}")
    return false unless ok
    @snd_vo = [pri, now + VOICE_HOLD_MS]
    true
  rescue => e
    Log.info("mahjong: voice #{name}: #{e.message}")
    false
  end

  # The fanfare of this seat's rank (a spectator: the winner's); after a
  # win's shout.
  def snd_fanfare(v)
    rank = 1
    fin = v["fin"]
    if @seat && fin.is_a?(Array)
      fin.each { |f| rank = f[1].to_i if f.is_a?(Array) && f[0] == @seat }
    end
    rank = 1 if rank < 1 || rank > 4
    wait = @shout ? @shout[2] - Machine.board_millis : 0
    snd_play([:fanfare1, :fanfare2, :fanfare3, :fanfare4][rank - 1], wait > 0 ? wait : 0)
  end

  # A step of the result's count (MJ9): a yaku line, or a winner's points.
  def snd_count(before)
    sig = @rv_sig
    return if sig.nil? || sig[0] < 1 || (before && before[0] == sig[0])
    wins = @view["res"]["wn"]
    return unless wins.is_a?(Array) && wins.size > 0
    n = 0
    i = 0
    while i < wins.size
      n += rv_steps(wins[i])
      return snd_play(:points) if n == sig[0]
      i += 1
    end
    snd_play(:yaku)
  end

  # ------------------------------------------------------------ the BGM

  def snd_scene_of(v)
    st = v["st"]
    s = if st == "wait" then :lobby
        elsif st == "over" then :none
        else
          r = v["r"]
          (v["ri"].is_a?(Integer) && v["ri"] != 0) || (st == "play" && r.is_a?(Array) && r.size > 5 && r[5].to_i <= TENSE_LEFT) ? :tense : :play
        end
    snd_bgm_to(s) if s != @snd_scene
  end

  # The scene's tune: [path, track] or nil.
  def snd_tune_of(scene)
    return nil if scene == :none
    p = @snd_conf["bgm_#{scene}"]
    tr = @snd_conf["bgm_#{scene}_track"]
    return [p, tr.is_a?(Integer) ? tr : 0] if p && !p.empty?
    BGM[scene]
  end

  def snd_bgm_to(scene)
    @snd_scene = scene
    return unless @audio && @snd_bgm > 0
    key = scene == :tense && snd_tune_of(:tense).nil? ? :play : scene
    tune = snd_tune_of(key)
    return if tune == @snd_tune
    return snd_bgm_stop if tune.nil?
    path = tune[0]
    dot = path.rindex(".")
    ext = dot ? path[dot + 1, path.size].to_s.downcase : ""
    cache = "#{CACHE_DIR}/bgm_#{key}.#{ext}"
    @gfx.sync_file(path, dest: cache)
    if ext == "nsf"
      @audio.play(cache, track: tune[1])
    else
      slot = BGM_SLOT[key]
      unless @snd_loaded[slot] == path
        @audio.load_fmsq_file(slot, cache)
        @snd_loaded[slot] = path
      end
      @audio.play_slot(slot, instance: FmrbAudio::MAIN)
    end
    @snd_tune = tune
    Log.info("mahjong: bgm #{key} #{path}")
  rescue => e
    Log.info("mahjong: bgm #{scene}: #{e.message}")
  end

  def snd_bgm_stop
    return unless @audio && @snd_tune
    @snd_tune = nil
    @audio.stop
  rescue => e
    Log.info("mahjong: bgm stop: #{e.message}")
  end

  # ------------------------------------------------------------ the base methods, with sound

  alias snd_base_take_view take_view
  alias snd_base_round_tick round_tick
  alias snd_base_leave_table leave_table
  alias snd_base_on_update on_update
  alias snd_base_lobby_key lobby_key
  alias snd_base_list_key list_key
  alias snd_base_run_button run_button
  alias snd_base_stop stop
  alias snd_base_on_destroy on_destroy

  def take_view(v)
    old = @view
    shout = @shout
    intro = @intro_until
    snd_base_take_view(v)
    snd_view(v, old, shout, intro)
  end

  def round_tick(now)
    before = @rv_sig
    snd_base_round_tick(now)
    snd_count(before) if @audio && !@rv_sig.equal?(before)
  end

  def leave_table
    snd_base_leave_table
    snd_bgm_to(:lobby) if @audio
  end

  def on_update
    snd_tick(snd_base_on_update)
  end

  def lobby_key(k)
    snd_base_lobby_key(k) unless snd_key(k)
  end

  def list_key(k)
    snd_base_list_key(k) unless snd_key(k)
  end

  def run_button(type, tiles)
    if type == B_BGM
      snd_toggle_bgm
    elsif type == B_SE
      snd_toggle_se
    elsif type == B_VOICE
      snd_toggle_voice
    else
      snd_base_run_button(type, tiles)
    end
  end

  # Every intended end (the close button, Ctrl+Q, the kernel's stop) comes
  # here while the app can still send; on_destroy is the last chance.
  def stop
    snd_stop
    snd_base_stop
  end

  def on_destroy
    snd_stop
    snd_base_on_destroy
  end
end
