// TLSF wrapper with renamed symbols to avoid conflicts with ESP-IDF heap
// This file renames all TLSF functions with prism_ prefix

#define tlsf_create prism_tlsf_create
#define tlsf_destroy prism_tlsf_destroy
#define tlsf_pool_t prism_tlsf_pool_t
#define tlsf_t prism_tlsf_t
#define tlsf_add_pool prism_tlsf_add_pool
#define tlsf_remove_pool prism_tlsf_remove_pool
#define tlsf_malloc prism_tlsf_malloc
#define tlsf_memalign prism_tlsf_memalign
#define tlsf_realloc prism_tlsf_realloc
#define tlsf_free prism_tlsf_free
#define tlsf_check prism_tlsf_check
#define tlsf_walk_pool prism_tlsf_walk_pool
#define tlsf_block_size prism_tlsf_block_size
#define tlsf_check_pool prism_tlsf_check_pool
#define tlsf_size prism_tlsf_size
#define tlsf_align_size prism_tlsf_align_size
#define tlsf_block_size_min prism_tlsf_block_size_min
#define tlsf_block_size_max prism_tlsf_block_size_max
#define tlsf_pool_overhead prism_tlsf_pool_overhead
#define tlsf_alloc_overhead prism_tlsf_alloc_overhead
#define tlsf_create_with_pool prism_tlsf_create_with_pool
#define tlsf_get_pool prism_tlsf_get_pool
#define pool_t prism_pool_t

#include "tlsf/tlsf.c"
