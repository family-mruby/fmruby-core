# NSF Player Application
# Browse and play NSF music files with track selection.
#
# The file list is drawn by hand (FmrbUI has no list widget); the transport
# below it -- the track stepper, the repeat range, the folder button and
# Play/Stop -- is FmrbUI, so this file no longer computes button rectangles or
# hit-tests them.
#
# doc/nsf_player_v2:
# - The folder is chosen with the desktop's file dialog: pick any file in it
#   (the dialog picks files, not folders), and the list becomes that file's
#   folder. It is remembered in /home/nsf_player.toml (app_settings), with the
#   repeat range, so the player opens there next time.
# - Play always plays what is selected. While a song plays, selecting another
#   file or track turns the button back to "Play"; pressing it stops the song
#   and plays the new one.
# - An NSF has no length, so the audio side decides when a song is over (it
#   fell silent, its loop was heard twice, or it hit the time limit), fades it
#   out and stops it. The app asks about once a second (FmrbAudio#nsf_status)
#   and then moves on as the repeat range says: "1 song" stops, "File" goes
#   round the tracks of the file, "Folder" goes on to the next file's tracks
#   and round the folder.

class NsfPlayerApp < FmrbApp
  MUSIC_DIR = "/usr/share/sounds/nsf"
  CACHE_DIR = "/cache/nsf_player"
  LIST_Y = 2
  LIST_ITEM_H = 10
  INFO_H = 38
  TEXT_COLOR = FmrbConst::THEME_TEXT
  BG_COLOR = FmrbConst::THEME_WINDOW_BG
  HL_COLOR = FmrbConst::THEME_HIGHLIGHT
  BORDER_COLOR = FmrbConst::THEME_BORDER
  BTN_H = 11
  SB_W = 10          # scrollbar width

  # The repeat range, in the order the button steps through them. The value is
  # what app_settings keeps.
  REPEAT_MODES = ["one", "file", "folder"]
  REPEAT_LABELS = ["1 song", "File", "Folder"]

  # on_update runs every 330 ms; the audio side is asked every third one.
  STATUS_EVERY = 3
  # After a play, how long to wait for the audio side to say it has the new
  # song when its count of plays was not known beforehand.
  PLAY_SETTLE_MS = 1500

  def initialize
    super()
    @files = []
    @selected = 0
    @scroll = 0
    @track = 0
    @playing = false
    @nsf_info = nil
    @audio = nil
    @dir = MUSIC_DIR
    @repeat = 2
    # What is playing (the selection can move away from it)
    @play_file = nil
    @play_track = 0
    @play_info = nil
    # The audio side's count of plays: the last one seen, the one before our
    # play went out, and the one that is our song.
    @last_gen = nil
    @gen_before = nil
    @my_gen = nil
    @play_sent_ms = 0
    @tick = 0
    @status_misses = 0
  end

  def on_create
    @audio = FmrbAudio.new(self)
    load_settings
    build_transport
    scan_files
    select_file(0) if @files.length > 0
    # Learn the audio side's count of plays before the first play
    st = @audio.nsf_status
    @last_gen = st[:gen] if st
    draw_ui
  end

  # ---- Remembered settings (/home/nsf_player.toml) ----

  def load_settings
    conf = app_settings
    d = conf["dir"]
    @dir = d if d.is_a?(String) && d.length > 0 && dir_exists?(d)
    r = conf["repeat"]
    i = 0
    while i < REPEAT_MODES.length
      @repeat = i if REPEAT_MODES[i] == r
      i += 1
    end
  end

  def save_settings
    save_app_settings({ "dir" => @dir, "repeat" => REPEAT_MODES[@repeat] })
  end

  def dir_exists?(path)
    d = Dir.open(path)
    d.close
    true
  rescue
    false
  end

  # The folder part of a path ("/a/b/c.nsf" -> "/a/b", "/c.nsf" -> "/").
  def dirname(path)
    i = path.rindex("/")
    return "/" if i.nil? || i == 0
    path[0, i]
  end

  def basename(path)
    i = path.rindex("/")
    i.nil? ? path : path[i + 1, path.length - i - 1]
  end

  # Track stepper, repeat range, folder and Play/Stop. The track count differs
  # per file, so the stepper's range and its "/n" suffix are set in
  # select_file rather than here. A narrow Window (the small machines) puts
  # the repeat and folder buttons on the line above.
  def build_transport
    @ui = FmrbUI.new(self)
    y = @user_area_height - INFO_H + 23
    w = @user_area_width
    @track_st = @ui.stepper(:track, 2, y, 66, BTN_H, 1, 1, 1)
    if w >= 200
      @ui.button(:repeat, 72, y, 44, BTN_H, REPEAT_LABELS[@repeat])
      @ui.button(:folder, 120, y, 40, BTN_H, "Open")
    else
      @ui.button(:repeat, 2, y - 12, 44, BTN_H, REPEAT_LABELS[@repeat])
      @ui.button(:folder, 50, y - 12, 40, BTN_H, "Open")
    end
    @ui.toggle(:play, w - 80, y, 35, BTN_H, "Play", on_text: "Stop")
    # The bar sits in the right edge of the list. It hides itself while
    # everything fits, and clicks pass through to the list when it does.
    @ui.scrollbar(:sb, w - SB_W, LIST_Y, SB_W,
                  visible_count * LIST_ITEM_H, 0, 1)
    nil
  end

  def scan_files
    @files = []
    begin
      dir = Dir.open(@dir)
      while (entry = dir.read)
        next if entry == "." || entry == ".."
        @files << entry if entry.end_with?(".nsf") || entry.end_with?(".NSF")
      end
      dir.close
    rescue => e
      Log.error("Failed to scan #{@dir}: #{e.message}")
    end
    @files.sort!
  end

  def visible_count
    (@user_area_height - INFO_H - LIST_Y) / LIST_ITEM_H
  end

  def path_of(name)
    @dir == "/" ? "/#{name}" : "#{@dir}/#{name}"
  end

  def select_file(idx)
    return if idx < 0 || idx >= @files.length
    @selected = idx
    @track = 0
    @nsf_info = NsfHeader.parse(path_of(@files[idx]))
    if @nsf_info
      @track = @nsf_info.starting_song > 0 ? @nsf_info.starting_song - 1 : 0
      total = @nsf_info.total_songs
      @ui.set_range(:track, 1, total)
      @track_st.suffix = "/#{total}"
    end
  end

  # Scroll just enough to show the selected row.
  def reveal_selected
    vc = visible_count
    @scroll = @selected if @selected < @scroll
    @scroll = @selected - vc + 1 if @selected >= @scroll + vc
    @scroll = 0 if @scroll < 0
  end

  # Is the selection (file and track) what is playing?
  def selection_playing?
    @playing && @selected < @files.length && @play_file == path_of(@files[@selected]) &&
      @play_track == @track
  end

  def draw_ui
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    vc = visible_count

    @gfx.fill_rect(x0, y0, w, h, BG_COLOR)
    # The frame is drawn once at startup, and the clear above paints over
    # the rounded corners' outline where they reach into the user area. Any
    # app that wipes its whole user area has to put the frame back.
    draw_window_frame

    # File list. The playing file is marked with ">".
    i = 0
    while i < vc
      fi = @scroll + i
      break if fi >= @files.length
      iy = y0 + LIST_Y + i * LIST_ITEM_H
      mark = (@playing && @play_file == path_of(@files[fi])) ? ">" : ""
      if fi == @selected
        @gfx.fill_rect(x0, iy, w - 8, LIST_ITEM_H, HL_COLOR)
        @gfx.draw_text(x0 + 2, iy + 1, mark + @files[fi], FmrbGfx::WHITE, HL_COLOR)
      else
        @gfx.draw_text(x0 + 2, iy + 1, mark + @files[fi], TEXT_COLOR, BG_COLOR)
      end

      # Show track count if header is cached for selected
      if fi == @selected && @nsf_info
        info_txt = "#{@nsf_info.total_songs}trk"
        @gfx.draw_text(x0 + w - 40, iy + 1, info_txt,
                        fi == @selected ? FmrbGfx::WHITE : TEXT_COLOR,
                        fi == @selected ? HL_COLOR : BG_COLOR)
      end
      i += 1
    end

    # Separator
    info_y = y0 + h - INFO_H
    @gfx.draw_line(x0, info_y, x0 + w, info_y, BORDER_COLOR)

    # Info area (a narrow Window has its buttons on the artist's line)
    wide = w >= 200
    if @nsf_info && @files.length > 0
      name = @nsf_info.song_name
      name = @files[@selected] if name.length == 0
      @gfx.draw_text(x0 + 2, info_y + 2, name, TEXT_COLOR, BG_COLOR)
      @gfx.draw_text(x0 + 2, info_y + 12, @nsf_info.artist, TEXT_COLOR, BG_COLOR) if wide

    elsif @files.length == 0
      @gfx.draw_text(x0 + 2, info_y + 2, "No NSF files in", TEXT_COLOR, BG_COLOR)
      @gfx.draw_text(x0 + 2, info_y + 12, @dir, TEXT_COLOR, BG_COLOR) if wide
    else
      # Selected file failed NsfHeader.parse (bad magic / truncated header):
      # say so instead of leaving the info area silently empty.
      @gfx.draw_text(x0 + 2, info_y + 2, @files[@selected], TEXT_COLOR, BG_COLOR)
      @gfx.draw_text(x0 + 2, info_y + 14, "Unsupported file", FmrbGfx::COLOR_RED, BG_COLOR) if wide
    end

    # Everything above repainted the whole user area, so the widgets have to
    # be told their pixels are gone. flush issues the one present.
    sync_transport
    @ui.invalidate_all
    @ui.flush
  end

  # Put the transport in step with the app. The track and Play widgets only
  # exist while there is a playable file; the folder and repeat buttons always.
  def sync_transport
    @ui.set_range(:sb, @files.length, visible_count)
    @ui.set_value(:sb, @scroll)
    @ui.set_text(:repeat, REPEAT_LABELS[@repeat])
    live = @nsf_info && @files.length > 0
    @ui.set_visible(:track, live)
    # Stop stays reachable while a song plays, even with nothing playable
    # selected.
    @ui.set_visible(:play, live || @playing)
    @ui.set_value(:track, @track + 1) if live
    @ui.set_on(:play, live ? selection_playing? : @playing)
    nil
  end

  # Play the selection, stopping whatever was playing first.
  def play_current
    return unless @nsf_info && @selected < @files.length
    play_file(path_of(@files[@selected]), @track, @nsf_info)
  end

  def play_file(src_path, track, info)
    name = basename(src_path)
    cache_path = "#{CACHE_DIR}/#{name}"

    @audio.stop if @playing
    # Bring the cached copy on the graphics-audio side up to date
    Log.info("Syncing #{src_path} -> #{cache_path}")
    @gfx.sync_file(src_path, dest: cache_path)

    @audio.play(cache_path, track: track)
    @playing = true
    @play_file = src_path
    @play_track = track
    @play_info = info
    # The next answers may still be about the song before this one
    @gen_before = @last_gen
    @my_gen = nil
    @play_sent_ms = Machine.board_millis
    @tick = 0
    draw_ui
  end

  def change_track(new_track)
    return unless @nsf_info
    return if new_track < 0 || new_track >= @nsf_info.total_songs
    # Only a change to the playing file's track plays at once; on another
    # file it is a selection, played with Play.
    same_file = @playing && @play_file == path_of(@files[@selected])
    @track = new_track
    if same_file
      play_current
    else
      draw_ui
    end
  end

  def stop_current
    @audio.stop
    @playing = false
    @my_gen = nil
    draw_ui
  end

  # Play/Stop and Enter: stop the selection if it is what plays, otherwise
  # play it (stopping the song before).
  def toggle_play
    if selection_playing?
      stop_current
    else
      play_current
    end
  end

  def cycle_repeat
    @repeat = (@repeat + 1) % REPEAT_MODES.length
    save_settings
    draw_ui
  end

  # ---- The end of a song ----

  def on_update
    if @playing && @status_misses < 2
      @tick += 1
      if @tick >= STATUS_EVERY
        @tick = 0
        poll_status
      end
    end
    330
  end

  def poll_status
    st = @audio.nsf_status
    if st.nil?
      # No answer (an older graphics-audio): two in a row and the player stops
      # asking -- each costs a timeout -- and plays on as it always did.
      @status_misses += 1
      return
    end
    @status_misses = 0
    gen = st[:gen]
    @last_gen = gen
    if @my_gen.nil?
      # Has our play reached the audio side yet?
      if @gen_before.nil?
        return if Machine.board_millis - @play_sent_ms < PLAY_SETTLE_MS
      elsif gen == @gen_before
        return
      end
      @my_gen = gen
    end
    if gen != @my_gen
      # Somebody else started a song: ours is gone.
      @playing = false
      draw_ui
      return
    end
    case st[:state]
    when FmrbAudio::NSF_ENDED
      Log.info("NSF song over (reason #{st[:reason]}, laps #{st[:laps]}, " \
               "#{st[:frames] / 60} s)")
      advance
    when FmrbAudio::NSF_IDLE
      # Stopped from somewhere else, or it could not be played
      @playing = false
      draw_ui
    end
  end

  # Move on after a song ended by itself, as the repeat range says.
  def advance
    mode = REPEAT_MODES[@repeat]
    info = @play_info
    total = info ? info.total_songs : 1
    if mode == "one" || info.nil?
      @playing = false
      draw_ui
      return
    end
    next_track = @play_track + 1
    if next_track < total
      play_next(@play_file, next_track, info)
    elsif mode == "file"
      play_next(@play_file, 0, info)
    else
      next_file_in_folder
    end
  end

  # The next file of the folder (round to the first), from its first track.
  def next_file_in_folder
    return stop_current if @files.length == 0
    idx = -1
    i = 0
    while i < @files.length
      idx = i if path_of(@files[i]) == @play_file
      i += 1
    end
    tries = 0
    while tries < @files.length
      idx = (idx + 1) % @files.length
      tries += 1
      path = path_of(@files[idx])
      info = NsfHeader.parse(path)
      next unless info
      select_file(idx)
      @track = 0
      reveal_selected
      play_file(path, 0, info)
      return
    end
    stop_current
  end

  # Another track of the playing file; the selection follows it.
  def play_next(path, track, info)
    i = 0
    while i < @files.length
      if path_of(@files[i]) == path
        select_file(i) if i != @selected
        reveal_selected
      end
      i += 1
    end
    @track = track
    play_file(path, track, info)
  end

  # ---- The folder ----

  # The desktop's file dialog answers with a file; its folder becomes ours.
  def on_control(msg)
    return unless msg["cmd"] == "file_selected"
    path = msg["path"]
    return if path.nil? || path.length == 0
    dir = dir_exists?(path) ? path : dirname(path)
    @dir = dir
    save_settings
    scan_files
    @scroll = 0
    @selected = 0
    @nsf_info = nil
    picked = basename(path)
    idx = 0
    i = 0
    while i < @files.length
      idx = i if @files[i] == picked
      i += 1
    end
    select_file(idx) if @files.length > 0
    reveal_selected
    draw_ui
  end

  def on_event(ev)

    if ev[:type] == :key_down
      ch = ev[:character] || 0
      # The arrows by scancode: the keycode is an SDL keysym on the
      # simulator, so KEY_UP (a HID usage) only matches the scancode.
      sc = ev[:scancode] || 0

      case sc
      when FmrbConst::KEY_UP
        if @selected > 0
          select_file(@selected - 1)
          reveal_selected
          draw_ui
        end
      when FmrbConst::KEY_DOWN
        if @selected < @files.length - 1
          select_file(@selected + 1)
          reveal_selected
          draw_ui
        end
      when FmrbConst::KEY_LEFT
        change_track(@track - 1) if @nsf_info
      when FmrbConst::KEY_RIGHT
        change_track(@track + 1) if @nsf_info
      else
        if ch == 10 || ch == 13  # Enter
          toggle_play
        elsif ch == 27 || ch == 113  # Escape or 'q'
          stop_current if @playing
        end
      end
    end

    case @ui.handle(ev)
    when :track  then change_track(@ui.value(:track) - 1)
    when :play   then toggle_play
    when :repeat then cycle_repeat
    when :folder then request_file_select("open")
    when :sb     then @scroll = @ui.value(:sb); draw_ui
    else
      @ui.flush
      handle_click(ev[:x], ev[:y]) if ev[:type] == :mouse_up
    end
  end

  # Only the file list is left here; the transport is FmrbUI's.
  def handle_click(x, y)
    y0 = @user_area_y0
    info_y = y0 + @user_area_height - INFO_H
    return if y < y0 + LIST_Y || y >= info_y
    idx = @scroll + (y - y0 - LIST_Y) / LIST_ITEM_H
    return if idx < 0 || idx >= @files.length
    select_file(idx)
    draw_ui
    nil
  end

  def on_destroy
    @audio.stop if @playing
    Log.info("NSF Player destroyed")
  end
end

Log.info("NsfPlayerApp.new")
begin
  app = NsfPlayerApp.new
  Log.info("NsfPlayerApp created successfully")
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("NSF Player script ended")
