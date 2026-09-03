#ifndef FMRB_DEVCTL_TASK_H
#define FMRB_DEVCTL_TASK_H

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start a small HTTP server carrying only the development endpoints
 * @return FMRB_OK, or FMRB_ERR_FAILED when the server will not start
 *
 * For a machine with no remote desktop (Retro). Modern registers the same
 * endpoints on the viewer's server instead and does not call this. Calling it
 * twice is a no-op.
 */
fmrb_err_t devctl_start(void);

/** Stop the server started by devctl_start. Safe when it was never started. */
void devctl_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_DEVCTL_TASK_H */
