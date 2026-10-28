/*
 * sys_linux_pw.c - the volume through the desktop's sound server
 * (doc/linux_native, L14): PipeWire's wpctl, else PulseAudio's pactl (also
 * PipeWire's pipewire-pulse), on the default output. See sys_linux.h.
 *
 * Neither has a D-Bus interface for this, and their libraries are a large
 * dependency for two numbers: the tools are run instead, on a thread of
 * their own, never on a FreeRTOS task. The value the desktop reads is the
 * last one known (read again when asked for and older than 2 s); a change
 * is taken at once into it and handed to the thread, which runs the tool.
 */

#define _GNU_SOURCE
#include "sys_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define STALE_MS 2000
#define TOOL_MS 1500

typedef enum { PW_NONE = 0, PW_WPCTL, PW_PACTL } pw_tool_t;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pw_tool_t tool;
    double volume;          /* 0.0 - 1.0 */
    bool muted;
    uint64_t read_ms;
    bool want_read;
    bool want_volume;
    double set_volume;
    bool want_mute;
    bool set_mute;
    bool started;
} s_pw = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* ---- the tools' output ---- */

bool sys_linux_pw_parse_wpctl(const char *out, double *volume, bool *muted)
{
    const char *p = strstr(out, "Volume:");
    if (p == NULL) return false;
    char *end = NULL;
    double v = strtod(p + 7, &end);
    if (end == p + 7) return false;
    *volume = v;
    *muted = strstr(end, "[MUTED]") != NULL;
    return true;
}

bool sys_linux_pw_parse_pactl_volume(const char *out, int *percent)
{
    /* "Volume: front-left: 26214 /  40% / -23.88 dB, front-right: ..." :
     * the first percentage */
    const char *p = strstr(out, "Volume:");
    if (p == NULL) return false;
    const char *pc = strchr(p, '%');
    if (pc == NULL) return false;
    const char *s = pc;
    while (s > p && s[-1] >= '0' && s[-1] <= '9') s--;
    if (s == pc) return false;
    *percent = atoi(s);
    return true;
}

bool sys_linux_pw_parse_pactl_mute(const char *out, bool *muted)
{
    const char *p = strstr(out, "Mute:");
    if (p == NULL) return false;
    p += 5;
    while (*p == ' ') p++;
    if (strncmp(p, "yes", 3) == 0) *muted = true;
    else if (strncmp(p, "no", 2) == 0) *muted = false;
    else return false;
    return true;
}

int sys_linux_pw_step(double volume, int max_step)
{
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    int s = (int)(volume * max_step + 0.5);
    return s > max_step ? max_step : s;
}

/* ---- running a tool ---- */

/* Runs argv with its output into out (cut to cap), at most TOOL_MS. The
 * exit status, or -1 when it did not run or end in time. */
static int run_tool(char *const argv[], char *out, size_t cap)
{
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&at, &none);
    sigset_t all;
    sigfillset(&all);
    posix_spawnattr_setsigdefault(&at, &all);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(p[1]);
    if (rc != 0) {
        close(p[0]);
        return -1;
    }
    size_t n = 0;
    uint64_t end = ms_now() + TOOL_MS;
    for (;;) {
        uint64_t now = ms_now();
        if (now >= end) break;
        struct pollfd pf = { .fd = p[0], .events = POLLIN };
        if (poll(&pf, 1, (int)(end - now)) <= 0) break;
        char tmp[256];
        ssize_t got = read(p[0], tmp, sizeof(tmp));
        if (got <= 0) break;
        for (ssize_t i = 0; i < got && n + 1 < cap; i++) out[n++] = tmp[i];
    }
    out[cap > 0 ? n : 0] = '\0';
    close(p[0]);
    int status = 0;
    for (int i = 0; i < 50; i++) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (w < 0) return -1;
        struct timespec ts = { 0, 20 * 1000000 };
        nanosleep(&ts, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -1;
}

static bool read_now(pw_tool_t tool, double *volume, bool *muted)
{
    char out[512];
    if (tool == PW_WPCTL) {
        char *const argv[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };
        return run_tool(argv, out, sizeof(out)) == 0 && sys_linux_pw_parse_wpctl(out, volume, muted);
    }
    if (tool == PW_PACTL) {
        char *const av[] = { "pactl", "get-sink-volume", "@DEFAULT_SINK@", NULL };
        int pc = 0;
        if (run_tool(av, out, sizeof(out)) != 0 || !sys_linux_pw_parse_pactl_volume(out, &pc)) return false;
        char *const am[] = { "pactl", "get-sink-mute", "@DEFAULT_SINK@", NULL };
        bool mu = false;
        if (run_tool(am, out, sizeof(out)) != 0 || !sys_linux_pw_parse_pactl_mute(out, &mu)) return false;
        *volume = pc / 100.0;
        *muted = mu;
        return true;
    }
    return false;
}

static void set_now(pw_tool_t tool, bool volume, double v, bool mute, bool m)
{
    char out[128], arg[32];
    if (tool == PW_WPCTL) {
        if (volume) {
            snprintf(arg, sizeof(arg), "%.2f", v);
            char *const argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", arg, NULL };
            run_tool(argv, out, sizeof(out));
        }
        if (mute) {
            char *const argv[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", m ? "1" : "0", NULL };
            run_tool(argv, out, sizeof(out));
        }
    } else if (tool == PW_PACTL) {
        if (volume) {
            snprintf(arg, sizeof(arg), "%d%%", (int)(v * 100 + 0.5));
            char *const argv[] = { "pactl", "set-sink-volume", "@DEFAULT_SINK@", arg, NULL };
            run_tool(argv, out, sizeof(out));
        }
        if (mute) {
            char *const argv[] = { "pactl", "set-sink-mute", "@DEFAULT_SINK@", m ? "1" : "0", NULL };
            run_tool(argv, out, sizeof(out));
        }
    }
}

static void *worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&s_pw.lock);
    for (;;) {
        while (!s_pw.want_read && !s_pw.want_volume && !s_pw.want_mute) pthread_cond_wait(&s_pw.cond, &s_pw.lock);
        bool wv = s_pw.want_volume, wm = s_pw.want_mute;
        double v = s_pw.set_volume;
        bool m = s_pw.set_mute;
        s_pw.want_volume = s_pw.want_mute = false;
        s_pw.want_read = false;
        pw_tool_t tool = s_pw.tool;
        pthread_mutex_unlock(&s_pw.lock);
        if (wv || wm) set_now(tool, wv, v, wm, m);
        double rv = 0;
        bool rm = false;
        bool ok = read_now(tool, &rv, &rm);
        pthread_mutex_lock(&s_pw.lock);
        /* A change asked for meanwhile wins over what was read. */
        if (ok && !s_pw.want_volume && !s_pw.want_mute) {
            s_pw.volume = rv;
            s_pw.muted = rm;
        }
        s_pw.read_ms = ms_now();
    }
    return NULL;
}

bool sys_linux_pw_open(char *why, size_t cap)
{
    double v = 0;
    bool m = false;
    pw_tool_t tool = PW_NONE;
    if (read_now(PW_WPCTL, &v, &m)) tool = PW_WPCTL;
    else if (read_now(PW_PACTL, &v, &m)) tool = PW_PACTL;
    if (tool == PW_NONE) {
        snprintf(why, cap, "no PipeWire (wpctl) or PulseAudio (pactl) answered");
        return false;
    }
    pthread_mutex_lock(&s_pw.lock);
    s_pw.tool = tool;
    s_pw.volume = v;
    s_pw.muted = m;
    s_pw.read_ms = ms_now();
    pthread_mutex_unlock(&s_pw.lock);
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        snprintf(why, cap, "no thread");
        s_pw.tool = PW_NONE;
        return false;
    }
    pthread_detach(t);
    s_pw.started = true;
    return true;
}

const char *sys_linux_pw_name(void)
{
    switch (s_pw.tool) {
    case PW_WPCTL: return "PipeWire (wpctl) on the default output";
    case PW_PACTL: return "PulseAudio (pactl) on the default output";
    default: return "none";
    }
}

bool sys_linux_pw_active(void)
{
    return s_pw.started && s_pw.tool != PW_NONE;
}

bool sys_linux_pw_get(int *step, bool *muted, int max_step)
{
    if (!sys_linux_pw_active()) return false;
    pthread_mutex_lock(&s_pw.lock);
    if (step) *step = sys_linux_pw_step(s_pw.volume, max_step);
    if (muted) *muted = s_pw.muted;
    if (ms_now() - s_pw.read_ms >= STALE_MS && !s_pw.want_read) {
        s_pw.want_read = true;
        pthread_cond_signal(&s_pw.cond);
    }
    pthread_mutex_unlock(&s_pw.lock);
    return true;
}

bool sys_linux_pw_set(int step, int max_step)
{
    if (!sys_linux_pw_active() || max_step <= 0) return false;
    pthread_mutex_lock(&s_pw.lock);
    s_pw.volume = (double)step / max_step;
    s_pw.set_volume = s_pw.volume;
    s_pw.want_volume = true;
    pthread_cond_signal(&s_pw.cond);
    pthread_mutex_unlock(&s_pw.lock);
    return true;
}

bool sys_linux_pw_mute(bool on)
{
    if (!sys_linux_pw_active()) return false;
    pthread_mutex_lock(&s_pw.lock);
    s_pw.muted = on;
    s_pw.set_mute = on;
    s_pw.want_mute = true;
    pthread_cond_signal(&s_pw.cond);
    pthread_mutex_unlock(&s_pw.lock);
    return true;
}
