/*
 * native_config.h - how the native program was started.
 *
 * Filled by main_native.c from the command line, the environment and the
 * machine definition (native_machine.h) before anything else runs, read by
 * the host side. The machine says which backends run (native_backends.h);
 * the paths say where the file layers are.
 */
#pragma once

#include "native_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_PATH_MAX 384

typedef struct {
    native_machine_t machine;
    char machines_dir[NATIVE_PATH_MAX];  /* where --machine <name> looks */
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
