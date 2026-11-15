/*
 * The app boundary's Log flat functions (doc/app_boundary): the C bodies
 * behind fmrb_gen/fmrb_boundary.h, generated from sig/fmrb_misc.rbs. Every
 * engine's glue is generated and calls these.
 *
 * One line at one level, tagged with the calling app's name (as the mruby
 * Log always did; "APP" outside an app). The text need not end in NUL.
 */
#include <stdint.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_app.h"
#include "fmrb_log.h"

static const char *app_tag(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (ctx && ctx->app_name[0] != '\0') {
        return ctx->app_name;
    }
    return "APP";
}

int32_t fmrb_bnd_log_debug_raw(const char *msg, int32_t msg_len)
{
    if (!msg || msg_len < 0) {
        return FMRB_BOUNDARY_ERR_ARG;
    }
    FMRB_LOGD(app_tag(), "%.*s", (int)msg_len, msg);
    return 0;
}

int32_t fmrb_bnd_log_info_raw(const char *msg, int32_t msg_len)
{
    if (!msg || msg_len < 0) {
        return FMRB_BOUNDARY_ERR_ARG;
    }
    FMRB_LOGI(app_tag(), "%.*s", (int)msg_len, msg);
    return 0;
}

int32_t fmrb_bnd_log_warn_raw(const char *msg, int32_t msg_len)
{
    if (!msg || msg_len < 0) {
        return FMRB_BOUNDARY_ERR_ARG;
    }
    FMRB_LOGW(app_tag(), "%.*s", (int)msg_len, msg);
    return 0;
}

int32_t fmrb_bnd_log_error_raw(const char *msg, int32_t msg_len)
{
    if (!msg || msg_len < 0) {
        return FMRB_BOUNDARY_ERR_ARG;
    }
    FMRB_LOGE(app_tag(), "%.*s", (int)msg_len, msg);
    return 0;
}
