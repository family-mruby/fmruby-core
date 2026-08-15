# One FFT, several engines, timed side by side (doc/mic_spectrum/plan.md).
#
# The same transform runs as:
#   ruby    fft_core.rb on the mruby VM
#   spinel  the same fft_core.rb, compiled to native code by Spinel
#   c       main/kernel/fmrb_fft_bench.c, the plain baseline
#   c64     that same C in double -- the precision control, not an engine
#   dsp     esp-dsp's assembler radix-2 (device builds only)
#
# then the same three engines again over fft_core_q15.rb / fmrb_fft_c_q15(),
# which do the transform in fixed point and never touch the FPU:
#
#   c_q15   ruby_q15   spinel_q15
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

  # :c64 sits next to :c on purpose -- it is the same C in double, the control
  # for the precision the two Ruby engines are stuck with. The _q15 three are
  # the same transform with no float in it at all (E1 and E4 in
  # doc/mic_spectrum/impl_plan_spinel_perf.md).
  BACKENDS = [:c, :c64, :ruby, :spinel, :dsp,
              :c_q15, :ruby_q15, :spinel_q15]

  # How far a backend's magnitudes may sit from the first engine's before the
  # run counts as a disagreement. int16 rounding can put a float backend one
  # count either way where a bin lands on a .5; fixed point gives up a bit per
  # butterfly stage, so its bins are a handful of counts low against a peak in
  # the thousands. Both numbers are checks on the arithmetic, not on the
  # engine -- a backend that is really wrong misses by far more than this.
  TOL_FLOAT = 1
  TOL_Q15   = 24

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
    tol = Fmrb::Fft.q15?(backend) ? TOL_Q15 : TOL_FLOAT
    dev = deviation(r[:mag])
    r[:dev] = dev
    r[:agrees] = (r[:peak_bin] == CYCLES) && dev <= tol
    @reference ||= r[:mag]
    @results << r

    Log.info("FFT #{backend}: avg=#{fmt(r[:us_avg])}us min=#{fmt(r[:us_min])}us " \
             "peak=#{r[:peak_bin]} (expected #{CYCLES}) agrees=#{r[:agrees]} " \
             "dev=#{dev} (tol #{tol}) size=#{SIZE} iters=#{ITERS} reps=#{REPS}")

    # For the Spinel backends, also what the last entry call cost as a whole.
    # entry - min*iters is everything the entry does around the transform:
    # decoding the samples, building the magnitudes, and -- unless the program
    # was compiled with --persistent-statics -- rebuilding the window and
    # twiddle tables, which is invisible at iters=20 and dominant at iters=1.
    #
    # Against min rather than avg on purpose. avg is the mean of all the reps
    # including the first, and with persistent statics the first rep is the
    # only one that builds the tables; subtracting that mean from the LAST
    # entry (which built nothing) came out negative and read like nonsense.
    if backend == :spinel || backend == :spinel_q15
      entry = Fmrb::Fft.spinel_total_us
      Log.info("FFT #{backend}: entry=#{entry}us around=#{entry - (r[:us_min] * ITERS).to_i}us " \
               "(last call; iters=#{ITERS})")
    end
  rescue => e
    @results << { backend: backend, error: e.message }
    Log.error("FFT #{backend}: #{e.class}: #{e.message}")
  end

  # Same input, same algorithm: how far the worst bin sits from the first
  # engine's. Reported rather than reduced to a yes/no so the tolerances above
  # stay honest -- a number nobody can see is a number nobody can argue with.
  def deviation(mag)
    return 0 if @reference.nil?
    return 99999 if mag.bytesize != @reference.bytesize
    worst = 0
    i = 0
    count = mag.bytesize / 2
    while i < count
      d = Fmrb::Fft.bin(mag, i) - Fmrb::Fft.bin(@reference, i)
      d = -d if d < 0
      worst = d if d > worst
      i += 1
    end
    worst
  end

  def fmt(v)
    sprintf("%.1f", v)
  end

  def draw_screen
    @gfx.clear(FmrbGfx::BLACK)
    @gfx.draw_text(COL_X, 2, "FFT #{SIZE} pt x #{ITERS}, #{REPS} runs", FmrbGfx::WHITE)

    y = TOP
    @gfx.draw_text(COL_X, y, "engine       avg us   min us  peak  dev  ok", FmrbGfx::GRAY)
    y += LINE_H

    @results.each do |r|
      name = r[:backend].to_s
      if r[:skipped]
        @gfx.draw_text(COL_X, y, sprintf("%-10s  not in this build", name), FmrbGfx::GRAY)
      elsif r[:error]
        @gfx.draw_text(COL_X, y, sprintf("%-10s  %s", name, r[:error]), FmrbGfx::RED)
      else
        color = r[:agrees] ? FmrbGfx::WHITE : FmrbGfx::RED
        @gfx.draw_text(COL_X, y,
                       sprintf("%-10s %8.1f %8.1f %5d %4d  %s",
                               name, r[:us_avg], r[:us_min], r[:peak_bin], r[:dev],
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
