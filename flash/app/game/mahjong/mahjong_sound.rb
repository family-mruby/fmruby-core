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
# - Tunes and voices (a WAV in place of a jingle, Modern only; Retro and a
#   failed play_wav fall back to the jingle) come from BGM / VOICE below,
#   and /home/mahjong.toml over them:
#     bgm_lobby = "/home/music/lobby.fmsq"   bgm_play_track = 2 (an NSF's track)
#     voice_riichi = "/home/voice/riichi.wav"
# - The player's switches "bgm" and "se" (the jingles are effects) are in
#   /home/mahjong.toml (save_assist writes them), both on by default.

class MahjongApp < FmrbApp
  B_BGM = 130
  B_SE = 131
  SND_CH = 4              # pulse1, pulse2, triangle, noise (FmrbAudio::CH_*)
  TENSE_LEFT = 10         # :tense from this many tiles left (or a riichi)
  BGM_SLOT = { lobby: 12, play: 13, tense: 14 }.freeze # FMSQ slots (machine-wide)
  # scene => [path, NSF track] or nil (no tune: silent). The defaults are
  # shiru8bit's tunes shipped next to lost_in_hyperspace.nsf (CC BY 3.0,
  # ASSETS.md and that directory's CREDITS.md); /home/mahjong.toml's
  # bgm_lobby / bgm_play / bgm_tense lines replace them (chosen 2026-10-19).
  BGM = { lobby: ["/usr/share/sounds/nsf/after_the_rain.nsf", 0],
          play: ["/usr/share/sounds/nsf/spring_thing.nsf", 0],
          tense: ["/usr/share/sounds/nsf/encounter.nsf", 0] }.freeze
  # A WAV per jingle (nil: the jingle).
  VOICE = { riichi: nil, pon: nil, chi: nil, kan: nil, ron: nil, tsumo: nil, kyuushu: nil, ryuukyoku: nil }.freeze
  SND_KEYS = ["bgm_lobby", "bgm_play", "bgm_tense", "voice_riichi", "voice_pon", "voice_chi", "voice_kan",
              "voice_ron", "voice_tsumo", "voice_kyuushu", "voice_ryuukyoku"].freeze

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
    @snd_bgm = true
    @snd_se = true
    snd_load_conf
    @audio = nil
    @audio = FmrbAudio.new(self)
    snd_bgm_to(:lobby)
  rescue => e
    Log.info("mahjong: no sound (#{e.message})")
    @audio = nil
  end

  # ASSIST_FILE's lines for the sound (load_assist reads the rest).
  def snd_load_conf
    File.open(ASSIST_FILE, "r") do |f|
      f.read.to_s.split("\n").each do |l|
        i = l.index("=")
        next unless i
        k = l[0, i].strip
        v = l[i + 1, l.size].to_s.strip
        if k == "bgm" || k == "se"
          on = v != "false" && v != "0"
          if k == "bgm"
            @snd_bgm = on
          else
            @snd_se = on
          end
        elsif v.size >= 2 && v[0] == "\"" && v[v.size - 1] == "\""
          @snd_conf[k] = v[1, v.size - 2] if SND_KEYS.index(k)
        elsif k.size > 6 && k[k.size - 6, 6] == "_track"
          @snd_conf[k] = v.to_i
        end
      end
    end
  rescue
    nil
  end

  # What save_assist adds to ASSIST_FILE: the switches and the tunes and
  # voices the player named there.
  def snd_lines
    s = "# Sound: BGM and the effects on (true) or off (false)\nbgm = #{@snd_bgm}\nse = #{@snd_se}\n"
    @snd_conf.each do |k, v|
      s = v.is_a?(String) ? "#{s}#{k} = \"#{v}\"\n" : "#{s}#{k} = #{v}\n"
    end
    s
  end

  def snd_toggle_bgm
    @snd_bgm = !@snd_bgm
    if @snd_bgm
      snd_bgm_to(@snd_scene)
    else
      snd_bgm_stop
    end
    save_assist
    @dirty = true
    Log.info("mahjong: bgm #{@snd_bgm}")
  end

  def snd_toggle_se
    @snd_se = !@snd_se
    snd_quiet unless @snd_se
    save_assist
    @dirty = true
    Log.info("mahjong: se #{@snd_se}")
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

  def snd_labels
    ["BGM #{@snd_bgm == false ? '切' : '入'}[B]", "効果音 #{@snd_se == false ? '切' : '入'}[E]"]
  end

  # The two switches from x; returns the x after them.
  def snd_buttons(x, y)
    l = snd_labels
    x += button(B_BGM, [], x, y, l[0]) + u(6)
    x + button(B_SE, [], x, y, l[1]) + u(6)
  end

  def snd_buttons_w
    l = snd_labels
    tw(l[0]) + tw(l[1]) + u(36)
  end

  # B or E in the list and the lobby (not while a name is typed).
  def snd_key(k)
    return false if @name_edit
    ch = k[1]
    ch += 32 if ch >= 65 && ch <= 90
    if ch == 98
      snd_toggle_bgm
    elsif ch == 101
      snd_toggle_se
    else
      return false
    end
    true
  end

  # ------------------------------------------------------------ effects

  # Starts a sound (at the next snd_tick, plus delay ms). A jingle with a
  # voice tries the WAV first.
  def snd_play(name, delay = 0)
    return unless @audio && @snd_se
    w = @snd_conf["voice_#{name}"] || VOICE[name]
    return if w && delay == 0 && @audio.play_wav(w)
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
          e[2] >= 0 ? @audio.note_on(e[1], e[2], e[3], e[4], 0) : @audio.note_off(e[1])
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
    snd_play(snd_jingle(@shout[0])) if @shout && !@shout.equal?(shout)
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
    return unless @audio && @snd_bgm
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
