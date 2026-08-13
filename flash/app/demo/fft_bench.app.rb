# One FFT, four engines, timed side by side (doc/mic_spectrum/plan.md).
#
# The same transform runs as:
#   ruby    fft_core.rb on the mruby VM
#   spinel  the same fft_core.rb, compiled to native code by Spinel
#   c       main/kernel/fmrb_fft_bench.c, the plain baseline
#   dsp     esp-dsp's assembler radix-2 (device builds only)
#
# Correctness first: every engine is fed the same synthetic sine and has to
# agree on the peak bin and on the magnitudes before its microseconds mean
# anything. The screen shows both -- the check and the number -- and every
# result also goes to the log, so the simulator can be read headless:
#
#   docker logs fmruby_core | grep FFT
class FftBenchApp < FmrbApp
  SIZE   = 512     # transform length
  CYCLES = 8       # cycles of sine across the window -> expected peak bin
  ITERS  = 20      # transforms per timed run
  REPS   = 5       # timed runs; avg over all, min of them

  LINE_H = 10
  TOP    = 14
  COL_X  = 4

  BACKENDS = [:c, :ruby, :spinel, :dsp]

  def initialize
    super()
    @results = []
    @pending = BACKENDS.dup
    @samples = nil
    @reference = nil
    @done = false
  end

  def on_create
    @samples = Fmrb::Fft.sine(size: SIZE, cycles: CYCLES)
    draw_screen
  end

  # One backend per update: a run takes a while (the mruby one especially), and
  # doing them all in on_create would leave the window blank until the end.
  def on_update
    return 200 if @done

    backend = @pending.shift
    if backend.nil?
      @done = true
      draw_screen
      return 500
    end

    measure(backend)
    draw_screen
    50
  end

  private

  def measure(backend)
    unless Fmrb::Fft.available?(backend)
      @results << { backend: backend, skipped: true }
      Log.info("FFT #{backend}: not in this build")
      return
    end

    r = Fmrb::Fft.bench(size: SIZE, iters: ITERS, backend: backend,
                        reps: REPS, samples: @samples)
    r[:agrees] = agrees?(r[:mag])
    @reference ||= r[:mag]
    @results << r

    Log.info("FFT #{backend}: avg=#{fmt(r[:us_avg])}us min=#{fmt(r[:us_min])}us " \
             "peak=#{r[:peak_bin]} (expected #{CYCLES}) agrees=#{r[:agrees]} " \
             "size=#{SIZE} iters=#{ITERS} reps=#{REPS}")
  rescue => e
    @results << { backend: backend, error: e.message }
    Log.error("FFT #{backend}: #{e.class}: #{e.message}")
  end

  # Same input, same algorithm: the magnitudes have to match the first engine's
  # bin for bin. int16 rounding can differ by one where a bin sits on a .5, so
  # one count of slack -- anything larger is a real disagreement.
  def agrees?(mag)
    return true if @reference.nil?
    return false if mag.bytesize != @reference.bytesize
    i = 0
    count = mag.bytesize / 2
    while i < count
      a = Fmrb::Fft.bin(mag, i)
      b = Fmrb::Fft.bin(@reference, i)
      d = a - b
      d = -d if d < 0
      return false if d > 1
      i += 1
    end
    true
  end

  def fmt(v)
    sprintf("%.1f", v)
  end

  def draw_screen
    @gfx.clear(FmrbGfx::BLACK)
    @gfx.draw_text(COL_X, 2, "FFT #{SIZE} pt x #{ITERS}, #{REPS} runs", FmrbGfx::WHITE)

    y = TOP
    @gfx.draw_text(COL_X, y, "engine    avg us   min us  peak  ok", FmrbGfx::GRAY)
    y += LINE_H

    @results.each do |r|
      name = r[:backend].to_s
      if r[:skipped]
        @gfx.draw_text(COL_X, y, sprintf("%-8s  not in this build", name), FmrbGfx::GRAY)
      elsif r[:error]
        @gfx.draw_text(COL_X, y, sprintf("%-8s  %s", name, r[:error]), FmrbGfx::RED)
      else
        color = r[:agrees] ? FmrbGfx::WHITE : FmrbGfx::RED
        @gfx.draw_text(COL_X, y,
                       sprintf("%-8s %8.1f %8.1f %5d  %s",
                               name, r[:us_avg], r[:us_min], r[:peak_bin],
                               r[:agrees] ? "yes" : "NO"),
                       color)
      end
      y += LINE_H
    end

    if @pending.size > 0 && !@done
      @gfx.draw_text(COL_X, y, "running #{@pending[0]}...", FmrbGfx::YELLOW)
    end

    draw_spectrum if @reference
    @gfx.present
  end

  # The first engine's spectrum, as a reminder that the numbers above came from
  # a transform that found the tone it was given.
  def draw_spectrum
    return if @reference.nil?
    w = @user_area_width
    h = @window_height
    base = h - 6
    top = base - 40
    return if top < TOP

    bins = @reference.bytesize / 2
    step = bins / (w - 8)
    step = 1 if step < 1
    peak = 1
    i = 0
    while i < bins
      v = Fmrb::Fft.bin(@reference, i)
      peak = v if v > peak
      i += 1
    end

    x = 4
    i = 0
    while i < bins && x < w - 4
      v = Fmrb::Fft.bin(@reference, i)
      bar = (v * 40) / peak
      @gfx.fill_rect(@user_area_x0 + x, base - bar, 1, bar + 1, FmrbGfx::CYAN) if bar > 0
      x += 1
      i += step
    end
  end
end

begin
  app = FftBenchApp.new
  app.start
rescue => e
  Log.error("FftBench: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
