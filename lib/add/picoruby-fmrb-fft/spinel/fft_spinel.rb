# The Spinel side of the FFT comparison: the same fft_core.rb the mruby VM
# runs, compiled to native code and called from an mruby task as a library
# (main/kernel/fmrb_fft_spinel.c explains how that is possible).
#
# Compiled with `spinel --no-main --entry fmrb_fft_spinel_entry`, so this
# file's top level IS the entry: it runs once per fmrb_fft_spinel_run() call.
# Input and output cross through the FFI functions in fmrb_fft_ffi.rb, since
# the entry takes no arguments.
#
# Both cores are here, chosen per call by the mode the caller sets: the double
# one for the :spinel backend, the Q15 one for :spinel_q15
# (doc/mic_spectrum/impl_plan_spinel_perf.md, E4). The two branches are written
# out twice instead of sharing a `core` variable on purpose -- one variable
# holding either class widens to untyped and the generated code stops being
# the direct native calls the whole comparison is about.
#
# THE CORES ARE CACHED IN GLOBALS, and that is the point of this file rather
# than an implementation detail. A core's constructor builds the window, the
# twiddle table and the bit-reversal permutation: a thousand Math.cos calls,
# and measured at 13.7ms (double) / 16.4ms (Q15) on the P4 -- several times the
# transform it prepares for. Rebuilding that per call is what made this backend
# useless for anything that asks for one transform per frame
# (doc/mic_spectrum/report/track_a.md, E5).
#
# Caching it here only works because the program is compiled with
# --persistent-statics, which turns the entry's `sp_reset_tu_statics()` from a
# per-call clear into a per-instance one. Without that flag these globals are
# nil again on every call and this is merely harmless. With it, the cores live
# as long as the Spinel instance does -- which is what an ordinary Ruby object
# in a gem would do.
#
# fft_core.rb and fft_core_q15.rb are copied here by `rake spinel:gen` from the
# gem (lib/add/picoruby-fmrb-fft/mrblib/) -- one file per core, two engines
# each, so the comparison cannot drift apart through an edit to one copy. The
# caching lives here, in the entry wrapper, and not in them: they stay the
# plain Ruby that the :ruby backend also runs.
require_relative "fft_core"
require_relative "fft_core_q15"
require_relative "fmrb_fft_ffi"

n = FftSpx.fmrb_fft_spx_size
iters = FftSpx.fmrb_fft_spx_iters
mode = FftSpx.fmrb_fft_spx_mode
bytes = FftSpx.fmrb_fft_spx_samples

if n < 64 || iters < 1 || bytes.bytesize < n * 2
  msg = "bad request n=#{n} iters=#{iters} bytes=#{bytes.bytesize}"
  FftSpx.fmrb_fft_spx_log(msg, msg.bytesize)
elsif mode == 1
  # A global per core rather than one holding either: a single variable that
  # can be two classes widens to untyped, and the generated code stops being
  # the direct calls this whole comparison is about.
  #
  # The cached object is its own cache key. Asking it for its size rather than
  # keeping the size in a second global is what makes this safe when the
  # statics ARE cleared: the reset nulls object globals but leaves integer
  # ones alone, so a remembered size would still match after the object it
  # described had been swept, and the next call would read through a null.
  q15 = $fft_q15
  if q15.nil? || q15.size != n
    q15 = FftCoreQ15.new(n)
    $fft_q15 = q15
  end
  q15.load(bytes)

  t0 = FftSpx.fmrb_fft_spx_micros
  q15.run(iters)
  us = FftSpx.fmrb_fft_spx_micros - t0

  qmag = q15.magnitudes_bytes
  FftSpx.fmrb_fft_spx_output(qmag, qmag.bytesize, us)
else
  core = $fft_core
  if core.nil? || core.size != n
    core = FftCore.new(n)
    $fft_core = core
  end
  core.load(bytes)

  t0 = FftSpx.fmrb_fft_spx_micros
  core.run(iters)
  us = FftSpx.fmrb_fft_spx_micros - t0

  mag = core.magnitudes_bytes
  FftSpx.fmrb_fft_spx_output(mag, mag.bytesize, us)
end

0
