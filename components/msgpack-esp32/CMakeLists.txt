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