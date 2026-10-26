/*
 * native_config.h - how the native program was started.
 *
 * Filled by main_native.c from the command line and the environment before
 * anything else runs, read by the host side (host_sdl.c, native_ctl.c). The
 * machine definition of L2 (doc/linux_native/plan.md, "機種の定義") replaces
 * the output and input choices here; the paths stay.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_PATH_MAX 384

typedef struct {
    int  scale;                          /* window magnification, default 3 */
    char system_dir[NATIVE_PATH_MAX];    /* distribution tree (read-only) */
    char etc_seed[NATIVE_PATH_MAX];      /* the first start's /etc */
    char user_dir[NATIVE_PATH_MAX];      /* the user's /home, /etc, /app/usr */
    char cache_dir[NATIVE_PATH_MAX];     /* /var, /cache, /data, /tmp */
    char run_dir[NATIVE_PATH_MAX];       /* sockets and the pid file */
} native_config_t;

extern native_config_t g_native_config;

#ifdef __cplusplus
}
#endif
