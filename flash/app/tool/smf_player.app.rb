# SMF Player Application
# Browse and play Standard MIDI Files, on the built-in APU or an external
# MIDI instrument.
#
# Laid out like the NSF player: a file list on top, information and controls
# underneath. What a .mid brings over an .nsf is that playback is live, so
# the tempo and even the instrument can be changed while the song runs.
#
# Keys: up/down select, Enter play/stop, left/right tempo, o output, q stop.

class SmfPlayerApp < FmrbApp
  MUSIC_DIR = "/data/midi"
  LIST_Y = 2
  LIST_ITEM_H = 10
  INFO_H = 48
  TEXT_COLOR = FmrbConst::THEME_TEXT
  BG_COLOR = FmrbConst::THEME_WINDOW_BG
  HL_COLOR = FmrbConst::THEME_HIGHLIGHT
  BORDER_COLOR = FmrbConst::THEME_BORDER

  # 1.0 is the file's own tempo. Kept as tenths to avoid float rounding in
  # the label.
  TEMPO_STEPS = [50, 75, 100, 125, 150, 200]
  DEFAULT_TEMPO_INDEX = 2

  def initialize
    super()
    @files = []
    @selected = 0
    @scroll = 0
    @playing = false
    @playing_name = nil
    @info = nil
    @tempo_index = DEFAULT_TEMPO_INDEX
    @external = false
    @status = nil
    @play_btn = nil
    @slower_btn = nil
    @faster_btn = nil
    @out_btn = nil
  end

  def on_create
    @device = FmrbMidi.device(self)
    @player = FmrbMidi::SmfPlayer.new(@device)
    scan_files
    select_file(0) if @files.length > 0
    draw_ui
  end

  def scan_files
    @files = []
    begin
      dir = Dir.open(MUSIC_DIR)
      while (entry = dir.read)
        next if entry == "." || entry == ".."
        @files << entry if entry.end_with?(".mid") || entry.end_with?(".midi")
      end
      dir.close
    rescue => e
      Log.error("Failed to scan #{MUSIC_DIR}: #{e.message}")
    end
    @files.sort!
  end

  # Load the file so its shape can be shown before committing to play it.
  # One pass over the bytes, no per-event objects (see the gem notes).
  def select_file(idx)
    return if idx < 0 || idx >= @files.length

    @selected = idx
    @info = nil
    return if @playing # do not disturb what is sounding

    path = "#{MUSIC_DIR}/#{@files[idx]}"
    if @player.load(path)
      usage = @player.channel_usage
      notes = 0
      usage.each { |_, stats| notes += stats[0] }
      @info = { tracks: @player.track_count, channels: usage.keys.length, notes: notes }
    else
      @status = @player.error
    end
  end

  # --- playback ---------------------------------------------------------

  def current_device
    (@external && @serial) ? @serial : @device
  end

  def play_current
    return if @files.length == 0

    path = "#{MUSIC_DIR}/#{@files[@selected]}"
    unless @player.load(path)
      @status = @player.error
      draw_ui
      return
    end

    # Songs use whatever channels they like; let the APU transport pick its
    # voices from what this file actually plays. An external instrument has
    # sixteen channels of its own and needs no mapping.
    @device.transport.auto_map(@player.channel_usage)
    @player.device = current_device
    @player.tempo_scale = TEMPO_STEPS[@tempo_index] / 100.0
    @player.start
    @playing = true
    @playing_name = @files[@selected]
    @status = nil
    Log.info("SMF Player: playing #{path}")
    draw_ui
  end

  def stop_current
    @player.stop
    @playing = false
    @playing_name = nil
    # The selection may have moved while it played; refresh its details.
    select_file(@selected)
    draw_ui
  end

  def change_tempo(step)
    index = @tempo_index + step
    return if index < 0 || index >= TEMPO_STEPS.length

    @tempo_index = index
    # Takes effect at once, without moving the part already played.
    @player.tempo_scale = TEMPO_STEPS[index] / 100.0
    draw_ui
  end

  def toggle_output
    if @external
      @external = false
    else
      @serial = FmrbMidi.sam2695_device if @serial.nil?
      if @serial.nil?
        @status = "no MIDI port"
        draw_ui
        return
      end
      @external = true
    end
    # Moving a playing song releases whatever the old output was sounding.
    @player.device = current_device
    draw_ui
  end

  # --- drawing ----------------------------------------------------------

  def visible_count
    (@user_area_height - INFO_H - LIST_Y) / LIST_ITEM_H
  end

  def hit_btn?(btn, x, y)
    btn && x >= btn[:x] && x < btn[:x] + btn[:w] &&
      y >= btn[:y] && y < btn[:y] + btn[:h]
  end

  def draw_button(rect, label, color)
    @gfx.fill_rect(rect[:x], rect[:y], rect[:w], rect[:h], color)
    @gfx.draw_text(rect[:x] + 3, rect[:y] + 1, label, FmrbGfx::WHITE, color)
  end

  def draw_ui
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height
    vc = visible_count

    @gfx.fill_rect(x0, y0, w, h, BG_COLOR)

    i = 0
    while i < vc
      fi = @scroll + i
      break if fi >= @files.length

      iy = y0 + LIST_Y + (i * LIST_ITEM_H)
      if fi == @selected
        @gfx.fill_rect(x0, iy, w - 8, LIST_ITEM_H, HL_COLOR)
        @gfx.draw_text(x0 + 2, iy + 1, @files[fi], FmrbGfx::WHITE, HL_COLOR)
      else
        @gfx.draw_text(x0 + 2, iy + 1, @files[fi], TEXT_COLOR, BG_COLOR)
      end
      i += 1
    end

    if @files.length > vc
      draw_scrollbar(@scroll, @files.length, vc, x0 + w - 6, y0 + LIST_Y, 5, vc * LIST_ITEM_H)
    end

    info_y = y0 + h - INFO_H
    @gfx.draw_line(x0, info_y, x0 + w, info_y, BORDER_COLOR)

    if @files.length == 0
      @gfx.draw_text(x0 + 2, info_y + 10, "No .mid files in #{MUSIC_DIR}", TEXT_COLOR, BG_COLOR)
      @gfx.present
      return
    end

    @gfx.draw_text(x0 + 2, info_y + 2, @files[@selected], TEXT_COLOR, BG_COLOR)

    # While a song plays the selection can move without reloading, so the
    # file's shape is unknown for the highlighted row; say what is actually
    # sounding instead of leaving the line blank.
    line2 = if @status
              @status
            elsif @info
              "#{@info[:tracks]} trk  #{@info[:channels]} ch  #{@info[:notes]} notes"
            elsif @playing && @playing_name
              "playing #{@playing_name}"
            else
              ""
            end
    @gfx.draw_text(x0 + 2, info_y + 12, line2, TEXT_COLOR, BG_COLOR)

    btn_y = info_y + 24
    btn_h = 11

    @play_btn = { x: x0 + 2, y: btn_y, w: 35, h: btn_h }
    if @playing
      draw_button(@play_btn, "Stop", FmrbGfx::COLOR_RED)
    else
      draw_button(@play_btn, "Play", FmrbGfx::COLOR_GREEN)
    end

    # Tempo, changeable while the song plays - the reason to play a .mid
    # live instead of converting it beforehand.
    @slower_btn = { x: x0 + 44, y: btn_y, w: 12, h: btn_h }
    draw_button(@slower_btn, "<", BORDER_COLOR)
    tempo = TEMPO_STEPS[@tempo_index]
    @gfx.draw_text(x0 + 58, btn_y + 1, "x#{tempo / 100}.#{(tempo % 100) / 10}", TEXT_COLOR, BG_COLOR)
    @faster_btn = { x: x0 + 88, y: btn_y, w: 12, h: btn_h }
    draw_button(@faster_btn, ">", BORDER_COLOR)

    @out_btn = { x: x0 + w - 60, y: btn_y, w: 56, h: btn_h }
    if @external
      draw_button(@out_btn, "MIDI out", FmrbGfx::COLOR_GREEN)
    else
      draw_button(@out_btn, "APU", BORDER_COLOR)
    end

    @gfx.draw_text(x0 + 2, btn_y + 14, "Enter play  < > tempo  o output", TEXT_COLOR, BG_COLOR)
    @gfx.present
  end

  # --- main loop --------------------------------------------------------

  def on_update
    if @playing
      # The player keeps its own schedule; ask it how long we may sleep.
      wait = @player.next_delay(300)
      unless @player.playing?
        @playing = false
        @playing_name = nil
        select_file(@selected)
        draw_ui
      end
      return wait
    end
    300
  end

  def on_event(ev)
    super(ev)

    if ev[:type] == :key_down
      ch = ev[:character] || 0
      case ev[:keycode] || 0
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
        change_tempo(-1)
      when FmrbConst::KEY_RIGHT
        change_tempo(1)
      else
        if ch == 10 || ch == 13
          @playing ? stop_current : play_current
        elsif ch == 111 # 'o'
          toggle_output
        elsif ch == 27 || ch == 113 # Escape or 'q'
          stop_current if @playing
        end
      end
    end

    handle_click(ev[:x], ev[:y]) if ev[:type] == :mouse_up
  end

  def handle_click(x, y)
    y0 = @user_area_y0
    info_y = y0 + @user_area_height - INFO_H

    if y >= y0 + LIST_Y && y < info_y
      idx = @scroll + ((y - y0 - LIST_Y) / LIST_ITEM_H)
      if idx >= 0 && idx < @files.length
        select_file(idx)
        draw_ui
      end
      return
    end

    if hit_btn?(@play_btn, x, y)
      @playing ? stop_current : play_current
    elsif hit_btn?(@slower_btn, x, y)
      change_tempo(-1)
    elsif hit_btn?(@faster_btn, x, y)
      change_tempo(1)
    elsif hit_btn?(@out_btn, x, y)
      toggle_output
    end
  end

  def on_destroy
    @player.stop if @player
    Log.info("SMF Player destroyed")
  end
end

Log.info("SmfPlayerApp.new")
begin
  app = SmfPlayerApp.new
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("SMF Player script ended")
