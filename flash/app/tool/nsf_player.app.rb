# NSF Player Application
# Browse and play NSF music files with track selection.

class NsfPlayerApp < FmrbApp
  #MUSIC_DIR = "/home/music"
  MUSIC_DIR = "/usr/share/sounds"
  CACHE_DIR = "/cache/nsf_player"
  LIST_Y = 2
  LIST_ITEM_H = 10
  INFO_H = 38
  TEXT_COLOR = FmrbConst::THEME_TEXT
  BG_COLOR = FmrbConst::THEME_WINDOW_BG
  HL_COLOR = FmrbConst::THEME_HIGHLIGHT
  BORDER_COLOR = FmrbConst::THEME_BORDER

  def initialize
    super()
    @files = []
    @selected = 0
    @scroll = 0
    @track = 0
    @playing = false
    @nsf_info = nil
    @audio = nil
    @prev_btn = nil
    @next_btn = nil
    @play_btn = nil
  end

  def on_create
    @audio = FmrbAudio.new(self)
    scan_files
    select_file(0) if @files.length > 0
    draw_ui
  end

  def scan_files
    @files = []
    begin
      dir = Dir.open(to_os_dir_path(MUSIC_DIR))
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

  def hit_btn?(btn, x, y)
    x >= btn[:x] && x < btn[:x] + btn[:w] &&
      y >= btn[:y] && y < btn[:y] + btn[:h]
  end

  def visible_count
    (@user_area_height - INFO_H - LIST_Y) / LIST_ITEM_H
  end

  def select_file(idx)
    return if idx < 0 || idx >= @files.length
    @selected = idx
    @track = 0
    path = to_file_path("#{MUSIC_DIR}/#{@files[idx]}")
    @nsf_info = NsfHeader.parse(path)
    if @nsf_info
      @track = @nsf_info.starting_song > 0 ? @nsf_info.starting_song - 1 : 0
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

      # Track selector with < > buttons
      track_y = info_y + 24
      btn_h = 11
      # [<] button
      @prev_btn = { x: x0 + 2, y: track_y - 1, w: 14, h: btn_h }
      @gfx.fill_rect(@prev_btn[:x], @prev_btn[:y], @prev_btn[:w], btn_h, BORDER_COLOR)
      @gfx.draw_text(@prev_btn[:x] + 3, track_y, "<", FmrbGfx::WHITE, BORDER_COLOR)
      # Track number
      track_text = " #{@track + 1}/#{@nsf_info.total_songs} "
      @gfx.draw_text(x0 + 18, track_y, track_text, TEXT_COLOR, BG_COLOR)
      # [>] button
      @next_btn = { x: x0 + 54, y: track_y - 1, w: 14, h: btn_h }
      @gfx.fill_rect(@next_btn[:x], @next_btn[:y], @next_btn[:w], btn_h, BORDER_COLOR)
      @gfx.draw_text(@next_btn[:x] + 3, track_y, ">", FmrbGfx::WHITE, BORDER_COLOR)

      # Play/Stop button
      btn_x = x0 + w - 80
      @play_btn = { x: btn_x, y: track_y - 1, w: 35, h: btn_h }
      if @playing
        @gfx.fill_rect(btn_x, track_y - 1, 35, btn_h, FmrbGfx::COLOR_RED)
        @gfx.draw_text(btn_x + 4, track_y, "Stop", FmrbGfx::WHITE, FmrbGfx::COLOR_RED)
      else
        @gfx.fill_rect(btn_x, track_y - 1, 35, btn_h, FmrbGfx::COLOR_GREEN)
        @gfx.draw_text(btn_x + 4, track_y, "Play", FmrbGfx::WHITE, FmrbGfx::COLOR_GREEN)
      end
    elsif @files.length == 0
      @gfx.draw_text(x0 + 2, info_y + 10, "No NSF files in #{MUSIC_DIR}", TEXT_COLOR, BG_COLOR)
    end

    @gfx.present
  end

  def play_current
    return unless @nsf_info && @selected < @files.length
    src_path = "#{MUSIC_DIR}/#{@files[@selected]}"
    cache_path = "#{CACHE_DIR}/#{@files[@selected]}"

    # Transfer file to cache directory on graphics-audio side
    status = @gfx.file_status(cache_path)
    unless status && status[:exists]
      Log.info("Transferring #{src_path} -> #{cache_path}")
      @gfx.transfer_file(src_path, dest: cache_path)
    end

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

    if ev[:type] == :mouse_up
      handle_click(ev[:x], ev[:y])
    end
  end

  def handle_click(x, y)
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    info_y = y0 + h - INFO_H

    # Click in file list area
    if y >= y0 + LIST_Y && y < info_y
      idx = @scroll + (y - y0 - LIST_Y) / LIST_ITEM_H
      if idx >= 0 && idx < @files.length
        select_file(idx)
        draw_ui
      end
      return
    end

    # Click in info area
    if y >= info_y && @nsf_info
      # [<] button
      if @prev_btn && hit_btn?(@prev_btn, x, y)
        change_track(@track - 1)
        return
      end

      # [>] button
      if @next_btn && hit_btn?(@next_btn, x, y)
        change_track(@track + 1)
        return
      end

      # Play/Stop button
      if @play_btn && hit_btn?(@play_btn, x, y)
        if @playing
          stop_current
        else
          play_current
        end
        return
      end
    end
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
