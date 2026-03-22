#include <stdio.h>
#include <string.h>

#include "fmrb_task_config.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_hid_msg.h"
#include "host_task.h"
#include "fmrb_gfx.h"
#include "fmrb_audio.h"
#include "fmrb_kernel.h"
#include "boot.h"
#include "fmrb_link_transport.h"
#include "fmrb_link_protocol.h"
#include "fmrb_keymap.h"
#include "status_led.h"

static const char *TAG = "host";

static bool g_cursor_shown = false;

// Host message types
typedef enum {
    HOST_MSG_HID_KEY_DOWN = 1,
    HOST_MSG_HID_KEY_UP = 2,
    HOST_MSG_HID_MOUSE_MOVE = 3,
    HOST_MSG_HID_MOUSE_CLICK = 4,
    HOST_MSG_DRAW_COMMAND = 5,
    HOST_MSG_AUDIO_COMMAND = 6,
    HOST_MSG_HID_GAMEPAD_BUTTON = 7,
    HOST_MSG_HID_GAMEPAD_AXIS = 8,
} host_msg_type_t;

// Host message structure (now uses HAL message format)
typedef struct {
    host_msg_type_t type;
    union {
        struct {
            int key_code;
            int scancode;
            int modifier;
        } key;
        struct {
            int x;
            int y;
        } mouse_move;
        struct {
            int x;
            int y;
            int button;
            int state;  // 1=pressed, 0=released
        } mouse_click;
        struct {
            int gamepad_id;  // 0-1
            int button_num;  // 0-15
            int state;       // 1=pressed, 0=released
        } gamepad_button;
        struct {
            int gamepad_id;  // 0-1
            int axis_num;    // 0-5
            int value;       // -128 to 127 (sticks) or 0 to 255 (triggers)
        } gamepad_axis;
        gfx_cmd_t gfx;
    } data;
} host_message_t;

// Host task handle
static fmrb_task_handle_t g_host_task_handle = 0;

// HOST message queue flow control semaphore
// Limits GFX commands to prevent queue overflow and reserve space for HID events
static fmrb_semaphore_t g_host_gfx_queue_semaphore = NULL;

// Task configuration (queue size defined in fmrb_task_config.h)

// Timing statistics for GFX pipeline
static uint32_t g_gfx_total_cmds = 0;       // Total commands since last stats log
static uint32_t g_gfx_present_count = 0;     // Number of PRESENT calls since last stats log
static uint64_t g_gfx_stats_last_us = 0;     // Last stats log time
#define GFX_STATS_INTERVAL_US (5000000ULL)    // Log stats every 5 seconds

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
    const fmrb_system_config_t* conf = fmrb_kernel_get_config();

    // Initialize Graphics subsystem (this initializes the transport layer)
    fmrb_gfx_config_t gfx_config = {
        .screen_width = conf->display_width,
        .screen_height = conf->display_height,
        .bits_per_pixel = 8,
        .double_buffered = false
    };

    fmrb_gfx_err_t gfx_ret = fmrb_gfx_init(&gfx_config);
    if (gfx_ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "Failed to initialize Graphics: %d", gfx_ret);
        return -1;
    } else {
        FMRB_LOGI(TAG, "Graphics transport initialized: %dx%d", gfx_config.screen_width, gfx_config.screen_height);

        // Get the graphics context which contains the transport handle
        fmrb_gfx_context_t ctx = fmrb_gfx_get_global_context();
        if (!ctx) {
            FMRB_LOGE(TAG, "Failed to get global graphics context");
            return -1;
        }

        // Send display initialization command to host
        // This tells the host to create the SDL2 window with the specified resolution
        fmrb_control_init_display_t init_cmd = {
            .width = conf->display_width,
            .height = conf->display_height,
            .color_depth = 8  // RGB332
        };

        FMRB_LOGI(TAG, "Sending display initialization to host: %dx%d, %d-bit",
                  init_cmd.width, init_cmd.height, init_cmd.color_depth);

        // Use singleton transport API (no handle needed)
        // Display initialization on slave takes ~2100ms (CVBS + PSRAM 2.3MB alloc)
        fmrb_err_t ret = fmrb_link_transport_send(
            FMRB_LINK_TYPE_CONTROL,
            FMRB_LINK_CONTROL_INIT_DISPLAY,
            (const uint8_t*)&init_cmd,
            sizeof(init_cmd),
            5000
        );

        if (ret != FMRB_OK) {
            FMRB_LOGE(TAG, "Failed to send display init command: %d", ret);
            return -1;
        }

        FMRB_LOGI(TAG, "Display initialization command sent successfully");

        // Give host time to initialize the display (200ms)
        fmrb_task_delay(FMRB_MS_TO_TICKS(200));

        FMRB_LOGI(TAG, "Graphics fully initialized: %dx%d", gfx_config.screen_width, gfx_config.screen_height);
    }

    // Initialize Audio subsystem (APU emulator)
    fmrb_audio_err_t audio_ret = fmrb_audio_init();
    if (audio_ret != FMRB_AUDIO_OK) {
        FMRB_LOGE(TAG, "Failed to initialize Audio: %d", audio_ret);
        return -1;
    } else {
        FMRB_LOGI(TAG, "Audio subsystem (APU emulator) initialized");
    }

    FMRB_LOGI(TAG, "Host task initialized");
    return 0;
}

/**
 * Process GFX command message - Passthrough to Graphics-Audio board
 *
 * HOST task simply forwards all graphics commands to Graphics-Audio board.
 * No local buffering or processing - Graphics-Audio handles all command buffering
 * and present() operations.
 */
static void host_task_process_gfx_command(const fmrb_msg_t *msg)
{
    gfx_cmd_t *gfx_cmd = (gfx_cmd_t *)msg->data;

    fmrb_gfx_context_t ctx = fmrb_gfx_get_global_context();
    if (!ctx) {
        FMRB_LOGE(TAG, "Graphics context not available");
        return;
    }

    fmrb_gfx_err_t ret = FMRB_GFX_OK;

    // Forward command directly to Graphics-Audio board
    switch (gfx_cmd->cmd_type) {
        case GFX_CMD_CLEAR:
            FMRB_LOGD(TAG, "Forwarding CLEAR: canvas=%d, color=0x%02X",
                     gfx_cmd->canvas_id, gfx_cmd->params.clear.color);
            ret = fmrb_gfx_clear(ctx, gfx_cmd->canvas_id, gfx_cmd->params.clear.color);
            break;

        case GFX_CMD_PIXEL:
            FMRB_LOGD(TAG, "Forwarding PIXEL: canvas=%d, pos=(%d,%d), color=0x%02X",
                     gfx_cmd->canvas_id, gfx_cmd->params.pixel.x, gfx_cmd->params.pixel.y,
                     gfx_cmd->params.pixel.color);
            ret = fmrb_gfx_set_pixel(ctx, gfx_cmd->canvas_id,
                                    gfx_cmd->params.pixel.x, gfx_cmd->params.pixel.y,
                                    gfx_cmd->params.pixel.color);
            break;

        case GFX_CMD_LINE:
            FMRB_LOGD(TAG, "Forwarding LINE: canvas=%d, from=(%d,%d) to=(%d,%d), color=0x%02X",
                     gfx_cmd->canvas_id,
                     gfx_cmd->params.line.x1, gfx_cmd->params.line.y1,
                     gfx_cmd->params.line.x2, gfx_cmd->params.line.y2,
                     gfx_cmd->params.line.color);
            ret = fmrb_gfx_draw_line(ctx, gfx_cmd->canvas_id,
                                    gfx_cmd->params.line.x1, gfx_cmd->params.line.y1,
                                    gfx_cmd->params.line.x2, gfx_cmd->params.line.y2,
                                    gfx_cmd->params.line.color);
            break;

        case GFX_CMD_RECT:
            FMRB_LOGD(TAG, "Forwarding RECT: canvas=%d, rect=(%d,%d,%d,%d), color=0x%02X, filled=%d",
                     gfx_cmd->canvas_id,
                     gfx_cmd->params.rect.rect.x, gfx_cmd->params.rect.rect.y,
                     gfx_cmd->params.rect.rect.width, gfx_cmd->params.rect.rect.height,
                     gfx_cmd->params.rect.color, gfx_cmd->params.rect.filled);
            if (gfx_cmd->params.rect.filled) {
                ret = fmrb_gfx_fill_rect(ctx, gfx_cmd->canvas_id,
                                        &gfx_cmd->params.rect.rect,
                                        gfx_cmd->params.rect.color);
            } else {
                ret = fmrb_gfx_draw_rect(ctx, gfx_cmd->canvas_id,
                                        &gfx_cmd->params.rect.rect,
                                        gfx_cmd->params.rect.color);
            }
            break;

        case GFX_CMD_CIRCLE:
            FMRB_LOGD(TAG, "Forwarding CIRCLE: canvas=%d, center=(%d,%d), r=%d, color=0x%02X, filled=%d",
                     gfx_cmd->canvas_id,
                     gfx_cmd->params.circle.x, gfx_cmd->params.circle.y,
                     gfx_cmd->params.circle.radius,
                     gfx_cmd->params.circle.color, gfx_cmd->params.circle.filled);
            if (gfx_cmd->params.circle.filled) {
                ret = fmrb_gfx_fill_circle(ctx, gfx_cmd->canvas_id,
                                          gfx_cmd->params.circle.x, gfx_cmd->params.circle.y,
                                          gfx_cmd->params.circle.radius,
                                          gfx_cmd->params.circle.color);
            } else {
                ret = fmrb_gfx_draw_circle(ctx, gfx_cmd->canvas_id,
                                          gfx_cmd->params.circle.x, gfx_cmd->params.circle.y,
                                          gfx_cmd->params.circle.radius,
                                          gfx_cmd->params.circle.color);
            }
            break;

        case GFX_CMD_TEXT:
            FMRB_LOGD(TAG, "Forwarding TEXT: canvas=%d, pos=(%d,%d), text='%s', color=0x%02X",
                     gfx_cmd->canvas_id,
                     gfx_cmd->params.text.x, gfx_cmd->params.text.y,
                     gfx_cmd->params.text.text, gfx_cmd->params.text.color);
            ret = fmrb_gfx_draw_text(ctx, gfx_cmd->canvas_id,
                                    gfx_cmd->params.text.x, gfx_cmd->params.text.y,
                                    gfx_cmd->params.text.text,
                                    gfx_cmd->params.text.color,
                                    gfx_cmd->params.text.bg_color,
                                    gfx_cmd->params.text.bg_transparent,
                                    gfx_cmd->params.text.font_size);
            break;

        case GFX_CMD_PRESENT:
            FMRB_LOGD(TAG, "Forwarding PRESENT: canvas=%d, pos=(%d,%d), transparent=0x%02X",
                     gfx_cmd->canvas_id,
                     gfx_cmd->params.present.x, gfx_cmd->params.present.y,
                     gfx_cmd->params.present.transparent_color);

            // Push canvas to render buffer
            ret = fmrb_gfx_push_canvas(ctx,
                                       gfx_cmd->canvas_id,
                                       FMRB_CANVAS_RENDER,
                                       gfx_cmd->params.present.x,
                                       gfx_cmd->params.present.y,
                                       gfx_cmd->params.present.transparent_color);

            // Update statistics
            g_gfx_present_count++;
            g_gfx_total_cmds++;

            // Log stats periodically
            fmrb_time_t now_us = fmrb_hal_time_get_us();
            if (g_gfx_stats_last_us == 0) {
                g_gfx_stats_last_us = now_us;
            } else if ((now_us - g_gfx_stats_last_us) >= GFX_STATS_INTERVAL_US) {
                uint64_t elapsed_us = now_us - g_gfx_stats_last_us;
                float elapsed_s = (float)elapsed_us / 1000000.0f;
                float cmds_per_sec = (float)g_gfx_total_cmds / elapsed_s;
                float presents_per_sec = (float)g_gfx_present_count / elapsed_s;

                FMRB_LOGI(TAG, "GFX STATS: %.1f cmds/s, %.1f presents/s",
                         cmds_per_sec, presents_per_sec);

                g_gfx_total_cmds = 0;
                g_gfx_present_count = 0;
                g_gfx_stats_last_us = now_us;
            }
            break;

        default:
            FMRB_LOGW(TAG, "Unknown graphics command type: %d", gfx_cmd->cmd_type);
            return;
    }

    if (ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "Failed to forward graphics command type=%d: %d", gfx_cmd->cmd_type, ret);
    } else {
        // Count non-present commands
        if (gfx_cmd->cmd_type != GFX_CMD_PRESENT) {
            g_gfx_total_cmds++;
        }
    }

    // Release semaphore slot now that command has been processed
    // This allows the sending app task to proceed with the next command
    if (g_host_gfx_queue_semaphore) {
        fmrb_semaphore_give(g_host_gfx_queue_semaphore);
    }
}

/**
 * Process a host message
 */
static void host_task_process_message(const fmrb_msg_t *hal_msg)
{
    // Check if it's a GFX message first
    if (hal_msg->type == FMRB_MSG_TYPE_APP_GFX) {
        host_task_process_gfx_command(hal_msg);
        return;
    }

    // Otherwise, extract host_message_t (for HID messages)
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
        case HOST_MSG_HID_KEY_UP: {
            // Get routing table
            fmrb_hid_routing_t routing;
            if (fmrb_kernel_get_hid_routing(&routing) != FMRB_OK) {
                FMRB_LOGE(TAG, "Failed to get HID routing");
                break;
            }

            if (!routing.routing_enabled || routing.target_pid == 0xFF) {
                // No target, discard event
                FMRB_LOGD(TAG, "No HID target, discarding key event");
                break;
            }

            FMRB_LOGD(TAG, "Key %s: %d -> PID %d",
                     msg->type == HOST_MSG_HID_KEY_DOWN ? "down" : "up",
                     msg->data.key.key_code, routing.target_pid);

            // Create HID message
            fmrb_msg_t hid_msg = {
                .type = FMRB_MSG_TYPE_HID_EVENT,
                .src_pid = PROC_ID_HOST,
                .size = sizeof(fmrb_hid_key_event_t)
            };
            fmrb_hid_key_event_t *key_event = (fmrb_hid_key_event_t*)hid_msg.data;
            key_event->subtype = (msg->type == HOST_MSG_HID_KEY_DOWN)
                ? HID_MSG_KEY_DOWN : HID_MSG_KEY_UP;
            key_event->keycode = (uint8_t)(msg->data.key.key_code & 0xFF);
            key_event->scancode = (uint8_t)(msg->data.key.scancode & 0xFF);
            key_event->modifier = (uint8_t)(msg->data.key.modifier & 0xFF);

            // Convert scancode to character
            key_event->character = fmrb_keymap_scancode_to_char(
                key_event->scancode,
                key_event->modifier,
                fmrb_keymap_get_layout()
            );

            // Send directly to focused window (current HID target)
            // Retry up to 3 times with longer timeout to handle busy Ruby execution
            fmrb_err_t ret = FMRB_ERR_TIMEOUT;
            for (int retry = 0; retry < 3; retry++) {
                ret = fmrb_msg_send(routing.target_pid, &hid_msg, 5000);
                if (ret == FMRB_OK) {
                    break;
                }
                FMRB_LOGW(TAG, "Failed to send keyboard event to PID %d, retry %d/3", routing.target_pid, retry + 1);
                fmrb_task_delay(FMRB_MS_TO_TICKS(100));  // Wait 100ms before retry
            }
            if (ret != FMRB_OK) {
                FMRB_LOGE(TAG, "Keyboard event dropped after 3 retries to PID %d", routing.target_pid);
            }
            break;
        }

        case HOST_MSG_HID_MOUSE_MOVE: {
            // Update cursor position via GFX API
            int x = msg->data.mouse_move.x;
            int y = msg->data.mouse_move.y;

            fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
            if (gfx_ctx) {
                // Show cursor on first mouse event
                if (!g_cursor_shown) {
                    g_cursor_shown = true;
                    fmrb_gfx_set_cursor_visible(gfx_ctx, true);
                    FMRB_LOGI(TAG, "Cursor made visible on first mouse event");
                }
                fmrb_gfx_err_t gfx_ret = fmrb_gfx_set_cursor_position(gfx_ctx, x, y);
                if (gfx_ret != FMRB_GFX_OK) {
                    FMRB_LOGW(TAG, "Failed to set cursor position: %d", gfx_ret);
                }
            }

            FMRB_LOGD(TAG, "Mouse move: (%d, %d) - forwarding to Kernel", x, y);

            // Forward mouse move to Kernel for drag and drop handling
            // Kernel will forward to target app if not dragging
            fmrb_msg_t hid_msg = {
                .type = FMRB_MSG_TYPE_HID_EVENT,
                .src_pid = PROC_ID_HOST,
                .size = 6  // subtype(1) + button(1) + x(2) + y(2)
            };
            hid_msg.data[0] = HID_MSG_MOUSE_MOVE;  // subtype
            hid_msg.data[1] = 0;  // button (not used for move)
            hid_msg.data[2] = (uint8_t)(x & 0xFF);
            hid_msg.data[3] = (uint8_t)((x >> 8) & 0xFF);
            hid_msg.data[4] = (uint8_t)(y & 0xFF);
            hid_msg.data[5] = (uint8_t)((y >> 8) & 0xFF);

            // Mouse move events can be dropped if queue is full (already rate-limited to 66ms)
            // Use single 5000ms timeout without retry to avoid blocking HOST task
            fmrb_err_t ret = fmrb_msg_send(PROC_ID_KERNEL, &hid_msg, 5000);
            if (ret != FMRB_OK) {
                // Silently drop - mouse moves are high-frequency and already rate-limited
                FMRB_LOGD(TAG, "Mouse move dropped (Kernel queue full)");
            }
            break;
        }

        case HOST_MSG_HID_MOUSE_CLICK: {
            int x = msg->data.mouse_click.x;
            int y = msg->data.mouse_click.y;
            int button = msg->data.mouse_click.button;
            int state = msg->data.mouse_click.state;

            FMRB_LOGI(TAG, "Mouse click: button=%d, pos=(%d,%d), state=%s - forwarding to Kernel for hit test",
                     button, x, y, state ? "pressed" : "released");

            // Forward mouse click to Kernel for window hit testing
            fmrb_msg_t kernel_msg = {
                .type = FMRB_MSG_TYPE_HID_EVENT,
                .src_pid = PROC_ID_HOST,
                .size = sizeof(fmrb_hid_mouse_button_event_t)
            };
            fmrb_hid_mouse_button_event_t *mouse_btn = (fmrb_hid_mouse_button_event_t*)kernel_msg.data;
            mouse_btn->subtype = state ? HID_MSG_MOUSE_BUTTON_DOWN : HID_MSG_MOUSE_BUTTON_UP;
            mouse_btn->button = button;
            mouse_btn->x = x;
            mouse_btn->y = y;

            // Send to Kernel for hit testing and routing
            // Retry up to 3 times - mouse clicks are important user actions
            fmrb_err_t ret = FMRB_ERR_TIMEOUT;
            for (int retry = 0; retry < 3; retry++) {
                ret = fmrb_msg_send(PROC_ID_KERNEL, &kernel_msg, 5000);
                if (ret == FMRB_OK) {
                    break;
                }
                FMRB_LOGW(TAG, "Failed to send mouse click to Kernel, retry %d/3", retry + 1);
                fmrb_task_delay(FMRB_MS_TO_TICKS(100));  // Wait 100ms before retry
            }
            if (ret != FMRB_OK) {
                FMRB_LOGE(TAG, "Mouse click dropped after 3 retries");
            }
            break;
        }

        case HOST_MSG_DRAW_COMMAND:
            FMRB_LOGD(TAG, "Draw command: cmd_type=%d, canvas_id=%d",
                     msg->data.gfx.cmd_type, msg->data.gfx.canvas_id);
            // TODO: Implement command buffering and execution
            break;

        case HOST_MSG_AUDIO_COMMAND:
            FMRB_LOGD(TAG, "Audio command (not yet implemented)");
            // TODO: Implement audio command processing
            break;

        case HOST_MSG_HID_GAMEPAD_BUTTON:
            FMRB_LOGD(TAG, "Gamepad button: id=%d, button=%d, state=%s",
                     msg->data.gamepad_button.gamepad_id,
                     msg->data.gamepad_button.button_num,
                     msg->data.gamepad_button.state ? "pressed" : "released");
            // TODO: Implement gamepad button event routing
            break;

        case HOST_MSG_HID_GAMEPAD_AXIS:
            FMRB_LOGD(TAG, "Gamepad axis: id=%d, axis=%d, value=%d",
                     msg->data.gamepad_axis.gamepad_id,
                     msg->data.gamepad_axis.axis_num,
                     msg->data.gamepad_axis.value);
            // TODO: Implement gamepad axis event routing
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
        FMRB_LOGE(TAG, "Host task initialization failed, suspending task");
        vTaskSuspend(NULL);
        return;
    }
    // Signal that host task initialization is complete
    FMRB_LOGI(TAG, "Host task initialized");
    fmrb_host_set_ready();

    fmrb_msg_t msg;
    fmrb_tick_t xLastUpdate = fmrb_task_get_tick_count();
    const fmrb_tick_t xUpdatePeriod = FMRB_MS_TO_TICKS(16);  // 16ms周期で定期更新

    while (1) {
        // Drain message queue (first wait up to 10ms, then non-blocking)
        bool first = true;
        while (fmrb_msg_receive(PROC_ID_HOST, &msg, first ? 10 : 0) == FMRB_OK) {
            host_task_process_message(&msg);
            first = false;
        }

        // Process incoming IPC messages (ACK/NACK responses)
        // This MUST be called regularly to receive responses for sync requests
        fmrb_link_transport_process();

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
    // Create GFX queue flow control semaphore
    // Initial count: FMRB_HOST_GFX_AVAILABLE_SLOTS (96)
    // This reserves FMRB_HOST_HID_RESERVED_SLOTS (32) for HID events
    g_host_gfx_queue_semaphore = fmrb_semaphore_create_counting(
        FMRB_HOST_GFX_AVAILABLE_SLOTS,  // Max count
        FMRB_HOST_GFX_AVAILABLE_SLOTS   // Initial count
    );
    if (!g_host_gfx_queue_semaphore) {
        FMRB_LOGE(TAG, "Failed to create GFX queue semaphore");
        return -1;
    }
    FMRB_LOGI(TAG, "Created GFX queue semaphore: %d available slots (reserving %d for HID)",
              FMRB_HOST_GFX_AVAILABLE_SLOTS, FMRB_HOST_HID_RESERVED_SLOTS);

    // Register host task's message queue
    fmrb_msg_queue_config_t queue_config = {
        .queue_length = FMRB_HOST_MSG_QUEUE_LEN,
        .message_size = sizeof(fmrb_msg_t)
    };

    fmrb_err_t hal_ret = fmrb_msg_create_queue(PROC_ID_HOST, &queue_config);
    if (hal_ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to create host message queue: %d", hal_ret);
        fmrb_semaphore_delete(g_host_gfx_queue_semaphore);
        g_host_gfx_queue_semaphore = NULL;
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
        fmrb_semaphore_delete(g_host_gfx_queue_semaphore);
        g_host_gfx_queue_semaphore = NULL;
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
        g_host_task_handle = 0;
    }

    // Delete host task's message queue
    fmrb_msg_delete_queue(PROC_ID_HOST);

    // Delete GFX queue semaphore
    if (g_host_gfx_queue_semaphore) {
        fmrb_semaphore_delete(g_host_gfx_queue_semaphore);
        g_host_gfx_queue_semaphore = NULL;
    }

    FMRB_LOGI(TAG, "Host task deinitialized");
}

/**
 * Send a host message
 */
static int fmrb_host_send_message(const host_message_t *msg)
{
    // Wrap host message in fmrb_msg_t format
    // Use a dummy type (not FMRB_MSG_TYPE_HID_EVENT which is for app->app messages)
    // host_task_process_message will extract host_message_t from hal_msg->data
    fmrb_msg_t hal_msg = {
        .type = FMRB_MSG_TYPE_MAX,  // Internal host message marker
        .src_pid = PROC_ID_HOST,
        .size = sizeof(host_message_t)
    };
    memcpy(hal_msg.data, msg, sizeof(host_message_t));

    fmrb_err_t result = fmrb_msg_send(PROC_ID_HOST, &hal_msg, 10);
    if (result != FMRB_OK) {
        // Silently drop mouse move events on queue full
        if (msg->type == HOST_MSG_HID_MOUSE_MOVE) {
            return -1;
        }
        FMRB_LOGW(TAG, "Failed to send host message: type=%d, err=%d", msg->type, result);
        return -1;
    }

    return 0;
}

/**
 * Convenience functions for sending specific message types
 */

int fmrb_host_send_key_down(int key_code, int scancode, int modifier)
{
    FMRB_LOGI(TAG, "KEY_DOWN: code=%d scan=%d mod=0x%x", key_code, scancode, modifier);
    host_message_t msg = {
        .type = HOST_MSG_HID_KEY_DOWN,
        .data.key.key_code = key_code,
        .data.key.scancode = scancode,
        .data.key.modifier = modifier
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_key_up(int key_code, int scancode, int modifier)
{
    FMRB_LOGI(TAG, "KEY_UP: code=%d scan=%d mod=0x%x", key_code, scancode, modifier);
    host_message_t msg = {
        .type = HOST_MSG_HID_KEY_UP,
        .data.key.key_code = key_code,
        .data.key.scancode = scancode,
        .data.key.modifier = modifier
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_mouse_move(int x, int y)
{
    // Rate limit mouse move events (~15fps, 66ms interval)
    static uint32_t last_send_ms = 0;
    uint32_t now_ms = (uint32_t)fmrb_hal_time_get_ms();
    if (now_ms - last_send_ms < 66) {
        return 0;  // Silently skip
    }
    last_send_ms = now_ms;

    FMRB_LOGI(TAG, "MOUSE_MOVE: x=%d y=%d", x, y);
    host_message_t msg = {
        .type = HOST_MSG_HID_MOUSE_MOVE,
        .data.mouse_move.x = x,
        .data.mouse_move.y = y
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_mouse_click(int x, int y, int button, int state)
{
    FMRB_LOGI(TAG, "MOUSE_CLICK: x=%d y=%d btn=%d state=%d", x, y, button, state);
    host_message_t msg = {
        .type = HOST_MSG_HID_MOUSE_CLICK,
        .data.mouse_click.x = x,
        .data.mouse_click.y = y,
        .data.mouse_click.button = button,
        .data.mouse_click.state = state
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_gamepad_button(int gamepad_id, int button_num, int state)
{
    host_message_t msg = {
        .type = HOST_MSG_HID_GAMEPAD_BUTTON,
        .data.gamepad_button.gamepad_id = gamepad_id,
        .data.gamepad_button.button_num = button_num,
        .data.gamepad_button.state = state
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_gamepad_axis(int gamepad_id, int axis_num, int value)
{
    host_message_t msg = {
        .type = HOST_MSG_HID_GAMEPAD_AXIS,
        .data.gamepad_axis.gamepad_id = gamepad_id,
        .data.gamepad_axis.axis_num = axis_num,
        .data.gamepad_axis.value = value
    };
    return fmrb_host_send_message(&msg);
}

fmrb_semaphore_t fmrb_host_get_gfx_queue_semaphore(void)
{
    return g_host_gfx_queue_semaphore;
}
