#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

// Family mruby modules
#include "lib/fmrb_hal/fmrb_hal.h"
#include "lib/fmrb_gfx/fmrb_gfx.h"
#include "lib/fmrb_audio/fmrb_audio.h"

// TODO: PicoRuby integration
// #include "picoruby.h"

static const char *TAG = "fmrb";

// Global contexts
static fmrb_gfx_context_t g_gfx_context = NULL;
static fmrb_audio_context_t g_audio_context = NULL;

// Family mruby OS initialization
static void fmrb_os_init(void)
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
        .screen_width = 320,
        .screen_height = 240,
        .bits_per_pixel = 16,
        .double_buffered = true
    };

    fmrb_gfx_err_t gfx_ret = fmrb_gfx_init(&gfx_config, &g_gfx_context);
    if (gfx_ret != FMRB_GFX_OK) {
        ESP_LOGE(TAG, "Failed to initialize Graphics: %d", gfx_ret);
    } else {
        ESP_LOGI(TAG, "Graphics initialized: %dx%d", gfx_config.screen_width, gfx_config.screen_height);

        // Test graphics with a simple clear
        fmrb_gfx_clear(g_gfx_context, FMRB_COLOR_BLUE);
        fmrb_gfx_draw_text(g_gfx_context, 10, 10, "Family mruby OS", FMRB_COLOR_WHITE, FMRB_FONT_SIZE_LARGE);
        fmrb_gfx_present(g_gfx_context);
    }

    // Initialize Audio subsystem
    fmrb_audio_config_t audio_config = {
        .sample_rate = 44100,
        .channels = 2,
        .format = FMRB_AUDIO_FORMAT_S16_LE,
        .buffer_size = 1024,
        .num_buffers = 4
    };

    fmrb_audio_err_t audio_ret = fmrb_audio_init(&audio_config, &g_audio_context);
    if (audio_ret != FMRB_AUDIO_OK) {
        ESP_LOGE(TAG, "Failed to initialize Audio: %d", audio_ret);
    } else {
        ESP_LOGI(TAG, "Audio initialized: %d Hz, %d ch", audio_config.sample_rate, audio_config.channels);
    }

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

// Family mruby OS main loop
static void fmrb_os_main_loop(void)
{
    ESP_LOGI(TAG, "Starting Family mruby OS main loop...");

    int frame_count = 0;
    const int target_fps = 60;
    const int frame_delay_ms = 1000 / target_fps;

    while (1) {
        fmrb_time_t frame_start = fmrb_hal_time_get_us();

        // Update graphics
        if (g_gfx_context) {
            // Simple animated display for demo
            fmrb_gfx_clear(g_gfx_context, FMRB_COLOR_BLACK);

            // Draw title
            fmrb_gfx_draw_text(g_gfx_context, 10, 10, "Family mruby OS", FMRB_COLOR_WHITE, FMRB_FONT_SIZE_LARGE);

            // Draw frame counter
            char frame_text[64];
            snprintf(frame_text, sizeof(frame_text), "Frame: %d", frame_count);
            fmrb_gfx_draw_text(g_gfx_context, 10, 40, frame_text, FMRB_COLOR_YELLOW, FMRB_FONT_SIZE_MEDIUM);

            // Draw animated rectangle
            int rect_x = 50 + (frame_count % 200);
            fmrb_rect_t rect = { rect_x, 80, 20, 20 };
            fmrb_gfx_fill_rect(g_gfx_context, &rect, FMRB_COLOR_RED);

            // Present the frame
            fmrb_gfx_present(g_gfx_context);
        }

        // TODO: Process mruby scripts
        /*
        // Execute Ruby scripts or process events
        mrb_value result = mrb_load_string(mrb, "puts 'Hello from Ruby!'");
        if (mrb->exc) {
            ESP_LOGE(TAG, "Ruby execution error");
            mrb->exc = NULL;
        }
        */

        frame_count++;

        // Frame rate limiting
        fmrb_time_t frame_end = fmrb_hal_time_get_us();
        int frame_time_ms = (frame_end - frame_start) / 1000;

        if (frame_time_ms < frame_delay_ms) {
            fmrb_hal_time_delay_ms(frame_delay_ms - frame_time_ms);
        }

#ifdef CONFIG_IDF_TARGET_LINUX
        // Linux版では100フレーム後に終了
        if (frame_count >= 100) {
            ESP_LOGI(TAG, "Linux demo complete after %d frames, exiting...", frame_count);
            break;
        }
#endif
    }
}

// Family mruby OS cleanup
static void fmrb_os_deinit(void)
{
    ESP_LOGI(TAG, "Shutting down Family mruby OS...");

    // TODO: Cleanup PicoRuby
    /*
    if (mrb) {
        mrb_close(mrb);
        ESP_LOGI(TAG, "PicoRuby deinitialized");
    }
    */

    // Cleanup Audio
    if (g_audio_context) {
        fmrb_audio_deinit(g_audio_context);
        g_audio_context = NULL;
        ESP_LOGI(TAG, "Audio deinitialized");
    }

    // Cleanup Graphics
    if (g_gfx_context) {
        fmrb_gfx_deinit(g_gfx_context);
        g_gfx_context = NULL;
        ESP_LOGI(TAG, "Graphics deinitialized");
    }

    // Cleanup HAL
    fmrb_hal_deinit();
    ESP_LOGI(TAG, "HAL deinitialized");

    ESP_LOGI(TAG, "Family mruby OS shutdown complete");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Family mruby Graphics-Audio Firmware Starting...");

#ifdef CONFIG_IDF_TARGET_LINUX
    ESP_LOGI(TAG, "Running on Linux target - Development mode");
#else
    ESP_LOGI(TAG, "Running on ESP32-S3-N16R8 - Production mode");
#endif

    // Initialize Family mruby OS
    fmrb_os_init();

    // Run main loop
    fmrb_os_main_loop();

    // Cleanup (only reached in Linux demo mode)
    fmrb_os_deinit();

    ESP_LOGI(TAG, "Family mruby OS exited");
}