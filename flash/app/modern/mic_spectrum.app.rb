# What the microphone hears, as a bar graph.
#
# The whole chain is on this machine: the ES7210 samples, Fmrb::Fft transforms
# (any of its engines -- press E to change), FmrbGfx draws. Nothing is
# recorded and nothing leaves the device.
#
# The engine list is arranged in pairs, each engine in double and then in Q15
# fixed point, because pressing E and watching the millisecond count is the
# clearest way to see what the benchmark found (doc/mic_spectrum/report/
# track_a.md, E4): fixed point makes the AOT-compiled Ruby four times faster
# and the interpreted Ruby slower. spinel_q15 is the one that matters here --
# at a couple of milliseconds a transform it keeps up with the display, so
# this whole visualiser can run on Ruby the user could have written.
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

  # The esp-dsp backend runs the ESP32-P4's optimized FFT assembly, which needs
  # its data buffer in internal RAM; giving it one would spend memory the whole
  # system shares, so DSP is disabled by default and the app starts on the C
  # engine. Set ENABLE_DSP to true to include it in the rotation on builds where
  # it is safe.
  ENABLE_DSP = false
  BACKENDS = if ENABLE_DSP
               [:dsp, :c, :c_q15, :spinel, :spinel_q15, :ruby, :ruby_q15]
             else
               [:c, :c_q15, :spinel, :spinel_q15, :ruby, :ruby_q15]
             end

  # A tone the app can play to itself, so the display can be checked in a quiet
  # room -- and so "the bars do not move" can be told apart from "there is no
  # sound". The speaker and the microphone are on the same board, which is what
  # makes this work at all.
  TEST_TONE_HZ = 1000

  # Margins inside the window's user area: the header line at the top, the
  # frequency ruler at the bottom, bars in between. Measured from the user
  # area rather than the canvas, so the window frame stays intact and a
  # resized window still lays out correctly.
  HEADER_H = 14
  RULER_H  = 12

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
    # First, always: the close button and the title bar live in FmrbApp's
    # on_event, so an override that returns before calling it leaves the
    # window with no way to shut. Every other app starts the same way.
    super(ev)
    return unless ev[:type] == :key_down
    ch = ev[:character] || 0
    if ch == 101 || ch == 69     # e / E: next engine
      @backend_idx = (@backend_idx + 1) % BACKENDS.size
      open_backend
    elsif ch == 116 || ch == 84  # t / T: the test tone, on and off
      # Only when there is a microphone: without one @audio was never built,
      # and the error screen still takes key events.
      return if @audio.nil?
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
  def bars_height
    h = @user_area_height - HEADER_H - RULER_H
    h < 8 ? 8 : h
  end

  def scale_bars(mag)
    height = bars_height
    i = 0
    while i < SHOWN_BINS
      v = Fmrb::Fft.bin(mag, i + 1)   # skip bin 0: DC, always the loudest
      # Fixed scale rather than auto-gain: a quiet room should look quiet.
      # Divisor halved (3000 -> 1500) to roughly double the bar sensitivity.
      # One divisor for every engine: the Q15 backends return magnitudes on
      # the same scale as the float ones (their per-stage shift already
      # divides by n, so the float version's 2/n becomes a doubling), which is
      # what makes pressing E a comparison rather than a rescale.
      h = v * height / 1500
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
    # Not @gfx.clear: that paints over the window frame and the title bar.
    clear_user_area(FmrbGfx::BLACK)

    if @error
      @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 3, @error, FmrbGfx::RED)
      draw_window_frame
      @gfx.present
      return
    end

    # Fixed field widths: the engine name and the millisecond count both change
    # length every frame, and without this the key hints slide left and right
    # while the app runs. The field is 10 wide because spinel_q15 is, which
    # puts the line at 53 characters -- the user area is 56, and the first
    # version of this line sat exactly on that edge and wrapped onto the bars.
    bin_hz = @rate / SIZE
    ms = @ms > 9999 ? 9999 : @ms
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 3,
                   sprintf("%dHz %dHz/bin %-10s %4dms [E]ngine [T]one%s",
                           @rate, bin_hz, backend.to_s, ms, @tone ? "*" : " "),
                   FmrbGfx::WHITE)

    bar_bottom = @user_area_y0 + HEADER_H + bars_height
    bw = @user_area_width / SHOWN_BINS
    bw = 1 if bw < 1
    i = 0
    while i < SHOWN_BINS
      x = @user_area_x0 + i * bw
      h = @bars[i]
      @gfx.fill_rect(x, bar_bottom - h, bw - 1, h, bar_color(h)) if h > 0
      p = @peaks[i]
      @gfx.fill_rect(x, bar_bottom - p - 1, bw - 1, 1, FmrbGfx::WHITE) if p > 0
      i += 1
    end

    # The frequency ruler: every 8th bar, in kHz.
    i = 0
    while i < SHOWN_BINS
      if i % 8 == 0
        hz = (i + 1) * bin_hz
        @gfx.draw_text(@user_area_x0 + i * bw, bar_bottom + 3,
                       "#{hz / 1000}k", FmrbGfx::GRAY)
      end
      i += 8
    end

    # Last, so the border and title bar survive this frame's drawing.
    draw_window_frame
    @gfx.present
  end

  # Taller bars run hotter, so the eye finds the peak without reading numbers.
  def bar_color(h)
    third = bars_height / 3
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
