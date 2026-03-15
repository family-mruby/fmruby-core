#include <string.h>
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_task_config.h"
#include "fmrb_err.h"
#include "ble_task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_task";

static bool g_ble_initialized = false;
static uint8_t g_own_addr_type;

// Custom 128-bit UUID for Family mruby File Service (placeholder for Phase 2)
static const ble_uuid128_t gatt_svr_svc_fmrb_uuid =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x45, 0x4c, 0x42, 0x59, 0x52, 0x42,
                     0x4d, 0x41, 0x52, 0x46);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_fmrb_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
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

    FMRB_LOGI(TAG, "BLE advertising started");
}

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        FMRB_LOGI(TAG, "BLE connection %s (handle=%d)",
                   event->connect.status == 0 ? "established" : "failed",
                   event->connect.conn_handle);
        if (event->connect.status != 0) {
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        FMRB_LOGI(TAG, "BLE disconnected (reason=%d)",
                   event->disconnect.reason);
        ble_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        FMRB_LOGI(TAG, "BLE advertising complete");
        ble_advertise();
        break;

    default:
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

    int rc = nimble_port_init();
    if (rc != 0) {
        FMRB_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return FMRB_ERR_FAILED;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

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
