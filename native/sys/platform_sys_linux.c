/*
 * platform_sys_linux.c - the OS services of the native program on Linux
 * (fmrb_platform_sys.h, doc/linux_native L6), installed by main_native.c
 * when the machine definition says system.services = "linux".
 *
 *   network  read only: the default route's interface from procfs, its
 *            state from sysfs, the signal from /proc/net/wireless, the SSID
 *            from the wireless extensions' SIOCGIWESSID (a read), the
 *            address from getifaddrs. Nothing here changes a network
 *            setting -- connecting is the OS's business (the user's
 *            decision, 2026-10-26).
 *   battery  sysfs power_supply (sys_linux_read.c).
 *   volume   the ALSA mixer (sys_linux_mixer.c) when the sound goes out
 *            through ALSA and a mixer element opens; else none, and the
 *            internal volume stays in charge.
 *   power    system.power lists "exit" (ending the program: the Cardputer
 *            back to APPLaunch, a PC session back to the login screen),
 *            "logind" (power off, restart and suspend), or both. logind is
 *            reached on the system bus, from a helper thread of the
 *            blocking-call gate. system.power_log_only = true logs the
 *            request instead of sending it (every test uses it).
 *   programs Linux programs under a pseudo terminal (linux_pty.c, L10),
 *            for the Shell's "!" lines. The Family mruby working directory
 *            becomes a real one through the file namespace; each program
 *            belongs to the task that started it and ends with it.
 */

#include <errno.h>
#include <dlfcn.h>
#include <limits.h>
#include <signal.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/wireless.h>

#include "FreeRTOS.h"
#include "task.h"

#include "fmrb_platform_sys.h"
#include "fmrb_kernel.h"
#include "fmrb_log.h"
#include "fmrb_gate.h"
#include "native_config.h"
#include "sys_linux_read.h"
#include "sys_linux.h"
#include "linux_pty.h"
#include "fmrb_hal_file.h"

static const char TAG[] = "sys_linux";

/* Where the tests' trees go instead (FMRB_NATIVE_SYSFS / FMRB_NATIVE_PROCFS
 * in the environment, read once at install). */
static char s_sysfs[256] = "/sys";
static char s_procfs[256] = "/proc";

/* ---- network ---- */

static bool iface_ipv4(const char *iface, char *ip, size_t cap)
{
    struct ifaddrs *ifaddr = NULL;
    bool found = false;
    if (getifaddrs(&ifaddr) != 0) return false;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, iface) != 0) continue;
        inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, ip, (socklen_t)cap);
        found = true;
        break;
    }
    freeifaddrs(ifaddr);
    return found;
}

/* The SSID the interface is associated with: a read through the wireless
 * extensions (cfg80211 answers it for every driver it handles). "" when
 * the driver does not say. */
static void iface_ssid(const char *iface, char *out, size_t cap)
{
    out[0] = '\0';
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct iwreq req;
    char essid[IW_ESSID_MAX_SIZE + 1];
    memset(&req, 0, sizeof(req));
    memset(essid, 0, sizeof(essid));
    snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", iface);
    req.u.essid.pointer = essid;
    req.u.essid.length = IW_ESSID_MAX_SIZE;
    if (ioctl(fd, SIOCGIWESSID, &req) == 0) {
        size_t len = req.u.essid.length;
        if (len > IW_ESSID_MAX_SIZE) len = IW_ESSID_MAX_SIZE;
        essid[len] = '\0';
        snprintf(out, cap, "%s", essid);
    }
    close(fd);
}

static bool linux_net_info(fmrb_sys_net_t *out)
{
    memset(out, 0, sizeof(*out));
    out->signal = -1;
    sys_linux_link_t link;
    if (!sys_linux_link_read(s_sysfs, s_procfs, &link)) return false;
    out->connected = link.connected;
    out->signal = (int8_t)link.signal;
    if (link.connected) {
        if (!iface_ipv4(link.iface, out->ip, sizeof(out->ip))) out->ip[0] = '\0';
    }
    if (link.wireless) iface_ssid(link.iface, out->ssid, sizeof(out->ssid));
    if (gethostname(out->hostname, sizeof(out->hostname)) != 0) out->hostname[0] = '\0';
    out->hostname[sizeof(out->hostname) - 1] = '\0';
    return true;
}

static bool linux_net_connected(void)
{
    sys_linux_link_t link;
    return sys_linux_link_read(s_sysfs, s_procfs, &link) && link.connected;
}

/* ---- battery ---- */

static bool linux_battery(fmrb_sys_battery_t *out)
{
    sys_linux_battery_t b;
    if (!sys_linux_battery_read(s_sysfs, &b)) return false;
    out->percent = (uint8_t)b.percent;
    out->charging = b.charging;
    return true;
}

/* ---- volume ---- */

static bool linux_volume_get(int *step, bool *muted, int max_step)
{
    return sys_linux_mixer_get(step, muted, max_step);
}

static bool linux_volume_set(int step, int max_step)
{
    return sys_linux_mixer_set(step, max_step);
}

static bool linux_mute_set(bool on)
{
    return sys_linux_mixer_mute(on);
}

/* ---- power ---- */

static bool power_logind(void)
{
    return native_machine_power_has(&g_native_config.machine, "logind");
}

static uint32_t linux_power_actions(void)
{
    uint32_t bits = 0;
    if (native_machine_power_has(&g_native_config.machine, "exit")) {
        bits |= FMRB_SYS_POWER_EXIT;
    }
    if (power_logind()) {
        bits |= FMRB_SYS_POWER_OFF | FMRB_SYS_POWER_RESTART | FMRB_SYS_POWER_SUSPEND;
    }
    return bits;
}

/* sd-bus, opened at run time: libsystemd0 is on every systemd machine (the
 * package depends on it), and nothing of it is needed to build. */
typedef struct {
    const char *name;
    const char *message;
    int need_free;
} sdbus_error_t;

typedef struct {
    int (*open_system)(void **bus);
    int (*call_method)(void *bus, const char *destination, const char *path,
                       const char *interface, const char *member, sdbus_error_t *ret_error,
                       void **reply, const char *types, ...);
    void (*error_free)(sdbus_error_t *e);
    void *(*message_unref)(void *m);
    void *(*flush_close_unref)(void *bus);
} sdbus_t;

typedef struct {
    const char *method;
    int rc;
    char why[160];
} logind_job_t;

static bool sdbus_load(sdbus_t *s, char *why, size_t cap)
{
    void *lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        snprintf(why, cap, "libsystemd.so.0: %s", dlerror());
        return false;
    }
    *(void **)&s->open_system = dlsym(lib, "sd_bus_open_system");
    *(void **)&s->call_method = dlsym(lib, "sd_bus_call_method");
    *(void **)&s->error_free = dlsym(lib, "sd_bus_error_free");
    *(void **)&s->message_unref = dlsym(lib, "sd_bus_message_unref");
    *(void **)&s->flush_close_unref = dlsym(lib, "sd_bus_flush_close_unref");
    if (!s->open_system || !s->call_method || !s->error_free || !s->message_unref ||
        !s->flush_close_unref) {
        snprintf(why, cap, "libsystemd.so.0 lacks the sd-bus calls");
        return false;
    }
    return true;   /* the library stays loaded: the program may ask again */
}

/* On a helper thread of the gate (no FreeRTOS, no fmrb calls). */
static void logind_run(void *arg)
{
    logind_job_t *job = (logind_job_t *)arg;
    sdbus_t s;
    if (!sdbus_load(&s, job->why, sizeof(job->why))) {
        job->rc = -1;
        return;
    }
    void *bus = NULL;
    int rc = s.open_system(&bus);
    if (rc < 0) {
        snprintf(job->why, sizeof(job->why), "system bus: %s", strerror(-rc));
        job->rc = rc;
        return;
    }
    sdbus_error_t err = { NULL, NULL, 0 };
    void *reply = NULL;
    /* interactive = false: no password prompt can be answered from here. */
    rc = s.call_method(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                       "org.freedesktop.login1.Manager", job->method, &err, &reply, "b", 0);
    if (rc < 0) {
        snprintf(job->why, sizeof(job->why), "%s: %s", err.name ? err.name : "error",
                 err.message ? err.message : strerror(-rc));
    }
    job->rc = rc;
    s.error_free(&err);
    if (reply) s.message_unref(reply);
    s.flush_close_unref(bus);
}

static fmrb_err_t logind_request(fmrb_sys_power_t action)
{
    const char *method = sys_linux_logind_method(action);
    if (method == NULL) return FMRB_ERR_NOT_SUPPORTED;
    if (g_native_config.machine.system.power_log_only) {
        FMRB_LOGI(TAG, "power: logind org.freedesktop.login1.Manager.%s(false) "
                       "(log only: system.power_log_only, not sent)", method);
        return FMRB_OK;
    }
    FMRB_LOGI(TAG, "power: asking logind for %s", method);
    logind_job_t job;
    memset(&job, 0, sizeof(job));
    job.method = method;
    fmrb_gate_job_t gj;
    memset(&gj, 0, sizeof(gj));
    gj.run = logind_run;
    gj.arg = &job;
    if (fmrb_gate_call(&gj) == FMRB_GATE_UNAVAILABLE) logind_run(&job);
    if (job.rc < 0) {
        FMRB_LOGW(TAG, "power: logind %s refused: %s", method, job.why);
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

static fmrb_err_t linux_power(fmrb_sys_power_t action)
{
    switch (action) {
    case FMRB_SYS_POWER_RESET:
        /* FmrbApp.reboot: Family mruby starts again from its launcher, so
         * ending the program is the nearest thing (never the computer's
         * restart: that is FMRB_SYS_POWER_RESTART, asked for by name). */
    case FMRB_SYS_POWER_EXIT:
        FMRB_LOGI(TAG, "power: ending the program");
        fmrb_native_request_quit();
        return FMRB_OK;
    default:
        if (!power_logind()) {
            FMRB_LOGW(TAG, "power: action 0x%x needs \"logind\" in system.power", (unsigned)action);
            return FMRB_ERR_NOT_SUPPORTED;
        }
        return logind_request(action);
    }
}

/* ---- Linux programs (L10) ---- */

static fmrb_err_t linux_proc_start(const fmrb_sys_proc_spec_t *spec, int32_t *id,
                                   char *why, size_t why_cap)
{
    if (spec == NULL || spec->cwd == NULL || id == NULL) return FMRB_ERR_INVALID_PARAM;
    char host[PATH_MAX];
    char real[PATH_MAX];
    if (fmrb_hal_file_resolve_path(spec->cwd, host, sizeof(host)) != FMRB_OK ||
        realpath(host, real) == NULL) {
        if (why != NULL && why_cap > 0) {
            snprintf(why, why_cap, "%s is not a place on Linux", spec->cwd);
        }
        return FMRB_ERR_NOT_FOUND;
    }
    linux_pty_spec_t ps = {
        .command = spec->command,
        .cwd = real,
        .cols = spec->cols,
        .rows = spec->rows,
        .owner = (void *)xTaskGetCurrentTaskHandle(),
    };
    fmrb_err_t err = linux_pty_start(&ps, id, why, why_cap);
    if (err == FMRB_OK) {
        FMRB_LOGI(TAG, "program %ld in %s: %s", (long)*id, real,
                  spec->command ? spec->command : "(interactive shell)");
    }
    return err;
}

static fmrb_err_t linux_proc_signal(int32_t id, fmrb_sys_signal_t sig)
{
    int s;
    switch (sig) {
    case FMRB_SYS_SIG_INT:  s = SIGINT; break;
    case FMRB_SYS_SIG_TERM: s = SIGTERM; break;
    case FMRB_SYS_SIG_KILL: s = SIGKILL; break;
    case FMRB_SYS_SIG_HUP:  s = SIGHUP; break;
    default: return FMRB_ERR_INVALID_PARAM;
    }
    return linux_pty_signal(id, s);
}

/* A task deleted (an app ended or killed): its programs go with it. */
static void proc_task_deleted(void *task)
{
    int n = linux_pty_close_owned(task);
    if (n > 0) {
        FMRB_LOGI(TAG, "ended %d program(s) of a deleted task", n);
    }
}

static const fmrb_platform_sys_t s_linux_sys = {
    .name = "linux",
    .net_info = linux_net_info,
    .net_connected = linux_net_connected,
    .battery = linux_battery,
    .volume_get = linux_volume_get,
    .volume_set = linux_volume_set,
    .mute_set = linux_mute_set,
    .power_actions = linux_power_actions,
    .power = linux_power,
    .proc_start = linux_proc_start,
    .proc_read = linux_pty_read,
    .proc_write = linux_pty_write,
    .proc_signal = linux_proc_signal,
    .proc_exited = linux_pty_exited,
    .proc_resize = linux_pty_resize,
    .proc_close = linux_pty_close,
};

void sys_linux_install(void)
{
    const char *sysfs = getenv("FMRB_NATIVE_SYSFS");
    const char *procfs = getenv("FMRB_NATIVE_PROCFS");
    if (sysfs && sysfs[0]) snprintf(s_sysfs, sizeof(s_sysfs), "%s", sysfs);
    if (procfs && procfs[0]) snprintf(s_procfs, sizeof(s_procfs), "%s", procfs);
    fmrb_platform_sys_install(&s_linux_sys);
    fmrb_gate_add_task_deleted_listener(proc_task_deleted);
    const native_machine_t *m = &g_native_config.machine;
    FMRB_LOGI(TAG, "power %s%s, mixer %s, sysfs %s, procfs %s", m->system.power,
              m->system.power_log_only ? " (log only)" : "",
              sys_linux_mixer_name(), s_sysfs, s_procfs);
}
