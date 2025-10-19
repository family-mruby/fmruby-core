#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "task_config.h"
#include "host/host_task.h"
#include "fmrb_hal.h"
#include "fmrb_gfx.h"
#include "fmrb_audio.h"

static const char *TAG = "host";

// Host message types
typedef enum {
    HOST_MSG_HID_KEY_DOWN = 1,
    HOST_MSG_HID_KEY_UP = 2,
    HOST_MSG_HID_MOUSE_MOVE = 3,
    HOST_MSG_HID_MOUSE_CLICK = 4,
    HOST_MSG_DRAW_COMMAND = 5,
    HOST_MSG_AUDIO_COMMAND = 6,
} host_msg_type_t;

// Host message structure
typedef struct {
    host_msg_type_t type;
    union {
        struct {
            int key_code;
        } key;
        struct {
            int x;
            int y;
        } mouse_move;
        struct {
            int x;
            int y;
            int button;
        } mouse_click;
    } data;
} host_message_t;

// Host message queue
static QueueHandle_t g_host_queue = NULL;

// Host task handle
static TaskHandle_t g_host_task_handle = NULL;

// Task configuration
#define HOST_QUEUE_SIZE (32)

// Graphics context (global)
static fmrb_gfx_context_t g_gfx_context = NULL;

// Forward declarations (implemented in picoruby-fmrb-app)
// extern int fmrb_app_dispatch_update(uint32_t delta_time_ms);
// extern int fmrb_app_dispatch_key_down(int key_code);
// extern int fmrb_app_dispatch_key_up(int key_code);
// extern int fmrb_app_dispatch_mouse_move(int x, int y);
// extern int fmrb_app_dispatch_mouse_click(int x, int y, int button);

/**
 * Initialize HAL layer and subsystems
 */
static int init_hal(void)
{
    // Initialize HAL layer
    fmrb_err_t hal_ret = fmrb_hal_init();
    if (hal_ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to initialize HAL: %d", hal_ret);
        return -1;
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
        return -1;
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
        return -1;
    } else {
        ESP_LOGI(TAG, "Audio subsystem (APU emulator) initialized");
    }
    return 0;
}

/**
 * Process a host message
 */
static void host_task_process_message(const host_message_t *msg)
{
    switch (msg->type) {
        case HOST_MSG_HID_KEY_DOWN:
            ESP_LOGD(TAG, "Key down: %d", msg->data.key.key_code);
            //fmrb_app_dispatch_key_down(msg->data.key.key_code);
            break;

        case HOST_MSG_HID_KEY_UP:
            ESP_LOGD(TAG, "Key up: %d", msg->data.key.key_code);
            //fmrb_app_dispatch_key_up(msg->data.key.key_code);
            break;

        case HOST_MSG_HID_MOUSE_MOVE:
            ESP_LOGD(TAG, "Mouse move: (%d, %d)", msg->data.mouse_move.x, msg->data.mouse_move.y);
            //fmrb_app_dispatch_mouse_move(msg->data.mouse_move.x, msg->data.mouse_move.y);
            break;

        case HOST_MSG_HID_MOUSE_CLICK:
            ESP_LOGD(TAG, "Mouse click: (%d, %d) button: %d",
                     msg->data.mouse_click.x,
                     msg->data.mouse_click.y,
                     msg->data.mouse_click.button);
            //fmrb_app_dispatch_mouse_click(msg->data.mouse_click.x,
                                        //   msg->data.mouse_click.y,
                                        //   msg->data.mouse_click.button);
            break;

        case HOST_MSG_DRAW_COMMAND:
            ESP_LOGD(TAG, "Draw command (not yet implemented)");
            // TODO: Implement draw command processing
            break;

        case HOST_MSG_AUDIO_COMMAND:
            ESP_LOGD(TAG, "Audio command (not yet implemented)");
            // TODO: Implement audio command processing
            break;

        default:
            ESP_LOGW(TAG, "Unknown message type: %d", msg->type);
            break;
    }
}

/**
 * Host task main loop
 */
static void fmrb_host_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Host task started");

    // Initialize HAL and subsystems
    int result = init_hal();
    if (result < 0) {
        ESP_LOGE(TAG, "Host task initialization failed, terminating");
        return;
    }

    host_message_t msg;
    TickType_t xLastUpdate = xTaskGetTickCount();
    const TickType_t xUpdatePeriod = pdMS_TO_TICKS(10);  // 10ms周期で定期更新

    while (1) {
        // Wait for messages with timeout
        if (xQueueReceive(g_host_queue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
            host_task_process_message(&msg);
        }

        // Periodic update processing
        TickType_t now = xTaskGetTickCount();
        if ((now - xLastUpdate) >= xUpdatePeriod) {
            uint32_t delta_ms = pdTICKS_TO_MS(now - xLastUpdate);

            // Dispatch update to application
            //fmrb_app_dispatch_update(delta_ms);

            xLastUpdate = now;
        }
    }

    ESP_LOGI(TAG, "Host task terminated");
}

/**
 * Initialize the host task
 */
int fmrb_host_task_init(void)
{
    // Create message queue
    g_host_queue = xQueueCreate(HOST_QUEUE_SIZE, sizeof(host_message_t));
    if (!g_host_queue) {
        ESP_LOGE(TAG, "Failed to create host message queue");
        return -1;
    }

    // Create host task
    BaseType_t result = xTaskCreate(
        fmrb_host_task,
        "fmrb_host",
        FMRB_HOST_TASK_STACK_SIZE,
        NULL,
        FMRB_HOST_TASK_PRIORITY,
        &g_host_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create host task");
        return -1;
    }

    ESP_LOGI(TAG, "Host task initialized");
    return 0;
}

/**
 * Deinitialize the host task
 */
void fmrb_host_task_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing host task...");

    if (g_host_task_handle) {
        vTaskDelete(g_host_task_handle);
        g_host_task_handle = NULL;
    }

    if (g_host_queue) {
        vQueueDelete(g_host_queue);
        g_host_queue = NULL;
    }

    ESP_LOGI(TAG, "Host task deinitialized");
}

/**
 * Send a host message
 */
static int fmrb_host_send_message(const host_message_t *msg)
{
    if (!g_host_queue) {
        ESP_LOGW(TAG, "Host queue not initialized");
        return -1;
    }

    if (xQueueSend(g_host_queue, msg, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to send host message (queue full?)");
        return -1;
    }

    return 0;
}

/**
 * Convenience functions for sending specific message types
 */

int fmrb_host_send_key_down(int key_code)
{
    host_message_t msg = {
        .type = HOST_MSG_HID_KEY_DOWN,
        .data.key.key_code = key_code
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_key_up(int key_code)
{
    host_message_t msg = {
        .type = HOST_MSG_HID_KEY_UP,
        .data.key.key_code = key_code
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_mouse_move(int x, int y)
{
    host_message_t msg = {
        .type = HOST_MSG_HID_MOUSE_MOVE,
        .data.mouse_move.x = x,
        .data.mouse_move.y = y
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_mouse_click(int x, int y, int button)
{
    host_message_t msg = {
        .type = HOST_MSG_HID_MOUSE_CLICK,
        .data.mouse_click.x = x,
        .data.mouse_click.y = y,
        .data.mouse_click.button = button
    };
    return fmrb_host_send_message(&msg);
}
