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
# fft_core.rb and fft_core_q15.rb are copied here by `rake spinel:gen` from the
# gem (lib/add/picoruby-fmrb-fft/mrblib/) -- one file per core, two engines
# each, so the comparison cannot drift apart through an edit to one copy.
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
  q15 = FftCoreQ15.new(n)
  q15.load(bytes)

  t0 = FftSpx.fmrb_fft_spx_micros
  q15.run(iters)
  us = FftSpx.fmrb_fft_spx_micros - t0

  qmag = q15.magnitudes_bytes
  FftSpx.fmrb_fft_spx_output(qmag, qmag.bytesize, us)
else
  # The tables are rebuilt per call. That is outside the timed region, and a
  # cached instance would have to survive across entry invocations -- state the
  # entry does not keep. The cost is one table build per run() (not per
  # transform), which the repetition count makes negligible.
  core = FftCore.new(n)
  core.load(bytes)

  t0 = FftSpx.fmrb_fft_spx_micros
  core.run(iters)
  us = FftSpx.fmrb_fft_spx_micros - t0

  mag = core.magnitudes_bytes
  FftSpx.fmrb_fft_spx_output(mag, mag.bytesize, us)
end

0
