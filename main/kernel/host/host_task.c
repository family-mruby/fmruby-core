#include <stdio.h>
#include <string.h>

#include "fmrb_task_config.h"
#include "fmrb_hal.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "host_task.h"
#include "fmrb_gfx.h"
#include "fmrb_audio.h"
#include "../fmrb_kernel.h"

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

// Host message structure (now uses HAL message format)
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

// Host task handle
static fmrb_task_handle_t g_host_task_handle = NULL;

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

// Internal forward declarations
static void host_task_process_host_message(const host_message_t *msg);

/**
 * Initialize Graphics Audio layer and subsystems
 */
static int init_gfx_audio(void)
{
    fmrb_system_config_t* conf = fmrb_kernel_get_config();

    // Initialize Graphics subsystem
    fmrb_gfx_config_t gfx_config = {
        .screen_width = conf->display_width,
        .screen_height = conf->display_height,
        .bits_per_pixel = 8,
        .double_buffered = false
    };

    fmrb_gfx_err_t gfx_ret = fmrb_gfx_init(&gfx_config, &g_gfx_context);
    if (gfx_ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "Failed to initialize Graphics: %d", gfx_ret);
        return -1;
    } else {
        FMRB_LOGI(TAG, "Graphics initialized: %dx%d", gfx_config.screen_width, gfx_config.screen_height);

        // Test graphics with a simple clear
        FMRB_LOGI(TAG, "============================== gfx demo ==========================");
        fmrb_gfx_clear(g_gfx_context, FMRB_CANVAS_SCREEN, FMRB_COLOR_BLUE);
        fmrb_gfx_draw_string(g_gfx_context, FMRB_CANVAS_SCREEN, "Family mruby OS", 10, 10, FMRB_COLOR_WHITE);
        fmrb_gfx_present(g_gfx_context, FMRB_CANVAS_SCREEN);
    }

    // Initialize Audio subsystem (APU emulator)
    fmrb_audio_err_t audio_ret = fmrb_audio_init();
    if (audio_ret != FMRB_AUDIO_OK) {
        FMRB_LOGE(TAG, "Failed to initialize Audio: %d", audio_ret);
        return -1;
    } else {
        FMRB_LOGI(TAG, "Audio subsystem (APU emulator) initialized");
    }

    return 0;
}

/**
 * Process a host message
 */
static void host_task_process_message(const fmrb_msg_t *hal_msg)
{
    // Extract host message from HAL message
    host_message_t *msg = (host_message_t *)hal_msg->data;
    host_task_process_host_message(msg);
}

/**
 * Process a host-specific message
 */
static void host_task_process_host_message(const host_message_t *msg)
{
    switch (msg->type) {
        case HOST_MSG_HID_KEY_DOWN:
            FMRB_LOGD(TAG, "Key down: %d", msg->data.key.key_code);
            //fmrb_app_dispatch_key_down(msg->data.key.key_code);
            break;

        case HOST_MSG_HID_KEY_UP:
            FMRB_LOGD(TAG, "Key up: %d", msg->data.key.key_code);
            //fmrb_app_dispatch_key_up(msg->data.key.key_code);
            break;

        case HOST_MSG_HID_MOUSE_MOVE:
            FMRB_LOGD(TAG, "Mouse move: (%d, %d)", msg->data.mouse_move.x, msg->data.mouse_move.y);
            //fmrb_app_dispatch_mouse_move(msg->data.mouse_move.x, msg->data.mouse_move.y);
            break;

        case HOST_MSG_HID_MOUSE_CLICK:
            FMRB_LOGD(TAG, "Mouse click: (%d, %d) button: %d",
                     msg->data.mouse_click.x,
                     msg->data.mouse_click.y,
                     msg->data.mouse_click.button);
            //fmrb_app_dispatch_mouse_click(msg->data.mouse_click.x,
                                        //   msg->data.mouse_click.y,
                                        //   msg->data.mouse_click.button);
            break;

        case HOST_MSG_DRAW_COMMAND:
            FMRB_LOGD(TAG, "Draw command (not yet implemented)");
            // TODO: Implement draw command processing
            break;

        case HOST_MSG_AUDIO_COMMAND:
            FMRB_LOGD(TAG, "Audio command (not yet implemented)");
            // TODO: Implement audio command processing
            break;

        default:
            FMRB_LOGW(TAG, "Unknown message type: %d", msg->type);
            break;
    }
}

/**
 * Host task main loop
 */
static void fmrb_host_task(void *pvParameters)
{
    FMRB_LOGI(TAG, "Host task started");

    // Initialize Gfx Audio subsystems
    int result = init_gfx_audio();
    if (result < 0) {
        FMRB_LOGE(TAG, "Host task initialization failed, terminating");
        return;
    }
    // Signal that host task initialization is complete
    FMRB_LOGI(TAG, "Host task initialized");
    fmrb_host_set_ready();

    fmrb_msg_t msg;
    fmrb_tick_t xLastUpdate = fmrb_task_get_tick_count();
    const fmrb_tick_t xUpdatePeriod = FMRB_MS_TO_TICKS(10);  // 10ms周期で定期更新

    while (1) {
        // Wait for messages with timeout
        if (fmrb_msg_receive(PROC_ID_HOST, &msg, 10) == FMRB_OK) {
            host_task_process_message(&msg);
        }

        // Periodic update processing
        fmrb_tick_t now = fmrb_task_get_tick_count();
        if ((now - xLastUpdate) >= xUpdatePeriod) {
            //uint32_t delta_ms = pdTICKS_TO_MS(now - xLastUpdate);

            // Dispatch update to application
            //fmrb_app_dispatch_update(delta_ms);

            xLastUpdate = now;
        }
    }

    FMRB_LOGI(TAG, "Host task terminated");
}

/**
 * Initialize the host task
 */
int fmrb_host_task_init(void)
{
    // Register host task's message queue
    fmrb_msg_queue_config_t queue_config = {
        .queue_length = HOST_QUEUE_SIZE,
        .message_size = sizeof(fmrb_msg_t)
    };

    fmrb_err_t hal_ret = fmrb_msg_create_queue(PROC_ID_HOST, &queue_config);
    if (hal_ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to create host message queue: %d", hal_ret);
        return -1;
    }

    // Create host task
    fmrb_base_type_t result = fmrb_task_create(
        fmrb_host_task,
        "fmrb_host",
        FMRB_HOST_TASK_STACK_SIZE,
        NULL,
        FMRB_HOST_TASK_PRIORITY,
        &g_host_task_handle
    );

    if (result != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create host task");
        fmrb_msg_delete_queue(PROC_ID_HOST);
        return -1;
    }

    return 0;
}

/**
 * Deinitialize the host task
 */
void fmrb_host_task_deinit(void)
{
    FMRB_LOGI(TAG, "Deinitializing host task...");

    if (g_host_task_handle) {
        fmrb_task_delete(g_host_task_handle);
        g_host_task_handle = NULL;
    }

    // Delete host task's message queue
    fmrb_msg_delete_queue(PROC_ID_HOST);

    FMRB_LOGI(TAG, "Host task deinitialized");
}

/**
 * Send a host message
 */
static int fmrb_host_send_message(const host_message_t *msg)
{
    // Wrap host message in message format
    fmrb_msg_t hal_msg = {
        .type = msg->type,
        .size = sizeof(host_message_t)
    };
    memcpy(hal_msg.data, msg, sizeof(host_message_t));

    fmrb_err_t result = fmrb_msg_send(PROC_ID_HOST, &hal_msg, 10);
    if (result != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to send host message: %d", result);
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
