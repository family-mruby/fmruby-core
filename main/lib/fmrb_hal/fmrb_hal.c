#include "fmrb_hal.h"
#include "esp_log.h"

static const char *TAG = "fmrb_hal";
static bool hal_initialized = false;

fmrb_err_t fmrb_hal_init(void) {
    if (hal_initialized) {
        ESP_LOGW(TAG, "HAL already initialized");
        return FMRB_OK;
    }

    ESP_LOGI(TAG, "Initializing Family mruby HAL");

#ifdef FMRB_PLATFORM_LINUX
    ESP_LOGI(TAG, "Platform: Linux");
#else
    ESP_LOGI(TAG, "Platform: ESP32");
#endif

    // Initialize link communication subsystem
    fmrb_err_t ret = fmrb_hal_link_init();
    if (ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to initialize link communication: %d", ret);
        return ret;
    }
    FMRB_LOGI(TAG, "HAL link communication initialized");

    // Initialize HAL message queue registry
    ret = fmrb_hal_msg_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize message queue: %d", ret);
        return -1;
    }
    FMRB_LOGI(TAG, "HAL message queue initialized");

    hal_initialized = true;
    ESP_LOGI(TAG, "HAL initialization complete");
    return FMRB_OK;
}

void fmrb_hal_deinit(void) {
    if (!hal_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing Family mruby HAL");

    // Deinitialize link communication subsystem
    fmrb_hal_link_deinit();

    hal_initialized = false;
    ESP_LOGI(TAG, "HAL deinitialization complete");
}