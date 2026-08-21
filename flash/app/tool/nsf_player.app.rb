# NSF Player Application
# Browse and play NSF music files with track selection.
#
# The file list is drawn by hand (FmrbUI has no list widget); the transport
# below it -- the track stepper and Play/Stop -- is FmrbUI, so this file no
# longer computes button rectangles or hit-tests them.

class NsfPlayerApp < FmrbApp
  #MUSIC_DIR = "/home/music"
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

  def initialize
    super()
    @files = []
    @selected = 0
    @scroll = 0
    @track = 0
    @playing = false
    @nsf_info = nil
    @audio = nil
  end

  def on_create
    @audio = FmrbAudio.new(self)
    build_transport
    scan_files
    select_file(0) if @files.length > 0
    draw_ui
  end

  # Track stepper and Play/Stop, along the bottom of the info area. The track
  # count differs per file, so the stepper's range and its "/n" suffix are set
  # in select_file rather than here.
  def build_transport
    @ui = FmrbUI.new(self)
    y = @user_area_height - INFO_H + 23
    @track_st = @ui.stepper(:track, 2, y, 66, BTN_H, 1, 1, 1)
    @ui.toggle(:play, @user_area_width - 80, y, 35, BTN_H, "Play", on_text: "Stop")
  end

  def scan_files
    @files = []
    begin
      dir = Dir.open(MUSIC_DIR)
      while (entry = dir.read)
        next if entry == "." || entry == ".."
        @files << entry if entry.end_with?(".nsf")
      end
      dir.close
    rescue => e
      Log.error("Failed to scan #{MUSIC_DIR}: #{e.message}")
    end
    @files.sort!
  end

  def visible_count
    (@user_area_height - INFO_H - LIST_Y) / LIST_ITEM_H
  end

  def select_file(idx)
    return if idx < 0 || idx >= @files.length
    @selected = idx
    @track = 0
    path = "#{MUSIC_DIR}/#{@files[idx]}"
    @nsf_info = NsfHeader.parse(path)
    if @nsf_info
      @track = @nsf_info.starting_song > 0 ? @nsf_info.starting_song - 1 : 0
      total = @nsf_info.total_songs
      @ui.set_range(:track, 1, total)
      @track_st.suffix = "/#{total}"
    end
  end

  def draw_ui
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    vc = visible_count

    @gfx.fill_rect(x0, y0, w, h, BG_COLOR)

    # File list
    list_h = vc * LIST_ITEM_H
    i = 0
    while i < vc
      fi = @scroll + i
      break if fi >= @files.length
      iy = y0 + LIST_Y + i * LIST_ITEM_H
      if fi == @selected
        @gfx.fill_rect(x0, iy, w - 8, LIST_ITEM_H, HL_COLOR)
        @gfx.draw_text(x0 + 2, iy + 1, @files[fi], FmrbGfx::WHITE, HL_COLOR)
      else
        @gfx.draw_text(x0 + 2, iy + 1, @files[fi], TEXT_COLOR, BG_COLOR)
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

    # Scrollbar
    if @files.length > vc
      draw_scrollbar(@scroll, @files.length, vc,
                     x0 + w - 6, y0 + LIST_Y, 5, list_h)
    end

    # Separator
    info_y = y0 + h - INFO_H
    @gfx.draw_line(x0, info_y, x0 + w, info_y, BORDER_COLOR)

    # Info area
    if @nsf_info && @files.length > 0
      name = @nsf_info.song_name
      name = @files[@selected] if name.length == 0
      @gfx.draw_text(x0 + 2, info_y + 2, name, TEXT_COLOR, BG_COLOR)
      @gfx.draw_text(x0 + 2, info_y + 12, @nsf_info.artist, TEXT_COLOR, BG_COLOR)

    elsif @files.length == 0
      @gfx.draw_text(x0 + 2, info_y + 10, "No NSF files in #{MUSIC_DIR}", TEXT_COLOR, BG_COLOR)
    else
      # Selected file failed NsfHeader.parse (bad magic / truncated header):
      # say so instead of leaving the info area silently empty.
      @gfx.draw_text(x0 + 2, info_y + 2, @files[@selected], TEXT_COLOR, BG_COLOR)
      @gfx.draw_text(x0 + 2, info_y + 14, "Unsupported file", FmrbGfx::COLOR_RED, BG_COLOR)
    end

    # Everything above repainted the whole user area, so the widgets have to
    # be told their pixels are gone. flush issues the one present.
    sync_transport
    @ui.invalidate_all
    @ui.flush
  end

  # Put the transport in step with the app. The widgets only exist while
  # there is a playable file.
  def sync_transport
    live = @nsf_info && @files.length > 0
    @ui.set_visible(:track, live)
    @ui.set_visible(:play, live)
    return unless live
    @ui.set_value(:track, @track + 1)
    @ui.set_on(:play, @playing)
    nil
  end

  def play_current
    return unless @nsf_info && @selected < @files.length
    src_path = "#{MUSIC_DIR}/#{@files[@selected]}"
    cache_path = "#{CACHE_DIR}/#{@files[@selected]}"

    # Bring the cached copy on the graphics-audio side up to date
    Log.info("Syncing #{src_path} -> #{cache_path}")
    @gfx.sync_file(src_path, dest: cache_path)

    @audio.play(cache_path, track: @track)
    @playing = true
    draw_ui
  end

  def change_track(new_track)
    return unless @nsf_info
    return if new_track < 0 || new_track >= @nsf_info.total_songs
    was_playing = @playing
    @audio.stop if @playing
    @track = new_track
    if was_playing
      play_current
    else
      draw_ui
    end
  end

  def stop_current
    @audio.stop
    @playing = false
    draw_ui
  end

  def on_update
    330
  end

  def on_event(ev)
    super(ev)

    if ev[:type] == :key_down
      ch = ev[:character] || 0
      kc = ev[:keycode] || 0

      case kc
      when FmrbConst::KEY_UP
        if @selected > 0
          select_file(@selected - 1)
          @scroll = @selected if @selected < @scroll
          draw_ui
        end
      when FmrbConst::KEY_DOWN
        if @selected < @files.length - 1
          select_file(@selected + 1)
          @scroll = @selected - visible_count + 1 if @selected >= @scroll + visible_count
          @scroll = 0 if @scroll < 0
          draw_ui
        end
      when FmrbConst::KEY_LEFT
        change_track(@track - 1) if @nsf_info
      when FmrbConst::KEY_RIGHT
        change_track(@track + 1) if @nsf_info
      else
        if ch == 10 || ch == 13  # Enter
          if @playing
            stop_current
          else
            play_current
          end
        elsif ch == 27 || ch == 113  # Escape or 'q'
          stop_current if @playing
        end
      end
    end

    case @ui.handle(ev)
    when :track then change_track(@ui.value(:track) - 1)
    when :play  then @playing ? stop_current : play_current
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
