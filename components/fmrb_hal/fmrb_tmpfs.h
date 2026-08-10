#pragma once

#include <stddef.h>
#include "fmrb_err.h"
#include "fmrb_mem_config.h"   // FMRB_TMPFS_CAPACITY_BYTES

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The /tmp mount point.
 *
 * /home is the persistent store; /tmp is RAM. It is emptied by a reboot, which
 * is the correct meaning for scene state and for the intermediate results one
 * app hands to another (doc/multivm_app/plan.md 3.1). It is also how a large
 * payload crosses between VMs at all -- a message caps out at 176 bytes, so
 * the sender writes a file and passes the path.
 *
 * v1 is a flat directory: /tmp holds files, not subdirectories. Both platforms
 * enforce that, so an app written against the simulator behaves the same on
 * device.
 */
#define FMRB_TMPFS_MOUNT "/tmp"

/**
 * @brief Bring up /tmp. Called from fmrb_hal_file_init.
 *
 * ESP32 registers a RAM-backed VFS at the mount point; POSIX (Linux dev mode)
 * maps it onto a host directory and empties it, so the volatility matches.
 *
 * @return FMRB_OK on success, error code otherwise.
 */
fmrb_err_t fmrb_tmpfs_init(void);

/**
 * @brief Report how much of /tmp is in use.
 *
 * @param used     Optional out param, bytes of file content currently stored.
 * @param capacity Optional out param, bytes the mount will hand out in total.
 */
void fmrb_tmpfs_usage(size_t *used, size_t *capacity);

#ifdef __cplusplus
}
#endif
