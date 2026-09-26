# FFI :varargs passes every Integer vararg as long long. On a 32-bit target a
# `%d` reads 4 bytes, so each later vararg shifts by 4 and `%s` dereferences
# an integer: SIGSEGV.
module C
  ffi_func :printf, [:str, :varargs], :int
end
C.printf("%d-%d-%s\n", 1, 22, "hi")
