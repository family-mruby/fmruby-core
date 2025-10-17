#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "system_task";

// System message types
typedef enum {
    SYS_MSG_HID_KEY_DOWN = 1,
    SYS_MSG_HID_KEY_UP = 2,
    SYS_MSG_HID_MOUSE_MOVE = 3,
    SYS_MSG_HID_MOUSE_CLICK = 4,
    SYS_MSG_DRAW_COMMAND = 5,
    SYS_MSG_AUDIO_COMMAND = 6,
} system_msg_type_t;

// System message structure
typedef struct {
    system_msg_type_t type;
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
} system_message_t;

// System message queue
static QueueHandle_t g_system_queue = NULL;

// System task handle
static TaskHandle_t g_system_task_handle = NULL;

// Task configuration
#define SYSTEM_TASK_STACK_SIZE (4 * 1024)
#define SYSTEM_TASK_PRIORITY (4)
#define SYSTEM_QUEUE_SIZE (32)

// Forward declarations (implemented in picoruby-fmrb-app)
extern int fmrb_app_dispatch_update(uint32_t delta_time_ms);
extern int fmrb_app_dispatch_key_down(int key_code);
extern int fmrb_app_dispatch_key_up(int key_code);
extern int fmrb_app_dispatch_mouse_move(int x, int y);
extern int fmrb_app_dispatch_mouse_click(int x, int y, int button);

/**
 * Process a system message
 */
static void system_task_process_message(const system_message_t *msg)
{
    switch (msg->type) {
        case SYS_MSG_HID_KEY_DOWN:
            ESP_LOGD(TAG, "Key down: %d", msg->data.key.key_code);
            fmrb_app_dispatch_key_down(msg->data.key.key_code);
            break;

        case SYS_MSG_HID_KEY_UP:
            ESP_LOGD(TAG, "Key up: %d", msg->data.key.key_code);
            fmrb_app_dispatch_key_up(msg->data.key.key_code);
            break;

        case SYS_MSG_HID_MOUSE_MOVE:
            ESP_LOGD(TAG, "Mouse move: (%d, %d)", msg->data.mouse_move.x, msg->data.mouse_move.y);
            fmrb_app_dispatch_mouse_move(msg->data.mouse_move.x, msg->data.mouse_move.y);
            break;

        case SYS_MSG_HID_MOUSE_CLICK:
            ESP_LOGD(TAG, "Mouse click: (%d, %d) button: %d",
                     msg->data.mouse_click.x,
                     msg->data.mouse_click.y,
                     msg->data.mouse_click.button);
            fmrb_app_dispatch_mouse_click(msg->data.mouse_click.x,
                                          msg->data.mouse_click.y,
                                          msg->data.mouse_click.button);
            break;

        case SYS_MSG_DRAW_COMMAND:
            ESP_LOGD(TAG, "Draw command (not yet implemented)");
            // TODO: Implement draw command processing
            break;

        case SYS_MSG_AUDIO_COMMAND:
            ESP_LOGD(TAG, "Audio command (not yet implemented)");
            // TODO: Implement audio command processing
            break;

        default:
            ESP_LOGW(TAG, "Unknown message type: %d", msg->type);
            break;
    }
}

/**
 * System task main loop
 */
static void system_task(void *pvParameters)
{
    ESP_LOGI(TAG, "System task started");

    system_message_t msg;

    while (1) {
        // Wait for messages with a timeout
        if (xQueueReceive(g_system_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            system_task_process_message(&msg);
        }

        // Yield to other tasks
        taskYIELD();
    }
}

/**
 * Initialize the system task
 */
int fmrb_system_task_init(void)
{
    ESP_LOGI(TAG, "Initializing system task...");

    // Create message queue
    g_system_queue = xQueueCreate(SYSTEM_QUEUE_SIZE, sizeof(system_message_t));
    if (!g_system_queue) {
        ESP_LOGE(TAG, "Failed to create system message queue");
        return -1;
    }

    // Create system task
    BaseType_t result = xTaskCreate(
        system_task,
        "system_task",
        SYSTEM_TASK_STACK_SIZE,
        NULL,
        SYSTEM_TASK_PRIORITY,
        &g_system_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create system task");
        vQueueDelete(g_system_queue);
        g_system_queue = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "System task initialized successfully");
    return 0;
}

/**
 * Deinitialize the system task
 */
void fmrb_system_task_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing system task...");

    if (g_system_task_handle) {
        vTaskDelete(g_system_task_handle);
        g_system_task_handle = NULL;
    }

    if (g_system_queue) {
        vQueueDelete(g_system_queue);
        g_system_queue = NULL;
    }

    ESP_LOGI(TAG, "System task deinitialized");
}

/**
 * Send a system message
 */
int fmrb_system_send_message(const system_message_t *msg)
{
    if (!g_system_queue) {
        ESP_LOGW(TAG, "System queue not initialized");
        return -1;
    }

    if (xQueueSend(g_system_queue, msg, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to send system message (queue full?)");
        return -1;
    }

    return 0;
}

/**
 * Convenience functions for sending specific message types
 */

int fmrb_system_send_key_down(int key_code)
{
    system_message_t msg = {
        .type = SYS_MSG_HID_KEY_DOWN,
        .data.key.key_code = key_code
    };
    return fmrb_system_send_message(&msg);
}

int fmrb_system_send_key_up(int key_code)
{
    system_message_t msg = {
        .type = SYS_MSG_HID_KEY_UP,
        .data.key.key_code = key_code
    };
    return fmrb_system_send_message(&msg);
}

int fmrb_system_send_mouse_move(int x, int y)
{
    system_message_t msg = {
        .type = SYS_MSG_HID_MOUSE_MOVE,
        .data.mouse_move.x = x,
        .data.mouse_move.y = y
    };
    return fmrb_system_send_message(&msg);
}

int fmrb_system_send_mouse_click(int x, int y, int button)
{
    system_message_t msg = {
        .type = SYS_MSG_HID_MOUSE_CLICK,
        .data.mouse_click.x = x,
        .data.mouse_click.y = y,
        .data.mouse_click.button = button
    };
    return fmrb_system_send_message(&msg);
}
