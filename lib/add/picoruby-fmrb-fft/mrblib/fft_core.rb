# The FFT, in Ruby. One source, two engines.
#
# This exact file is what the :ruby and :spinel backends both run: mruby
# compiles it to bytecode and runs it on the VM, Spinel compiles it to C and
# runs it natively (main/prebuild_scripts/spinel/fft_spinel.rb requires it).
# The only difference between those two numbers is the engine, which is the
# whole point of the comparison in doc/mic_spectrum/plan.md.
#
# It is also written to mirror main/kernel/fmrb_fft_bench.c line for line --
# same window, same bit-reversal, same twiddle table, same scaling -- so the C
# number is a baseline for the same work rather than for a different algorithm.
#
# How it is written matters as much as what it does:
#   - while loops, not Integer#times: a block call per iteration would be
#     measured instead of the transform
#   - tables (window, twiddles, bit-reversal) precomputed in the constructor;
#     a Math.sin in the inner loop would make this a benchmark of Math.sin
#   - the arrays the inner loop touches are pulled into locals first; an ivar
#     read per access is a real cost on both engines
#   - no parallel assignment on array elements (broken in picoruby)
class FftCore
  def initialize(n)
    pi = 3.141592653589793
    @n = n
    @half = n / 2

    @window = []
    @cos = []
    @sin = []
    @rev = []
    @re = []
    @im = []
    @samples = []

    i = 0
    while i < n
      @window << (0.5 - 0.5 * Math.cos(2.0 * pi * i / n))
      @re << 0.0
      @im << 0.0
      @samples << 0
      i += 1
    end

    k = 0
    while k < @half
      a = -2.0 * pi * k / n
      @cos << Math.cos(a)
      @sin << Math.sin(a)
      k += 1
    end

    bits = 0
    bits += 1 while (1 << bits) < n
    i = 0
    while i < n
      r = 0
      v = i
      b = 0
      while b < bits
        r = (r << 1) | (v & 1)
        v >>= 1
        b += 1
      end
      @rev << r
      i += 1
    end
  end

  def size
    @n
  end

  # Decode n little-endian int16 samples out of a byte String. Done once,
  # outside the timed loop, because the C baseline is handed an int16 array --
  # timing the decode here would be timing something C never does.
  def load(bytes)
    n = @n
    samples = @samples
    i = 0
    while i < n
      lo = bytes.getbyte(i * 2)
      hi = bytes.getbyte(i * 2 + 1)
      lo = 0 if lo.nil?
      hi = 0 if hi.nil?
      v = lo | (hi << 8)
      v -= 65536 if v >= 32768
      samples[i] = v
      i += 1
    end
    nil
  end

  # One transform of the loaded samples, in place.
  def forward
    n = @n
    re = @re
    im = @im
    rev = @rev
    win = @window
    samples = @samples
    cos = @cos
    sin = @sin

    # Window and bit-reverse in one pass: the sample that belongs at index i is
    # the one at the reversed position.
    i = 0
    while i < n
      r = rev[i]
      re[r] = samples[i] * win[i]
      im[r] = 0.0
      i += 1
    end

    len = 2
    while len <= n
      half = len >> 1
      step = n / len
      i = 0
      while i < n
        k = 0
        j = 0
        while j < half
          wr = cos[k]
          wi = sin[k]
          a = i + j
          b = a + half
          rb = re[b]
          ib = im[b]
          tr = rb * wr - ib * wi
          ti = rb * wi + ib * wr
          ra = re[a]
          ia = im[a]
          re[b] = ra - tr
          im[b] = ia - ti
          re[a] = ra + tr
          im[a] = ia + ti
          k += step
          j += 1
        end
        i += len
      end
      len = len << 1
    end
    nil
  end

  # Run the transform `iters` times. The loop lives here so the Ruby engines
  # are timed the way the C one is: the repetition is inside the measured
  # region, not around a foreign call.
  def run(iters)
    i = 0
    while i < iters
      forward
      i += 1
    end
    nil
  end

  # n/2 magnitudes as little-endian int16, scaled 2/n and clamped -- the same
  # arithmetic fft_magnitudes() does in C, so the bins can be compared one by
  # one.
  def magnitudes_bytes
    half = @half
    re = @re
    im = @im
    scale = 2.0 / @n
    out = "\x00" * (half * 2)
    i = 0
    while i < half
      m = Math.sqrt(re[i] * re[i] + im[i] * im[i]) * scale
      m = 32767.0 if m > 32767.0
      v = (m + 0.5).to_i
      out.setbyte(i * 2, v & 0xFF)
      out.setbyte(i * 2 + 1, (v >> 8) & 0xFF)
      i += 1
    end
    out
  end
end
