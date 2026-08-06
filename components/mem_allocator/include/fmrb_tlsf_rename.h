/*
** fmrb_tlsf_rename.h - rename the vendored TLSF's public symbols
**
** ESP-IDF's heap component links an espressif fork of TLSF whose public
** functions share these names but NOT these signatures (e.g. its
** tlsf_create_with_pool takes a third max_bytes argument). With both in
** the image, the linker resolved our 2-argument calls to the 3-argument
** implementation, which then read a garbage register as max_bytes -- it
** happened to work at boot and failed at every app spawn. Renaming every
** public symbol keeps the two allocators apart for good.
**
** Must be in effect BEFORE tlsf.h is preprocessed, for definitions
** (fmrb_tlsf.c) and callers (via include/tlsf.h) alike.
*/
#pragma once

#define tlsf_create           fmrb_tlsf_create
#define tlsf_create_with_pool fmrb_tlsf_create_with_pool
#define tlsf_destroy          fmrb_tlsf_destroy
#define tlsf_get_pool         fmrb_tlsf_get_pool
#define tlsf_add_pool         fmrb_tlsf_add_pool
#define tlsf_remove_pool      fmrb_tlsf_remove_pool
#define tlsf_malloc           fmrb_tlsf_malloc
#define tlsf_memalign         fmrb_tlsf_memalign
#define tlsf_realloc          fmrb_tlsf_realloc
#define tlsf_free             fmrb_tlsf_free
#define tlsf_block_size       fmrb_tlsf_block_size
#define tlsf_size             fmrb_tlsf_size
#define tlsf_align_size       fmrb_tlsf_align_size
#define tlsf_block_size_min   fmrb_tlsf_block_size_min
#define tlsf_block_size_max   fmrb_tlsf_block_size_max
#define tlsf_pool_overhead    fmrb_tlsf_pool_overhead
#define tlsf_alloc_overhead   fmrb_tlsf_alloc_overhead
#define tlsf_walk_pool        fmrb_tlsf_walk_pool
#define tlsf_check            fmrb_tlsf_check
#define tlsf_check_pool       fmrb_tlsf_check_pool
