# FFI declaration for the Spinel Hello sample (native/spinel_hello_native.c).
# The greeting crosses back as a byte String plus its length; nothing else needs
# to cross for this minimal example.
module SpinelHelloSpx
  ffi_func :spinel_hello_spx_output, [:str, :int], :void
end
