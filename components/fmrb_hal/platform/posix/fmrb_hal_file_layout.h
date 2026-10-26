#pragma once

/*
 * Where the POSIX file HAL puts the Family mruby namespace on the host.
 *
 * Two shapes, one resolver (fmrb_hal_file_resolve_path):
 *
 *   flat     the simulation and the browser. Every path lands under the
 *            cwd-relative "flash" directory, which is also writable. This is
 *            the default; nothing has to be called for it.
 *   layered  the native Linux program (doc/linux_native/plan.md, "ファイルの
 *            置き場所"). The top-level name decides the root: the
 *            distribution tree (read-only), the user's directory (/home, /etc,
 *            /app/usr) or the cache directory (/var, /cache, /data, /tmp).
 *
 * Only the native build calls this header's function, before
 * fmrb_hal_file_init.
 */

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *system_root;  /* distribution tree, never written (repo flash/) */
    const char *user_root;    /* the user's files: /home, /etc, /app/usr */
    const char *cache_root;   /* can be rebuilt: /var, /cache, /data, /tmp */
    /* Copied to <user_root>/etc on the first start, when that does not exist
     * yet. NULL: <system_root>/etc. */
    const char *etc_seed;
} fmrb_hal_file_layout_t;

/*
 * Switch the resolver to the layered table. The strings are copied; every
 * root should be an absolute path (Dir.chdir changes the process cwd). The
 * roots other than system_root are created by fmrb_hal_file_init.
 */
fmrb_err_t fmrb_hal_file_set_layout(const fmrb_hal_file_layout_t *layout);

#ifdef __cplusplus
}
#endif
