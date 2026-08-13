# The Spinel side of the FFT comparison: the same fft_core.rb the mruby VM
# runs, compiled to native code and called from an mruby task as a library
# (main/kernel/fmrb_fft_spinel.c explains how that is possible).
#
# Compiled with `spinel --no-main --entry fmrb_fft_spinel_entry`, so this
# file's top level IS the entry: it runs once per fmrb_fft_spinel_run() call.
# Input and output cross through the FFI functions in fmrb_fft_ffi.rb, since
# the entry takes no arguments.
#
# fft_core.rb is copied here by `rake spinel:gen` from the gem
# (lib/add/picoruby-fmrb-fft/mrblib/fft_core.rb) -- one file, two engines, so
# the comparison cannot drift apart through an edit to one copy.
require_relative "fft_core"
require_relative "fmrb_fft_ffi"

n = FftSpx.fmrb_fft_spx_size
iters = FftSpx.fmrb_fft_spx_iters
bytes = FftSpx.fmrb_fft_spx_samples

if n < 64 || iters < 1 || bytes.bytesize < n * 2
  msg = "bad request n=#{n} iters=#{iters} bytes=#{bytes.bytesize}"
  FftSpx.fmrb_fft_spx_log(msg, msg.bytesize)
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
