#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

// Family mruby modules
#include "lib/fmrb_hal/fmrb_hal.h"
#include "lib/fmrb_gfx/fmrb_gfx.h"
#include "lib/fmrb_audio/fmrb_audio.h"

// PicoRuby integration
#include "picoruby-esp32.h"

#include "boot.h"
#include "kernel/fmrb_kernel.h"
#include "kernel/system_task.h"

static const char *TAG = "boot";

// Global contexts
static fmrb_gfx_context_t g_gfx_context = NULL;

// Family mruby OS initialization
void fmrb_os_init(void)
{
    ESP_LOGI(TAG, "Initializing Family mruby OS...");

    // Initialize HAL layer
    fmrb_err_t hal_ret = fmrb_hal_init();
    if (hal_ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to initialize HAL: %d", hal_ret);
        return;
    }
    ESP_LOGI(TAG, "HAL initialized successfully");

    // Initialize Graphics subsystem
    fmrb_gfx_config_t gfx_config = {
        .screen_width = 480,
        .screen_height = 320,
        .bits_per_pixel = 8,
        .double_buffered = true
    };

    fmrb_gfx_err_t gfx_ret = fmrb_gfx_init(&gfx_config, &g_gfx_context);
    if (gfx_ret != FMRB_GFX_OK) {
        ESP_LOGE(TAG, "Failed to initialize Graphics: %d", gfx_ret);
    } else {
        ESP_LOGI(TAG, "Graphics initialized: %dx%d", gfx_config.screen_width, gfx_config.screen_height);

        // Test graphics with a simple clear
        fmrb_gfx_clear(g_gfx_context, FMRB_COLOR_BLUE);
        fmrb_gfx_draw_string(g_gfx_context, "Family mruby OS", 10, 10, FMRB_COLOR_WHITE);
        fmrb_gfx_present(g_gfx_context);
    }

    // Initialize Audio subsystem (APU emulator)
    fmrb_audio_err_t audio_ret = fmrb_audio_init();
    if (audio_ret != FMRB_AUDIO_OK) {
        ESP_LOGE(TAG, "Failed to initialize Audio: %d", audio_ret);
    } else {
        ESP_LOGI(TAG, "Audio subsystem (APU emulator) initialized");
    }


    // Create system task

    // Create kernel task

    // Create system apps

    // TODO: Initialize PicoRuby
    /*
    mrb_state *mrb = mrb_open();
    if (!mrb) {
        ESP_LOGE(TAG, "Failed to initialize PicoRuby");
        return;
    }

    // Initialize FMRB bindings
    mrb_picoruby_fmrb_init(mrb);

    ESP_LOGI(TAG, "PicoRuby initialized successfully");
    */

    ESP_LOGI(TAG, "Family mruby OS initialization complete");



}

