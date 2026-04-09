# Piano - 1 octave keyboard with real-time APU control
# Hold keys to play notes, release to stop
# Control panel: channel select, sweep direction and speed

class PianoApp < FmrbApp
  # Note frequencies (C4..B4)
  NOTE_FREQS = [262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494]

  # Noise period values per key (0-15, lower=higher pitch)
  # White keys: long-cycle noise, Black keys: short-cycle noise (bit7=0x80)
  NOISE_PERIODS = [
    0,        # C  - lowest rumble
    0x80 | 2, # C# - short metallic
    3,        # D
    0x80 | 4, # D# - short
    5,        # E
    6,        # F
    0x80 | 7, # F# - short
    8,        # G
    0x80 | 9, # G# - short
    10,       # A
    0x80 | 11,# A# - short
    13        # B  - highest hiss
  ]

  # White key indices (C=0, D=2, E=4, F=5, G=7, A=9, B=11)
  WHITE_KEYS = [0, 2, 4, 5, 7, 9, 11]
  # Black key indices (C#=1, D#=3, F#=6, G#=8, A#=10)
  BLACK_KEYS = [1, 3, 6, 8, 10]

  # Channel labels
  CH_LABELS = ["P1", "P2", "Tri", "Noi"]
  # Sweep direction labels
  SW_LABELS = ["OFF", "UP", "DN"]
  # Sweep speed labels
  SPD_LABELS = ["1", "2", "3"]
  # Sweep shift values for each speed (1=slow, 2=mid, 3=fast)
  SPD_SHIFTS = [7, 4, 1]

  # Panel height
  PANEL_H = 28

  # Colors (RGB332)
  COL_WHITE  = 0xFF
  COL_BLACK  = 0x00
  COL_GRAY   = 0x92
  COL_BG     = 0x49
  COL_BTN    = 0x6D
  COL_BTN_ON = 0x1C
  COL_BTN_DIS = 0x24
  COL_TEXT   = 0xFF

  def initialize
    super()
    @audio = nil
    @active_key = -1
    @channel = 0      # 0=P1, 1=P2, 2=Tri, 3=Noi
    @sweep_dir = 0    # 0=OFF, 1=UP, 2=DOWN
    @sweep_spd = 0    # 0-2 index into SPD_SHIFTS
  end

  def on_create
    Log.info("Piano app started")
    @audio = FmrbAudio.new(self)
    draw_all
  end

  def on_update
    200
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_down
      x = ev[:x]
      y = ev[:y]
      panel_y = @user_area_y0 + PANEL_H
      if y < panel_y
        handle_panel_click(x, y)
      else
        key = hit_test(x, y)
        if key >= 0 && key != @active_key
          @active_key = key
          freq = (@channel == 3) ? NOISE_PERIODS[key] : NOTE_FREQS[key]
          @audio.note_on(@channel, freq, 10, 2, build_sweep)
          draw_all
        end
      end
    elsif ev[:type] == :mouse_up
      if @active_key >= 0
        @audio.note_off(@channel)
        @active_key = -1
        draw_all
      end
    end
  end

  def on_destroy
    @audio.note_off(@channel) if @audio
    Log.info("Piano destroyed")
  end

  # Build sweep register value
  def build_sweep
    return 0 if @sweep_dir == 0
    negate = (@sweep_dir == 1) ? 0x08 : 0x00
    shift = SPD_SHIFTS[@sweep_spd]
    0x80 | (1 << 4) | negate | shift
  end

  # Is current channel a pulse channel?
  def pulse_ch?
    @channel == 0 || @channel == 1
  end

  # Handle click on control panel
  def handle_panel_click(mx, my)
    x0 = @user_area_x0
    y0 = @user_area_y0
    btn_h = 12
    btn_y = y0 + 2

    # Row 1: Channel buttons
    bx = x0 + 2
    4.times do |i|
      bw = (i < 2) ? 18 : 22
      if mx >= bx && mx < bx + bw && my >= btn_y && my < btn_y + btn_h
        @channel = i
        @active_key = -1
        draw_all
        return
      end
      bx += bw + 2
    end

    # Row 2: Sweep direction
    btn_y2 = y0 + 16
    bx = x0 + 2
    # Label "Sw:"
    bx += 18
    3.times do |i|
      bw = 22
      if mx >= bx && mx < bx + bw && my >= btn_y2 && my < btn_y2 + btn_h
        if pulse_ch?
          @sweep_dir = i
          draw_all
        end
        return
      end
      bx += bw + 2
    end

    # Sweep speed
    bx += 6
    3.times do |i|
      bw = 12
      if mx >= bx && mx < bx + bw && my >= btn_y2 && my < btn_y2 + btn_h
        if pulse_ch? && @sweep_dir != 0
          @sweep_spd = i
          draw_all
        end
        return
      end
      bx += bw + 2
    end
  end

  # Draw everything
  def draw_all
    x0 = @user_area_x0
    y0 = @user_area_y0
    w = @user_area_width
    h = @user_area_height

    @gfx.fill_rect(x0, y0, w, h, COL_BG)
    draw_panel
    draw_keyboard
    draw_window_frame
    @gfx.present
  end

  # Draw control panel
  def draw_panel
    x0 = @user_area_x0
    y0 = @user_area_y0
    btn_h = 12
    btn_y = y0 + 2

    # Row 1: Channel select
    bx = x0 + 2
    4.times do |i|
      bw = (i < 2) ? 18 : 22
      col = (i == @channel) ? COL_BTN_ON : COL_BTN
      @gfx.fill_rect(bx, btn_y, bw, btn_h, col)
      @gfx.draw_text(bx + 2, btn_y + 2, CH_LABELS[i], COL_TEXT)
      bx += bw + 2
    end

    # Row 2: Sweep
    btn_y2 = y0 + 16
    bx = x0 + 2
    @gfx.draw_text(bx, btn_y2 + 2, "Sw", COL_TEXT)
    bx += 18

    # Sweep direction buttons
    3.times do |i|
      bw = 22
      if !pulse_ch?
        col = COL_BTN_DIS
      elsif i == @sweep_dir
        col = COL_BTN_ON
      else
        col = COL_BTN
      end
      @gfx.fill_rect(bx, btn_y2, bw, btn_h, col)
      @gfx.draw_text(bx + 2, btn_y2 + 2, SW_LABELS[i], COL_TEXT)
      bx += bw + 2
    end

    # Speed buttons
    bx += 6
    3.times do |i|
      bw = 12
      if !pulse_ch? || @sweep_dir == 0
        col = COL_BTN_DIS
      elsif i == @sweep_spd
        col = COL_BTN_ON
      else
        col = COL_BTN
      end
      @gfx.fill_rect(bx, btn_y2, bw, btn_h, col)
      @gfx.draw_text(bx + 3, btn_y2 + 2, SPD_LABELS[i], COL_TEXT)
      bx += bw + 2
    end
  end

  # Draw piano keyboard
  def draw_keyboard
    x0 = @user_area_x0
    y0 = @user_area_y0 + PANEL_H
    w = @user_area_width
    h = @user_area_height - PANEL_H

    # White keys
    key_w = w / 7
    key_h = h - 2

    WHITE_KEYS.each_with_index do |note_idx, i|
      kx = x0 + i * key_w
      col = (@active_key == note_idx) ? COL_GRAY : COL_WHITE
      @gfx.fill_rect(kx + 1, y0 + 1, key_w - 2, key_h, col)
    end

    # Black keys
    bk_w = key_w * 2 / 3
    bk_h = key_h * 3 / 5
    bk_positions = [0, 1, 3, 4, 5]

    BLACK_KEYS.each_with_index do |note_idx, i|
      wi = bk_positions[i]
      kx = x0 + (wi + 1) * key_w - bk_w / 2
      col = (@active_key == note_idx) ? COL_GRAY : COL_BLACK
      @gfx.fill_rect(kx, y0 + 1, bk_w, bk_h, col)
    end
  end

  # Hit test for keyboard area
  def hit_test(mx, my)
    x0 = @user_area_x0
    y0 = @user_area_y0 + PANEL_H
    w = @user_area_width
    h = @user_area_height - PANEL_H
    key_w = w / 7
    key_h = h - 2
    bk_w = key_w * 2 / 3
    bk_h = key_h * 3 / 5

    return -1 if my < y0

    bk_positions = [0, 1, 3, 4, 5]
    BLACK_KEYS.each_with_index do |note_idx, i|
      wi = bk_positions[i]
      kx = x0 + (wi + 1) * key_w - bk_w / 2
      ky = y0 + 1
      if mx >= kx && mx < kx + bk_w && my >= ky && my < ky + bk_h
        return note_idx
      end
    end

    WHITE_KEYS.each_with_index do |note_idx, i|
      kx = x0 + i * key_w
      ky = y0 + 1
      if mx >= kx && mx < kx + key_w && my >= ky && my < ky + key_h
        return note_idx
      end
    end

    -1
  end
end

Log.info("PianoApp.new")
begin
  app = PianoApp.new
  Log.info("PianoApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
