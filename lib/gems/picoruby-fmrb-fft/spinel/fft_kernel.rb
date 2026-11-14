# spinel-ext-init: Init_fft
# spinel-ext-entry: FftKernel.load,FftKernel.run,FftKernel.magnitudes,FftKernel.load_q15,FftKernel.run_q15,FftKernel.magnitudes_q15
#
# The Spinel side of the FFT comparison: the same fft_core.rb and
# fft_core_q15.rb the mruby VM runs, compiled as a Spinel ext program and
# called from an mruby task as a library (native/fmrb_fft_spinel.c explains
# how that is possible).
#
# rake spinel:gen reads the two lines above and emits fft_kernel.c and the
# header fft_kernel.h, with every entry a typed C function, e.g.
#
#   sp_int       sp_FftKernel_s_load(const char *lv_samples, sp_int lv_n);
#   sp_int       sp_FftKernel_s_run(sp_int lv_iters);
#   const char * sp_FftKernel_s_magnitudes(void);
#
# A transform is three calls -- load, run, magnitudes -- so the receiver can
# time `run` alone, exactly the region the C backend times for itself: the
# sample decode and the magnitude encode stay outside it, as they do in C.
#
# The cores are cached in module instance variables and rebuilt only when the
# size changes. A core's constructor builds the window, the twiddle table and
# the bit-reversal permutation: a thousand Math.cos calls, several times the
# transform it prepares for (doc/mic_spectrum/report/track_a.md, E5). The top
# level runs once, in Init_fft, so the cores live as long as the instance.
#
# Two variables and two sets of entries on purpose: one variable that can hold
# either class widens to untyped, and the generated code stops being the
# direct native calls the whole comparison is about.
#
# fft_core.rb and fft_core_q15.rb are copied next to this file by
# `rake spinel:gen` from the gem's mrblib -- one file per core, two engines
# each, so the comparison cannot drift apart through an edit to one copy.
require_relative "fft_core"
require_relative "fft_core_q15"

module FftKernel
  def self.check(samples, n)
    pow2 = n >= 64 && (n & (n - 1)) == 0
    if !pow2 || samples.bytesize < n * 2
      raise ArgumentError, "bad request n=#{n} bytes=#{samples.bytesize}"
    end
    nil
  end

  # Double-precision core. samples: n little-endian int16.
  def self.load(samples, n)
    check(samples, n)
    core = @core
    if core.nil? || core.size != n
      core = FftCore.new(n)
      @core = core
    end
    core.load(samples)
    n
  end

  def self.run(iters)
    core = @core
    raise RuntimeError, "load has not been called" if core.nil?
    raise ArgumentError, "iters must be positive" if iters < 1
    core.run(iters)
    iters
  end

  # n/2 magnitudes as little-endian int16.
  def self.magnitudes
    core = @core
    raise RuntimeError, "load has not been called" if core.nil?
    core.magnitudes_bytes
  end

  # Q15 fixed-point core: the same three steps, integer arithmetic only.
  def self.load_q15(samples, n)
    check(samples, n)
    q15 = @q15
    if q15.nil? || q15.size != n
      q15 = FftCoreQ15.new(n)
      @q15 = q15
    end
    q15.load(samples)
    n
  end

  def self.run_q15(iters)
    q15 = @q15
    raise RuntimeError, "load_q15 has not been called" if q15.nil?
    raise ArgumentError, "iters must be positive" if iters < 1
    q15.run(iters)
    iters
  end

  def self.magnitudes_q15
    q15 = @q15
    raise RuntimeError, "load_q15 has not been called" if q15.nil?
    q15.magnitudes_bytes
  end
end

# Type inference driver: the entries' argument types are taken from these
# calls. Left out of Init_fft.
if __FILE__ == $0
  s = "\x00\x10\x00\xf0" * 64
  p FftKernel.load(s, 128)
  p FftKernel.run(1)
  p FftKernel.magnitudes.bytesize
  p FftKernel.load_q15(s, 128)
  p FftKernel.run_q15(1)
  p FftKernel.magnitudes_q15.bytesize
end
