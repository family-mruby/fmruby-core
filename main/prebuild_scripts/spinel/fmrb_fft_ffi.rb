# FFI declarations for the Spinel FFT backend (main/kernel/fmrb_fft_spinel.c).
#
# Kept apart from fmrb_ffi.rb because this program is not the kernel and does
# not want the kernel's surface: it is a library called from an mruby task, and
# these six calls are all it needs. (Separate FFI file per program is also what
# the editor's Spinel build does -- see doc/editor_serious_mode P5.)
#
# Nothing but bytes and ints crosses: the samples arrive as a binary String
# (:binstr, byte-exact via sp_net_bin_len), the magnitudes go back as a String
# plus its length, and the microseconds are an int. No Float ever touches the
# boundary.
module FftSpx
  ffi_func :fmrb_fft_spx_samples, [], :binstr
  ffi_func :fmrb_fft_spx_size, [], :int
  ffi_func :fmrb_fft_spx_iters, [], :int
  ffi_func :fmrb_fft_spx_micros, [], :int
  ffi_func :fmrb_fft_spx_output, [:str, :int, :int], :void
  ffi_func :fmrb_fft_spx_log, [:str, :int], :void
end
