idf_component_register(
    SRCS
        "fmrb_transport.c"
        "fmrb_transport_fragment.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_cobs fmrb_common msgpack-esp32
    PRIV_REQUIRES fmrb_hal
)