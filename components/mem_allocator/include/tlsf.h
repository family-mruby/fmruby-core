/*
** Shadow header for the vendored TLSF (components/mem_allocator/tlsf is a
** git submodule and must not be edited). Consumers keep writing
** #include "tlsf.h" and calling tlsf_*(); the rename below turns every
** reference into fmrb_tlsf_* so it can never bind to the incompatible
** TLSF inside ESP-IDF's heap component. See fmrb_tlsf_rename.h.
*/
#pragma once

#include "fmrb_tlsf_rename.h"
#include "../tlsf/tlsf.h"
