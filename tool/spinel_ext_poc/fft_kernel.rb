# FFT as an upstream Spinel ext library (P1 prototype, T4; not wired into any build).
#
# Compiled with
#   spinel fft_kernel.rb -c --ext-init Init_fft \
#     --ext-entry FftKernel.run,FftKernel.run_q15,FftKernel.level_db
#
# The same shape as raycast_kernel.rb: the cores are cached in module instance
# variables (rebuilt only when the size changes), samples arrive as a byte
# String of little-endian int16, magnitudes go back as a byte String. The
# two cores sit in two variables on purpose, as in the fork's entry: one
# variable holding either class would widen to untyped.
#
# level_db is here to put a Float across the boundary both ways (an argument
# and the return value); the gem does not need it today.
#
# fft_core.rb and fft_core_q15.rb are read from the gem's mrblib in place.
require_relative "../../lib/add/picoruby-fmrb-fft/mrblib/fft_core"
require_relative "../../lib/add/picoruby-fmrb-fft/mrblib/fft_core_q15"

module FftKernel
  def self.check(samples, n, iters)
    pow2 = n >= 64 && (n & (n - 1)) == 0
    if !pow2 || iters < 1 || samples.bytesize < n * 2
      raise ArgumentError, "bad request n=#{n} iters=#{iters} bytes=#{samples.bytesize}"
    end
    nil
  end

  # Double-precision core: n/2 magnitudes as little-endian int16.
  def self.run(samples, n, iters)
    check(samples, n, iters)
    core = @core
    if core.nil? || core.size != n
      core = FftCore.new(n)
      @core = core
    end
    core.load(samples)
    core.run(iters)
    mags = core.magnitudes_bytes
    @mags = mags
    mags
  end

  # Q15 core: the same output format, integer arithmetic only.
  def self.run_q15(samples, n, iters)
    check(samples, n, iters)
    q15 = @q15
    if q15.nil? || q15.size != n
      q15 = FftCoreQ15.new(n)
      @q15 = q15
    end
    q15.load(samples)
    q15.run(iters)
    q15.magnitudes_bytes
  end

  # Bin i of the last run's magnitudes in dB relative to full scale, never
  # below floor_db.
  def self.level_db(i, floor_db)
    mags = @mags
    raise RuntimeError, "run has not been called" if mags.nil?
    lo = mags.getbyte(i * 2)
    hi = mags.getbyte(i * 2 + 1)
    raise ArgumentError, "bin #{i} is out of range" if lo.nil? || hi.nil?
    m = lo | (hi << 8)
    return floor_db if m == 0
    db = 20.0 * Math.log10(m / 32767.0)
    db < floor_db ? floor_db : db
  end
end

# Type inference driver (excluded from Init_fft).
if __FILE__ == $0
  s = "\x00\x10\x00\xf0" * 64
  p FftKernel.run(s, 128, 1).bytesize
  p FftKernel.run_q15(s, 128, 1).bytesize
  p FftKernel.level_db(0, -96.0)
end
