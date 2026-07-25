# MessagePack-C ESP32 Component
# Based on esp-msgpack implementation

idf_component_register(
    SRCS
        "msgpack-c/src/unpack.c"
        "msgpack-c/src/objectc.c"
        "msgpack-c/src/version.c"
        "msgpack-c/src/vrefbuffer.c"
        "msgpack-c/src/zone.c"
    INCLUDE_DIRS
        "msgpack-c/include"
        "include"
    REQUIRES
        log
    PRIV_REQUIRES
        fmrb_mem
)

# Endianness for MessagePack. PUBLIC, not PRIVATE: msgpack/sysdep.h only falls
# back to msgpack/predef auto-detection when these are unset, and that fallback
# knows nothing about Xtensa/RISC-V. It then depends on whether an earlier
# include happened to pull in a header defining __BYTE_ORDER, so a consumer that
# includes <msgpack.h> early fails to build with "supports only big endian and
# little endian". Exporting the defines makes every consumer agree with the
# library itself regardless of include order.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MSGPACK_ENDIAN_LITTLE_BYTE=1
    MSGPACK_ENDIAN_BIG_BYTE=0
)

# Zone chunk size. Upstream's 8192 assumes a desktop heap: msgpack_unpack_next()
# allocates a whole chunk per received message, and our link frames are tens of
# bytes (a canvas-create command is 20). Oversized payloads still work --
# msgpack_zone_malloc_expand doubles until the request fits and then allocates
# once -- so a small chunk only trades one right-sized allocation for the old
# blanket 8 KB. PUBLIC so a consumer calling msgpack_zone_new(MSGPACK_ZONE_CHUNK_SIZE)
# directly agrees with the library.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MSGPACK_ZONE_CHUNK_SIZE=512
)

# msgpack-c is vendored third-party C that calls libc malloc/realloc/free
# directly and offers no allocator hook, which left it the one component
# bypassing fmrb_mem.h. Force-include a header that remaps those names onto the
# SYSTEM pool; see msgpack_mem_override.h for why this is done with macros
# rather than by editing the vendored sources.
#
# PRIVATE, never INTERFACE: the remap must reach msgpack-c's own sources and
# nothing else. Consumers allocate with heap_caps_* for DMA-capable or
# PSRAM-specific buffers and would break if their malloc were rerouted. (The
# header-inline packer path -- msgpack_sbuffer_write -- expands in the caller's
# TU and is deliberately out of reach here; those sites pack into fixed buffers
# instead.)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -include ${CMAKE_CURRENT_LIST_DIR}/msgpack_mem_override.h
)