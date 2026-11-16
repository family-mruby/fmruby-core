#ifndef FMRB_DEVCTL_HTTP_H
#define FMRB_DEVCTL_HTTP_H

#include "fmrb_err.h"
#include "fmrb_remote_guard.h"
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
 * Whoever started the server owns it; this only adds routes. It needs
 * eighteen free URI handler slots (httpd_config_t::max_uri_handlers), three
 * more with the checkpoint gem. A release build arms the guard here
 * (fmrb_remote_guard.h): every route but /pair and /auth asks for a token.
 */
fmrb_err_t devctl_http_register(httpd_handle_t server);

/**
 * @brief Whether the file endpoints read and write everything under "/".
 *
 * The one place that says what a development build may do: true in a build
 * with FMRB_DEV_REMOTE_CTL (every path, the keys and passwords included, so
 * /home can be backed up and restored over WiFi, and no pairing), false in a
 * release build (reads in the four roots apps use, writes under /home and
 * /app, keys never read, the guard armed). doc/remote_guard/plan.md.
 */
bool devctl_fs_unrestricted(void);

/**
 * @brief Whether the request's token (X-Fmrb-Token header, or token= in the
 *        query) lets it in. FMRB_RGUARD_ALLOW when the guard is not armed.
 *
 * For a route that cannot answer 401 itself (a WebSocket: the handshake is
 * answered before its handler runs).
 */
fmrb_rguard_verdict_t devctl_guard_verdict(httpd_req_t *req);

/**
 * @brief devctl_guard_verdict, and the 401 with the reason when it says no.
 * @return true when the handler should go on
 */
bool devctl_guard_admit(httpd_req_t *req);

/**
 * @brief The board's identity for /status:
 *        `,"board":"<name>","host":"<mdns>","guard":"free|closed|open"`.
 *
 * board is FmrbConst::BOARD's spelling (naryav4, tab5, narya_v3, ...), host
 * the mDNS name without ".local" (fmruby-XXXXXX, the WiFi MAC's last three
 * bytes). The backup tools name a board's directory from them. On the P4
 * the flash layout and the font image follow (font_ja_flash_status), which
 * rake flash:app_sync compares with the build.
 * @return the length written (out is always terminated)
 */
size_t devctl_status_ident(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_DEVCTL_HTTP_H */
