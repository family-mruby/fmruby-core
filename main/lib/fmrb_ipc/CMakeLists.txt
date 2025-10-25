idf_component_register(
    SRCS
        "fmrb_ipc_protocol.c"
        "fmrb_ipc_transport.c"
        "fmrb_ipc_cobs.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES fmrb_hal msgpack-esp32
)