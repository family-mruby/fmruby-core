# fmrb_link_cobs.h is in components/fmrb_wire (shared with graphics-audio).
idf_component_register(
    SRCS
        "fmrb_link_cobs.c"
    REQUIRES fmrb_wire log
)
