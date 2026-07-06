#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Remote-desktop lifecycle: reads the [remote_desktop] section of
// /etc/system_conf.toml, waits for a WiFi IP address in a small one-shot
// task, then starts the HTTP/WebSocket server (rd_http).

/**
 * @brief Start the remote desktop service (asynchronous).
 *
 * Returns FMRB_ERR_NOT_FOUND when disabled in the configuration.
 */
fmrb_err_t rd_task_init(void);

#ifdef __cplusplus
}
#endif
