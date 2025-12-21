idf_component_register(
    SRCS
        "fmrb_link_protocol.c"
        "fmrb_link_transport.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_cobs fmrb_common msgpack-esp32
    PRIV_REQUIRES fmrb_hal
)