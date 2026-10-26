#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Common return codes for Family mruby OS
typedef enum {
    FMRB_OK = 0,
    FMRB_ERR_INVALID_PARAM = -1,
    FMRB_ERR_NO_MEMORY = -2,
    FMRB_ERR_TIMEOUT = -3,
    FMRB_ERR_NOT_SUPPORTED = -4,
    FMRB_ERR_BUSY = -5,
    FMRB_ERR_FAILED = -6,
    FMRB_ERR_NOT_FOUND = -7,
    FMRB_ERR_INVALID_STATE = -8,
    FMRB_ERR_NO_RESOURCE = -9,
    // A write, delete or rename aimed at a read-only part of the namespace
    // (the native build's distribution tree, doc/linux_native/).
    FMRB_ERR_READ_ONLY = -10,
} fmrb_err_t;

#ifdef __cplusplus
}
#endif
