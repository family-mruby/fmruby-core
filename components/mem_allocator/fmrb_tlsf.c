/*
** Compilation wrapper for the vendored TLSF (the submodule sources stay
** untouched). Pulling tlsf.c in after the rename header makes it define
** fmrb_tlsf_* symbols, so the linker can never substitute ESP-IDF heap's
** own (ABI-incompatible) tlsf_* implementation for ours.
*/
#include "include/fmrb_tlsf_rename.h"
#include "tlsf/tlsf.c"
