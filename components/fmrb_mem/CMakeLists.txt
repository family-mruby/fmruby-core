idf_component_register(
    SRCS
        "fmrb_alloc.c"
        "fmrb_mempool.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_common fmrb_hal fmrb_log mem_allocator
)

# fmrb_mem_config.h sizes the /tmp arena per machine family, and fmrb_hal sizes
# its capacity from the same header. Both have to see the same definition or
# the filesystem hands out more than the arena holds.
include(${CMAKE_CURRENT_LIST_DIR}/../fmrb_common/fmrb_hw_defines.cmake)
fmrb_add_hw_defines()