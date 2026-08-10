#pragma once

/*
 * Private hooks the POSIX file HAL uses to make the host-directory /tmp behave
 * like the device's RAM store. Not part of the public HAL surface.
 */

#include <stdbool.h>
#include <stddef.h>

#include "fmrb_tmpfs.h"

// True when a resolved host path lives under the /tmp mount.
bool fmrb_tmpfs_owns_path(const char *host_path);

// True when the path names something below the flat mount's single level.
bool fmrb_tmpfs_path_too_deep(const char *host_path);

// Charge/refund bytes against the mount capacity. charge() returns false and
// charges nothing when the mount is full.
bool fmrb_tmpfs_charge(size_t delta);
void fmrb_tmpfs_release(size_t bytes);

// Current size of a regular file, 0 when absent.
size_t fmrb_tmpfs_file_size(const char *host_path);
