// WiFi STA bring-up and the saved networks (doc/wifi_settings).
//
// Modern (ESP32-P4 / Tab5): the radio is on the ESP32-C6 coprocessor;
// esp_wifi_* calls are RPC'd to the slave by esp_wifi_remote
// (CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED), while esp_netif + lwIP run
// locally on the P4, so sockets and the HTTP server behave as on native
// WiFi. Ordering constraint: esp_hosted 1.4.0 corrupts the heap if an RPC
// is issued before the SDIO transport is up (doc/reference/ble_c6_web_console.md).
// ble_task_init normally brings the transport up first; when BLE is
// disabled, the wait below polls the transport without issuing any RPC.
//
// Retro (Narya v3, ESP32-S3): native radio, same esp_wifi API without the
// transport dance. WiFi and BLE are mutually exclusive here -- coexistence
// is off in sdkconfig (it destabilizes NimBLE even when idle) and the two
// share the internal-RAM budget -- so init refuses to start while BLE is up
// (and ble_service_start refuses while WiFi is up).
//
// Settings come from /home/wifi.toml, the board's own (written by the Wi-Fi
// page, kept by an app-only flash: doc/ruby_asterism/report/cf1.md), else
// from /etc/wifi.toml, the one a development build bakes in from
// config/wifi.toml (kept out of git; see config/wifi.toml.example). When
// /home/wifi.toml is there it is the only one read. Up to five networks
// (wifi_conf.h; the old one-network [wifi] form is read too).
//
// Which network: a manager task (wifi_mgr, low priority, PSRAM stack) scans
// at start and whenever the link drops, and joins the strongest saved
// network in reach, then the next strongest, then the hidden ones by name.
// When none works it waits and scans again, the wait doubling from 5 s to
// 2 min. The Wi-Fi page's requests (FmrbApp.wifi_networks / wifi_connect /
// wifi_disconnect / wifi_forget, through fmrb_platform_sys.h) are queued to
// the same task and never wait: a scan is asynchronous (SCAN_DONE), and the
// event handler only wakes the task. A network chosen on the page is joined
// at once and saved when it gets an address; when it fails, the task goes
// back to the saved ones, so a wrong choice cannot strand the board.
// Passwords live only in wifi.toml (which no remote service hands out) and,
// for one attempt, in a PSRAM buffer that is cleared afterwards.

#include "wifi_task.h"
#include "wifi_conf.h"

#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_toml.h"
#include "fmrb_hal_file.h"
#include "fmrb_hal_time.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_secret_path.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#ifdef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
#include "esp_hosted.h"
#else
#include "nvs_flash.h"
#include "ble_task.h"   /* ble_service_is_started: radio exclusivity */
#endif
#include "mdns.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
// esp_hosted 1.4.0 transport state probes (host/drivers/transport/
// transport_drv.h; declared here to avoid the deep include path)
extern uint8_t is_transport_rx_ready(void);
extern uint8_t is_transport_tx_ready(void);
#endif

static const char *TAG = "wifi";

// The order lives with the secret-file rules (the BLE console and the setup
// mode read the same two files).
#define WIFI_TOML_HOME_PATH   FMRB_WIFI_TOML_HOME_PATH
#define WIFI_TOML_ETC_PATH    FMRB_WIFI_TOML_ETC_PATH
#define WIFI_TOML_TMP_PATH    FMRB_WIFI_TOML_HOME_PATH ".tmp"

#define WIFI_BACKOFF_MIN_MS   5000
#define WIFI_BACKOFF_MAX_MS   120000
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define WIFI_SCAN_TIMEOUT_MS  15000
#define WIFI_LEAVE_WAIT_MS    1500
#define WIFI_SCAN_MAX         24   /* networks shown (and ranked) */
#define WIFI_RECORDS_MAX      48   /* access points read from one scan */

#define WIFI_EV_GOT_IP  (1 << 0)

// Wake-up bits to the manager task, in the same event group as
// WIFI_EV_GOT_IP (bit 0, which the manager never waits on). An event group
// rather than task notifications: its functions are already linked, and on
// the S3 FreeRTOS lives in internal RAM, so a new kernel API there would
// grow it.
#define N_START        (1u << 1)   /* the station started */
#define N_SCAN_DONE    (1u << 2)
#define N_GOT_IP       (1u << 3)
#define N_DISC         (1u << 4)   /* disconnected (s_last_reason) */
#define N_USER_SCAN    (1u << 5)
#define N_USER_CONNECT (1u << 6)   /* s_req_ssid / s_req_pass */
#define N_USER_OFF     (1u << 7)
#define N_USER_FORGET  (1u << 8)   /* s_forget_ssid */
#define N_ALL          (N_START | N_SCAN_DONE | N_GOT_IP | N_DISC | N_USER_SCAN | \
                        N_USER_CONNECT | N_USER_OFF | N_USER_FORGET)

typedef enum {
    PH_IDLE = 0,     /* connected, switched off by the user, or nothing saved */
    PH_SCANNING,
    PH_CONNECTING,
    PH_WAITING,      /* backoff before the next scan */
} wifi_phase_t;

typedef struct {
    char ssid[WIFI_CONF_SSID_MAX + 1];
    int8_t rssi;
    bool secured;
} wifi_seen_t;

static EventGroupHandle_t s_wifi_events = NULL;
static volatile bool s_connected = false;
static esp_netif_t *s_netif = NULL;
static char s_ip_str[16] = "0.0.0.0";
/* The name every Family mruby answers to, in addition to its own. Two boards
 * on one network both claiming it is a first-come race -- which is exactly
 * why each also gets a name of its own -- but the tools have always resolved
 * this one, and on a network with a single board it is the convenient
 * answer. */
#define MDNS_SHARED_HOST "fmruby"

/* Empty until the configuration is read; if the configuration does not name
 * one, mdns_start() builds it from the WiFi MAC. */
static char s_hostname[32] = "";
static char s_ssid[33] = "";

// Everything new lives in PSRAM: the devices' internal RAM does not grow
// (doc/reference/internal_ram_budget.md).
EXT_RAM_BSS_ATTR static TaskHandle_t s_mgr;
EXT_RAM_BSS_ATTR static SemaphoreHandle_t s_lock;     /* the fields the page reads */
EXT_RAM_BSS_ATTR static volatile int s_last_reason;
EXT_RAM_BSS_ATTR static volatile bool s_associated;
EXT_RAM_BSS_ATTR static volatile uint8_t s_off_reason; /* why not started, for the page */
/* What the page sees (under s_lock). */
EXT_RAM_BSS_ATTR static wifi_seen_t s_seen[WIFI_SCAN_MAX];
EXT_RAM_BSS_ATTR static int s_seen_count;
EXT_RAM_BSS_ATTR static char s_saved[WIFI_CONF_MAX][WIFI_CONF_SSID_MAX + 1];
EXT_RAM_BSS_ATTR static int s_saved_count;
EXT_RAM_BSS_ATTR static volatile uint8_t s_phase;
EXT_RAM_BSS_ATTR static volatile bool s_scanning;
EXT_RAM_BSS_ATTR static char s_try_ssid[WIFI_CONF_SSID_MAX + 1];
EXT_RAM_BSS_ATTR static char s_code[16];               /* the last outcome the page asked for */
EXT_RAM_BSS_ATTR static char s_code_arg[WIFI_CONF_SSID_MAX + 1];
/* Requests from the page (under s_lock). */
EXT_RAM_BSS_ATTR static char s_req_ssid[WIFI_CONF_SSID_MAX + 1];
EXT_RAM_BSS_ATTR static char s_req_pass[WIFI_CONF_PASS_MAX + 1];
EXT_RAM_BSS_ATTR static char s_forget_ssid[WIFI_CONF_SSID_MAX + 1];
/* The manager's own (its task only). */
EXT_RAM_BSS_ATTR static wifi_conf_t s_conf;            /* a round of saved networks, wiped after */
EXT_RAM_BSS_ATTR static wifi_conf_t s_edit;            /* the page's changes, wiped after */
EXT_RAM_BSS_ATTR static wifi_ap_record_t s_records[WIFI_RECORDS_MAX];
EXT_RAM_BSS_ATTR static int s_cand[WIFI_CONF_MAX];     /* indexes into s_conf, in the order to try */
EXT_RAM_BSS_ATTR static int s_cand_n, s_cand_i;
EXT_RAM_BSS_ATTR static bool s_try_user;               /* the attempt was chosen on the page */
EXT_RAM_BSS_ATTR static bool s_try_save;               /* save it when it gets an address */
EXT_RAM_BSS_ATTR static char s_try_pass[WIFI_CONF_PASS_MAX + 1];
EXT_RAM_BSS_ATTR static bool s_user_off;               /* Disconnect on the page: stay off */
EXT_RAM_BSS_ATTR static bool s_scan_for_auto;
EXT_RAM_BSS_ATTR static bool s_user_scan_wanted;
EXT_RAM_BSS_ATTR static uint32_t s_backoff_ms;
EXT_RAM_BSS_ATTR static uint64_t s_deadline_ms;        /* connect, scan or backoff deadline */
EXT_RAM_BSS_ATTR static uint32_t s_pending;            /* bits seen while waiting for one */

enum { OFF_NOT_ASKED = 0, OFF_DISABLED, OFF_BLE, OFF_FAILED };

static void mdns_share_address(esp_ip4_addr_t ip);

static void mgr_notify(uint32_t bits)
{
    if (s_mgr && s_wifi_events) xEventGroupSetBits(s_wifi_events, bits);
}

// The bits that arrived (cleared as they are taken), 0 on a timeout.
static uint32_t mgr_wait(TickType_t ticks)
{
    return (uint32_t)xEventGroupWaitBits(s_wifi_events, N_ALL, pdTRUE, pdFALSE, ticks) & N_ALL;
}

static void lock(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

static void wipe(void *p, size_t n)
{
    volatile char *v = (volatile char *)p;
    while (n--) *v++ = 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *data)
{
    (void)arg;
    // Only flags and a wake-up here: this runs in the event loop task, and
    // everything that takes time (records, the file, the next attempt) is
    // the manager's.
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            mgr_notify(N_START);
            break;
        case WIFI_EVENT_STA_CONNECTED:
            s_associated = true;
            FMRB_LOGI(TAG, "Associated with AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
            bool was_connected = s_connected;
            s_connected = false;
            s_associated = false;
            xEventGroupClearBits(s_wifi_events, WIFI_EV_GOT_IP);
            s_last_reason = ev ? (int)ev->reason : -1;
            // reason codes: esp_wifi_types.h (2=AUTH_EXPIRE, 15=4WAY_TIMEOUT /
            // wrong password, 201=NO_AP_FOUND / wrong SSID or 5GHz-only AP)
            FMRB_LOGW(TAG, "Disconnected%s (reason=%d)",
                      was_connected ? "" : " (connect failed)", s_last_reason);
            mgr_notify(N_DISC);
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            mgr_notify(N_SCAN_DONE);
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_EV_GOT_IP);
        mdns_share_address(ev->ip_info.ip);
        FMRB_LOGI(TAG, "Connected, ip=%s (http://%s.local/, also http://%s.local/)",
                  s_ip_str, s_hostname, MDNS_SHARED_HOST);
        mgr_notify(N_GOT_IP);
    }
}

/* A name this board alone answers to.
 *
 * Two boards used to be two "fmruby.local"s, and which one a lookup reached
 * was a race -- the tools would drive a Tab5 and get a NARYA. The last three
 * bytes of the MAC settle it, written the way BLE already writes them for
 * its "Family-mruby-XXXXXX" (drivers/ble/ble_task.c): lower case, no
 * separators. The two suffixes are not the same digits -- WiFi and Bluetooth
 * are given different addresses off the same base -- but they are read the
 * same way, and both are printed at boot.
 *
 * The WiFi MAC, not the Bluetooth one: this is a WiFi name, and on a machine
 * whose radio is a separate chip the BT address only exists once BLE has
 * started, which a WiFi-only configuration never does. */
static void hostname_from_mac(void)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "esp_wifi_get_mac failed: %d, using %s",
                  err, MDNS_SHARED_HOST);
        strlcpy(s_hostname, MDNS_SHARED_HOST, sizeof(s_hostname));
        return;
    }
    snprintf(s_hostname, sizeof(s_hostname), "%s-%02x%02x%02x",
             MDNS_SHARED_HOST, mac[3], mac[4], mac[5]);
}

static void mdns_start(void)
{
    if (s_hostname[0] == '\0') hostname_from_mac();

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "mDNS init failed: %d", err);
        return;
    }
    mdns_hostname_set(s_hostname);
    mdns_instance_name_set("Family mruby OS");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    FMRB_LOGI(TAG, "mDNS hostname: %s.local", s_hostname);
    /* The shared name is answered for as well, but only once there is an
     * address to answer with -- see mdns_share_address(), called from the
     * got-IP handler. */
}

/* Answer for "fmruby.local" too, at the address just obtained.
 *
 * A delegated name carries an address list rather than following the
 * interface, so it has to be set again every time the address changes;
 * without that, a lease renewal onto a different address leaves the shared
 * name pointing at the old one. Called from the got-IP handler for exactly
 * that reason. */
static void mdns_share_address(esp_ip4_addr_t ip)
{
    if (strcmp(s_hostname, MDNS_SHARED_HOST) == 0) return;  /* it is the name */

    mdns_ip_addr_t addr = {0};
    addr.addr.type = ESP_IPADDR_TYPE_V4;
    addr.addr.u_addr.ip4 = ip;
    addr.next = NULL;

    esp_err_t err = mdns_hostname_exists(MDNS_SHARED_HOST)
                  ? mdns_delegate_hostname_set_address(MDNS_SHARED_HOST, &addr)
                  : mdns_delegate_hostname_add(MDNS_SHARED_HOST, &addr);
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "mDNS %s.local not claimed: %d", MDNS_SHARED_HOST, err);
    }
}

// ---- The settings file ----------------------------------------------------

static const char *settings_path(void)
{
    fmrb_file_info_t finfo;
    return (fmrb_hal_file_stat(WIFI_TOML_HOME_PATH, &finfo) == FMRB_OK)
               ? WIFI_TOML_HOME_PATH : WIFI_TOML_ETC_PATH;
}

// The file wifi_task reads into conf; a missing file is the empty (enabled)
// one. false only when a file is there but cannot be parsed.
static bool conf_load(wifi_conf_t *conf, const char **from)
{
    const char *path = settings_path();
    if (from) *from = path;
    fmrb_file_info_t finfo;
    if (fmrb_hal_file_stat(path, &finfo) != FMRB_OK) {
        wifi_conf_from_toml(NULL, conf, fmrb_toml_xfree);
        return true;
    }
    char errbuf[96];
    toml_table_t *root = fmrb_toml_load_file(path, errbuf, sizeof(errbuf));
    if (!root) {
        FMRB_LOGW(TAG, "%s unreadable (%s)", path, errbuf);
        wifi_conf_from_toml(NULL, conf, fmrb_toml_xfree);
        return false;
    }
    wifi_conf_from_toml(root, conf, fmrb_toml_xfree);
    toml_free(root);
    return true;
}

// The SSIDs the page marks "saved" (no passwords).
static void saved_cache_update(const wifi_conf_t *conf)
{
    lock();
    s_saved_count = conf->count;
    for (int i = 0; i < conf->count; i++) {
        strlcpy(s_saved[i], conf->nets[i].ssid, sizeof(s_saved[i]));
    }
    unlock();
}

// Written beside the file and renamed over it, so a reset in the middle
// leaves the old file or the new one, never half of one. Always to /home:
// /etc/wifi.toml is the build's and stays as it is (the networks it had
// are in conf, so they move to /home with the rest).
static bool conf_save(const wifi_conf_t *conf)
{
    size_t need = wifi_conf_to_text(conf, NULL, 0) + 1;
    char *text = fmrb_sys_malloc(need);
    if (!text) return false;
    size_t len = wifi_conf_to_text(conf, text, need);
    bool ok = false;
    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(WIFI_TOML_TMP_PATH, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &f) == FMRB_OK) {
        size_t wrote = 0;
        ok = fmrb_hal_file_write(f, text, len, &wrote) == FMRB_OK && wrote == len;
        fmrb_hal_file_close(f);
        if (ok) ok = fmrb_hal_file_rename(WIFI_TOML_TMP_PATH, WIFI_TOML_HOME_PATH) == FMRB_OK;
        if (!ok) fmrb_hal_file_remove(WIFI_TOML_TMP_PATH);
    }
    wipe(text, need);
    fmrb_sys_free(text);
    if (ok) {
        FMRB_LOGI(TAG, "%d network(s) saved in %s", conf->count, WIFI_TOML_HOME_PATH);
        saved_cache_update(conf);
    } else {
        FMRB_LOGE(TAG, "could not write %s", WIFI_TOML_HOME_PATH);
    }
    return ok;
}

// ---- The manager --------------------------------------------------------

static uint64_t now_ms(void)
{
    return fmrb_hal_time_get_ms();
}

static void set_code(const char *code, const char *arg)
{
    lock();
    strlcpy(s_code, code, sizeof(s_code));
    strlcpy(s_code_arg, arg ? arg : "", sizeof(s_code_arg));
    unlock();
}

static void set_phase(wifi_phase_t ph)
{
    s_phase = (uint8_t)ph;
}

// Wait for one of these bits for up to ms; others that arrive meanwhile are
// kept for the main loop.
static bool wait_for(uint32_t bits, uint32_t ms)
{
    uint64_t until = now_ms() + ms;
    for (;;) {
        if (s_pending & bits) {
            s_pending &= ~bits;
            return true;
        }
        uint64_t t = now_ms();
        if (t >= until) return false;
        s_pending |= mgr_wait(pdMS_TO_TICKS((uint32_t)(until - t)));
    }
}

// Off the current network (or attempt) and wait for the driver to say so,
// so the event does not count against the next attempt.
static void leave_current(void)
{
    if (!s_associated && !s_connected && s_phase != PH_CONNECTING) return;
    s_pending &= ~N_DISC;
    esp_wifi_disconnect();
    wait_for(N_DISC, WIFI_LEAVE_WAIT_MS);
    s_pending &= ~(N_DISC | N_GOT_IP);
}

static void backoff_wait(void)
{
    if (s_backoff_ms < WIFI_BACKOFF_MIN_MS) s_backoff_ms = WIFI_BACKOFF_MIN_MS;
    FMRB_LOGI(TAG, "no saved network joined; scanning again in %u s",
              (unsigned)(s_backoff_ms / 1000));
    set_phase(PH_WAITING);
    s_deadline_ms = now_ms() + s_backoff_ms;
    s_backoff_ms *= 2;
    if (s_backoff_ms > WIFI_BACKOFF_MAX_MS) s_backoff_ms = WIFI_BACKOFF_MAX_MS;
    wifi_conf_wipe(&s_conf);
}

static bool start_scan(bool for_auto)
{
    s_scan_for_auto = for_auto;
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "scan not started: %d", (int)err);
        s_scanning = false;
        return false;
    }
    s_scanning = true;
    set_phase(PH_SCANNING);
    s_deadline_ms = now_ms() + WIFI_SCAN_TIMEOUT_MS;
    return true;
}

static void connect_to(const char *ssid, const char *password, bool hidden, bool user, bool save)
{
    leave_current();
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    (void)hidden;   /* a hidden network is joined by name like any other */
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    wipe(&cfg, sizeof(cfg));
    lock();
    strlcpy(s_try_ssid, ssid, sizeof(s_try_ssid));
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    unlock();
    s_try_user = user;
    s_try_save = save;
    if (save && password != s_try_pass) strlcpy(s_try_pass, password, sizeof(s_try_pass));
    set_phase(PH_CONNECTING);
    s_deadline_ms = now_ms() + WIFI_CONNECT_TIMEOUT_MS;
    s_pending &= ~(N_DISC | N_GOT_IP);
    if (err == ESP_OK) err = esp_wifi_connect();
    FMRB_LOGI(TAG, "joining %s (%s)%s", ssid, user ? "chosen on the page" : "saved",
              err == ESP_OK ? "" : " -- refused by the driver");
    if (err != ESP_OK) {
        s_last_reason = -1;
        s_pending |= N_DISC;   /* handled as a failed attempt */
    }
}

static void try_next(void)
{
    if (s_cand_i < s_cand_n) {
        const wifi_conf_net_t *n = &s_conf.nets[s_cand[s_cand_i++]];
        connect_to(n->ssid, n->password, n->hidden, false, false);
        return;
    }
    backoff_wait();
}

// The saved networks in the order to try: those in the last scan by
// strength, then the hidden ones; with no scan to go by (it failed), all of
// them in the file's order.
static void plan_candidates(bool have_scan)
{
    s_cand_n = 0;
    s_cand_i = 0;
    int rssi[WIFI_CONF_MAX];
    if (have_scan) {
        for (int i = 0; i < s_conf.count; i++) {
            int best = -1000;
            lock();
            for (int k = 0; k < s_seen_count; k++) {
                if (strcmp(s_seen[k].ssid, s_conf.nets[i].ssid) == 0 && s_seen[k].rssi > best) {
                    best = s_seen[k].rssi;
                }
            }
            unlock();
            if (best == -1000) continue;
            // insertion by strength, strongest first
            int at = s_cand_n;
            while (at > 0 && rssi[at - 1] < best) {
                s_cand[at] = s_cand[at - 1];
                rssi[at] = rssi[at - 1];
                at--;
            }
            s_cand[at] = i;
            rssi[at] = best;
            s_cand_n++;
        }
        for (int i = 0; i < s_conf.count; i++) {
            bool listed = false;
            for (int k = 0; k < s_cand_n; k++) listed |= (s_cand[k] == i);
            if (!listed && s_conf.nets[i].hidden) s_cand[s_cand_n++] = i;
        }
    } else {
        for (int i = 0; i < s_conf.count; i++) s_cand[s_cand_n++] = i;
    }
}

// Look for a saved network to join (at start, after the link dropped, after
// a failed choice on the page, when the backoff is over).
static void start_auto(void)
{
    if (s_user_off) {
        set_phase(PH_IDLE);
        return;
    }
    conf_load(&s_conf, NULL);
    saved_cache_update(&s_conf);
    if (s_conf.count == 0) {
        FMRB_LOGI(TAG, "no saved networks: choose one on the Wi-Fi page");
        set_phase(PH_IDLE);
        return;
    }
    if (!start_scan(true)) {
        plan_candidates(false);
        try_next();
    }
}

static bool is_saved_locked(const char *ssid)
{
    for (int i = 0; i < s_saved_count; i++) {
        if (strcmp(s_saved[i], ssid) == 0) return true;
    }
    return false;
}

// The scan's access points into the page's list: one entry per name (its
// strongest), the saved networks always kept, the rest strongest first up
// to WIFI_SCAN_MAX -- a crowded place has more than that, and a saved
// network must not fall off the end (the choice of which to join reads
// this list).
static void read_scan(void)
{
    uint16_t total = 0;
    esp_wifi_scan_get_ap_num(&total);
    uint16_t n = total > WIFI_RECORDS_MAX ? WIFI_RECORDS_MAX : total;
    if (n > 0 && esp_wifi_scan_get_ap_records(&n, s_records) != ESP_OK) {
        // The coprocessor may cap one answer; ask again for fewer.
        n = WIFI_SCAN_MAX;
        if (esp_wifi_scan_get_ap_records(&n, s_records) != ESP_OK) n = 0;
    } else if (n == 0) {
        esp_wifi_clear_ap_list();
    }
    // Names first, each with its strongest signal, in s_records' own slots.
    int uniq = 0;
    for (int i = 0; i < n; i++) {
        if (s_records[i].ssid[0] == '\0') continue;   /* hidden: nothing to show */
        int at = -1;
        for (int k = 0; k < uniq; k++) {
            if (strcmp((const char *)s_records[k].ssid, (const char *)s_records[i].ssid) == 0) at = k;
        }
        if (at >= 0) {
            if (s_records[i].rssi > s_records[at].rssi) s_records[at] = s_records[i];
            continue;
        }
        if (uniq != i) s_records[uniq] = s_records[i];
        uniq++;
    }
    lock();
    s_seen_count = 0;
    bool taken[WIFI_RECORDS_MAX] = { false };
    for (int pass = 0; pass < 2; pass++) {   /* the saved ones, then the strongest */
        for (;;) {
            int best = -1;
            for (int k = 0; k < uniq; k++) {
                if (taken[k]) continue;
                if (pass == 0 && !is_saved_locked((const char *)s_records[k].ssid)) continue;
                if (best < 0 || s_records[k].rssi > s_records[best].rssi) best = k;
            }
            if (best < 0 || s_seen_count >= WIFI_SCAN_MAX) break;
            taken[best] = true;
            wifi_seen_t *e = &s_seen[s_seen_count++];
            strlcpy(e->ssid, (const char *)s_records[best].ssid, sizeof(e->ssid));
            e->rssi = s_records[best].rssi;
            e->secured = s_records[best].authmode != WIFI_AUTH_OPEN;
        }
    }
    int seen = s_seen_count;
    unlock();
    FMRB_LOGI(TAG, "scan: %u access point(s), %d read, %d network(s) listed",
              (unsigned)total, (int)n, seen);
}

static void on_scan_done(bool ok)
{
    // The file may have changed by other hands (the BLE console, an
    // editor): the saved marks follow it at every scan.
    if (!s_scan_for_auto) {
        conf_load(&s_edit, NULL);
        saved_cache_update(&s_edit);
        wifi_conf_wipe(&s_edit);
    }
    if (ok) read_scan();
    s_scanning = false;
    bool for_auto = s_scan_for_auto;
    s_scan_for_auto = false;
    if (s_phase == PH_SCANNING) set_phase(PH_IDLE);
    // Not connected and not told to stay off: any scan is a chance to join.
    if (!s_connected && !s_user_off && s_phase != PH_CONNECTING) {
        if (!for_auto) {
            conf_load(&s_conf, NULL);
            saved_cache_update(&s_conf);
        }
        if (s_conf.count == 0) {
            set_phase(PH_IDLE);
            return;
        }
        // A scan that saw nothing at all is not trusted (a radio that
        // cannot scan must still join): then every saved one, in order.
        plan_candidates(ok && s_seen_count > 0);
        try_next();
    }
}

static const char *failure_code(int reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "auth_failed";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "no_network";
    default:
        return "failed";
    }
}

static void attempt_failed(int reason)
{
    char ssid[WIFI_CONF_SSID_MAX + 1];
    strlcpy(ssid, s_try_ssid, sizeof(ssid));
    set_phase(PH_IDLE);
    FMRB_LOGW(TAG, "could not join %s (reason=%d)", ssid, reason);
    wipe(s_try_pass, sizeof(s_try_pass));
    s_try_save = false;
    if (s_associated) leave_current();
    if (s_try_user) {
        s_try_user = false;
        set_code(failure_code(reason), ssid);
        // Back to the saved ones, so a wrong choice cannot strand the board.
        start_auto();
        return;
    }
    try_next();
}

static void on_got_ip(void)
{
    char ssid[WIFI_CONF_SSID_MAX + 1];
    strlcpy(ssid, s_try_ssid, sizeof(ssid));
    set_phase(PH_IDLE);
    s_backoff_ms = WIFI_BACKOFF_MIN_MS;
    s_cand_n = s_cand_i = 0;
    if (s_try_user) set_code("connected", ssid);
    if (s_try_save) {
        conf_load(&s_edit, NULL);
        if (wifi_conf_add(&s_edit, ssid, s_try_pass)) conf_save(&s_edit);
        wifi_conf_wipe(&s_edit);
    }
    s_try_user = false;
    s_try_save = false;
    wipe(s_try_pass, sizeof(s_try_pass));
    wifi_conf_wipe(&s_conf);
}

static void on_disconnected(void)
{
    int reason = s_last_reason;
    if (s_phase == PH_CONNECTING) {
        attempt_failed(reason);
        return;
    }
    if (s_user_off) return;
    if (s_phase == PH_IDLE) {
        // The link dropped: look again at once (the backoff starts over).
        FMRB_LOGI(TAG, "link lost (reason=%d): looking for a saved network", reason);
        s_backoff_ms = WIFI_BACKOFF_MIN_MS;
        start_auto();
    }
}

static void on_user_connect(void)
{
    char ssid[WIFI_CONF_SSID_MAX + 1];
    lock();
    strlcpy(ssid, s_req_ssid, sizeof(ssid));
    strlcpy(s_try_pass, s_req_pass, sizeof(s_try_pass));
    wipe(s_req_pass, sizeof(s_req_pass));
    s_req_ssid[0] = '\0';
    unlock();
    if (ssid[0] == '\0') return;
    s_user_off = false;
    s_backoff_ms = WIFI_BACKOFF_MIN_MS;
    if (s_scanning) {   /* the driver refuses to connect while it scans */
        esp_wifi_scan_stop();
        s_scanning = false;
    }

    conf_load(&s_edit, NULL);
    int at = wifi_conf_find(&s_edit, ssid);
    bool typed = s_try_pass[0] != '\0';
    bool hidden = at >= 0 && s_edit.nets[at].hidden;
    if (!typed && at >= 0) {
        strlcpy(s_try_pass, s_edit.nets[at].password, sizeof(s_try_pass));
    } else if (!typed) {
        bool secured = false;
        lock();
        for (int k = 0; k < s_seen_count; k++) {
            if (strcmp(s_seen[k].ssid, ssid) == 0) secured = s_seen[k].secured;
        }
        unlock();
        if (secured) {
            set_code("need_password", ssid);
            wifi_conf_wipe(&s_edit);
            return;
        }
    }
    wifi_conf_wipe(&s_edit);
    set_code("", "");
    // Saved when it gets an address: new, or its password typed again.
    connect_to(ssid, s_try_pass, hidden, true, at < 0 || typed);
}

static void on_user_off(void)
{
    s_user_off = true;
    s_try_user = false;
    s_try_save = false;
    wipe(s_try_pass, sizeof(s_try_pass));
    if (s_scanning) {
        esp_wifi_scan_stop();
        s_scanning = false;
    }
    leave_current();
    set_phase(PH_IDLE);
    set_code("disconnected", "");
    FMRB_LOGI(TAG, "disconnected on the Wi-Fi page (stays off until a network is chosen)");
}

static void on_user_forget(void)
{
    char ssid[WIFI_CONF_SSID_MAX + 1];
    lock();
    strlcpy(ssid, s_forget_ssid, sizeof(ssid));
    s_forget_ssid[0] = '\0';
    unlock();
    if (ssid[0] == '\0') return;
    conf_load(&s_edit, NULL);
    if (wifi_conf_forget(&s_edit, ssid)) {
        if (conf_save(&s_edit)) set_code("forgotten", ssid);
    }
    wifi_conf_wipe(&s_edit);
}

static void wifi_mgr_task(void *arg)
{
    (void)arg;
    s_backoff_ms = WIFI_BACKOFF_MIN_MS;
    for (;;) {
        uint32_t wait = UINT32_MAX;
        if (s_phase != PH_IDLE) {
            uint64_t t = now_ms();
            wait = s_deadline_ms > t ? (uint32_t)(s_deadline_ms - t) : 0;
        }
        if (s_pending == 0) {
            s_pending |= mgr_wait(wait == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(wait));
        }
        uint32_t bits = s_pending;
        s_pending = 0;

        if (bits & N_GOT_IP) on_got_ip();
        if (bits & N_DISC) on_disconnected();
        if (bits & N_SCAN_DONE) on_scan_done(true);
        if (bits & N_USER_OFF) on_user_off();
        if (bits & N_USER_FORGET) on_user_forget();
        if (bits & N_USER_CONNECT) on_user_connect();
        if (bits & N_USER_SCAN) s_user_scan_wanted = true;
        if (bits & N_START) start_auto();

        uint64_t t = now_ms();
        if (s_phase == PH_CONNECTING && t >= s_deadline_ms) {
            s_pending &= ~N_DISC;
            esp_wifi_disconnect();
            wait_for(N_DISC, WIFI_LEAVE_WAIT_MS);
            s_pending &= ~(N_DISC | N_GOT_IP);
            attempt_failed(-2);
        } else if (s_phase == PH_SCANNING && t >= s_deadline_ms) {
            FMRB_LOGW(TAG, "scan did not finish");
            esp_wifi_scan_stop();
            on_scan_done(false);
        } else if (s_phase == PH_WAITING && t >= s_deadline_ms) {
            set_phase(PH_IDLE);
            start_auto();
        }
        if (s_user_scan_wanted && s_phase != PH_CONNECTING && !s_scanning) {
            s_user_scan_wanted = false;
            s_backoff_ms = WIFI_BACKOFF_MIN_MS;
            bool waiting = s_phase == PH_WAITING;
            if (!start_scan(waiting && !s_user_off) && waiting) set_phase(PH_WAITING);
        }
    }
}

// ---- Bring-up -------------------------------------------------------------

static bool s_started = false;

bool wifi_task_is_started(void)
{
    return s_started;
}

fmrb_err_t wifi_task_init(void)
{
#ifndef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
    // Retro: one radio at a time (see the header comment).
    if (ble_service_is_started()) {
        FMRB_LOGW(TAG, "BLE is running: WiFi not started (S3 runs one radio; "
                       "set ble_auto_start=false to use WiFi)");
        s_off_reason = OFF_BLE;
        return FMRB_ERR_INVALID_STATE;
    }
#endif

    // --- /home/wifi.toml, else /etc/wifi.toml. No file, or no network in
    // it, still starts the station: the Wi-Fi page can then add one. ---
    const char *path = NULL;
    conf_load(&s_conf, &path);
    if (!s_conf.enable) {
        FMRB_LOGI(TAG, "WiFi disabled in %s", path);
        wifi_conf_wipe(&s_conf);
        s_off_reason = OFF_DISABLED;
        return FMRB_ERR_NOT_FOUND;
    }
    FMRB_LOGI(TAG, "Settings from %s: %d saved network(s)", path, s_conf.count);
    /* No default: an empty name means "one of your own", resolved once the
     * radio is up and its MAC can be asked for. */
    strlcpy(s_hostname, s_conf.hostname, sizeof(s_hostname));
    saved_cache_update(&s_conf);
    wifi_conf_wipe(&s_conf);

#ifdef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
    // --- Make sure the esp_hosted transport is up before any WiFi RPC
    // (esp_hosted 1.4.0 corrupts the heap otherwise). BLE init normally
    // brings the SDIO link up first; poll the transport state without
    // issuing RPCs (esp_hosted_setup() is compiled out in 1.4.0). ---
    esp_err_t err = esp_hosted_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        FMRB_LOGE(TAG, "esp_hosted_init failed: %d", err);
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }
    bool transport_up = false;
    for (int i = 0; i < 300; i++) {  // up to 15 s
        if (is_transport_rx_ready() && is_transport_tx_ready()) {
            transport_up = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!transport_up) {
        FMRB_LOGE(TAG, "esp_hosted transport not up, aborting WiFi init");
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }
#else
    // --- Native radio (S3): esp_wifi keeps calibration data in NVS. Same
    // init-or-erase dance as ble_task_init (whichever radio starts first
    // does it; nvs_flash_init is idempotent). ---
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        FMRB_LOGW(TAG, "NVS partition full or outdated, erasing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "nvs_flash_init failed: %d", (int)err);
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }
#endif

    // --- Standard STA bring-up; every esp_wifi_* call is RPC'd to the C6 ---
    s_wifi_events = xEventGroupCreate();
    s_lock = xSemaphoreCreateMutex();
    if (!s_wifi_events || !s_lock) {
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_NO_MEMORY;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_netif_init failed: %d", err);
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }
    // esp_hosted may already have created the default event loop
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        FMRB_LOGE(TAG, "event loop create failed: %d", err);
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) {
        FMRB_LOGE(TAG, "netif create failed");
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }

    if (fmrb_task_create_ex(wifi_mgr_task, "wifi_mgr", FMRB_WIFI_MGR_TASK_STACK_SIZE, NULL,
                            FMRB_WIFI_MGR_TASK_PRIORITY, &s_mgr,
                            FMRB_WIFI_MGR_TASK_FLAGS) != pdPASS) {
        FMRB_LOGE(TAG, "wifi_mgr task not created");
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_NO_MEMORY;
    }

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               wifi_event_handler, NULL);

    // Zeroed init config is rejected by the remote implementation
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_wifi_init failed: %d", err);
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }
#ifndef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
    // The settings come from wifi.toml every time: keeping them in RAM means
    // no NVS write from the manager's PSRAM stack (and no password in NVS).
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
#endif
    esp_wifi_set_mode(WIFI_MODE_STA);
    // HT20: HT40 on the crowded 2.4GHz band loses more to retries than it
    // gains, and the C6 is the throughput ceiling anyway
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    // The station starts with no network; the manager chooses one on
    // STA_START (a scan, then the strongest saved one).
    err = esp_wifi_start();
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_wifi_start failed: %d", err);
        s_off_reason = OFF_FAILED;
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "STA starting");

    mdns_start();
    s_started = true;
    fmrb_mem_log_boot_snapshot("wifi_ready");
    return FMRB_OK;
}

bool wifi_wait_for_ip(uint32_t timeout_ms)
{
    if (!s_wifi_events) return false;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_EV_GOT_IP,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_EV_GOT_IP) != 0;
}

bool wifi_is_connected(void)
{
    return s_connected;
}

void wifi_get_ip_str(char *buf, size_t buf_len)
{
    strlcpy(buf, s_ip_str, buf_len);
}

void wifi_get_ssid(char *buf, size_t buf_len)
{
    lock();
    strlcpy(buf, s_ssid, buf_len);
    unlock();
}

void wifi_get_hostname(char *buf, size_t buf_len)
{
    strlcpy(buf, s_hostname, buf_len);
}

// ---- The Wi-Fi page (fmrb_platform_sys.h wifi_*) ----------------------------

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} text_out_t;

static void out_printf(text_out_t *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t room = o->len < o->cap ? o->cap - o->len : 0;
    int n = vsnprintf(room ? o->buf + o->len : NULL, room, fmt, ap);
    va_end(ap);
    if (n > 0) o->len += (size_t)n;
}

// A field must not carry the separators (an SSID can hold any byte).
static void clean_field(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) {
        char c = src[i];
        dst[i] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    dst[i] = '\0';
}

static int strength_of(int rssi)
{
    int s = (rssi + 100) * 2;   /* -100 dBm: 0, -50 dBm and above: 100 */
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    return s;
}

int32_t wifi_list_text(bool scan, char *buf, size_t cap)
{
    text_out_t o = { buf, cap, 0 };
    if (buf && cap) buf[0] = '\0';
    if (!s_started) {
        const char *st = s_off_reason == OFF_BLE ? "ble"
                       : s_off_reason == OFF_FAILED ? "failed" : "disabled";
        out_printf(&o, "wifi\t%s\t\t\t\t\tforget\n", st);
        return (int32_t)o.len;
    }
    if (scan) mgr_notify(N_USER_SCAN);

    char active[WIFI_CONF_SSID_MAX + 1] = "";
    char f1[WIFI_CONF_SSID_MAX + 1];
    char f2[WIFI_CONF_SSID_MAX + 1];
    lock();
    if (s_connected) clean_field(active, sizeof(active), s_ssid);
    const char *busy = s_phase == PH_CONNECTING ? "connecting"
                     : (s_scanning || scan) ? "scanning" : "";
    clean_field(f1, sizeof(f1), s_phase == PH_CONNECTING ? s_try_ssid : s_code_arg);
    out_printf(&o, "wifi\tok\t%s\t%s\t%s\t%s\tforget\n", active, busy, s_code, f1);
    // In reach: the one in use first, then by strength.
    bool done[WIFI_SCAN_MAX] = { false };
    for (int round = 0; round < s_seen_count; round++) {
        int pick = -1;
        for (int k = 0; k < s_seen_count; k++) {
            if (done[k]) continue;
            if (pick < 0) { pick = k; continue; }
            bool ka = active[0] && strcmp(s_seen[k].ssid, s_ssid) == 0;
            bool pa = active[0] && strcmp(s_seen[pick].ssid, s_ssid) == 0;
            if ((ka && !pa) || (ka == pa && s_seen[k].rssi > s_seen[pick].rssi)) pick = k;
        }
        done[pick] = true;
        const wifi_seen_t *e = &s_seen[pick];
        bool saved = false;
        for (int i = 0; i < s_saved_count; i++) saved |= strcmp(s_saved[i], e->ssid) == 0;
        bool is_active = active[0] && strcmp(e->ssid, s_ssid) == 0;
        clean_field(f2, sizeof(f2), e->ssid);
        out_printf(&o, "ap\t%s\t%d\t%d\t%d\t%d\t1\n", f2, strength_of(e->rssi),
                   e->secured ? 1 : 0, is_active ? 1 : 0, saved ? 1 : 0);
    }
    // Saved but not in the last scan (out of reach, or hidden): listed so
    // they can be forgotten (the 7th field 0: not in reach).
    for (int i = 0; i < s_saved_count; i++) {
        bool seen = false;
        for (int k = 0; k < s_seen_count; k++) seen |= strcmp(s_seen[k].ssid, s_saved[i]) == 0;
        if (seen) continue;
        bool is_active = active[0] && strcmp(s_saved[i], s_ssid) == 0;
        clean_field(f2, sizeof(f2), s_saved[i]);
        out_printf(&o, "ap\t%s\t0\t0\t%d\t1\t0\n", f2, is_active ? 1 : 0);
    }
    unlock();
    return (int32_t)o.len;
}

fmrb_err_t wifi_request_connect(const char *ssid, const char *password)
{
    if (!s_started) return FMRB_ERR_NOT_SUPPORTED;
    if (!ssid || !ssid[0] || strlen(ssid) > WIFI_CONF_SSID_MAX) return FMRB_ERR_INVALID_PARAM;
    if (password && strlen(password) > WIFI_CONF_PASS_MAX) return FMRB_ERR_INVALID_PARAM;
    lock();
    strlcpy(s_req_ssid, ssid, sizeof(s_req_ssid));
    strlcpy(s_req_pass, password ? password : "", sizeof(s_req_pass));
    unlock();
    mgr_notify(N_USER_CONNECT);
    return FMRB_OK;
}

fmrb_err_t wifi_request_disconnect(void)
{
    if (!s_started) return FMRB_ERR_NOT_SUPPORTED;
    mgr_notify(N_USER_OFF);
    return FMRB_OK;
}

fmrb_err_t wifi_request_forget(const char *ssid)
{
    if (!s_started) return FMRB_ERR_NOT_SUPPORTED;
    if (!ssid || !ssid[0] || strlen(ssid) > WIFI_CONF_SSID_MAX) return FMRB_ERR_INVALID_PARAM;
    lock();
    strlcpy(s_forget_ssid, ssid, sizeof(s_forget_ssid));
    unlock();
    mgr_notify(N_USER_FORGET);
    return FMRB_OK;
}
