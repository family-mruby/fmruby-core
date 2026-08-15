# MML demo - the same tune on the built-in APU and on an external instrument.
#
# Four tunes are written into this file as strings:
#
#   player.load_string("o4 l4 cdefgab>c")
#
# and a fifth is whatever .mml file is picked with the system file selector:
#
#   player.load_file("/usr/share/music/round.mml")
#
# A file brings what a string cannot say: the tempo, whether to repeat, and
# what plays each part (voice, duty, volume, program). Those go to the device
# as the file is loaded, so the output has to be chosen first - which is why
# switching output reloads the file. Tunes are written with
# tools/fm_asset_editor.
#
# The dialect is the one MIDI::MML::Sequence parses (imported from Midori);
# the playing is FmrbMidi::MmlPlayer, which hands timed commands to the C
# scheduler so the beat comes from a timer rather than from this app's
# update loop (doc/midi/report/p7_7.md).
#
# Keys 1-4 pick a tune, 5 replays the loaded file, f picks a file,
# o switches the output, l loops, space stops.

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

  # The rows under the tunes, in the order they are drawn.
  BTN_FILE = TUNES.size      # play the file that was picked
  BTN_OPEN = TUNES.size + 1  # pick one
  BTN_OUT  = TUNES.size + 2
  BTN_LOOP = TUNES.size + 3
  BTN_STOP = TUNES.size + 4
  BTN_COUNT = TUNES.size + 5

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
    @file_path = nil
    @file_name = nil
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
      when 53 # 5
        play_file
      when 102 # f
        open_file
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
      when BTN_FILE then play_file
      when BTN_OPEN then open_file
      when BTN_OUT then toggle_output
      when BTN_LOOP then toggle_loop
      when BTN_STOP then stop_tune
      else play_tune(index)
      end
    end
  end

  # The file selector is the system's, so the answer comes back as a message
  # rather than as a return value.
  def on_control(msg)
    return unless msg["cmd"] == "file_selected"

    path = msg["path"]
    if path.nil?
      @status = "no file picked"
      draw
      return
    end
    load_file(path)
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

  # --- tunes from a file ---

  def open_file
    @status = "pick a .mml file"
    draw
    request_file_select("open")
  end

  # Load and play a file. The device is chosen first because load_file sends
  # the file's voice, duty, volume and program to whatever is listening then.
  def load_file(path, start = true)
    @player.stop
    @player.device = out
    unless @player.load_file(path)
      @file_path = nil
      @file_name = nil
      @playing = false
      @status = "#{path.split("/").last}: #{@player.error}"
      Log.error("MmlDemo: #{@status}")
      draw
      return false
    end

    @file_path = path
    @file_name = path.split("/").last
    # The file decides whether it repeats; the l key still overrides it.
    @loop = @player.loop
    @index = BTN_FILE
    if start
      @player.start
      @playing = true
      @status = "#{@file_name} #{@external ? '-> MIDI out' : '-> APU'}"
    else
      @playing = false
      @status = "#{@file_name} loaded"
    end
    Log.info("MmlDemo: #{@status}")
    draw
    true
  end

  def play_file
    return open_file if @file_path.nil?

    load_file(@file_path)
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
    # A file's sound settings went to the device that was listening when it
    # was loaded, so the new one has to be told as well: load it again, and
    # keep playing if it was.
    return draw if @file_path.nil?

    was_playing = @playing
    status = @status
    return unless load_file(@file_path, was_playing)

    @status = status unless was_playing
    draw
  end

  def toggle_loop
    @loop = !@loop
    @player.loop = @loop
    @status = @loop ? "loop on" : "loop off"
    draw
  end

  # --- drawing ---

  # Nine rows have to fit the window (see mml.app.toml), so they are a little
  # tighter than they were when there were seven.
  def button_rect(index)
    [@user_area_x0 + 8, @user_area_y0 + 26 + (index * 18), @user_area_width - 16, 15]
  end

  def button_at(x, y)
    i = 0
    while i < BTN_COUNT
      r = button_rect(i)
      return i if x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]

      i += 1
    end
    nil
  end

  # A name longer than the button would be drawn over the edge of it.
  def fitted(name)
    return name if name.length <= 16

    "#{name[0, 15]}."
  end

  def label_for(index)
    return "#{index + 1} #{TUNES[index][0]}" if index < TUNES.size

    case index
    when BTN_FILE then @file_name ? "5 #{fitted(@file_name)}" : "5 (no file yet)"
    when BTN_OPEN then "f Open file..."
    when BTN_OUT then @external ? "o Out: MIDI" : "o Out: APU"
    when BTN_LOOP then @loop ? "l Loop: on" : "l Loop: off"
    else "space Stop"
    end
  end

  def lit?(index)
    return @playing && @index == index if index < TUNES.size

    case index
    when BTN_FILE then @playing && @index == BTN_FILE
    when BTN_OUT then @external
    when BTN_LOOP then @loop
    else false
    end
  end

  def draw
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width,
                   @user_area_height, COL_BG)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 4, "MML", COL_TEXT, COL_BG)
    @gfx.draw_text(@user_area_x0 + 8, @user_area_y0 + 16, @status, COL_TEXT, COL_BG)

    i = 0
    while i < BTN_COUNT
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
