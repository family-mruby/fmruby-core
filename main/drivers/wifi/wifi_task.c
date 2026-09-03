// WiFi STA bring-up.
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
// Credentials come from /etc/wifi.toml (kept out of git; see
// config/wifi.toml.example). Reconnection uses exponential backoff.

#include "wifi_task.h"

#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_toml.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#ifdef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
#include "esp_hosted.h"
#else
#include "nvs_flash.h"
#include "ble_task.h"   /* ble_service_is_started: radio exclusivity */
#endif
#include "mdns.h"

#include <string.h>

#ifdef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
// esp_hosted 1.4.0 transport state probes (host/drivers/transport/
// transport_drv.h; declared here to avoid the deep include path)
extern uint8_t is_transport_rx_ready(void);
extern uint8_t is_transport_tx_ready(void);
#endif

static const char *TAG = "wifi";

#define WIFI_TOML_PATH        "/etc/wifi.toml"
#define WIFI_RECONNECT_MIN_MS 1000
#define WIFI_RECONNECT_MAX_MS 30000

#define WIFI_EV_GOT_IP  (1 << 0)

static EventGroupHandle_t s_wifi_events = NULL;
static esp_timer_handle_t s_reconnect_timer = NULL;
static uint32_t s_reconnect_delay_ms = WIFI_RECONNECT_MIN_MS;
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

static void mdns_share_address(esp_ip4_addr_t ip);

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    FMRB_LOGI(TAG, "Reconnecting...");
    esp_wifi_connect();
}

static void schedule_reconnect(void)
{
    if (!s_reconnect_timer) return;
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)s_reconnect_delay_ms * 1000);
    s_reconnect_delay_ms *= 2;
    if (s_reconnect_delay_ms > WIFI_RECONNECT_MAX_MS) {
        s_reconnect_delay_ms = WIFI_RECONNECT_MAX_MS;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            FMRB_LOGI(TAG, "Associated with AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
            bool was_connected = s_connected;
            s_connected = false;
            xEventGroupClearBits(s_wifi_events, WIFI_EV_GOT_IP);
            // reason codes: esp_wifi_types.h (2=AUTH_EXPIRE, 15=4WAY_TIMEOUT /
            // wrong password, 201=NO_AP_FOUND / wrong SSID or 5GHz-only AP)
            FMRB_LOGW(TAG, "Disconnected%s (reason=%d), retry in %u ms",
                      was_connected ? "" : " (connect failed)",
                      ev ? (int)ev->reason : -1,
                      (unsigned)s_reconnect_delay_ms);
            schedule_reconnect();
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        s_reconnect_delay_ms = WIFI_RECONNECT_MIN_MS;
        xEventGroupSetBits(s_wifi_events, WIFI_EV_GOT_IP);
        mdns_share_address(ev->ip_info.ip);
        FMRB_LOGI(TAG, "Connected, ip=%s (http://%s.local/, also http://%s.local/)",
                  s_ip_str, s_hostname, MDNS_SHARED_HOST);
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
        return FMRB_ERR_INVALID_STATE;
    }
#endif

    // --- Read /etc/wifi.toml ---
    char errbuf[128];
    toml_table_t *root = fmrb_toml_load_file(WIFI_TOML_PATH, errbuf, sizeof(errbuf));
    if (!root) {
        FMRB_LOGI(TAG, "No %s (%s): WiFi disabled", WIFI_TOML_PATH, errbuf);
        return FMRB_ERR_NOT_FOUND;
    }
    toml_table_t *wifi = toml_table_in(root, "wifi");
    if (!wifi || !fmrb_toml_get_bool(wifi, "enable", false)) {
        FMRB_LOGI(TAG, "WiFi disabled in %s", WIFI_TOML_PATH);
        toml_free(root);
        return FMRB_ERR_NOT_FOUND;
    }

    wifi_config_t sta_cfg = { 0 };
    const char *ssid = fmrb_toml_get_string(wifi, "ssid", "");
    const char *pass = fmrb_toml_get_string(wifi, "password", "");
    /* No default: an empty name means "one of your own", resolved once the
     * radio is up and its MAC can be asked for. */
    const char *host = fmrb_toml_get_string(wifi, "hostname", "");
    if (ssid[0] == '\0') {
        FMRB_LOGW(TAG, "ssid missing in %s: WiFi disabled", WIFI_TOML_PATH);
        toml_free(root);
        return FMRB_ERR_NOT_FOUND;
    }
    strlcpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password));
    strlcpy(s_hostname, host, sizeof(s_hostname));
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    toml_free(root);

#ifdef CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
    // --- Make sure the esp_hosted transport is up before any WiFi RPC
    // (esp_hosted 1.4.0 corrupts the heap otherwise). BLE init normally
    // brings the SDIO link up first; poll the transport state without
    // issuing RPCs (esp_hosted_setup() is compiled out in 1.4.0). ---
    esp_err_t err = esp_hosted_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        FMRB_LOGE(TAG, "esp_hosted_init failed: %d", err);
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
        return FMRB_ERR_FAILED;
    }
#endif

    // --- Standard STA bring-up; every esp_wifi_* call is RPC'd to the C6 ---
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) return FMRB_ERR_NO_MEMORY;

    err = esp_netif_init();
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_netif_init failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    // esp_hosted may already have created the default event loop
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        FMRB_LOGE(TAG, "event loop create failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) {
        FMRB_LOGE(TAG, "netif create failed");
        return FMRB_ERR_FAILED;
    }

    const esp_timer_create_args_t targs = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconn",
    };
    esp_timer_create(&targs, &s_reconnect_timer);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               wifi_event_handler, NULL);

    // Zeroed init config is rejected by the remote implementation
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_wifi_init failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    // HT20: HT40 on the crowded 2.4GHz band loses more to retries than it
    // gains, and the C6 is the throughput ceiling anyway
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_wifi_set_config failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_wifi_start failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "STA starting, ssid=%s", sta_cfg.sta.ssid);

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
    strlcpy(buf, s_ssid, buf_len);
}

void wifi_get_hostname(char *buf, size_t buf_len)
{
    strlcpy(buf, s_hostname, buf_len);
}
