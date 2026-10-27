/*
 * main_native.c - process entry of the native Linux program
 * (doc/linux_native/).
 *
 * Two kinds of thread, kept apart:
 *
 *   the main thread  the output (SDL window, fbdev, DRM), the control socket
 *                    and the main loop; the audio sink has a thread of its
 *                    own. Not FreeRTOS tasks; they talk to the machine only
 *                    through native_frame (screen), native_audio (sound) and
 *                    the input sources, the way the browser's page talks to
 *                    the wasm machine.
 *   the machine      every FreeRTOS task, on the cooperative port
 *                    (hosted/port/), started from a thread of its own that
 *                    then parks in the scheduler. No signal is used by it.
 *
 * Which output, sound and input run is the machine definition's
 * (native_machine.h), chosen with --machine; native_backends.c turns it into
 * backends and refuses, with the reason, what this build or this system
 * cannot do -- before the machine starts, so a refusal is a clean exit.
 *
 * Options (each also as an environment variable; the option wins):
 *
 *   --machine NAME|PATH  FMRB_NATIVE_MACHINE    the machine definition (sdl)
 *   --machines-dir DIR   FMRB_NATIVE_MACHINES_DIR  where a NAME is looked up
 *                                            (native/build/root/machines)
 *   --set KEY=VALUE      one field over the definition (display.rotate=90);
 *                        may be repeated
 *   --scale N            FMRB_NATIVE_SCALE      display.scale (the window's
 *                                            magnification on sdl, 3)
 *   --user-dir DIR       FMRB_NATIVE_USER_DIR   /home /etc /app/usr
 *                                            ($XDG_DATA_HOME/family-mruby-dev)
 *   --cache-dir DIR      FMRB_NATIVE_CACHE_DIR  /var /cache /data /tmp
 *                                            ($XDG_CACHE_HOME/family-mruby-dev)
 *   --system-dir DIR     FMRB_NATIVE_SYSTEM_DIR distribution tree, read-only
 *                                            (the repository's flash/)
 *   --etc-seed DIR       FMRB_NATIVE_ETC_SEED   the first start's /etc
 *                                            (native/build/root/etc)
 *   --run-dir DIR        FMRB_NATIVE_RUN_DIR    control and inject sockets, pid
 *                                            (/tmp/fmrb-native-<uid>)
 *
 * The defaults in parentheses are the development build's. A package build
 * (NATIVE_PACKAGE, rake native:deb) has none of the repository's paths in
 * it: it finds the distribution tree from where it is installed (its own
 * executable, /proc/self/exe, then FMRB_NATIVE_DATA_REL: the .deb puts the
 * tree at /usr/share/family-mruby/root), takes machines/ and etc/ from that
 * tree, and keeps the user's files under the name family-mruby
 * ($XDG_DATA_HOME/family-mruby, $XDG_CACHE_HOME/family-mruby), apart from a
 * development build's family-mruby-dev.
 *
 * Every start after the first brings the user's /etc up to the
 * distribution's (native/etc/etc_update.h): keys, service sections and files
 * the user does not have yet are added; nothing the user has is changed.
 *
 * Diagnostics (environment only):
 *
 *   FMRB_NATIVE_GATE=off  waiting network calls are made on the task's own
 *                         thread, as without the blocking-call gate -- every
 *                         task stops while one waits. For measuring what the
 *                         gate is for (doc/linux_native/report/l2n.md).
 *
 * Exit codes: 0 quit, 1 a failure while running, 2 bad options, 3 the
 * machine was refused (the reason is on stderr, "native: refused: ...").
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "etc_update.h"
#include "fmrb_kernel.h"
#include "fmrb_gate.h"
#include "net_wrap.h"
#include "fmrb_hal_file_layout.h"
#include "native_backends.h"
#include "native_config.h"
#include "sys_linux.h"
#include "native_ctl.h"
#include "native_frame.h"
#include "native_input.h"
#include "native_machine.h"

void app_main(void);

native_config_t g_native_config;

#define MAIN_TASK_STACK_BYTES  (256 * 1024)
#define MAIN_TASK_PRIORITY     5
/* Largest frame the loop copies out (any size the display task can make). */
#define FRAME_CAP_PX (1920 * 1080)
#define LOOP_WAIT_MS 5
#define MAX_SETS 16

#if FMRB_NATIVE_PACKAGE
#ifndef FMRB_NATIVE_DATA_REL
#error "FMRB_NATIVE_DATA_REL must be set by native/CMakeLists.txt for a package build"
#endif
#define USER_LEAF "family-mruby"
#else
#ifndef FMRB_NATIVE_DEFAULT_SYSTEM_DIR
#error "FMRB_NATIVE_DEFAULT_SYSTEM_DIR must be set by native/CMakeLists.txt"
#endif
#ifndef FMRB_NATIVE_DEFAULT_ETC_SEED
#error "FMRB_NATIVE_DEFAULT_ETC_SEED must be set by native/CMakeLists.txt"
#endif
#ifndef FMRB_NATIVE_DEFAULT_MACHINES_DIR
#error "FMRB_NATIVE_DEFAULT_MACHINES_DIR must be set by native/CMakeLists.txt"
#endif
#define USER_LEAF "family-mruby-dev"
#endif

static volatile sig_atomic_t s_quit_signal = 0;

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--machine NAME|PATH] [--set KEY=VALUE]... [--scale N]\n"
            "          [--machines-dir DIR] [--user-dir DIR] [--cache-dir DIR]\n"
            "          [--system-dir DIR] [--etc-seed DIR] [--run-dir DIR]\n"
            "outputs in this build: %s; audio: %s; input: sdl, evdev\n",
            argv0, native_backends_outputs_built(), native_backends_audio_built());
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

#if FMRB_NATIVE_PACKAGE
/* The installed distribution tree: FMRB_NATIVE_DATA_REL from the directory
 * of this executable, never a path of the build machine. 0 when it is there;
 * otherwise -1 with the place it was looked for in root. */
static int find_installed_root(char *root)
{
    char exe[NATIVE_PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        snprintf(root, NATIVE_PATH_MAX, "(/proc/self/exe: %s)", strerror(errno));
        return -1;
    }
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash != NULL) *slash = '\0';
    char want[NATIVE_PATH_MAX * 2];
    snprintf(want, sizeof(want), "%s/%s", exe, FMRB_NATIVE_DATA_REL);
    char real[PATH_MAX];
    struct stat st;
    if (realpath(want, real) == NULL || stat(real, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(root, NATIVE_PATH_MAX, "%s", want);
        return -1;
    }
    snprintf(root, NATIVE_PATH_MAX, "%s", real);
    return 0;
}
#endif

/* The definition: the file (or the built-in one when none was asked for
 * and the default file is not staged), then every --set, then --scale. */
static int load_machine(const char *machine_arg, const char *const *sets, int n_sets,
                        const char *scale_arg)
{
    native_config_t *c = &g_native_config;
    native_machine_t *m = &c->machine;
    char err[400];
    native_machine_defaults(m);
    char path[NATIVE_PATH_MAX * 2];
    native_machine_resolve(machine_arg ? machine_arg : "sdl", c->machines_dir, path, sizeof(path));
    struct stat st;
    if (machine_arg == NULL && stat(path, &st) != 0) {
        fprintf(stderr, "native: no %s (rake native:etc stages it); using the built-in sdl machine\n", path);
    } else if (native_machine_load_file(m, path, err, sizeof(err)) != 0) {
        fprintf(stderr, "native: refused: machine definition: %s\n", err);
        return 3;
    }
    for (int i = 0; i < n_sets; i++) {
        char key[96];
        const char *eq = strchr(sets[i], '=');
        if (eq == NULL || (size_t)(eq - sets[i]) >= sizeof(key)) {
            fprintf(stderr, "native: --set %s: expected KEY=VALUE\n", sets[i]);
            return 2;
        }
        memcpy(key, sets[i], (size_t)(eq - sets[i]));
        key[eq - sets[i]] = '\0';
        if (native_machine_set(m, key, eq + 1, err, sizeof(err)) != 0) {
            fprintf(stderr, "native: refused: --set %s\n", err);
            return 3;
        }
    }
    if (scale_arg != NULL) {
        int s = atoi(scale_arg);
        if (s < 1) s = 1;
        if (s > 8) s = 8;
        char v[8];
        snprintf(v, sizeof(v), "%d", s);
        native_machine_set(m, "display.scale", v, err, sizeof(err));
    }
    return 0;
}

#if FMRB_NATIVE_PACKAGE
static bool has_option(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) return true;
    }
    return false;
}
#endif

static int parse_options(int argc, char **argv)
{
    native_config_t *c = &g_native_config;
    memset(c, 0, sizeof(*c));
#if FMRB_NATIVE_PACKAGE
    char root[NATIVE_PATH_MAX];
    bool installed = find_installed_root(root) == 0;
    snprintf(c->system_dir, NATIVE_PATH_MAX, "%s", root);
    snprintf(c->etc_seed, NATIVE_PATH_MAX, "%.*s/etc", NATIVE_PATH_MAX - 8, root);
    snprintf(c->machines_dir, NATIVE_PATH_MAX, "%.*s/machines", NATIVE_PATH_MAX - 16, root);
#else
    set_path(c->machines_dir, FMRB_NATIVE_DEFAULT_MACHINES_DIR);
    set_path(c->system_dir, FMRB_NATIVE_DEFAULT_SYSTEM_DIR);
    set_path(c->etc_seed, FMRB_NATIVE_DEFAULT_ETC_SEED);
#endif
    xdg_path(c->user_dir, "XDG_DATA_HOME", ".local/share", USER_LEAF);
    xdg_path(c->cache_dir, "XDG_CACHE_HOME", ".cache", USER_LEAF);
    snprintf(c->run_dir, NATIVE_PATH_MAX, "/tmp/fmrb-native-%u", (unsigned)getuid());

    const char *machine_arg = NULL;
    const char *scale_arg = NULL;
    const char *sets[MAX_SETS];
    int n_sets = 0;

    const char *v;
    if ((v = getenv("FMRB_NATIVE_MACHINE")) != NULL && v[0] != '\0') machine_arg = v;
    if ((v = getenv("FMRB_NATIVE_MACHINES_DIR")) != NULL) set_path(c->machines_dir, v);
    if ((v = getenv("FMRB_NATIVE_SCALE")) != NULL) scale_arg = v;
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
            return 2;
        } else if (strcmp(a, "--machine") == 0) {
            machine_arg = next;
        } else if (strcmp(a, "--machines-dir") == 0) {
            set_path(c->machines_dir, next);
        } else if (strcmp(a, "--set") == 0) {
            if (n_sets >= MAX_SETS) {
                fprintf(stderr, "native: more than %d --set\n", MAX_SETS);
                return 2;
            }
            sets[n_sets++] = next;
        } else if (strcmp(a, "--scale") == 0) {
            scale_arg = next;
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
            return 2;
        }
        i++;
    }
#if FMRB_NATIVE_PACKAGE
    /* An installed program without its tree is a broken installation; it
     * must not go looking for a repository's flash/. Pointing all three
     * places elsewhere by hand (a tree unpacked somewhere) is still allowed. */
    if (!installed && getenv("FMRB_NATIVE_SYSTEM_DIR") == NULL && !has_option(argc, argv, "--system-dir")) {
        fprintf(stderr, "native: the distribution tree is not at %s, where this installed program "
                        "expects it (reinstall the package, or give --system-dir)\n", root);
        return 2;
    }
#endif
    make_absolute(c->machines_dir);
    make_absolute(c->user_dir);
    make_absolute(c->cache_dir);
    make_absolute(c->system_dir);
    make_absolute(c->etc_seed);
    make_absolute(c->run_dir);
    return load_machine(machine_arg, sets, n_sets, scale_arg);
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
    /* Waiting network calls go to helper threads (native/net/net_wrap.c). */
    const char *gate = getenv("FMRB_NATIVE_GATE");
    if (gate != NULL && strcmp(gate, "off") == 0) {
        fprintf(stderr, "native: FMRB_NATIVE_GATE=off: waiting calls stop every task\n");
    } else {
        fmrb_gate_init();
        native_net_init();
    }
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

static void on_quit_signal(int sig)
{
    (void)sig;
    s_quit_signal = 1;
}

/* The machine asking to end (the keys desktop's Esc held on the desktop):
 * the same way out as the control socket's quit. */
void fmrb_native_request_quit(void)
{
    fprintf(stderr, "native: quit asked for by the machine\n");
    s_quit_signal = 1;
}

/* SDL turns SIGINT/SIGTERM into SDL_QUIT itself; the other outputs have no
 * event queue, so the loop looks at a flag. The handler only sets it. */
static void install_quit_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_quit_signal;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* The machine's frames onto the output until something says quit. */
static int main_loop(const native_output_t *out)
{
    uint16_t *frame = (uint16_t *)malloc((size_t)FRAME_CAP_PX * sizeof(uint16_t));
    if (frame == NULL) {
        fprintf(stderr, "native: no memory for the frame copy\n");
        return 1;
    }
    uint32_t shown = 0;
    int w = 0, h = 0;
    bool quit = false;
    while (!quit) {
        quit |= out->pump(LOOP_WAIT_MS);
        bool changed = false;
        if (native_frame_seq() != shown) {
            int nw = 0, nh = 0;
            uint32_t got = 0;
            if (native_frame_copy(frame, FRAME_CAP_PX, &nw, &nh, &got)) {
                w = nw;
                h = nh;
                shown = got;
                changed = true;
            }
        }
        if (w > 0 && h > 0) out->present(frame, w, h, changed);
        if (native_ctl_poll()) quit = true;
        if (s_quit_signal) quit = true;
    }
    free(frame);
    return 0;
}

static void etc_log(void *ctx, const char *line)
{
    (void)ctx;
    fprintf(stderr, "native: /etc: %s\n", line);
}

/* A user's /etc from an earlier release gets what this one added
 * (native/etc/etc_update.h). Before the machine starts, so nothing reads the
 * files while they change; on the first start there is no user etc yet and
 * the file HAL copies the seed whole. */
static void update_user_etc(const char *seed)
{
    const native_config_t *c = &g_native_config;
    char seed_dir[NATIVE_PATH_MAX + 8];
    char user_etc[NATIVE_PATH_MAX + 8];
    if (seed != NULL) {
        snprintf(seed_dir, sizeof(seed_dir), "%s", seed);
    } else {
        snprintf(seed_dir, sizeof(seed_dir), "%s/etc", c->system_dir);
    }
    snprintf(user_etc, sizeof(user_etc), "%s/etc", c->user_dir);
    int n = native_etc_update(seed_dir, user_etc, etc_log, NULL);
    if (n > 0) {
        fprintf(stderr, "native: /etc: %d addition(s) from %s\n", n, seed_dir);
    } else if (n < 0) {
        fprintf(stderr, "native: /etc: the update from %s was not complete (see above)\n", seed_dir);
    }
}

/* files.host / files.root to the host directory the file HAL shows, or ""
 * to leave the place out. "auto" is the user's home ($HOME, else the
 * password entry's) for /mnt/host and "/" for /mnt/root. A directory that
 * is not there leaves the place out, with a line on stderr. */
static void files_place(const char *value, bool home, char *out, size_t out_len)
{
    out[0] = '\0';
    if (strcmp(value, "none") == 0) return;
    const char *dir = value;
    if (strcmp(value, "auto") == 0) {
        dir = "/";
        if (home) {
            dir = getenv("HOME");
            if (dir == NULL || dir[0] != '/') {
                struct passwd *pw = getpwuid(getuid());
                dir = (pw != NULL) ? pw->pw_dir : NULL;
            }
        }
    }
    struct stat st;
    if (dir == NULL || dir[0] != '/' || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "native: /mnt/%s: %s is not a directory here; not shown\n",
                home ? "host" : "root", dir ? dir : "(no home directory)");
        return;
    }
    snprintf(out, out_len, "%s", dir);
}

static int refuse(const char *what, const char *why)
{
    fprintf(stderr, "native: refused: machine %s: %s: %s\n", g_native_config.machine.name, what, why);
    return 3;
}

int main(int argc, char **argv)
{
    int prc = parse_options(argc, argv);
    if (prc != 0) return prc;
    const native_config_t *c = &g_native_config;
    const native_machine_t *m = &c->machine;

    char line[600];
    native_machine_describe(m, line, sizeof(line));
    fprintf(stderr, "native: %s\n", line);

    /* Everything a machine can be refused for, before anything starts. */
    char why[512];
    if (native_machine_check(m, why, sizeof(why)) != 0) return refuse("definition", why);
    native_backends_t be;
    if (!native_backends_select(m, &be, why, sizeof(why))) return refuse("backend", why);
    /* The screen and the desktop the kernel will use (fmrb_kernel.h). */
    fmrb_kernel_set_native_machine((uint16_t)m->screen.w, (uint16_t)m->screen.h,
                                   strcmp(m->desktop.variant, "keys") == 0,
                                   strcmp(m->system.exit_key, "esc_long") == 0);

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

    /* Linux's own files (L7): the user's home at /mnt/host, "/" at
     * /mnt/root (read-only), as the machine definition's [files] says. */
    char host_home[NATIVE_PATH_MAX];
    char host_root[NATIVE_PATH_MAX];
    files_place(m->files.host, true, host_home, sizeof(host_home));
    files_place(m->files.root, false, host_root, sizeof(host_root));

    fmrb_hal_file_layout_t layout = {
        .system_root = c->system_dir,
        .user_root   = c->user_dir,
        .cache_root  = c->cache_dir,
        .etc_seed    = c->etc_seed,
        .host_home   = host_home,
        .host_root   = host_root,
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
    update_user_etc(layout.etc_seed);

    char sock[NATIVE_PATH_MAX + 16];
    snprintf(sock, sizeof(sock), "%s/inject", c->run_dir);
    native_input_set_inject_path(sock);

    fprintf(stderr, "native: system=%s user=%s cache=%s run=%s\n",
            c->system_dir, c->user_dir, c->cache_dir, c->run_dir);

    /* The devices: screen, sound, input. Each one either is had now or the
     * machine does not start. */
    if (!be.output->open(m, why, sizeof(why))) return refuse("display", why);
    if (!be.audio->open(m, why, sizeof(why))) {
        be.output->close();
        return refuse("audio", why);
    }
    if (!native_backends_open_input(m, why, sizeof(why))) {
        be.audio->close();
        be.output->close();
        return refuse("input", why);
    }
    if (be.output != &native_output_sdl) install_quit_signals();

    /* The OS services (L6): the Linux table when the machine says so, with
     * the ALSA mixer behind the volume when the sound goes out through ALSA.
     * No mixer (the SDL output, WSL2) leaves the internal volume in charge. */
    if (strcmp(m->system.services, "linux") == 0) {
        if (strcmp(m->audio.backend, "alsa") == 0) {
            if (sys_linux_mixer_open(m->audio.device, m->audio.mixer, why, sizeof(why))) {
                fprintf(stderr, "native: volume: the mixer %s\n", sys_linux_mixer_name());
            } else {
                fprintf(stderr, "native: volume: the internal volume (%s)\n", why);
            }
        }
        sys_linux_install();
    }

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

    int rc = main_loop(be.output);

    native_ctl_close();
    be.audio->close();
    be.output->close();
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
