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

# Add ESP32-specific definitions for MessagePack
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MSGPACK_ENDIAN_LITTLE_BYTE=1
    MSGPACK_ENDIAN_BIG_BYTE=0
)