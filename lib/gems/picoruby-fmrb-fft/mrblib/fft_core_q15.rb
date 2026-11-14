# The same FFT with no floating point in it at all. One source, two engines.
#
# fft_core.rb computes in mrb_float, which is a double on both mruby and
# Spinel. That is a poor fit for a microcontroller whose FPU is single
# precision only (the ESP32-P4 is RV32IMAFC: no "D"), where every double
# multiply is a call into a software emulation. This file is the answer an
# embedded person would give: scale everything to Q15 and let the integer ALU
# do the work. On Spinel that matters twice over, because mrb_int follows
# pointer width -- 32 bits on the device -- so these are native machine
# integers, not boxed numbers (doc/mic_spectrum/impl_plan_spinel_perf.md, E4).
#
# Q15 means a real number x in [-1, 1) is carried as round(x * 32768), so a
# product of two of them is a Q30 and comes back with >> 15. The sample values
# are kept as plain int16 counts rather than fractions, so what the arrays hold
# is really "int16 scaled by a twiddle", which is the usual fixed-point FFT
# arrangement:
#
#   - each butterfly stage shifts its outputs right by one, so the transform
#     comes out divided by n and no intermediate can outgrow int16. That is the
#     whole overflow argument: a butterfly is (a+t)/2 and (a-t)/2 with |t|=|b|,
#     so |z| never rises, the products stay under 32767 * 32768 < 2^31, and 32
#     bits is enough everywhere.
#   - the float version scales its magnitudes by 2/n at the end. Here the n is
#     already gone into the per-stage shifts, so the same number is 2 * |z|.
#   - the magnitude needs a square root, and using Math.sqrt would put a double
#     back in. isqrt below is Newton's, in integers.
#
# It is written like fft_core.rb for the same reasons -- while loops, tables
# built once in the constructor, arrays hoisted into locals -- and mirrors
# fmrb_fft_c_q15() in main/kernel/fmrb_fft_bench.c line for line, so the
# numbers compare engines rather than algorithms.
class FftCoreQ15
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

    # Tables are built in float and truncated once, here, outside anything that
    # is timed -- the same thing the C version does, with the same expression,
    # so the two tables come out bit for bit identical.
    i = 0
    while i < n
      w = (0.5 - 0.5 * Math.cos(2.0 * pi * i / n)) * 32768.0
      w = 32767.0 if w > 32767.0
      @window << w.to_i
      @re << 0
      @im << 0
      @samples << 0
      i += 1
    end

    k = 0
    while k < @half
      a = -2.0 * pi * k / n
      c = Math.cos(a) * 32768.0
      s = Math.sin(a) * 32768.0
      c = 32767.0 if c > 32767.0
      c = -32767.0 if c < -32767.0
      s = 32767.0 if s > 32767.0
      s = -32767.0 if s < -32767.0
      @cos << c.to_i
      @sin << s.to_i
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

  # Decode n little-endian int16 samples out of a byte String, outside the
  # timed loop -- the C baseline is handed an int16 array.
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

  # One transform of the loaded samples, in place. Integers only.
  def forward
    n = @n
    re = @re
    im = @im
    rev = @rev
    win = @window
    samples = @samples
    cos = @cos
    sin = @sin

    i = 0
    while i < n
      r = rev[i]
      re[r] = (samples[i] * win[i]) >> 15
      im[r] = 0
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
          tr = (rb * wr - ib * wi) >> 15
          ti = (rb * wi + ib * wr) >> 15
          ra = re[a]
          ia = im[a]
          # The >> 1 that keeps the transform inside int16 -- see the header.
          re[b] = (ra - tr) >> 1
          im[b] = (ia - ti) >> 1
          re[a] = (ra + tr) >> 1
          im[a] = (ia + ti) >> 1
          k += step
          j += 1
        end
        i += len
      end
      len = len << 1
    end
    nil
  end

  # Run the transform `iters` times, the repetition inside the measured region
  # exactly as fft_core.rb and the C baseline do it.
  def run(iters)
    i = 0
    while i < iters
      forward
      i += 1
    end
    nil
  end

  # Integer square root, Newton's method. Only ever called on the magnitudes,
  # which is outside the timed region -- it is here so that no part of this
  # file needs a float, not because its speed matters.
  def isqrt(v)
    return 0 if v <= 0
    x = v
    y = (x + 1) >> 1
    while y < x
      x = y
      y = (x + v / x) >> 1
    end
    x
  end

  # n/2 magnitudes as little-endian int16. The per-stage shifts already divided
  # by n, so the float version's 2/n scaling is just a doubling here.
  def magnitudes_bytes
    half = @half
    re = @re
    im = @im
    out = "\x00" * (half * 2)
    i = 0
    while i < half
      r = re[i]
      m = im[i]
      v = 2 * isqrt(r * r + m * m)
      v = 32767 if v > 32767
      out.setbyte(i * 2, v & 0xFF)
      out.setbyte(i * 2 + 1, (v >> 8) & 0xFF)
      i += 1
    end
    out
  end
end
