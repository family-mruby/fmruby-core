#include <string.h>
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_task_config.h"
#include "fmrb_err.h"
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

static bool g_ble_initialized = false;
static bool g_ble_advertising = false;
static uint8_t g_own_addr_type;
static uint16_t g_conn_handle;

static void ble_advertise(void);

static void ble_advertise_if_needed(void)
{
    if (g_ble_advertising) {
        FMRB_LOGI(TAG, "BLE advertising already active");
        return;
    }

    ble_advertise();
}

// Custom 128-bit UUID for Family mruby File Service
static const ble_uuid128_t gatt_svr_svc_fmrb_uuid =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

// Device info characteristic UUID
static const ble_uuid128_t gatt_chr_device_info_uuid =
    BLE_UUID128_INIT(0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

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
            { 0 },
        },
    },
    {
        0,
    },
};

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
            // Initiate security (pairing) from peripheral side
            int sec_rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (sec_rc != 0) {
                FMRB_LOGW(TAG, "Security initiation failed: %d (non-fatal)", sec_rc);
            }
        } else {
            FMRB_LOGE(TAG, "BLE connection failed (status=%d, handle=%d)",
                       event->connect.status, event->connect.conn_handle);
            g_ble_advertising = false;
            g_conn_handle = 0;
            ble_advertise_if_needed();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        FMRB_LOGI(TAG, "BLE disconnected (reason=%d)",
                   event->disconnect.reason);
        g_ble_advertising = false;
        g_conn_handle = 0;
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
        // Accept connection parameter update request from central
        FMRB_LOGI(TAG, "BLE connection update request");
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        // Just Works pairing: no user interaction needed
        FMRB_LOGI(TAG, "BLE passkey action: %d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
            // No action required for Just Works
        }
        break;

    case BLE_GAP_EVENT_MTU:
        FMRB_LOGI(TAG, "BLE MTU updated: %d (handle=%d)",
                   event->mtu.value, event->mtu.conn_handle);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        FMRB_LOGI(TAG, "BLE encryption change (status=%d)",
                   event->enc_change.status);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // Delete old bond and allow re-pairing
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

    nimble_port_freertos_init(ble_host_task);

    g_ble_initialized = true;
    FMRB_LOGI(TAG, "BLE initialized");
    return FMRB_OK;
}

fmrb_err_t ble_task_deinit(void)
{
    if (!g_ble_initialized) {
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Deinitializing BLE...");

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
