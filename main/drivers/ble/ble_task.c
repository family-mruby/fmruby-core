#include <string.h>
#include "esp_attr.h"
#include "fmrb_rtos.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include "fmrb_log_buffer.h"
#include "fmrb_task_config.h"
#include "fmrb_err.h"
#include "fmrb_hal.h"
#include "ble_task.h"
#include "ble_framing.h"
#include "ble_debug_link.h"

#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
// Modern (Tab5): the BLE controller lives on the ESP32-C6 coprocessor,
// reached over SDIO via esp_hosted (host-only NimBLE + vHCI transport).
#include "esp_hosted.h"

// esp_hosted 1.4.0 implements ble_transport_ll_init() but not the deinit
// counterpart that IDF 5.5's nimble_port_deinit() references. BLE stays
// up for the firmware's lifetime here, so an empty stub suffices.
void ble_transport_ll_deinit(void) {}
#endif
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "store/config/ble_store_config.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// ESP-IDF's NimBLE store config exports the function but does not declare it
// in a public header in this environment.
void ble_store_config_init(void);

static const char *TAG = "ble_task";

// ============================================================
// BLE state
// ============================================================
static bool g_ble_initialized = false;
static volatile bool g_ble_start_pending = false;  // admission gate for ble_service_start
static bool g_ble_advertising = false;
static uint8_t g_own_addr_type;
static uint16_t g_conn_handle;
static bool g_connected = false;
static bool g_tx_subscribed = false;

// ============================================================
// File service over BLE - protocol definitions
// ============================================================
// COBS-encoded frames with CRC32.
// Frame: COBS([cmd(1B)][json_len(2B BE)][json][binary][CRC32(4B)]) + 0x00

#define BLE_FS_MAX_FRAME_SIZE 4096
#define BLE_FS_MAX_PATH_LEN   256
#define BLE_FS_MAX_JSON_LEN   2048
#define BLE_FS_MAX_CHUNK_SIZE 2048

// Command codes
#define BLE_FS_CMD_CD     0x11
#define BLE_FS_CMD_LS     0x12
#define BLE_FS_CMD_RM     0x13
#define BLE_FS_CMD_MKDIR  0x14
#define BLE_FS_CMD_STATFS 0x15
#define BLE_FS_CMD_RENAME 0x16
#define BLE_FS_CMD_GET    0x21
#define BLE_FS_CMD_PUT    0x22
#define BLE_FS_CMD_LOG_SUBSCRIBE   0x31
#define BLE_FS_CMD_LOG_UNSUBSCRIBE 0x32
#define BLE_FS_CMD_LOG_SET_LEVEL   0x33
#define BLE_FS_RESP       0x00

// Log streaming defaults / limits
#define BLE_LOG_DEFAULT_PERIOD_MS  200
#define BLE_LOG_MIN_PERIOD_MS      50
#define BLE_LOG_MAX_PERIOD_MS      2000
#define BLE_LOG_DEFAULT_MAX_LINES  20
#define BLE_LOG_MAX_MAX_LINES      40
#define BLE_LOG_LINE_BUF_SIZE      1024

// File service context
typedef struct {
    uint8_t rx_buffer[BLE_FS_MAX_FRAME_SIZE];
    size_t rx_len;
    bool frame_ready;
    char current_dir[BLE_FS_MAX_PATH_LEN];
    fmrb_semaphore_t rx_sem;
    fmrb_semaphore_t mutex;
    // Log streaming state (filled in by LOG_SUBSCRIBE handler)
    bool     log_subscribed;
    uint32_t log_read_pos;
    uint32_t log_seq;
    uint16_t log_period_ms;
    uint16_t log_max_lines;
} ble_fs_context_t;

// PSRAM: only ever touched by CPU copies (os_mbuf_copydata in the GATT
// callback, file writes go through the HAL's internal-RAM bounce buffer),
// so it does not need to sit in internal DRAM (doc/internal_ram_budget.md E).
EXT_RAM_BSS_ATTR static ble_fs_context_t g_fs_ctx;
static fmrb_task_handle_t g_fs_task_handle;
static uint16_t g_fs_tx_val_handle;

// ============================================================
// Debug service over BLE - state
// ============================================================
// Same framing as the file service, but the payload is the remote debugger's
// msgpack body (see ble_debug_link.h). Frames are handed to the debugd task
// through queues; nothing is decoded on the NimBLE host task.

static uint16_t g_dbg_tx_val_handle;
static bool g_dbg_tx_subscribed = false;

// Queues owned by the BLE debug transport, NULL until it registers.
static fmrb_queue_t g_dbg_free_q;
static fmrb_queue_t g_dbg_ready_q;

// Debug service RX: ping-pong raw-frame buffers handed to debugd via queues.
typedef struct {
    uint8_t  buf[BLE_DBG_RX_BUF_COUNT][BLE_DBG_MAX_ENC];
    uint16_t fill_len;      // bytes accumulated into buf[fill_idx]
    int8_t   fill_idx;      // buffer currently being filled, -1 = none held
} ble_dbg_rx_t;

EXT_RAM_BSS_ATTR static ble_dbg_rx_t g_dbg_rx;

// Forward declarations
static void ble_advertise(void);
static void ble_fs_task_func(void *arg);

// ============================================================
// Utility functions (JSON)
// ============================================================

static bool json_get_string(const char *json, const char *key, char *value, size_t value_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *pos = strstr(json, search);
    if (!pos) {
        return false;
    }

    pos += strlen(search);
    while (*pos == ' ' || *pos == '\t') pos++;

    if (*pos != '"') {
        return false;
    }
    pos++;

    size_t i = 0;
    while (*pos != '"' && *pos != '\0' && i < value_size - 1) {
        value[i++] = *pos++;
    }
    value[i] = '\0';

    return true;
}

static bool json_get_int(const char *json, const char *key, int32_t *value)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *pos = strstr(json, search);
    if (!pos) {
        return false;
    }

    pos += strlen(search);
    while (*pos == ' ' || *pos == '\t') pos++;

    *value = atoi(pos);
    return true;
}

// ============================================================
// GATT UUIDs
// ============================================================
// Base UUID: xxxxxxxx-4252-5942-4c45-00000000000Y
// (encodes "FMARBYBLE" in the constant octets)

// Service UUID (byte[0] = 0x01)
static const ble_uuid128_t gatt_svr_svc_fmrb_uuid =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// Device info characteristic (byte[0] = 0x02) - read only
static const ble_uuid128_t gatt_chr_device_info_uuid =
    BLE_UUID128_INIT(0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// File service RX characteristic (byte[0] = 0x03) - browser writes here
static const ble_uuid128_t gatt_chr_fs_rx_uuid =
    BLE_UUID128_INIT(0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// File service TX characteristic (byte[0] = 0x04) - device notifies here
static const ble_uuid128_t gatt_chr_fs_tx_uuid =
    BLE_UUID128_INIT(0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// Debug service (byte[0] = 0x05) - remote debugger, see ble_debug_link.h
static const ble_uuid128_t gatt_svr_svc_dbg_uuid =
    BLE_UUID128_INIT(0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// Debug RX characteristic (byte[0] = 0x06) - host writes here
static const ble_uuid128_t gatt_chr_dbg_rx_uuid =
    BLE_UUID128_INIT(0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// Debug TX characteristic (byte[0] = 0x07) - device notifies here
static const ble_uuid128_t gatt_chr_dbg_tx_uuid =
    BLE_UUID128_INIT(0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// ============================================================
// GATT callbacks
// ============================================================

static const char *DEVICE_INFO_STR = "FamilyMruby v0.1";

static int gatt_chr_device_info_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, DEVICE_INFO_STR, strlen(DEVICE_INFO_STR));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// RX: browser writes COBS-encoded frame data here (may span multiple writes)
static int gatt_chr_fs_rx_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    size_t prev_len = g_fs_ctx.rx_len;

    if (prev_len + len > BLE_FS_MAX_FRAME_SIZE) {
        FMRB_LOGW(TAG, "BLE FS RX buffer overflow, resetting");
        g_fs_ctx.rx_len = 0;
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    os_mbuf_copydata(ctxt->om, 0, len, g_fs_ctx.rx_buffer + prev_len);
    g_fs_ctx.rx_len += len;

    // Scan for frame delimiter (0x00) in newly received data
    // COBS guarantees no 0x00 in encoded payload, so first 0x00 = delimiter
    for (size_t i = prev_len; i < g_fs_ctx.rx_len; i++) {
        if (g_fs_ctx.rx_buffer[i] == BLE_FRAMING_DELIM) {
            // Frame data is rx_buffer[0..i-1], delimiter at i
            g_fs_ctx.rx_len = i;
            g_fs_ctx.frame_ready = true;
            fmrb_semaphore_give(g_fs_ctx.rx_sem);
            break;
        }
    }

    return 0;
}

// TX: notification characteristic (no direct read/write access needed)
static int gatt_chr_fs_tx_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

// Debug RX: host writes COBS-encoded frame data here (may span several writes).
// Runs on the NimBLE host task, so it only accumulates bytes and posts the
// finished frame; COBS/CRC verification happens on the debugd task.
static int gatt_chr_dbg_rx_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    // The debug transport has not registered yet (boot order, or the debugger
    // is not built in). Drop silently; the host retries.
    if (!g_dbg_free_q || !g_dbg_ready_q) {
        return 0;
    }

    if (g_dbg_rx.fill_idx < 0) {
        ble_dbg_frame_ref_t got;
        if (fmrb_queue_receive(g_dbg_free_q, &got, 0) != FMRB_TRUE) {
            FMRB_LOGW(TAG, "BLE debug RX has no free buffer, dropping write");
            return 0;
        }
        g_dbg_rx.fill_idx = (int8_t)got.idx;
        g_dbg_rx.fill_len = 0;
    }

    uint8_t *dst = g_dbg_rx.buf[g_dbg_rx.fill_idx];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    if ((size_t)g_dbg_rx.fill_len + len > BLE_DBG_MAX_ENC) {
        FMRB_LOGW(TAG, "BLE debug RX buffer overflow, resetting");
        g_dbg_rx.fill_len = 0;
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    os_mbuf_copydata(ctxt->om, 0, len, dst + g_dbg_rx.fill_len);
    uint16_t prev_len = g_dbg_rx.fill_len;
    g_dbg_rx.fill_len += len;

    // COBS guarantees no 0x00 inside the encoded payload, so the first 0x00
    // ends the frame. Anything the host appended after it is discarded: the
    // debugger writes one frame per request.
    for (uint16_t i = prev_len; i < g_dbg_rx.fill_len; i++) {
        if (dst[i] != BLE_FRAMING_DELIM) {
            continue;
        }
        ble_dbg_frame_ref_t ref = { .idx = (uint8_t)g_dbg_rx.fill_idx, .len = i };
        if (fmrb_queue_send(g_dbg_ready_q, &ref, 0) != FMRB_TRUE) {
            FMRB_LOGW(TAG, "BLE debug ready queue full, dropping frame");
            fmrb_queue_send(g_dbg_free_q, &ref, 0);
        }
        g_dbg_rx.fill_idx = -1;
        g_dbg_rx.fill_len = 0;
        break;
    }

    return 0;
}

// Debug TX: notification characteristic (no direct read/write access needed)
static int gatt_chr_dbg_tx_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_fmrb_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_chr_device_info_uuid.u,
                .access_cb = gatt_chr_device_info_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &gatt_chr_fs_rx_uuid.u,
                .access_cb = gatt_chr_fs_rx_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &gatt_chr_fs_tx_uuid.u,
                .access_cb = gatt_chr_fs_tx_cb,
                .val_handle = &g_fs_tx_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_dbg_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_chr_dbg_rx_uuid.u,
                .access_cb = gatt_chr_dbg_rx_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &gatt_chr_dbg_tx_uuid.u,
                .access_cb = gatt_chr_dbg_tx_cb,
                .val_handle = &g_dbg_tx_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    {
        0,
    },
};

// ============================================================
// GAP event handling
// ============================================================

static void ble_advertise_if_needed(void)
{
    if (g_ble_advertising) {
        FMRB_LOGI(TAG, "BLE advertising already active");
        return;
    }
    ble_advertise();
}

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        FMRB_LOGE(TAG, "Failed to set adv fields: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        FMRB_LOGE(TAG, "Failed to start advertising: %d", rc);
        return;
    }

    g_ble_advertising = true;
    FMRB_LOGI(TAG, "BLE advertising started");
}

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            FMRB_LOGI(TAG, "BLE connection established (handle=%d)",
                       event->connect.conn_handle);
            g_ble_advertising = false;
            g_conn_handle = event->connect.conn_handle;
            g_connected = true;
            // No security_initiate here: characteristics are unencrypted, so
            // forcing pairing on connect deadlocks Web Bluetooth on Windows
            // (Chrome/Edge surface the OS pair dialog only via toast and the
            // GATT promise hangs if the user does not act on it).
        } else {
            FMRB_LOGE(TAG, "BLE connection failed (status=%d)", event->connect.status);
            g_ble_advertising = false;
            g_conn_handle = 0;
            g_connected = false;
            ble_advertise_if_needed();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        FMRB_LOGI(TAG, "BLE disconnected (reason=%d)", event->disconnect.reason);
        g_ble_advertising = false;
        g_conn_handle = 0;
        g_connected = false;
        g_tx_subscribed = false;
        // Reset FS receive state
        g_fs_ctx.rx_len = 0;
        g_fs_ctx.frame_ready = false;
        // Stop log streaming so we don't try to notify a gone peer
        g_fs_ctx.log_subscribed = false;
        // Reset debug receive state, returning any half-filled buffer.
        g_dbg_tx_subscribed = false;
        if (g_dbg_rx.fill_idx >= 0 && g_dbg_free_q) {
            ble_dbg_frame_ref_t ref = { .idx = (uint8_t)g_dbg_rx.fill_idx, .len = 0 };
            fmrb_queue_send(g_dbg_free_q, &ref, 0);
        }
        g_dbg_rx.fill_idx = -1;
        g_dbg_rx.fill_len = 0;
        // Drop frames that arrived but were never decoded. debugd does not
        // call the transport's close_client, so without this a request from
        // the old session would be executed after the next client connects.
        if (g_dbg_ready_q && g_dbg_free_q) {
            ble_dbg_frame_ref_t stale;
            while (fmrb_queue_receive(g_dbg_ready_q, &stale, 0) == FMRB_TRUE) {
                fmrb_queue_send(g_dbg_free_q, &stale, 0);
            }
        }
        ble_advertise_if_needed();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        FMRB_LOGI(TAG, "BLE advertising complete");
        g_ble_advertising = false;
        ble_advertise();
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        FMRB_LOGI(TAG, "BLE connection updated (status=%d)",
                   event->conn_update.status);
        break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        FMRB_LOGI(TAG, "BLE connection update request");
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        FMRB_LOGI(TAG, "BLE passkey action: %d", event->passkey.params.action);
        break;

    case BLE_GAP_EVENT_MTU:
        FMRB_LOGI(TAG, "BLE MTU updated: %d (handle=%d)",
                   event->mtu.value, event->mtu.conn_handle);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        FMRB_LOGI(TAG, "BLE encryption change (status=%d)",
                   event->enc_change.status);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        FMRB_LOGI(TAG, "BLE subscribe: handle=%d, notify=%d",
                   event->subscribe.attr_handle, event->subscribe.cur_notify);
        if (event->subscribe.attr_handle == g_fs_tx_val_handle) {
            g_tx_subscribed = (event->subscribe.cur_notify != 0);
            FMRB_LOGI(TAG, "FS TX notifications %s",
                       g_tx_subscribed ? "enabled" : "disabled");
        } else if (event->subscribe.attr_handle == g_dbg_tx_val_handle) {
            g_dbg_tx_subscribed = (event->subscribe.cur_notify != 0);
            FMRB_LOGI(TAG, "Debug TX notifications %s",
                       g_dbg_tx_subscribed ? "enabled" : "disabled");
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        FMRB_LOGD(TAG, "BLE GAP event: %d", event->type);
        break;
    }
    return 0;
}

// ============================================================
// BLE notification sender (fragments response into MTU-sized chunks)
// ============================================================

// Shared by the file service and the debug service. Callers run on different
// tasks (ble_fs / debugd), which is safe: the NimBLE API is thread safe and
// each call builds its own mbuf.
static fmrb_err_t ble_send_notify(uint16_t val_handle, bool subscribed,
                                  const uint8_t *data, size_t len)
{
    if (!g_connected || !subscribed) {
        FMRB_LOGW(TAG, "Cannot send: connected=%d, subscribed=%d",
                   g_connected, subscribed);
        return FMRB_ERR_INVALID_STATE;
    }

    uint16_t mtu = ble_att_mtu(g_conn_handle);
    uint16_t chunk_size = (mtu > 3) ? (mtu - 3) : 20;

    size_t offset = 0;
    while (offset < len) {
        size_t to_send = len - offset;
        if (to_send > chunk_size) {
            to_send = chunk_size;
        }

        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + offset, to_send);
        if (!om) {
            FMRB_LOGE(TAG, "Failed to allocate mbuf for notification");
            return FMRB_ERR_NO_MEMORY;
        }

        int rc = ble_gatts_notify_custom(g_conn_handle, val_handle, om);
        if (rc != 0) {
            FMRB_LOGW(TAG, "Notification send failed: %d, retrying...", rc);
            fmrb_task_delay(FMRB_MS_TO_TICKS(20));
            om = ble_hs_mbuf_from_flat(data + offset, to_send);
            if (!om) return FMRB_ERR_NO_MEMORY;
            rc = ble_gatts_notify_custom(g_conn_handle, val_handle, om);
            if (rc != 0) {
                FMRB_LOGE(TAG, "Notification retry failed: %d", rc);
                return FMRB_ERR_FAILED;
            }
        }

        offset += to_send;
        if (offset < len) {
            fmrb_task_delay(FMRB_MS_TO_TICKS(5));
        }
    }

    return FMRB_OK;
}

// ============================================================
// Debug service link (see ble_debug_link.h)
// ============================================================

fmrb_err_t ble_debug_register(fmrb_queue_t free_q, fmrb_queue_t ready_q)
{
    if (!free_q || !ready_q) {
        return FMRB_ERR_INVALID_PARAM;
    }
    g_dbg_rx.fill_idx = -1;
    g_dbg_rx.fill_len = 0;
    // Publish the queues last: the access callback treats a NULL handle as
    // "not registered" and drops writes, so it never sees a half-set pair.
    g_dbg_free_q = free_q;
    g_dbg_ready_q = ready_q;
    FMRB_LOGI(TAG, "BLE debug transport registered");
    return FMRB_OK;
}

fmrb_err_t ble_debug_send(const uint8_t *encoded, size_t len)
{
    return ble_send_notify(g_dbg_tx_val_handle, g_dbg_tx_subscribed, encoded, len);
}

bool ble_debug_link_ready(void)
{
    return g_connected && g_dbg_tx_subscribed;
}

const uint8_t *ble_debug_rx_buf(uint8_t idx)
{
    if (idx >= BLE_DBG_RX_BUF_COUNT) {
        return NULL;
    }
    return g_dbg_rx.buf[idx];
}

// Send a COBS-encoded response frame + delimiter via BLE notifications
static void ble_fs_send_response(const char *json_response,
                                 const uint8_t *binary_data, size_t binary_size)
{
    EXT_RAM_BSS_ATTR static uint8_t packet[BLE_FS_MAX_FRAME_SIZE];
    EXT_RAM_BSS_ATTR static uint8_t encoded[BLE_FS_MAX_FRAME_SIZE];

    size_t json_len = strlen(json_response);
    size_t pos = 0;

    // Build packet: [resp_code][json_len_hi][json_len_lo][json][binary][crc32]
    packet[pos++] = BLE_FS_RESP;
    packet[pos++] = (json_len >> 8) & 0xFF;
    packet[pos++] = json_len & 0xFF;

    memcpy(packet + pos, json_response, json_len);
    pos += json_len;

    if (binary_data && binary_size > 0) {
        memcpy(packet + pos, binary_data, binary_size);
        pos += binary_size;
    }

    uint32_t crc = ble_framing_crc32(packet, pos);
    packet[pos++] = (crc >> 24) & 0xFF;
    packet[pos++] = (crc >> 16) & 0xFF;
    packet[pos++] = (crc >> 8) & 0xFF;
    packet[pos++] = crc & 0xFF;

    // COBS encode
    size_t encoded_len = ble_framing_cobs_encode(packet, pos, encoded, sizeof(encoded));
    if (encoded_len == 0) {
        FMRB_LOGE(TAG, "COBS encode failed");
        return;
    }

    // Append delimiter
    if (encoded_len < sizeof(encoded)) {
        encoded[encoded_len++] = BLE_FRAMING_DELIM;
    }

    FMRB_LOGD(TAG, "Sending BLE response: JSON len=%zu, binary=%zu, total=%zu",
              json_len, binary_size, encoded_len);

    ble_send_notify(g_fs_tx_val_handle, g_tx_subscribed, encoded, encoded_len);
}

// ============================================================
// File command handlers
// ============================================================

static void ble_fs_cmd_cd(const char *json_params, char *response, size_t response_size)
{
    char path[BLE_FS_MAX_PATH_LEN];

    if (!json_get_string(json_params, "path", path, sizeof(path))) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Missing path parameter\"}");
        return;
    }

    fmrb_dir_t dir;
    fmrb_err_t err = fmrb_hal_file_opendir(path, &dir);
    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Directory not found\"}");
        return;
    }
    fmrb_hal_file_closedir(dir);

    strncpy(g_fs_ctx.current_dir, path, sizeof(g_fs_ctx.current_dir) - 1);
    g_fs_ctx.current_dir[sizeof(g_fs_ctx.current_dir) - 1] = '\0';

    snprintf(response, response_size, "{\"ok\":true}");
}

static void ble_fs_cmd_ls(const char *json_params, char *response, size_t response_size)
{
    char path[BLE_FS_MAX_PATH_LEN];

    if (!json_get_string(json_params, "path", path, sizeof(path))) {
        strncpy(path, g_fs_ctx.current_dir, sizeof(path));
    }

    fmrb_dir_t dir;
    fmrb_err_t err = fmrb_hal_file_opendir(path, &dir);
    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Cannot open directory\"}");
        return;
    }

    int pos = snprintf(response, response_size, "{\"ok\":true,\"entries\":[");
    bool first = true;

    fmrb_file_info_t info;
    while (fmrb_hal_file_readdir(dir, &info) == FMRB_OK) {
        if (info.name[0] == '\0') break;

        if (!first) {
            pos += snprintf(response + pos, response_size - pos, ",");
        }
        first = false;

        const char *type = (info.is_dir) ? "d" : "f";
        pos += snprintf(response + pos, response_size - pos,
                       "{\"n\":\"%s\",\"t\":\"%s\",\"s\":%lu}",
                       info.name, type, (unsigned long)info.size);

        if (pos >= (int)response_size - 100) break;
    }

    fmrb_hal_file_closedir(dir);
    snprintf(response + pos, response_size - pos, "]}");
}

static void ble_fs_cmd_rm(const char *json_params, char *response, size_t response_size)
{
    char path[BLE_FS_MAX_PATH_LEN];

    if (!json_get_string(json_params, "path", path, sizeof(path))) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Missing path parameter\"}");
        return;
    }

    fmrb_file_info_t info;
    fmrb_err_t err = fmrb_hal_file_stat(path, &info);
    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"File not found\"}");
        return;
    }

    if (info.is_dir) {
        err = fmrb_hal_file_rmdir(path);
    } else {
        err = fmrb_hal_file_remove(path);
    }

    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Failed to remove\"}");
        return;
    }

    snprintf(response, response_size, "{\"ok\":true}");
}

static void ble_fs_cmd_mkdir(const char *json_params, char *response, size_t response_size)
{
    char path[BLE_FS_MAX_PATH_LEN];

    if (!json_get_string(json_params, "path", path, sizeof(path))) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Missing path parameter\"}");
        return;
    }

    fmrb_err_t err = fmrb_hal_file_mkdir(path);
    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"mkdir failed\"}");
        return;
    }

    snprintf(response, response_size, "{\"ok\":true}");
}

static void ble_fs_cmd_rename(const char *json_params, char *response, size_t response_size)
{
    char from[BLE_FS_MAX_PATH_LEN];
    char to[BLE_FS_MAX_PATH_LEN];

    if (!json_get_string(json_params, "from", from, sizeof(from))) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Missing from parameter\"}");
        return;
    }
    if (!json_get_string(json_params, "to", to, sizeof(to))) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Missing to parameter\"}");
        return;
    }

    fmrb_err_t err = fmrb_hal_file_rename(from, to);
    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"rename failed\"}");
        return;
    }

    snprintf(response, response_size, "{\"ok\":true}");
}

// ============================================================
// Log streaming commands
// ============================================================

static bool log_level_valid(char level)
{
    return level == 'E' || level == 'W' || level == 'I' || level == 'D';
}

static void ble_fs_cmd_log_subscribe(const char *json_params, char *response, size_t response_size)
{
    int32_t period = BLE_LOG_DEFAULT_PERIOD_MS;
    int32_t max_lines = BLE_LOG_DEFAULT_MAX_LINES;
    char level_str[8] = {0};

    json_get_int(json_params, "period", &period);
    json_get_int(json_params, "max_lines", &max_lines);

    if (period < BLE_LOG_MIN_PERIOD_MS) period = BLE_LOG_MIN_PERIOD_MS;
    if (period > BLE_LOG_MAX_PERIOD_MS) period = BLE_LOG_MAX_PERIOD_MS;
    if (max_lines < 1) max_lines = 1;
    if (max_lines > BLE_LOG_MAX_MAX_LINES) max_lines = BLE_LOG_MAX_MAX_LINES;

    if (json_get_string(json_params, "level", level_str, sizeof(level_str)) &&
        log_level_valid(level_str[0])) {
        fmrb_log_buffer_set_level(level_str[0]);
    }

    g_fs_ctx.log_period_ms = (uint16_t)period;
    g_fs_ctx.log_max_lines = (uint16_t)max_lines;
    g_fs_ctx.log_read_pos  = fmrb_log_buffer_get_write_pos();
    g_fs_ctx.log_seq       = 0;
    g_fs_ctx.log_subscribed = true;

    snprintf(response, response_size,
             "{\"ok\":true,\"start_pos\":%lu,\"period\":%u,\"max_lines\":%u,\"level\":\"%c\"}",
             (unsigned long)g_fs_ctx.log_read_pos,
             g_fs_ctx.log_period_ms,
             g_fs_ctx.log_max_lines,
             fmrb_log_buffer_get_level());
}

static void ble_fs_cmd_log_unsubscribe(char *response, size_t response_size)
{
    g_fs_ctx.log_subscribed = false;
    snprintf(response, response_size, "{\"ok\":true}");
}

static void ble_fs_cmd_log_set_level(const char *json_params, char *response, size_t response_size)
{
    char level_str[8] = {0};

    if (!json_get_string(json_params, "level", level_str, sizeof(level_str)) ||
        !log_level_valid(level_str[0])) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Invalid level\"}");
        return;
    }

    fmrb_log_buffer_set_level(level_str[0]);
    snprintf(response, response_size, "{\"ok\":true,\"level\":\"%c\"}", level_str[0]);
}

// Poll the log ring buffer; if new lines exist, push one event frame.
// Called from ble_fs_task_func (same task as frame handler), so no extra
// locking is required against ble_fs_send_response's static buffers.
static void ble_fs_poll_logs(void)
{
    if (!g_fs_ctx.log_subscribed) return;
    if (!g_connected || !g_tx_subscribed) return;

    uint32_t wp = fmrb_log_buffer_get_write_pos();
    if (wp == g_fs_ctx.log_read_pos) return;

    EXT_RAM_BSS_ATTR static char line_buf[BLE_LOG_LINE_BUF_SIZE];
    EXT_RAM_BSS_ATTR static char json_hdr[96];

    uint32_t before_pos = g_fs_ctx.log_read_pos;
    int n = fmrb_log_buffer_read_lines(line_buf, sizeof(line_buf),
                                       g_fs_ctx.log_max_lines,
                                       &g_fs_ctx.log_read_pos);

    // Detect ring overrun: fmrb_log_buffer_read_lines silently clamps
    // read_pos forward when oldest_pos overtook it. The expected ring
    // advance for n returned lines is bin_len + 2*n (each entry: u16 len
    // + text), and out_buf adds one '\n' per line so bin_len = sum(text)+n,
    // giving expected_advance = bin_len + n. A larger actual advance means
    // some entries were skipped.
    size_t bin_len = (n > 0) ? strlen(line_buf) : 0;
    uint32_t actual_advance = g_fs_ctx.log_read_pos - before_pos;
    uint32_t expected_advance = (uint32_t)(bin_len + (size_t)n);
    bool dropped = (actual_advance > expected_advance);

    if (n <= 0 && !dropped) return;

    int hdr_len;
    if (dropped) {
        hdr_len = snprintf(json_hdr, sizeof(json_hdr),
                           "{\"evt\":\"log\",\"seq\":%lu,\"bin\":%zu,\"dropped\":true}",
                           (unsigned long)g_fs_ctx.log_seq, bin_len);
    } else {
        hdr_len = snprintf(json_hdr, sizeof(json_hdr),
                           "{\"evt\":\"log\",\"seq\":%lu,\"bin\":%zu}",
                           (unsigned long)g_fs_ctx.log_seq, bin_len);
    }
    if (hdr_len <= 0) return;

    g_fs_ctx.log_seq++;
    ble_fs_send_response(json_hdr, (const uint8_t *)line_buf, bin_len);
}

static void ble_fs_cmd_statfs(const char *json_params, char *response, size_t response_size)
{
    char path[BLE_FS_MAX_PATH_LEN];

    if (!json_get_string(json_params, "path", path, sizeof(path))) {
        strncpy(path, "/", sizeof(path));
    }

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    fmrb_err_t err = fmrb_hal_file_statfs(path, &total_bytes, &free_bytes);
    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"statfs failed\"}");
        return;
    }

    snprintf(response, response_size,
             "{\"ok\":true,\"total\":%llu,\"free\":%llu}",
             (unsigned long long)total_bytes,
             (unsigned long long)free_bytes);
}

static void ble_fs_cmd_get(const char *json_params, char *response, size_t response_size,
                           uint8_t *binary_data, size_t *binary_size, size_t binary_max)
{
    char path[BLE_FS_MAX_PATH_LEN];
    int32_t offset = 0;

    if (!json_get_string(json_params, "path", path, sizeof(path))) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Missing path parameter\"}");
        *binary_size = 0;
        return;
    }

    json_get_int(json_params, "off", &offset);

    fmrb_file_t file;
    fmrb_err_t err = fmrb_hal_file_open(path, FMRB_O_RDONLY, &file);
    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Cannot open file\"}");
        *binary_size = 0;
        return;
    }

    if (offset > 0) {
        err = fmrb_hal_file_seek(file, offset, FMRB_SEEK_SET);
        if (err != FMRB_OK) {
            fmrb_hal_file_close(file);
            snprintf(response, response_size, "{\"ok\":false,\"err\":\"Seek failed\"}");
            *binary_size = 0;
            return;
        }
    }

    size_t bytes_read = 0;
    err = fmrb_hal_file_read(file, binary_data, binary_max, &bytes_read);
    fmrb_hal_file_close(file);

    if (err != FMRB_OK && bytes_read == 0) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Read failed\"}");
        *binary_size = 0;
        return;
    }

    *binary_size = bytes_read;
    bool eof = (bytes_read == 0 || bytes_read < binary_max);
    snprintf(response, response_size, "{\"ok\":true,\"eof\":%s,\"bin\":%zu}",
             eof ? "true" : "false", bytes_read);

    FMRB_LOGD(TAG, "BLE GET: path=%s, off=%d, read=%zu, eof=%s",
              path, offset, bytes_read, eof ? "true" : "false");
}

static void ble_fs_cmd_put(const char *json_params,
                           const uint8_t *binary_data, size_t binary_size,
                           char *response, size_t response_size)
{
    char path[BLE_FS_MAX_PATH_LEN];
    int32_t offset = 0;

    if (!json_get_string(json_params, "path", path, sizeof(path))) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Missing path parameter\"}");
        return;
    }

    json_get_int(json_params, "off", &offset);

    fmrb_file_t file;
    fmrb_err_t err;

    if (offset == 0) {
        err = fmrb_hal_file_open(path, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &file);
    } else {
        err = fmrb_hal_file_open(path, FMRB_O_WRONLY, &file);
    }

    if (err != FMRB_OK) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Cannot open file\"}");
        return;
    }

    if (offset > 0) {
        err = fmrb_hal_file_seek(file, offset, FMRB_SEEK_SET);
        if (err != FMRB_OK) {
            fmrb_hal_file_close(file);
            snprintf(response, response_size, "{\"ok\":false,\"err\":\"Seek failed\"}");
            return;
        }
    }

    size_t bytes_written = 0;
    err = fmrb_hal_file_write(file, binary_data, binary_size, &bytes_written);
    fmrb_hal_file_close(file);

    if (err != FMRB_OK || bytes_written != binary_size) {
        snprintf(response, response_size, "{\"ok\":false,\"err\":\"Write failed\"}");
        return;
    }

    snprintf(response, response_size, "{\"ok\":true}");
}

// ============================================================
// Frame processing
// ============================================================

static void ble_fs_process_frame(const uint8_t *frame, size_t frame_len)
{
    EXT_RAM_BSS_ATTR static uint8_t decoded[BLE_FS_MAX_FRAME_SIZE];
    EXT_RAM_BSS_ATTR static uint8_t binary_buffer[BLE_FS_MAX_CHUNK_SIZE];
    EXT_RAM_BSS_ATTR static char json_response[BLE_FS_MAX_JSON_LEN];

    size_t decoded_len = ble_framing_cobs_decode(frame, frame_len, decoded, sizeof(decoded));
    if (decoded_len < 7) {
        ble_fs_send_response("{\"ok\":false,\"err\":\"Frame too short\"}", NULL, 0);
        return;
    }

    // Verify CRC32
    uint32_t received_crc = ((uint32_t)decoded[decoded_len - 4] << 24) |
                           ((uint32_t)decoded[decoded_len - 3] << 16) |
                           ((uint32_t)decoded[decoded_len - 2] << 8) |
                           ((uint32_t)decoded[decoded_len - 1]);

    uint32_t calculated_crc = ble_framing_crc32(decoded, decoded_len - 4);
    if (received_crc != calculated_crc) {
        FMRB_LOGW(TAG, "CRC mismatch: received=0x%08lx, calc=0x%08lx",
                   (unsigned long)received_crc, (unsigned long)calculated_crc);
        ble_fs_send_response("{\"ok\":false,\"err\":\"CRC mismatch\"}", NULL, 0);
        return;
    }

    uint8_t cmd = decoded[0];
    uint16_t json_len = ((uint16_t)decoded[1] << 8) | decoded[2];

    if (3 + json_len + 4 > decoded_len) {
        ble_fs_send_response("{\"ok\":false,\"err\":\"Invalid length\"}", NULL, 0);
        return;
    }

    char json_params[BLE_FS_MAX_JSON_LEN];
    if (json_len >= sizeof(json_params)) {
        ble_fs_send_response("{\"ok\":false,\"err\":\"JSON too large\"}", NULL, 0);
        return;
    }
    memcpy(json_params, decoded + 3, json_len);
    json_params[json_len] = '\0';

    const uint8_t *binary_data = decoded + 3 + json_len;
    size_t binary_size = decoded_len - 4 - 3 - json_len;

    FMRB_LOGD(TAG, "BLE FS CMD=0x%02x, JSON len=%u, binary=%zu",
              cmd, json_len, binary_size);

    fmrb_semaphore_take(g_fs_ctx.mutex, FMRB_TICK_MAX);

    size_t response_binary_size = 0;

    switch (cmd) {
    case BLE_FS_CMD_CD:
        ble_fs_cmd_cd(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_LS:
        ble_fs_cmd_ls(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_RM:
        ble_fs_cmd_rm(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_MKDIR:
        ble_fs_cmd_mkdir(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_RENAME:
        ble_fs_cmd_rename(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_STATFS:
        ble_fs_cmd_statfs(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_GET:
        ble_fs_cmd_get(json_params, json_response, sizeof(json_response),
                       binary_buffer, &response_binary_size, sizeof(binary_buffer));
        ble_fs_send_response(json_response, binary_buffer, response_binary_size);
        break;

    case BLE_FS_CMD_PUT:
        ble_fs_cmd_put(json_params, binary_data, binary_size,
                       json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_LOG_SUBSCRIBE:
        ble_fs_cmd_log_subscribe(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_LOG_UNSUBSCRIBE:
        ble_fs_cmd_log_unsubscribe(json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    case BLE_FS_CMD_LOG_SET_LEVEL:
        ble_fs_cmd_log_set_level(json_params, json_response, sizeof(json_response));
        ble_fs_send_response(json_response, NULL, 0);
        break;

    default:
        snprintf(json_response, sizeof(json_response),
                 "{\"ok\":false,\"err\":\"Unknown command 0x%02x\"}", cmd);
        ble_fs_send_response(json_response, NULL, 0);
        break;
    }

    fmrb_semaphore_give(g_fs_ctx.mutex);
}

// ============================================================
// BLE file service processing task
// ============================================================

static void ble_fs_task_func(void *arg)
{
    FMRB_LOGI(TAG, "BLE file service task started");

    strncpy(g_fs_ctx.current_dir, "/", sizeof(g_fs_ctx.current_dir));

    int diag_counter = 0;
    int last_synced = -1;
    int last_enabled = -1;
    bool last_advertising = false;
    bool last_connected = false;
    while (1) {
        // Periodic diagnostic: log NimBLE state transitions
        diag_counter++;
        if (diag_counter >= 1) {
            diag_counter = 0;
            int synced = ble_hs_synced();
            int enabled = ble_hs_is_enabled();
            if (synced != last_synced || enabled != last_enabled ||
                g_ble_advertising != last_advertising || g_connected != last_connected) {
                FMRB_LOGI(TAG, "BLE state: synced=%d enabled=%d adv=%d conn=%d",
                          synced, enabled, g_ble_advertising, g_connected);
                last_synced = synced;
                last_enabled = enabled;
                last_advertising = g_ble_advertising;
                last_connected = g_connected;
            }
        }

        // When a log subscription is active, shorten the wait so we can poll
        // the log ring buffer at the requested cadence. Otherwise idle 1s.
        uint32_t wait_ms = g_fs_ctx.log_subscribed ? g_fs_ctx.log_period_ms : 1000;
        bool got_frame = (fmrb_semaphore_take(g_fs_ctx.rx_sem,
                                              FMRB_MS_TO_TICKS(wait_ms)) == FMRB_TRUE);

        if (got_frame && g_fs_ctx.frame_ready) {
            // Copy frame data and release buffer for next receive
            EXT_RAM_BSS_ATTR static uint8_t frame_copy[BLE_FS_MAX_FRAME_SIZE];
            size_t frame_len = g_fs_ctx.rx_len;
            memcpy(frame_copy, g_fs_ctx.rx_buffer, frame_len);

            g_fs_ctx.rx_len = 0;
            g_fs_ctx.frame_ready = false;

            FMRB_LOGD(TAG, "Processing BLE FS frame (%zu bytes)", frame_len);
            ble_fs_process_frame(frame_copy, frame_len);
        }

        // Always poll the log ring after a wait (or after a frame). The
        // helper is a no-op when no subscription / no client is attached.
        ble_fs_poll_logs();
    }
}

// ============================================================
// NimBLE host callbacks
// ============================================================

static void ble_on_sync(void)
{
    int rc;

    FMRB_LOGI(TAG, "ble_on_sync: synced=%d", ble_hs_synced());

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        FMRB_LOGE(TAG, "Failed to ensure address: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        FMRB_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }

    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(g_own_addr_type, addr, NULL);
    FMRB_LOGI(TAG, "BLE address: %02x:%02x:%02x:%02x:%02x:%02x",
              addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    // Override the static device name with one that includes the last 3
    // bytes of the MAC, so multiple devices can be distinguished from a
    // browser's pairing dialog. Format: "Family-mruby-XXXXXX" (lowercase
    // hex, no separator). addr[] is little-endian (LSB first), so the last
    // 3 bytes of the printed MAC correspond to addr[2], addr[1], addr[0].
    char devname[32];
    snprintf(devname, sizeof(devname), "Family-mruby-%02x%02x%02x",
             addr[2], addr[1], addr[0]);
    rc = ble_svc_gap_device_name_set(devname);
    if (rc != 0) {
        FMRB_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
    } else {
        FMRB_LOGI(TAG, "BLE device name: %s", devname);
    }

    ble_advertise();
}

static void ble_on_reset(int reason)
{
    FMRB_LOGE(TAG, "BLE host reset: reason=%d", reason);
}

static void ble_host_task(void *param)
{
    FMRB_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ============================================================
// Public API
// ============================================================

fmrb_err_t ble_task_init(void)
{
    if (g_ble_initialized) {
        FMRB_LOGW(TAG, "BLE already initialized");
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Initializing BLE...");

    // NVS is required by NimBLE for storing bonding keys and RF calibration data
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

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
    // esp_hosted self-initializes from a constructor at startup. The SDIO
    // link to the C6 (slave reset via GPIO15 + handshake) is brought up
    // lazily by ble_transport_ll_init() inside nimble_port_init() below,
    // which blocks until the slave connects. Do NOT issue any esp_hosted
    // RPC before that point: the 1.4.0 pre-transport failure path
    // (uninitialized semaphore / double free in rpc_core) corrupts the
    // heap and crash-loops the system.
    FMRB_LOGI(TAG, "Connecting to C6 controller via esp_hosted (SDIO)...");
#endif

    int rc = nimble_port_init();
    if (rc != 0) {
        FMRB_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return FMRB_ERR_FAILED;
    }
    // M-1 breakdown: nimble_port_init covers the BT controller + NimBLE host
    // bring-up, the dominant share of BLE's internal-RAM cost.
    fmrb_mem_log_boot_snapshot("ble_nimble_port");

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
    // Transport is up now (nimble_port_init blocked on the slave
    // handshake), so RPC calls are safe. Log the C6 firmware version to
    // judge stock-firmware compatibility.
    esp_hosted_coprocessor_fwver_t cop_ver = {0};
    if (esp_hosted_get_coprocessor_fwversion(&cop_ver) == ESP_OK) {
        FMRB_LOGI(TAG, "C6 coprocessor fw: %u.%u.%u",
                  (unsigned)cop_ver.major1, (unsigned)cop_ver.minor1,
                  (unsigned)cop_ver.patch1);
    } else {
        FMRB_LOGW(TAG, "C6 fw version query failed (continuing)");
    }
#endif

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    // No pairing / bonding: characteristics carry no encryption flags, so the
    // GATT operations work over an unencrypted link. Enabling bonding causes
    // Windows BT stack to retain half-paired entries that deadlock subsequent
    // gatt.connect() attempts from Web Bluetooth (Chrome/Edge).
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    ble_store_config_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        FMRB_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        nimble_port_deinit();
        return FMRB_ERR_FAILED;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        FMRB_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        nimble_port_deinit();
        return FMRB_ERR_FAILED;
    }

    ble_svc_gap_device_name_set("FamilyMruby");

    // Initialize file service context
    memset(&g_fs_ctx, 0, sizeof(g_fs_ctx));
    g_fs_ctx.rx_sem = fmrb_semaphore_create_binary();
    g_fs_ctx.mutex = fmrb_semaphore_create_mutex();

    if (!g_fs_ctx.rx_sem || !g_fs_ctx.mutex) {
        FMRB_LOGE(TAG, "Failed to create FS semaphores");
        nimble_port_deinit();
        return FMRB_ERR_NO_MEMORY;
    }

    // Start file service processing task
    fmrb_base_type_t result = fmrb_task_create_ex(
        ble_fs_task_func,
        "ble_fs",
        FMRB_BLE_FS_TASK_STACK_SIZE,
        NULL,
        FMRB_BLE_FS_TASK_PRIORITY,
        &g_fs_task_handle,
        FMRB_BLE_FS_TASK_FLAGS
    );

    if (result != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create BLE FS task");
        fmrb_semaphore_delete(g_fs_ctx.rx_sem);
        fmrb_semaphore_delete(g_fs_ctx.mutex);
        nimble_port_deinit();
        return FMRB_ERR_FAILED;
    }

    nimble_port_freertos_init(ble_host_task);

    // Boost NimBLE host task priority above fmrb_host (priority 10) so that
    // core-0 UART blocking in fmrb_host cannot starve the BLE event loop and
    // let the host sync state silently degrade. NimBLE port creates the task
    // named "nimble_host" via xTaskCreatePinnedToCore at default priority 4.
    // Retry briefly because nimble_port_freertos_init is async.
    {
        TaskHandle_t nimble_host_handle = NULL;
        for (int i = 0; i < 50; i++) {
            nimble_host_handle = xTaskGetHandle("nimble_host");
            if (nimble_host_handle) break;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (nimble_host_handle) {
            vTaskPrioritySet(nimble_host_handle, 11);
            FMRB_LOGI(TAG, "NimBLE host task priority raised to 11");
        } else {
            FMRB_LOGW(TAG, "Failed to get nimble_host handle; priority not adjusted");
        }
    }

    g_ble_initialized = true;
    FMRB_LOGI(TAG, "BLE initialized (with file service)");
    fmrb_mem_log_boot_snapshot("ble_ready");

    return FMRB_OK;
}

// One-shot init runner for ble_service_start: neither the boot task nor the
// desktop task should carry the NimBLE init frames on its own stack.
static void ble_start_task_func(void *arg)
{
    (void)arg;
    if (ble_task_init() != FMRB_OK) {
        FMRB_LOGW(TAG, "BLE start failed");
    }
    g_ble_start_pending = false;
    fmrb_task_delete_ex(NULL);
}

fmrb_err_t ble_service_start(void)
{
    if (g_ble_initialized) {
        FMRB_LOGI(TAG, "BLE already running");
        return FMRB_OK;
    }
    // Single admission gate: boot (ble_auto_start) and the desktop menu can
    // both call this; only one init task may ever run.
    if (__atomic_test_and_set(&g_ble_start_pending, __ATOMIC_SEQ_CST)) {
        FMRB_LOGI(TAG, "BLE start already in progress");
        return FMRB_OK;
    }
    if (fmrb_task_create(ble_start_task_func, "ble_start", 6144, NULL,
                         4, NULL) != FMRB_PASS) {
        g_ble_start_pending = false;
        FMRB_LOGE(TAG, "Failed to spawn BLE start task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

fmrb_err_t ble_task_deinit(void)
{
    if (!g_ble_initialized) {
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Deinitializing BLE...");

    // Stop file service task
    if (g_fs_task_handle) {
        fmrb_task_delete(g_fs_task_handle);
        g_fs_task_handle = NULL;
    }

    if (g_fs_ctx.rx_sem) {
        fmrb_semaphore_delete(g_fs_ctx.rx_sem);
        g_fs_ctx.rx_sem = NULL;
    }

    if (g_fs_ctx.mutex) {
        fmrb_semaphore_delete(g_fs_ctx.mutex);
        g_fs_ctx.mutex = NULL;
    }

    int rc = nimble_port_stop();
    if (rc != 0) {
        FMRB_LOGE(TAG, "nimble_port_stop failed: %d", rc);
        return FMRB_ERR_FAILED;
    }

    nimble_port_deinit();
    g_ble_initialized = false;
    FMRB_LOGI(TAG, "BLE deinitialized");
    return FMRB_OK;
}
