# MIDI APU demo - plays the built-in APU through the MIDI API.
#
# Everything here goes through MIDI::Device (picoruby-midi) on top of
# FmrbMidi::ApuTransport, so the same code shape works for an external MIDI
# instrument once a hardware transport exists.
#
# Buttons (click, or press 1-4):
#   1 Scale   C4 to C5 on channel 0 (pulse 1)
#   2 Chord   C-E-G at once on channels 0/1/2 (pulse 1, pulse 2, triangle)
#   3 Drums   channel 9, using device.trigger (note off is scheduled)
#   4 BGM     FMSQ music on the other APU instance, mixed with the MIDI notes
#   5 SMF     play a .mid from /usr/share/sounds/midi with FmrbMidi::SmfPlayer
#   6 Fast    speed the .mid up while it plays (what real-time playback buys)
#   7 Out     send to the APU or out of the serial MIDI port (Unit MIDI)
#
# The panel is an FmrbUI widget list: the three sequence buttons share one
# exclusive group so the running one shows, the four switches are plain
# toggles, and the status line is a Label. Keys 1-7 run the same handlers and
# then let refresh put the widgets back in step with the app's state.

class MidiApuApp < FmrbApp
  BGM_SRC   = "/app/game/rpg_demo/bgm.fmsq"
  CACHE_DIR = "/cache/app/midi_apu"
  BGM_SLOT  = 0

  SCALE = [60, 62, 64, 65, 67, 69, 71, 72]
  SCALE_NOTE_MS = 200
  SCALE_GAP_MS = 60
  CHORD = [[0, 60], [1, 64], [2, 67]]
  CHORD_MS = 1200
  # Kick, hi-hat, snare, hi-hat on GM percussion note numbers.
  DRUMS = [36, 42, 38, 42, 36, 42, 38, 42]
  DRUM_STEP_MS = 180

  # First existing file wins, so dropping a song in as song.mid takes over
  # without touching this list. The SMF player app (tool/smf_player) shows
  # everything in the directory instead.
  SONGS = ["/usr/share/sounds/midi/song.mid", "/usr/share/sounds/midi/joplin_entertainer.mid",
           "/usr/share/sounds/midi/scale.mid"]

  def initialize
    super()
    @mode = nil
    @step = 0
    @bgm = false
    @next_at = 0
    @smf_playing = false
    @fast = false
    @external = false
    @serial = nil
    @status = "ready"
  end

  def on_create
    # Collect between updates instead of inside a note (doc/midi/report/p7.md).
    self.idle_gc = true
    @device = FmrbMidi.device(self)
    @audio = FmrbAudio.new(self)
    @player = FmrbMidi::SmfPlayer.new(@device)
    Log.info("MidiApu: device=#{@device.info}")
    build_panel
    clear_user_area(FmrbConst::THEME_WINDOW_BG)
    draw_window_frame
    refresh
  end

  # Two clocks share this loop: the sequence (when the next note starts) and
  # the note offs that device.trigger scheduled. They must be kept apart -
  # waking early to release a note must not also advance the sequence - so
  # the next step keeps its own deadline instead of riding on the delay.
  def on_update
    # Family mruby has no MIDI timer task, so the app pumps the scheduled
    # note offs itself (see the gem notes).
    FmrbMidi.tick

    now = Machine.board_millis
    if @mode && now >= @next_at
      delay = case @mode
              when :scale then step_scale
              when :chord then step_chord
              when :drums then step_drums
              else 100
              end
      @next_at = Machine.board_millis + delay
    end

    wait = @mode ? (@next_at - Machine.board_millis) : 100
    wait = 0 if wait < 0

    # The SMF player keeps its own schedule; ask it how long we may sleep.
    if @player && @player.playing?
      wait = @player.next_delay(wait)
      # Report how late events are actually going out (device timing is
      # dominated by GC and by whatever else runs at a higher priority).
      now_ms = Machine.board_millis
      @stats_at ||= 0
      if now_ms - @stats_at > 5000
        @stats_at = now_ms
        Log.info("MidiApu: #{@player.timing_stats} #{gc_line}")
      end
    elsif @smf_playing
      # The song ended by itself.
      @smf_playing = false
      @status = "smf done"
      Log.info("MidiApu: final #{@player.timing_stats}")
      refresh
    end

    # Come back sooner if a triggered note is due to be released before the
    # next step, otherwise short drum hits last until the next step.
    FmrbMidi.tick_delay(wait)
  end

  def on_event(ev)
    if ev[:type] == :key_down
      case ev[:scancode]
      when 0x1E then play(:scale)
      when 0x1F then play(:chord)
      when 0x20 then play(:drums)
      when 0x21 then toggle_bgm
      when 0x22 then toggle_smf
      when 0x23 then toggle_fast
      when 0x24 then toggle_out
      end
      return
    end

    id = @ui.handle(ev)
    if id.nil?
      @ui.flush
      return
    end
    case id
    when :scale then play(:scale)
    when :chord then play(:chord)
    when :drums then play(:drums)
    when :bgm  then toggle_bgm
    when :smf  then toggle_smf
    when :fast then toggle_fast
    when :out  then toggle_out
    end
  end

  def on_destroy
    @player.stop if @player
    @device.all_notes_off(channel: 0) if @device
    @device.transport.all_off if @device
    @audio.stop if @audio && @bgm
    Log.info("MidiApu: done")
  end

  private

  # Whichever output "7 Out" currently points at. The demo buttons and the
  # SMF player both use it, so the APU and an external instrument can be
  # compared by ear on the same material.
  def out
    (@external && @serial) ? @serial : @device
  end

  # Not named `start`: FmrbApp#start is the app entry point.
  def play(mode)
    out.transport.all_off
    @mode = mode
    @step = 0
    @next_at = Machine.board_millis
    @status = "#{mode} playing"
    refresh
  end

  # Even steps start a note, odd steps release it. The gap keeps the notes
  # separable (both by ear and for the pitch measurement in the report) and
  # makes sure nothing is left sounding when the scale ends.
  def step_scale
    index = @step / 2
    if index >= SCALE.size
      finish
      return 100
    end

    if @step % 2 == 0
      out.note_on(SCALE[index], 100, channel: 0)
      @step += 1
      SCALE_NOTE_MS
    else
      out.note_off(SCALE[index], 0, channel: 0)
      @step += 1
      SCALE_GAP_MS
    end
  end

  def step_chord
    if @step == 0
      # One note per voice: the APU cannot stack a chord on one channel.
      i = 0
      while i < CHORD.size
        out.note_on(CHORD[i][1], 100, channel: CHORD[i][0])
        i += 1
      end
      @step = 1
      return CHORD_MS
    end

    i = 0
    while i < CHORD.size
      out.note_off(CHORD[i][1], 0, channel: CHORD[i][0])
      i += 1
    end
    finish
    100
  end

  def step_drums
    if @step >= DRUMS.size
      finish
      return 100
    end

    # trigger sends the note off by itself once FmrbMidi.tick comes round.
    out.trigger(DRUMS[@step], 110, duration: 90, channel: 9)
    @step += 1
    DRUM_STEP_MS
  end

  def finish
    @mode = nil
    @step = 0
    @next_at = 0
    @status = "ready"
    refresh
  end

  # Load and play a .mid straight from the filesystem. Stopping releases
  # whatever it was sounding, so nothing is left hanging.
  def toggle_smf
    if @smf_playing
      @player.stop
      @smf_playing = false
      @status = "smf stopped"
      refresh
      return
    end

    path = nil
    i = 0
    while i < SONGS.size
      path = SONGS[i] if path.nil? && File.exist?(SONGS[i])
      i += 1
    end
    if path.nil?
      @status = "no .mid found"
      refresh
      return
    end

    # Time each phase: on a device the Ruby cost of loading and scanning is
    # what makes the button feel unresponsive, and it is not obvious which
    # part dominates.
    t0 = Machine.board_millis
    before = pool_used
    unless @player.load(path)
      @status = "load failed"
      Log.error("MidiApu: #{@player.error}")
      refresh
      return
    end

    t1 = Machine.board_millis
    after = pool_used
    Log.info("MidiApu: #{path} loaded in #{t1 - t0}ms, pool used #{before} -> " \
             "#{after} (+#{after - before} B for #{File.size(path)} B of file)")
    Log.info("MidiApu: before scan #{gc_line}")

    # Songs in the wild use whatever channels they like, so let the transport
    # pick voices from what this file actually plays. This walks every event
    # in the file, which is the expensive part on a device.
    usage = @player.channel_usage
    t2 = Machine.board_millis
    Log.info("MidiApu: channel_usage scan took #{t2 - t1}ms #{gc_line}")
    @device.transport.auto_map(usage)
    @player.tempo_scale = @fast ? 1.5 : 1.0
    @player.start
    @smf_playing = true
    @status = "smf: #{path.split("/").last}"
    Log.info("MidiApu: playing #{path}")
    refresh
  end

  # Move the song between the built-in APU and an external MIDI instrument
  # on the serial port. The player does not know the difference, which is
  # the whole point of it talking to a MIDI::Device.
  def toggle_out
    if @external
      @player.device = @device if @player
      @external = false
      @status = "out: APU"
    else
      @serial = FmrbMidi.sam2695_device if @serial.nil?
      if @serial.nil?
        @status = "no MIDI port"
        Log.warn("MidiApu: serial MIDI port not available")
        refresh
        return
      end
      @player.device = @serial if @player
      @external = true
      @status = "out: serial"
    end
    refresh
  end

  # Tempo can be changed while the song plays - the thing the pre-converted
  # FMSQ path cannot do.
  def toggle_fast
    @fast = !@fast
    @player.tempo_scale = @fast ? 1.5 : 1.0 if @player
    @status = @fast ? "tempo x1.5" : "tempo x1.0"
    refresh
  end

  def toggle_bgm
    if @bgm
      @audio.stop
      @bgm = false
      @status = "BGM off"
    else
      dest = "#{CACHE_DIR}/bgm.fmsq"
      @gfx.sync_file(BGM_SRC, dest: dest)
      @audio.load_fmsq_file(BGM_SLOT, dest)
      # Slot 0 is the MAIN APU instance; MIDI notes go to SUB, so the two
      # mix instead of stealing each other's voices.
      @audio.play_slot(BGM_SLOT, instance: 0)
      @bgm = true
      @status = "BGM on"
    end
    refresh
  rescue => e
    @status = "BGM error"
    Log.error("MidiApu: BGM failed: #{e.message}")
    refresh
  end

  # What the collector has been doing. :live and :total are always there;
  # the pause figures need a measurement build (FMRB_GC_PROFILE=1, see
  # doc/midi/report/p6.md) and read as 0 otherwise. This is the line that
  # answers "was that pause GC?" without having to guess.
  def gc_line
    st = GC.stat
    # pause* is what stopped the app (the allocation path collecting);
    # step* is the same work taken while it was idle instead, and jit is how
    # long a step made a waiting message wait. With idle_gc on, pause should
    # be flat and step should be the one that climbs (report/p7.md).
    "[gc live=#{st[:live]} n=#{st[:total] || 0} major=#{st[:major] || 0} " \
      "pause=#{st[:prof_sync_count] || 0}x tot=#{(st[:prof_sync_total_us] || 0) / 1000}ms " \
      "max=#{(st[:prof_sync_max_us] || 0) / 1000}ms " \
      "step=#{st[:prof_step_count] || 0}x tot=#{(st[:prof_step_total_us] || 0) / 1000}ms " \
      "max=#{(st[:prof_step_max_us] || 0) / 1000}ms " \
      "jit=#{st[:prof_step_jitter_count] || 0}x max=#{(st[:prof_step_jitter_max_us] || 0) / 1000}ms]"
  end

  # This app's slice of the VM pool, from the process table.
  def pool_used
    list = FmrbApp.ps
    i = 0
    while i < list.size
      return list[i][:mem_used] if list[i][:name] == "MIDI APU"
      i += 1
    end
    0
  rescue => e
    Log.warn("MidiApu: pool_used failed: #{e.message}")
    0
  end

  # --- panel ---

  # Rows are 22px apart starting below the two text lines, the same layout the
  # hand-drawn version had. The three sequence buttons are toggles in one
  # group rather than plain buttons so the running one stays lit, which is
  # what the old draw did with @mode.
  def build_panel
    @ui = FmrbUI.new(self)
    w = @user_area_width - 16
    @ui.label(:title, 8, 6, w, 10, "MIDI -> APU")
    @ui.label(:status, 8, 18, w, 10, @status)
    @ui.toggle(:scale, 8, 34, w, 18, "1 Scale", group: :mode)
    @ui.toggle(:chord, 8, 56, w, 18, "2 Chord", group: :mode)
    @ui.toggle(:drums, 8, 78, w, 18, "3 Drums", group: :mode)
    @ui.toggle(:bgm, 8, 100, w, 18, "4 BGM")
    @ui.toggle(:smf, 8, 122, w, 18, "5 SMF")
    @ui.toggle(:fast, 8, 144, w, 18, "6 Fast")
    @ui.toggle(:out, 8, 166, w, 18, "7 Out", on_text: "7 Out: serial")
    nil
  end

  # Put the widgets back in step with the app and draw whatever changed. It
  # runs after every action, from the keys as well as the mouse, and it is
  # also what corrects a toggle the user flipped for something that then
  # failed (no .mid on disk, no serial port). Nothing is drawn when nothing
  # moved.
  def refresh
    @ui.set_text(:status, @status)
    m = @mode
    @ui.set_on(:scale, m == :scale)
    @ui.set_on(:chord, m == :chord)
    @ui.set_on(:drums, m == :drums)
    @ui.set_on(:bgm, @bgm)
    @ui.set_on(:smf, @smf_playing)
    @ui.set_on(:fast, @fast)
    @ui.set_on(:out, @external)
    @ui.flush
    nil
  end
end

Log.info("MidiApuApp.new")
begin
  app = MidiApuApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
