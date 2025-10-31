#include "fmrb_hal.h"
#include "fmrb_log.h"

static const char *TAG = "fmrb_hal";
static bool hal_initialized = false;

fmrb_err_t fmrb_hal_init(void) {
    if (hal_initialized) {
        FMRB_LOGW(TAG, "HAL already initialized");
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Initializing Family mruby HAL");

#ifdef FMRB_PLATFORM_LINUX
    FMRB_LOGI(TAG, "Platform: Linux");
#else
    FMRB_LOGI(TAG, "Platform: ESP32");
#endif

    // Initialize link communication subsystem
    fmrb_err_t ret = fmrb_hal_link_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize link communication: %d", ret);
        return ret;
    }
    FMRB_LOGI(TAG, "HAL link communication initialized");

    hal_initialized = true;
    FMRB_LOGI(TAG, "HAL initialization complete");
    return FMRB_OK;
}

void fmrb_hal_deinit(void) {
    if (!hal_initialized) {
        return;
    }

    FMRB_LOGI(TAG, "Deinitializing Family mruby HAL");

    // Deinitialize link communication subsystem
    fmrb_hal_link_deinit();

    hal_initialized = false;
    FMRB_LOGI(TAG, "HAL deinitialization complete");
}