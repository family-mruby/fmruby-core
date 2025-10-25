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
    FMRB_ERR_FAILED = -6
} fmrb_err_t;

#ifdef __cplusplus
}
#endif
