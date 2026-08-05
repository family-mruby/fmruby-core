# MIDI timing bench (P7.6)
#
# Plays the same short piece to the serial MIDI port twice: once with the
# player filing commands ahead for the C scheduler to send, and once with it
# sending from its own task the way it did before. The two runs are the
# before and after of "who owns the clock".
#
# Read the answer off the wire, not from here: with
# tools/fmrb_midi_monitor.rb running on the host, every byte arrives with a
# timestamp, so the spacing of the notes can be measured directly. The piece
# is scale.mid - eight notes exactly half a second apart - so any wobble is
# the machine's, not the music's.
#
#   ruby tools/fmrb_midi_monitor.rb --log /tmp/p76.jsonl --duration 40
#   python3 tool/debug/fmrb_dbg_client.py localhost:5555 spawn \
#       path=/app/debug/midi_time.app.rb
#   ruby tool/midi/midi_interval.rb /tmp/p76.jsonl
#
# The log lines here mark which run is which.

class MidiTimeApp < FmrbApp
  SONG = "/data/midi/scale.mid"
  # Long enough for the eight notes plus the tail, short enough to sit
  # through twice.
  RUN_MS = 6000
  GAP_MS = 1500

  def on_create
    @lines = []
    @phase = 0
    @phase_at = 0
    @device = FmrbMidi.sam2695_device
    if @device.nil?
      say("no serial MIDI port (start tools/fmrb_midi_monitor.rb first)")
      draw
      return
    end

    @player = FmrbMidi::SmfPlayer.new(@device)
    unless @player.load(SONG)
      say("cannot load #{SONG}: #{@player.error}")
      draw
      return
    end

    say("scheduler: #{FmrbMidi.scheduler ? 'yes' : 'no'}")
    start_phase(1)
    draw
  end

  def on_update
    return 300 if @phase == 0 || @player.nil?

    @player.tick if @player.playing?
    now = Machine.board_millis
    return 20 if now - @phase_at < RUN_MS

    case @phase
    when 1
      @player.stop
      say("run 1 (scheduled) done: #{@player.timing_stats}")
      @phase = 2
      @phase_at = now
      20
    when 2
      # A gap so the two runs are easy to tell apart in the log.
      start_phase(3) if now - @phase_at >= GAP_MS
      20
    when 3
      @player.stop
      say("run 2 (app task) done: #{@player.timing_stats}")
      @phase = 4
      draw
      300
    else
      300
    end
  end

  def on_destroy
    @player.stop if @player
  end

  private

  # Phase 1 files commands ahead; phase 3 sends them from this task.
  def start_phase(phase)
    @phase = phase
    @phase_at = Machine.board_millis
    @player.lookahead_us = (phase == 1) ? FmrbMidi::SmfPlayer::LOOKAHEAD_US : 0
    say(phase == 1 ? "run 1: scheduled (lookahead #{@player.lookahead_us}us)"
                   : "run 2: app task (lookahead 0)")
    @player.start
  end

  def say(text)
    Log.info("midi_time: #{text}")
    @lines << text
    @lines.shift while @lines.size > 8
  end

  def draw
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width,
                   @user_area_height, FmrbConst::THEME_WINDOW_BG)
    y = @user_area_y0 + 2
    i = 0
    while i < @lines.size
      @gfx.draw_text(@user_area_x0 + 2, y, @lines[i], FmrbConst::THEME_TEXT,
                     FmrbConst::THEME_WINDOW_BG)
      y += 9
      i += 1
    end
    draw_window_frame
    @gfx.present
  end
end

Log.info("MidiTimeApp.new")
begin
  app = MidiTimeApp.new
  app.start
rescue Exception => e
  Log.error("midi_time: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("midi_time script ended")
