# MML demo - the same tune on the built-in APU and on an external instrument.
#
# A tune is a string here, not a file:
#
#   player.load_string("o4 l4 cdefgab>c")
#
# The dialect is the one MIDI::MML::Sequence parses (imported from Midori);
# the playing is FmrbMidi::MmlPlayer, which hands timed commands to the C
# scheduler so the beat comes from a timer rather than from this app's
# update loop (doc/midi/report/p7_7.md).
#
# Keys 1-4 pick a tune, o switches the output, l loops, space stops.

class MmlDemoApp < FmrbApp
  # Written as three parts where a part is wanted: the player merges them
  # into one tune, so two voices land on the same instant.
  # The scale is written staccato (an eighth note and an eighth rest) rather
  # than legato: on a monophonic voice the notes of a legato scale run into
  # one another with no gap at all, which is harder to hear and impossible to
  # measure off a recording (doc/midi/report/p7_7.md). The onsets are still
  # half a second apart at 120 BPM.
  TUNES = [
    ["Scale", ["o4 l8 crdrerfrgrarbr>cr"]],
    ["Round", ["o5 l4 cegegegc", "o3 l2 c   g   c"]],
    ["March", ["o4 l8 [ccg g]2 l4 aagr", "o3 l4 [c   g]2 f e"]],
    ["Chime", ["o5 l4 e.c8 d2 r4 c.d8 e2", "o3 l2 c g"]]
  ]

  BPM = 120

  COL_BG    = FmrbConst::THEME_WINDOW_BG
  COL_TEXT  = FmrbConst::THEME_TEXT
  COL_BTN   = 0x25
  COL_ON    = 0x1C

  def initialize
    super()
    @index = 0
    @external = false
    @serial = nil
    @loop = false
    @status = "ready"
  end

  def on_create
    @device = FmrbMidi.device(self)
    @player = FmrbMidi::MmlPlayer.new(@device)
    @player.bpm = BPM
    load_tune(0)
    draw
  end

  def on_update
    if @player.playing?
      wait = @player.next_delay(200)
      now = Machine.board_millis
      @stats_at ||= 0
      if now - @stats_at > 5000
        @stats_at = now
        Log.info("MmlDemo: #{@player.timing_stats}")
      end
      return wait
    end

    if @playing
      @playing = false
      @status = "done"
      draw
    end
    200
  end

  def on_event(ev)
    super(ev)
    case ev[:type]
    when :key_down
      ch = ev[:character] || 0
      case ch
      when 49, 50, 51, 52 # 1-4
        play_tune(ch - 49)
      when 111 # o
        toggle_output
      when 108 # l
        toggle_loop
      when 32 # space
        stop_tune
      end
    when :mouse_up
      index = button_at(ev[:x], ev[:y])
      return if index.nil?

      case index
      when TUNES.size then toggle_output
      when TUNES.size + 1 then toggle_loop
      when TUNES.size + 2 then stop_tune
      else play_tune(index)
      end
    end
  end

  def on_destroy
    @player.stop if @player
    Log.info("MmlDemo: done")
  end

  private

  def out
    (@external && @serial) ? @serial : @device
  end

  def load_tune(index)
    @index = index
    parts = TUNES[index][1]
    @player.load_string(parts[0], channel: 0, velocity: 100)
    i = 1
    while i < parts.size
      @player.add_string(parts[i], channel: i, velocity: 80)
      i += 1
    end
  end

  def play_tune(index)
    return if index >= TUNES.size

    @player.stop
    load_tune(index)
    # An external instrument has a voice per note; the APU has four, and the
    # transport picks which channel gets which (see auto_map in the gem).
    @device.transport.map_channel(1, :pulse2) if @device
    @player.device = out
    @player.loop = @loop
    @player.start
    @playing = true
    @status = "#{TUNES[index][0]} #{@external ? '-> MIDI out' : '-> APU'}"
    Log.info("MmlDemo: #{@status}")
    draw
  end

  def stop_tune
    @player.stop
    @playing = false
    @status = "stopped"
    draw
  end

  def toggle_output
    if @external
      @external = false
      @status = "out: APU"
    else
      @serial = FmrbMidi.sam2695_device if @serial.nil?
      if @serial.nil?
        @status = "no MIDI port"
        draw
        return
      end
      @external = true
      @status = "out: serial"
    end
    @player.device = out
    draw
  end

  def toggle_loop
    @loop = !@loop
    @player.loop = @loop
    @status = @loop ? "loop on" : "loop off"
    draw
  end

  # --- drawing ---

  def button_rect(index)
    [@user_area_x0 + 8, @user_area_y0 + 30 + (index * 20), @user_area_width - 16, 16]
  end

  def button_at(x, y)
    i = 0
    total = TUNES.size + 3
    while i < total
      r = button_rect(i)
      return i if x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]

      i += 1
    end
    nil
  end

  def label_for(index)
    return "#{index + 1} #{TUNES[index][0]}" if index < TUNES.size

    case index - TUNES.size
    when 0 then @external ? "o Out: MIDI" : "o Out: APU"
    when 1 then @loop ? "l Loop: on" : "l Loop: off"
    else "space Stop"
    end
  end

  def lit?(index)
    return @playing && @index == index if index < TUNES.size

    case index - TUNES.size
    when 0 then @external
    when 1 then @loop
    else false
    end
  end

  def draw
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width,
                   @user_area_height, COL_BG)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 4, "MML", COL_TEXT, COL_BG)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 16, @status, COL_TEXT, COL_BG)

    i = 0
    total = TUNES.size + 3
    while i < total
      r = button_rect(i)
      @gfx.fill_rect(r[0], r[1], r[2], r[3], lit?(i) ? COL_ON : COL_BTN)
      @gfx.draw_text(r[0] + 6, r[1] + 4, label_for(i), FmrbGfx::WHITE)
      i += 1
    end

    draw_window_frame
    @gfx.present
  end
end

Log.info("MmlDemoApp.new")
begin
  app = MmlDemoApp.new
  app.start
rescue Exception => e
  Log.error("MmlDemo: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("MmlDemo script ended")
