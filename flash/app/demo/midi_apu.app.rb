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
#   5 SMF     play a .mid from /data/midi with FmrbMidi::SmfPlayer
#   6 Fast    speed the .mid up while it plays (what real-time playback buys)

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
  # from the built-in fixture without touching this list.
  SONGS = ["/data/midi/song.mid", "/data/midi/scale.mid"]

  BUTTONS = [
    ["1 Scale", :scale],
    ["2 Chord", :chord],
    ["3 Drums", :drums],
    ["4 BGM", :bgm],
    ["5 SMF", :smf],
    ["6 Fast", :fast]
  ]

  COL_BG    = 0x00
  COL_TEXT  = 0xFF
  COL_LABEL = 0x92
  COL_BTN   = 0x25
  COL_ON    = 0x1C

  def initialize
    super()
    @mode = nil
    @step = 0
    @bgm = false
    @next_at = 0
    @smf_playing = false
    @fast = false
    @status = "ready"
  end

  def on_create
    @device = FmrbMidi.device(self)
    @audio = FmrbAudio.new(self)
    @player = FmrbMidi::SmfPlayer.new(@device)
    Log.info("MidiApu: device=#{@device.info}")
    draw
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
    elsif @smf_playing
      # The song ended by itself.
      @smf_playing = false
      @status = "smf done"
      draw
    end

    # Come back sooner if a triggered note is due to be released before the
    # next step, otherwise short drum hits last until the next step.
    FmrbMidi.tick_delay(wait)
  end

  def on_event(ev)
    super(ev)
    case ev[:type]
    when :key_down
      case ev[:scancode]
      when 0x1E then play(:scale)
      when 0x1F then play(:chord)
      when 0x20 then play(:drums)
      when 0x21 then toggle_bgm
      when 0x22 then toggle_smf
      when 0x23 then toggle_fast
      end
    when :mouse_up
      index = button_at(ev[:x], ev[:y])
      return if index.nil?

      action = BUTTONS[index][1]
      case action
      when :bgm then toggle_bgm
      when :smf then toggle_smf
      when :fast then toggle_fast
      else play(action)
      end
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

  # Not named `start`: FmrbApp#start is the app entry point.
  def play(mode)
    @device.transport.all_off
    @mode = mode
    @step = 0
    @next_at = Machine.board_millis
    @status = "#{mode} playing"
    draw
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
      @device.note_on(SCALE[index], 100, channel: 0)
      @step += 1
      SCALE_NOTE_MS
    else
      @device.note_off(SCALE[index], 0, channel: 0)
      @step += 1
      SCALE_GAP_MS
    end
  end

  def step_chord
    if @step == 0
      # One note per voice: the APU cannot stack a chord on one channel.
      i = 0
      while i < CHORD.size
        @device.note_on(CHORD[i][1], 100, channel: CHORD[i][0])
        i += 1
      end
      @step = 1
      return CHORD_MS
    end

    i = 0
    while i < CHORD.size
      @device.note_off(CHORD[i][1], 0, channel: CHORD[i][0])
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
    @device.trigger(DRUMS[@step], 110, duration: 90, channel: 9)
    @step += 1
    DRUM_STEP_MS
  end

  def finish
    @mode = nil
    @step = 0
    @next_at = 0
    @status = "ready"
    draw
  end

  # Load and play a .mid straight from the filesystem. Stopping releases
  # whatever it was sounding, so nothing is left hanging.
  def toggle_smf
    if @smf_playing
      @player.stop
      @smf_playing = false
      @status = "smf stopped"
      draw
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
      draw
      return
    end

    # Measure what holding the song costs in the app's pool (P4 report 5).
    GC.start
    before = pool_used
    unless @player.load(path)
      @status = "load failed"
      Log.error("MidiApu: #{@player.error}")
      draw
      return
    end

    GC.start
    after = pool_used
    Log.info("MidiApu: #{path} loaded, pool used #{before} -> #{after} " \
             "(+#{after - before} B for #{File.size(path)} B of file)")

    # Songs in the wild use whatever channels they like, so let the transport
    # pick voices from what this file actually plays.
    @device.transport.auto_map(@player.channel_usage)
    @player.tempo_scale = @fast ? 1.5 : 1.0
    @player.start
    @smf_playing = true
    @status = "smf: #{path.split("/").last}"
    Log.info("MidiApu: playing #{path}")
    draw
  end

  # Tempo can be changed while the song plays - the thing the pre-converted
  # FMSQ path cannot do.
  def toggle_fast
    @fast = !@fast
    @player.tempo_scale = @fast ? 1.5 : 1.0 if @player
    @status = @fast ? "tempo x1.5" : "tempo x1.0"
    draw
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
    draw
  rescue => e
    @status = "BGM error"
    Log.error("MidiApu: BGM failed: #{e.message}")
    draw
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

  # --- drawing ---

  def button_rect(index)
    [@user_area_x0 + 8, @user_area_y0 + 34 + (index * 22), @user_area_width - 16, 18]
  end

  def button_at(x, y)
    i = 0
    while i < BUTTONS.size
      rect = button_rect(i)
      if x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3]
        return i
      end

      i += 1
    end
    nil
  end

  def draw
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, COL_BG)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 6, "MIDI -> APU", COL_TEXT)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 18, @status, COL_LABEL)

    i = 0
    while i < BUTTONS.size
      rect = button_rect(i)
      on = case BUTTONS[i][1]
           when :bgm then @bgm
           when :smf then @smf_playing
           when :fast then @fast
           else @mode == BUTTONS[i][1]
           end
      @gfx.fill_rect(rect[0], rect[1], rect[2], rect[3], on ? COL_ON : COL_BTN)
      @gfx.draw_text(rect[0] + 6, rect[1] + 5, BUTTONS[i][0], COL_TEXT)
      i += 1
    end

    draw_window_frame
    @gfx.present
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
