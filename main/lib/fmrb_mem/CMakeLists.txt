idf_component_register(
    SRCS
        "fmrb_alloc.c"
        "fmrb_mempool.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_hal mem_allocator
)