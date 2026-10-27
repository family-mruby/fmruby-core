/*
 * fmrb_platform_sys.c - the default OS services table (fmrb_platform_sys.h).
 *
 * What every target answered before the table existed, moved here from the
 * FmrbApp bindings unchanged:
 *   - network: the device's WiFi STA (FMRB_HAS_WIFI); on the Linux builds
 *     (the simulation, the browser build, a native machine without
 *     system.services = "linux") the first non-loopback IPv4 of the host;
 *     elsewhere none;
 *   - battery: none;
 *   - volume: none (the internal volume, doc/reference/audio_output.md);
 *   - power: the device's reset (esp_restart); on the Linux builds the
 *     process ends, which is what FmrbApp.reboot did there.
 */

#include <string.h>
#include <stdlib.h>

#include "fmrb_platform_sys.h"
#include "fmrb_log.h"

#if defined(FMRB_HAS_WIFI)
#include "wifi_task.h"
#endif

#if defined(CONFIG_IDF_TARGET_LINUX)
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#else
#include "esp_system.h"
#endif

static const char TAG[] = "platform_sys";

#if defined(FMRB_HAS_WIFI)
static bool default_net_info(fmrb_sys_net_t *out)
{
    memset(out, 0, sizeof(*out));
    wifi_get_ip_str(out->ip, sizeof(out->ip));
    wifi_get_ssid(out->ssid, sizeof(out->ssid));
    wifi_get_hostname(out->hostname, sizeof(out->hostname));
    out->connected = wifi_is_connected();
    out->signal = -1;
    return true;
}

static bool default_net_connected(void)
{
    return wifi_is_connected();
}
#elif defined(CONFIG_IDF_TARGET_LINUX)
// The host's network: the first IPv4 address that is not loopback. No SSID
// and no mDNS name here; the dialog shows "-" and hides the name row.
static bool host_first_ipv4(char *ip, size_t ip_size)
{
    bool found = false;
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) return false;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        if ((ntohl(sin->sin_addr.s_addr) >> 24) == 127) continue;  // loopback
        if (ip) inet_ntop(AF_INET, &sin->sin_addr, ip, (socklen_t)ip_size);
        found = true;
        break;
    }
    freeifaddrs(ifaddr);
    return found;
}

static bool default_net_info(fmrb_sys_net_t *out)
{
    memset(out, 0, sizeof(*out));
    strcpy(out->ip, "127.0.0.1");
    out->connected = host_first_ipv4(out->ip, sizeof(out->ip));
    out->signal = -1;
    return true;
}

static bool default_net_connected(void)
{
    return host_first_ipv4(NULL, 0);
}
#endif

// What the desktop's menu offers: the device's reset. The Linux builds offer
// nothing (FmrbApp.reboot still ends the process there, see default_power).
static uint32_t default_power_actions(void)
{
#if defined(CONFIG_IDF_TARGET_LINUX)
    return 0;
#else
    return FMRB_SYS_POWER_RESET;
#endif
}

static fmrb_err_t default_power(fmrb_sys_power_t action)
{
    if (action != FMRB_SYS_POWER_RESET) return FMRB_ERR_NOT_SUPPORTED;
#if defined(CONFIG_IDF_TARGET_LINUX)
    // The simulation's launcher script brings the process back.
    exit(0);
#else
    esp_restart();
#endif
    return FMRB_OK;  // unreachable
}

static const fmrb_platform_sys_t s_default_sys = {
    .name = "default",
#if defined(FMRB_HAS_WIFI) || defined(CONFIG_IDF_TARGET_LINUX)
    .net_info = default_net_info,
    .net_connected = default_net_connected,
#endif
    .power_actions = default_power_actions,
    .power = default_power,
};

#if defined(FMRB_PLATFORM_NATIVE)
// Only the native program installs a table of its own (from its machine
// definition); everywhere else the default is the table, and no variable is
// spent on the choice (the devices' internal RAM).
static const fmrb_platform_sys_t *s_installed;

const fmrb_platform_sys_t *fmrb_platform_sys(void)
{
    return s_installed ? s_installed : &s_default_sys;
}

void fmrb_platform_sys_install(const fmrb_platform_sys_t *sys)
{
    s_installed = sys;
    FMRB_LOGI(TAG, "OS services: %s", fmrb_platform_sys()->name);
}
#else
const fmrb_platform_sys_t *fmrb_platform_sys(void)
{
    return &s_default_sys;
}

void fmrb_platform_sys_install(const fmrb_platform_sys_t *sys)
{
    (void)sys;
    FMRB_LOGW(TAG, "OS services: only the default table on this target");
}
#endif


bool fmrb_platform_sys_net_info(fmrb_sys_net_t *out)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->net_info ? s->net_info(out) : false;
}

bool fmrb_platform_sys_net_connected(void)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->net_connected ? s->net_connected() : false;
}

bool fmrb_platform_sys_battery(fmrb_sys_battery_t *out)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->battery ? s->battery(out) : false;
}

bool fmrb_platform_sys_volume_get(int *step, bool *muted, int max_step)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->volume_get ? s->volume_get(step, muted, max_step) : false;
}

bool fmrb_platform_sys_volume_set(int step, int max_step)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->volume_set ? s->volume_set(step, max_step) : false;
}

bool fmrb_platform_sys_mute_set(bool on)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->mute_set ? s->mute_set(on) : false;
}

uint32_t fmrb_platform_sys_power_actions(void)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->power_actions ? s->power_actions() : 0;
}

fmrb_err_t fmrb_platform_sys_power(fmrb_sys_power_t action)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    if (!s->power) return FMRB_ERR_NOT_SUPPORTED;
    return s->power(action);
}

bool fmrb_platform_sys_proc_available(void)
{
    return fmrb_platform_sys()->proc_start != NULL;
}

fmrb_err_t fmrb_platform_sys_proc_start(const fmrb_sys_proc_spec_t *spec, int32_t *id,
                                        char *why, size_t why_cap)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    if (!s->proc_start) return FMRB_ERR_NOT_SUPPORTED;
    return s->proc_start(spec, id, why, why_cap);
}

int32_t fmrb_platform_sys_proc_read(int32_t id, void *buf, size_t cap)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->proc_read ? s->proc_read(id, buf, cap) : -1;
}

int32_t fmrb_platform_sys_proc_write(int32_t id, const void *buf, size_t len)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->proc_write ? s->proc_write(id, buf, len) : -1;
}

fmrb_err_t fmrb_platform_sys_proc_signal(int32_t id, fmrb_sys_signal_t sig)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->proc_signal ? s->proc_signal(id, sig) : FMRB_ERR_NOT_SUPPORTED;
}

bool fmrb_platform_sys_proc_exited(int32_t id, int *code)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->proc_exited ? s->proc_exited(id, code) : true;
}

fmrb_err_t fmrb_platform_sys_proc_resize(int32_t id, uint16_t cols, uint16_t rows)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->proc_resize ? s->proc_resize(id, cols, rows) : FMRB_ERR_NOT_SUPPORTED;
}

void fmrb_platform_sys_proc_close(int32_t id)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    if (s->proc_close) s->proc_close(id);
}

int32_t fmrb_platform_sys_apps_list(int mode, const char *lang, char *buf, size_t cap)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->apps_list ? s->apps_list(mode, lang, buf, cap) : -1;
}

fmrb_err_t fmrb_platform_sys_apps_launch(const char *id)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->apps_launch ? s->apps_launch(id) : FMRB_ERR_NOT_SUPPORTED;
}

int32_t fmrb_platform_sys_apps_handlers(const char *path, char *buf, size_t cap)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->apps_handlers ? s->apps_handlers(path, buf, cap) : -1;
}

fmrb_err_t fmrb_platform_sys_apps_open(const char *id, const char *path, char *name, size_t name_cap,
                                       char *why, size_t why_cap)
{
    const fmrb_platform_sys_t *s = fmrb_platform_sys();
    return s->apps_open ? s->apps_open(id, path, name, name_cap, why, why_cap) : FMRB_ERR_NOT_SUPPORTED;
}
