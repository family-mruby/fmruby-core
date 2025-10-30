idf_component_register(
    SRCS
        "fmrb_link_protocol.c"
        "fmrb_link_transport.c"
        "fmrb_link_cobs.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES fmrb_hal msgpack-esp32
)