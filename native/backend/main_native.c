/*
 * main_native.c - process entry of the native Linux program
 * (doc/linux_native/, L1).
 *
 * Two kinds of thread, kept apart:
 *
 *   the main thread  SDL (window, events, audio device), the control socket.
 *                    Not a FreeRTOS task; talks to the machine only through
 *                    native_frame (screen), native_audio (sound) and the
 *                    input ring (native_input), the way the browser's page
 *                    talks to the wasm machine.
 *   the machine      every FreeRTOS task, on the cooperative port
 *                    (hosted/port/), started from a thread of its own that
 *                    then parks in the scheduler. No signal is used, so SDL
 *                    and the scheduler share the process without the
 *                    simulation's SIGALRM trouble.
 *
 * Options (each also as an environment variable; the option wins):
 *
 *   --scale N         FMRB_NATIVE_SCALE      window magnification (3)
 *   --user-dir DIR    FMRB_NATIVE_USER_DIR   /home /etc /app/usr
 *                                            ($XDG_DATA_HOME/family-mruby-dev)
 *   --cache-dir DIR   FMRB_NATIVE_CACHE_DIR  /var /cache /data /tmp
 *                                            ($XDG_CACHE_HOME/family-mruby-dev)
 *   --system-dir DIR  FMRB_NATIVE_SYSTEM_DIR distribution tree, read-only
 *                                            (the repository's flash/)
 *   --etc-seed DIR    FMRB_NATIVE_ETC_SEED   the first start's /etc
 *                                            (native/build/root/etc)
 *   --run-dir DIR     FMRB_NATIVE_RUN_DIR    control and inject sockets, pid
 *                                            (/tmp/fmrb-native-<uid>)
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "fmrb_hal_file_layout.h"
#include "host_sdl.h"
#include "input_source.h"
#include "native_config.h"
#include "native_ctl.h"
#include "native_input.h"

void app_main(void);

/* The native program's input: the window, and the injection socket the
 * headless tools use. */
const fmrb_input_source_t *const fmrb_input_sources[] = {
    &fmrb_input_source_sdl,
    &fmrb_input_source_inject,
    NULL,
};

native_config_t g_native_config;

#define MAIN_TASK_STACK_BYTES  (256 * 1024)
#define MAIN_TASK_PRIORITY     5

#ifndef FMRB_NATIVE_DEFAULT_SYSTEM_DIR
#error "FMRB_NATIVE_DEFAULT_SYSTEM_DIR must be set by native/CMakeLists.txt"
#endif
#ifndef FMRB_NATIVE_DEFAULT_ETC_SEED
#error "FMRB_NATIVE_DEFAULT_ETC_SEED must be set by native/CMakeLists.txt"
#endif

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--scale N] [--user-dir DIR] [--cache-dir DIR]\n"
            "          [--system-dir DIR] [--etc-seed DIR] [--run-dir DIR]\n",
            argv0);
}

static void set_path(char *dst, const char *value)
{
    snprintf(dst, NATIVE_PATH_MAX, "%s", value);
}

/* $VAR/<leaf>, or $HOME/<fallback>/<leaf>. */
static void xdg_path(char *dst, const char *var, const char *fallback, const char *leaf)
{
    const char *base = getenv(var);
    if (base != NULL && base[0] == '/') {
        snprintf(dst, NATIVE_PATH_MAX, "%s/%s", base, leaf);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(dst, NATIVE_PATH_MAX, "%s/%s/%s", home ? home : "/tmp", fallback, leaf);
}

/* Relative paths are made absolute: Dir.chdir moves the process cwd, and
 * every root must keep naming the same place. */
static void make_absolute(char *path)
{
    if (path[0] == '/' || path[0] == '\0') return;
    char cwd[NATIVE_PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return;
    char abs[NATIVE_PATH_MAX * 2];
    snprintf(abs, sizeof(abs), "%s/%s", cwd, path);
    set_path(path, abs);
}

static int parse_options(int argc, char **argv)
{
    native_config_t *c = &g_native_config;
    memset(c, 0, sizeof(*c));
    c->scale = 3;
    set_path(c->system_dir, FMRB_NATIVE_DEFAULT_SYSTEM_DIR);
    set_path(c->etc_seed, FMRB_NATIVE_DEFAULT_ETC_SEED);
    xdg_path(c->user_dir, "XDG_DATA_HOME", ".local/share", "family-mruby-dev");
    xdg_path(c->cache_dir, "XDG_CACHE_HOME", ".cache", "family-mruby-dev");
    snprintf(c->run_dir, NATIVE_PATH_MAX, "/tmp/fmrb-native-%u", (unsigned)getuid());

    const char *v;
    if ((v = getenv("FMRB_NATIVE_SCALE")) != NULL) c->scale = atoi(v);
    if ((v = getenv("FMRB_NATIVE_USER_DIR")) != NULL) set_path(c->user_dir, v);
    if ((v = getenv("FMRB_NATIVE_CACHE_DIR")) != NULL) set_path(c->cache_dir, v);
    if ((v = getenv("FMRB_NATIVE_SYSTEM_DIR")) != NULL) set_path(c->system_dir, v);
    if ((v = getenv("FMRB_NATIVE_ETC_SEED")) != NULL) set_path(c->etc_seed, v);
    if ((v = getenv("FMRB_NATIVE_RUN_DIR")) != NULL) set_path(c->run_dir, v);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else if (next == NULL) {
            usage(argv[0]);
            return -1;
        } else if (strcmp(a, "--scale") == 0) {
            c->scale = atoi(next);
        } else if (strcmp(a, "--user-dir") == 0) {
            set_path(c->user_dir, next);
        } else if (strcmp(a, "--cache-dir") == 0) {
            set_path(c->cache_dir, next);
        } else if (strcmp(a, "--system-dir") == 0) {
            set_path(c->system_dir, next);
        } else if (strcmp(a, "--etc-seed") == 0) {
            set_path(c->etc_seed, next);
        } else if (strcmp(a, "--run-dir") == 0) {
            set_path(c->run_dir, next);
        } else {
            usage(argv[0]);
            return -1;
        }
        i++;
    }
    if (c->scale < 1) c->scale = 1;
    if (c->scale > 8) c->scale = 8;
    make_absolute(c->user_dir);
    make_absolute(c->cache_dir);
    make_absolute(c->system_dir);
    make_absolute(c->etc_seed);
    make_absolute(c->run_dir);
    return 0;
}

static void prvMainTask(void *arg)
{
    (void)arg;
    app_main();
    vTaskDelete(NULL);
}

/* The machine's thread: create app_main's task and hand the thread to the
 * scheduler, exactly the way the browser build bootstraps (main_wasm.c). */
static void *machine_thread(void *arg)
{
    (void)arg;
    if (xTaskCreate(prvMainTask, "app_main", MAIN_TASK_STACK_BYTES, NULL,
                    MAIN_TASK_PRIORITY, NULL) != pdPASS) {
        fprintf(stderr, "native: could not create the main task\n");
        exit(1);
    }
    vTaskStartScheduler();
    fprintf(stderr, "native: the scheduler returned\n");
    exit(1);
    return NULL;
}

static void write_pid_file(void)
{
    char path[NATIVE_PATH_MAX + 16];
    snprintf(path, sizeof(path), "%s/pid", g_native_config.run_dir);
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%d\n", (int)getpid());
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    if (parse_options(argc, argv) != 0) return 2;
    const native_config_t *c = &g_native_config;

    /* The sockets live here, and a Unix socket's path has to fit in 108
     * bytes: refuse a run directory that would silently truncate them. */
    if (strlen(c->run_dir) + sizeof("/inject") > 100) {
        fprintf(stderr, "native: run directory too long for a socket path: %s\n", c->run_dir);
        return 2;
    }
    if (mkdir(c->run_dir, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "native: cannot create %s: %s\n", c->run_dir, strerror(errno));
        return 1;
    }

    fmrb_hal_file_layout_t layout = {
        .system_root = c->system_dir,
        .user_root   = c->user_dir,
        .cache_root  = c->cache_dir,
        .etc_seed    = c->etc_seed,
    };
    struct stat st;
    if (stat(c->etc_seed, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "native: no /etc seed at %s (rake native:etc makes it); "
                        "using the distribution's etc\n", c->etc_seed);
        layout.etc_seed = NULL;
    }
    if (fmrb_hal_file_set_layout(&layout) != 0) {
        fprintf(stderr, "native: bad directory layout\n");
        return 2;
    }

    char sock[NATIVE_PATH_MAX + 16];
    snprintf(sock, sizeof(sock), "%s/inject", c->run_dir);
    native_input_set_inject_path(sock);

    fprintf(stderr, "native: system=%s user=%s cache=%s run=%s scale=%d\n",
            c->system_dir, c->user_dir, c->cache_dir, c->run_dir, c->scale);

    if (!host_sdl_init()) return 1;

    snprintf(sock, sizeof(sock), "%s/ctl", c->run_dir);
    if (!native_ctl_open(sock)) {
        fprintf(stderr, "native: control socket unavailable (screenshots and quit "
                        "from the tools will not work)\n");
    }
    write_pid_file();

    pthread_t machine;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (pthread_create(&machine, &attr, machine_thread, NULL) != 0) {
        fprintf(stderr, "native: cannot start the machine thread\n");
        return 1;
    }
    pthread_attr_destroy(&attr);

    int rc = host_sdl_run();

    char pid_path[NATIVE_PATH_MAX + 16];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", c->run_dir);
    unlink(pid_path);
    snprintf(sock, sizeof(sock), "%s/inject", c->run_dir);
    unlink(sock);
    /* The machine's threads are parked in the scheduler or mid-task. Flush
     * every stdio stream (a file an app has open keeps what it wrote; each
     * FILE has its own lock, so this is safe beside them) and leave without
     * running the C++ destructors of objects those threads may be using. */
    fflush(NULL);
    _exit(rc);
}
