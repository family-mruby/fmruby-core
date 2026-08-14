# FFI declarations for the Spinel Hello sample (native/spinel_hello_native.c).
# Two calls cross the boundary: the name comes IN as a byte String (:binstr,
# byte-exact via sp_net_bin_len), the greeting goes OUT as a String plus length.
module SpinelHelloSpx
  ffi_func :spinel_hello_spx_name,   [], :binstr
  ffi_func :spinel_hello_spx_output, [:str, :int], :void
end
