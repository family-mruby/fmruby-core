idf_component_register(
    SRCS
        "fmrb_ipc_protocol.c"
        "fmrb_ipc_transport.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_hal
)