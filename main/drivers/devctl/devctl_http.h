#ifndef FMRB_DEVCTL_HTTP_H
#define FMRB_DEVCTL_HTTP_H

#include "fmrb_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add the development control endpoints to an already-started server
 * @param server the httpd instance to register on
 * @return FMRB_OK, or FMRB_ERR_INVALID_PARAM for a NULL server
 *
 * Whoever started the server owns it; this only adds routes. It needs eight
 * free URI handler slots (httpd_config_t::max_uri_handlers).
 */
fmrb_err_t devctl_http_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_DEVCTL_HTTP_H */
