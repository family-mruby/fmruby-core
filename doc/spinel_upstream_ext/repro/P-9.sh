#!/bin/sh
# usage: P-9.sh <spinel checkout> <32-bit libspinel_rt.a>
# The 32-bit runtime is built with `make CC='cc -m32'` in a scratch clone
# (bin/spinel itself fails to link there without the i386 libcrypt, but
# lib/libspinel_rt.a is built before that step).
SP=$1; RT=$2; D=$(dirname "$0")
"$SP/bin/spinel" "$D/P-9.rb" -c --no-line-map --cc='cc -m32' -o /tmp/ffi_va32.c &&
cc -m32 -O1 -w -ffunction-sections -fdata-sections -I"$SP/lib" -c /tmp/ffi_va32.c -o /tmp/ffi_va32.o &&
cc -m32 -Wl,--gc-sections /tmp/ffi_va32.o "$RT" -lm -o /tmp/ffi_va32 && /tmp/ffi_va32; echo "exit=$?"
