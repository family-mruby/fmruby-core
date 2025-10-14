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

static const char *TAG = "fmrb";

// Global contexts
static fmrb_gfx_context_t g_gfx_context = NULL;

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
        fmrb_gfx_draw_text(g_gfx_context, 10, 10, "Family mruby OS", FMRB_COLOR_WHITE, FMRB_FONT_SIZE_LARGE);
        fmrb_gfx_present(g_gfx_context);
    }

    // Initialize Audio subsystem (APU emulator)
    fmrb_audio_err_t audio_ret = fmrb_audio_init();
    if (audio_ret != FMRB_AUDIO_OK) {
        ESP_LOGE(TAG, "Failed to initialize Audio: %d", audio_ret);
    } else {
        ESP_LOGI(TAG, "Audio subsystem (APU emulator) initialized");
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
            fmrb_gfx_fill_screen(g_gfx_context, FMRB_COLOR_RED);

            fmrb_gfx_present(g_gfx_context);
            break;

            // // Draw title using LovyanGFX compatible API
            // fmrb_gfx_draw_string(g_gfx_context, "LovyanGFX Compatible API Test", 10, 10, FMRB_COLOR_WHITE);

            // // Draw frame counter
            // char frame_text[64];
            // snprintf(frame_text, sizeof(frame_text), "Frame: %d", frame_count);
            // fmrb_gfx_draw_string(g_gfx_context, frame_text, 10, 40, FMRB_COLOR_YELLOW);

            // // Test various drawing primitives
            // int anim = frame_count % 100;

            // // Draw pixel
            // fmrb_gfx_draw_pixel(g_gfx_context, 50 + anim, 80, FMRB_COLOR_RED);

            // // Draw lines
            // fmrb_gfx_draw_fast_hline(g_gfx_context, 50, 100, 100, FMRB_COLOR_GREEN);
            // fmrb_gfx_draw_fast_vline(g_gfx_context, 50 + anim, 100, 50, FMRB_COLOR_BLUE);

            // // Draw rectangles (using old API with fmrb_rect_t)
            // fmrb_rect_t rect_outline = { 200, 80, 80, 60 };
            // fmrb_gfx_draw_rect(g_gfx_context, &rect_outline, FMRB_COLOR_CYAN);
            // fmrb_rect_t rect_filled = { 210 + anim/2, 90, 20, 20 };
            // fmrb_gfx_fill_rect(g_gfx_context, &rect_filled, FMRB_COLOR_RED);

            // // Draw rounded rectangles
            // fmrb_gfx_draw_round_rect(g_gfx_context, 50, 170, 80, 50, 10, FMRB_COLOR_MAGENTA);
            // fmrb_gfx_fill_round_rect(g_gfx_context, 60 + anim/3, 180, 30, 30, 5, FMRB_COLOR_YELLOW);

            // // Draw circles
            // fmrb_gfx_draw_circle(g_gfx_context, 200, 200, 30, FMRB_COLOR_GREEN);
            // fmrb_gfx_fill_circle(g_gfx_context, 200 + anim/2, 200, 10, FMRB_COLOR_BLUE);

            // // Draw triangles
            // fmrb_gfx_draw_triangle(g_gfx_context, 300, 80, 280, 120, 320, 120, FMRB_COLOR_WHITE);
            // fmrb_gfx_fill_triangle(g_gfx_context, 310 + anim/4, 100, 300 + anim/4, 130, 320 + anim/4, 130, FMRB_COLOR_RED);

            // // Draw ellipse
            // fmrb_gfx_draw_ellipse(g_gfx_context, 350, 200, 40, 25, FMRB_COLOR_CYAN);
            // fmrb_gfx_fill_ellipse(g_gfx_context, 350, 200, 10 + anim/10, 8 + anim/12, FMRB_COLOR_YELLOW);

            // // Present the frame
            // fmrb_gfx_present(g_gfx_context);
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
    fmrb_audio_deinit();
    ESP_LOGI(TAG, "Audio deinitialized");

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