# What the microphone hears, as a bar graph.
#
# The whole chain is on this machine: the ES7210 samples, Fmrb::Fft transforms
# (any of its four engines -- press E to change), FmrbGfx draws. Nothing is
# recorded and nothing leaves the device.
#
# The bin width is decided by hardware, not by this app: the microphone shares
# the speaker's I2S clocks, so it samples at 47160 Hz and a 512-point window is
# 92 Hz per bin, reaching 23.5 kHz. Only the low end is worth drawing for
# voices and music, hence SHOWN_BINS.
class MicSpectrumApp < FmrbApp
  SIZE       = 512    # FFT window
  SHOWN_BINS = 64     # low 64 bins ~= 0..5.9 kHz, which is where the sound is
  PEAK_FALL  = 2      # how fast the peak-hold marks sink, per frame
  WARMUP     = 8      # blocks thrown away after power-up (the codec settles)

  BACKENDS = [:dsp, :c, :spinel, :ruby]

  # A tone the app can play to itself, so the display can be checked in a quiet
  # room -- and so "the bars do not move" can be told apart from "there is no
  # sound". The speaker and the microphone are on the same board, which is what
  # makes this work at all.
  TEST_TONE_HZ = 1000

  BAR_TOP    = 26
  BAR_BOTTOM = 150
  LABEL_Y    = 4

  def initialize
    super()
    @fft = nil
    @backend_idx = 0
    @peaks = []
    @bars = []
    @ms = 0
    @error = nil
    @frames = 0
    @tone = false
  end

  def on_create
    i = 0
    while i < SHOWN_BINS
      @peaks << 0
      @bars << 0
      i += 1
    end

    unless FmrbMic.available?
      @error = "no microphone on this machine"
      draw_screen
      return
    end

    @audio = FmrbAudio.new(self)
    @audio.mic_enable
    @rate = @audio.mic_rate
    open_backend
    # The first blocks after power-up are the microphone's bias settling, not
    # sound; drop them so the display does not open with a wall of bars.
    w = 0
    while w < WARMUP
      @audio.mic_read(SIZE, 300)
      w += 1
    end
    draw_screen
  end

  def on_update
    return 200 if @error
    capture
    draw_screen
    @frames += 1
    1   # as fast as the frame budget allows; the read paces us
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    ch = ev[:character] || 0
    if ch == 101 || ch == 69     # e / E: next engine
      @backend_idx = (@backend_idx + 1) % BACKENDS.size
      open_backend
    elsif ch == 116 || ch == 84  # t / T: the test tone, on and off
      @tone = !@tone
      if @tone
        @audio.note_on(0, TEST_TONE_HZ, 8, 2, 0)
      else
        @audio.note_off(0)
      end
    end
  end

  def on_destroy
    @audio.note_off(0) if @audio && @tone
    @fft.close if @fft
    @audio.mic_enable(false) if @audio
  end

  private

  def backend
    BACKENDS[@backend_idx]
  end

  def open_backend
    @fft.close if @fft
    @fft = nil
    # Not every engine is in every build (esp-dsp is device only), so fall
    # through to the next one rather than failing.
    tried = 0
    while tried < BACKENDS.size
      if Fmrb::Fft.available?(backend)
        @fft = Fmrb::Fft.new(size: SIZE, backend: backend)
        return
      end
      @backend_idx = (@backend_idx + 1) % BACKENDS.size
      tried += 1
    end
    @error = "no FFT engine in this build"
  end

  def capture
    bytes = @audio.mic_read(SIZE, 300)
    if bytes.nil?
      @ms = 0
      return
    end
    t0 = FmrbApp.uptime_us
    us, mag = @fft.run(bytes, 1)
    @ms = (FmrbApp.uptime_us - t0) / 1000
    scale_bars(mag)
  end

  # Magnitudes to bar heights, with the peak marks sinking a little each frame.
  def scale_bars(mag)
    height = BAR_BOTTOM - BAR_TOP
    i = 0
    while i < SHOWN_BINS
      v = Fmrb::Fft.bin(mag, i + 1)   # skip bin 0: DC, always the loudest
      # Fixed scale rather than auto-gain: a quiet room should look quiet.
      h = v * height / 3000
      h = height if h > height
      @bars[i] = h
      p = @peaks[i] - PEAK_FALL
      p = h if h > p
      p = 0 if p < 0
      @peaks[i] = p
      i += 1
    end
  end

  def draw_screen
    @gfx.clear(FmrbGfx::BLACK)

    if @error
      @gfx.draw_text(4, LABEL_Y, @error, FmrbGfx::RED)
      @gfx.present
      return
    end

    bin_hz = @rate / SIZE
    @gfx.draw_text(4, LABEL_Y,
                   "mic #{@rate} Hz  #{bin_hz} Hz/bin  #{backend} #{@ms} ms  " \
                   "[E]ngine [T]one#{@tone ? "*" : ""}",
                   FmrbGfx::WHITE)

    w = @user_area_width
    bw = w / SHOWN_BINS
    bw = 1 if bw < 1
    i = 0
    while i < SHOWN_BINS
      x = @user_area_x0 + i * bw
      h = @bars[i]
      @gfx.fill_rect(x, BAR_BOTTOM - h, bw - 1, h, bar_color(h)) if h > 0
      p = @peaks[i]
      @gfx.fill_rect(x, BAR_BOTTOM - p - 1, bw - 1, 1, FmrbGfx::WHITE) if p > 0
      i += 1
    end

    # The frequency ruler: every 8th bar, in kHz.
    i = 0
    while i < SHOWN_BINS
      if i % 8 == 0
        hz = (i + 1) * bin_hz
        @gfx.draw_text(@user_area_x0 + i * bw, BAR_BOTTOM + 3,
                       "#{hz / 1000}k", FmrbGfx::GRAY)
      end
      i += 8
    end

    @gfx.present
  end

  # Taller bars run hotter, so the eye finds the peak without reading numbers.
  def bar_color(h)
    third = (BAR_BOTTOM - BAR_TOP) / 3
    return FmrbGfx::CYAN if h < third
    return FmrbGfx::YELLOW if h < third * 2
    FmrbGfx::RED
  end
end

begin
  app = MicSpectrumApp.new
  app.start
rescue => e
  Log.error("MicSpectrum: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
