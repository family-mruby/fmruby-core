# The Spinel side of the minimal sample. Spinel compiles this to native code and
# native/spinel_hello_native.c calls spinel_hello_entry() as a library
# (doc/spinel_aot/adding_a_spinel_gem.md).
#
# Compiled with `spinel --no-main --entry spinel_hello_entry`, so this file's
# top level IS the entry: it runs once per spinel_hello_run() call. The entry
# takes no arguments, so the greeting crosses back through the FFI function in
# spinel_hello_ffi.rb.
require_relative "spinel_hello_core"
require_relative "spinel_hello_ffi"

name = SpinelHelloSpx.spinel_hello_spx_name          # value crossing IN
s = SpinelHelloCore.new.greet(name)
SpinelHelloSpx.spinel_hello_spx_output(s, s.bytesize) # value crossing OUT
0
