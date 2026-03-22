#include <string.h>
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_task_config.h"
#include "fmrb_err.h"
#include "fmrb_hal.h"
#include "ble_task.h"

#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
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
static bool g_ble_advertising = false;
static uint8_t g_own_addr_type;
static uint16_t g_conn_handle;
static bool g_connected = false;
static bool g_tx_subscribed = false;

// ============================================================
// File service over BLE - protocol definitions
// ============================================================
// Same protocol as fs_proxy_task: COBS-encoded frames with CRC32
// Frame: COBS([cmd(1B)][json_len(2B BE)][json][binary][CRC32(4B)]) + 0x00

#define BLE_FS_DELIM          0x00
#define BLE_FS_MAX_FRAME_SIZE 4096
#define BLE_FS_MAX_PATH_LEN   256
#define BLE_FS_MAX_JSON_LEN   2048
#define BLE_FS_MAX_CHUNK_SIZE 2048

// Command codes (same as fs_proxy)
#define BLE_FS_CMD_CD   0x11
#define BLE_FS_CMD_LS   0x12
#define BLE_FS_CMD_RM   0x13
#define BLE_FS_CMD_GET  0x21
#define BLE_FS_CMD_PUT  0x22
#define BLE_FS_RESP     0x00

// File service context
typedef struct {
    uint8_t rx_buffer[BLE_FS_MAX_FRAME_SIZE];
    size_t rx_len;
    bool frame_ready;
    char current_dir[BLE_FS_MAX_PATH_LEN];
    fmrb_semaphore_t rx_sem;
    fmrb_semaphore_t mutex;
} ble_fs_context_t;

static ble_fs_context_t g_fs_ctx;
static fmrb_task_handle_t g_fs_task_handle;
static uint16_t g_fs_tx_val_handle;

// Forward declarations
static void ble_advertise(void);
static void ble_fs_task_func(void *arg);

// ============================================================
// CRC32 lookup table (standard polynomial 0xEDB88320)
// ============================================================
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// ============================================================
// Utility functions (COBS, CRC32, JSON)
// ============================================================

static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

static size_t cobs_encode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_size)
{
    if (input_len + 2 > output_size) {
        return 0;
    }

    size_t read_idx = 0;
    size_t write_idx = 1;
    size_t code_idx = 0;
    uint8_t code = 1;

    while (read_idx < input_len) {
        if (input[read_idx] == 0) {
            output[code_idx] = code;
            code = 1;
            code_idx = write_idx++;
            read_idx++;
        } else {
            output[write_idx++] = input[read_idx++];
            code++;
            if (code == 0xFF) {
                output[code_idx] = code;
                code = 1;
                code_idx = write_idx++;
            }
        }
    }

    output[code_idx] = code;
    return write_idx;
}

static size_t cobs_decode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_size)
{
    if (input_len == 0) {
        return 0;
    }

    size_t read_idx = 0;
    size_t write_idx = 0;

    while (read_idx < input_len) {
        uint8_t code = input[read_idx++];

        if (code == 0) {
            break;
        }

        for (uint8_t i = 1; i < code && read_idx < input_len && write_idx < output_size; i++) {
            output[write_idx++] = input[read_idx++];
        }

        if (code < 0xFF && write_idx < output_size && read_idx < input_len) {
            output[write_idx++] = 0;
        }
    }

    return write_idx;
}

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
        if (g_fs_ctx.rx_buffer[i] == BLE_FS_DELIM) {
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
            int sec_rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (sec_rc != 0) {
                FMRB_LOGW(TAG, "Security initiation failed: %d (non-fatal)", sec_rc);
            }
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

static fmrb_err_t ble_fs_send_notify(const uint8_t *data, size_t len)
{
    if (!g_connected || !g_tx_subscribed) {
        FMRB_LOGW(TAG, "Cannot send: connected=%d, subscribed=%d",
                   g_connected, g_tx_subscribed);
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

        int rc = ble_gatts_notify_custom(g_conn_handle, g_fs_tx_val_handle, om);
        if (rc != 0) {
            FMRB_LOGW(TAG, "Notification send failed: %d, retrying...", rc);
            fmrb_task_delay(FMRB_MS_TO_TICKS(20));
            om = ble_hs_mbuf_from_flat(data + offset, to_send);
            if (!om) return FMRB_ERR_NO_MEMORY;
            rc = ble_gatts_notify_custom(g_conn_handle, g_fs_tx_val_handle, om);
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

// Send a COBS-encoded response frame + delimiter via BLE notifications
static void ble_fs_send_response(const char *json_response,
                                 const uint8_t *binary_data, size_t binary_size)
{
    static uint8_t packet[BLE_FS_MAX_FRAME_SIZE];
    static uint8_t encoded[BLE_FS_MAX_FRAME_SIZE];

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

    uint32_t crc = crc32_calc(packet, pos);
    packet[pos++] = (crc >> 24) & 0xFF;
    packet[pos++] = (crc >> 16) & 0xFF;
    packet[pos++] = (crc >> 8) & 0xFF;
    packet[pos++] = crc & 0xFF;

    // COBS encode
    size_t encoded_len = cobs_encode(packet, pos, encoded, sizeof(encoded));
    if (encoded_len == 0) {
        FMRB_LOGE(TAG, "COBS encode failed");
        return;
    }

    // Append delimiter
    if (encoded_len < sizeof(encoded)) {
        encoded[encoded_len++] = BLE_FS_DELIM;
    }

    FMRB_LOGD(TAG, "Sending BLE response: JSON len=%zu, binary=%zu, total=%zu",
              json_len, binary_size, encoded_len);

    ble_fs_send_notify(encoded, encoded_len);
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
    static uint8_t decoded[BLE_FS_MAX_FRAME_SIZE];
    static uint8_t binary_buffer[BLE_FS_MAX_CHUNK_SIZE];
    static char json_response[BLE_FS_MAX_JSON_LEN];

    size_t decoded_len = cobs_decode(frame, frame_len, decoded, sizeof(decoded));
    if (decoded_len < 7) {
        ble_fs_send_response("{\"ok\":false,\"err\":\"Frame too short\"}", NULL, 0);
        return;
    }

    // Verify CRC32
    uint32_t received_crc = ((uint32_t)decoded[decoded_len - 4] << 24) |
                           ((uint32_t)decoded[decoded_len - 3] << 16) |
                           ((uint32_t)decoded[decoded_len - 2] << 8) |
                           ((uint32_t)decoded[decoded_len - 1]);

    uint32_t calculated_crc = crc32_calc(decoded, decoded_len - 4);
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

    while (1) {
        // Wait for a complete frame
        if (fmrb_semaphore_take(g_fs_ctx.rx_sem, FMRB_MS_TO_TICKS(1000)) != FMRB_TRUE) {
            continue;
        }

        if (!g_fs_ctx.frame_ready) {
            continue;
        }

        // Copy frame data and release buffer for next receive
        static uint8_t frame_copy[BLE_FS_MAX_FRAME_SIZE];
        size_t frame_len = g_fs_ctx.rx_len;
        memcpy(frame_copy, g_fs_ctx.rx_buffer, frame_len);

        g_fs_ctx.rx_len = 0;
        g_fs_ctx.frame_ready = false;

        FMRB_LOGD(TAG, "Processing BLE FS frame (%zu bytes)", frame_len);
        ble_fs_process_frame(frame_copy, frame_len);
    }
}

// ============================================================
// NimBLE host callbacks
// ============================================================

static void ble_on_sync(void)
{
    int rc;

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

    int rc = nimble_port_init();
    if (rc != 0) {
        FMRB_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return FMRB_ERR_FAILED;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

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

    g_ble_initialized = true;
    FMRB_LOGI(TAG, "BLE initialized (with file service)");
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
