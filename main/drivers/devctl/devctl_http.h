#ifndef FMRB_DEVCTL_HTTP_H
#define FMRB_DEVCTL_HTTP_H

#include "fmrb_err.h"
#include "esp_http_server.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add the development control endpoints to an already-started server
 * @param server the httpd instance to register on
 * @return FMRB_OK, or FMRB_ERR_INVALID_PARAM for a NULL server
 *
 * Whoever started the server owns it; this only adds routes. It needs fourteen
 * free URI handler slots (httpd_config_t::max_uri_handlers), three more with
 * the checkpoint gem.
 */
fmrb_err_t devctl_http_register(httpd_handle_t server);

/**
 * @brief Whether the file endpoints read and write everything under "/".
 *
 * The one place that says what a development build may do: true in a build
 * with FMRB_DEV_REMOTE_CTL (every path, the keys and passwords included, so
 * /home can be backed up and restored over WiFi), false otherwise (the four
 * roots apps use, keys never read). doc/remote_guard/plan.md.
 */
bool devctl_fs_unrestricted(void);

/**
 * @brief The board's identity for /status: `,"board":"<name>","host":"<mdns>"`.
 *
 * board is FmrbConst::BOARD's spelling (naryav4, tab5, narya_v3, ...), host
 * the mDNS name without ".local" (fmruby-XXXXXX, the WiFi MAC's last three
 * bytes). The backup tools name a board's directory from them.
 * @return the length written (out is always terminated)
 */
size_t devctl_status_ident(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_DEVCTL_HTTP_H */
