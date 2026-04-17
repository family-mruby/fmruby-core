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
#include "fmrb_transport.h"
#include "fmrb_link_protocol.h"
#include "fmrb_keymap.h"
#include "status_led.h"
#include "fmrb_file_transfer_msg.h"
#include "fmrb_mem.h"
#include "fmrb_app.h"

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
static void host_task_process_message(const fmrb_msg_t *hal_msg);

/**
 * Initialize Graphics Audio layer and subsystems
 */
// Callback for INIT_DISPLAY async response
static void init_display_response_cb(uint8_t status, const uint8_t *payload,
                                      uint32_t payload_len, void *user_data) {
    volatile bool *flag = (volatile bool *)user_data;
    *flag = true;
    FMRB_LOGI(TAG, "INIT_DISPLAY ACK received (status=%u)", status);
}

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
            .color_depth = 8,  // RGB332
            .margin_x = conf->display_margin_x,
            .margin_y = conf->display_margin_y
        };

        FMRB_LOGI(TAG, "Sending display initialization to host: %dx%d, %d-bit",
                  init_cmd.width, init_cmd.height, init_cmd.color_depth);

        // Send INIT_DISPLAY with async + manual polling.
        // Cannot use send_sync here because host_task main loop is not running yet,
        // so fmrb_transport_process() would never be called to receive the ACK.
        static volatile bool s_init_display_ack = false;
        s_init_display_ack = false;

        fmrb_err_t ret = fmrb_transport_send_async(
            FMRB_LINK_TYPE_CONTROL,
            FMRB_LINK_CONTROL_INIT_DISPLAY,
            (const uint8_t*)&init_cmd,
            sizeof(init_cmd),
            init_display_response_cb,
            (void*)&s_init_display_ack,
            15000  // 15 second slot timeout
        );

        if (ret != FMRB_OK) {
            FMRB_LOGE(TAG, "Failed to send display init command: %d", ret);
            return -1;
        }

        FMRB_LOGI(TAG, "Display init command sent, polling for ACK...");

        // Poll for ACK while calling fmrb_transport_process() to receive responses
        fmrb_tick_t start_tick = fmrb_task_get_tick_count();
        fmrb_tick_t timeout_ticks = FMRB_MS_TO_TICKS(10000);
        while (!s_init_display_ack) {
            fmrb_transport_process();
            fmrb_task_delay(FMRB_MS_TO_TICKS(10));
            if ((fmrb_task_get_tick_count() - start_tick) > timeout_ticks) {
                FMRB_LOGE(TAG, "INIT_DISPLAY ACK timeout");
                return -1;
            }
        }

        FMRB_LOGI(TAG, "Display initialization confirmed by host");

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

// Maximum batch size for GFX commands
#define GFX_BATCH_MAX 16

// Payload buffer for batch entries (stack-allocated, shared across batch)
// Each entry needs at most sizeof(fmrb_link_graphics_text_t) + 255 bytes
#define GFX_BATCH_PAYLOAD_BUF_SIZE 512

// Convert a gfx_cmd_t to batch entry (sub_cmd + payload).
// payload_buf must be at least GFX_BATCH_PAYLOAD_BUF_SIZE bytes.
// Returns payload size, or -1 on error.
static int gfx_cmd_to_batch_entry(const gfx_cmd_t *cmd,
                                   uint8_t *sub_cmd_out,
                                   uint8_t *payload_buf) {
    switch (cmd->cmd_type) {
        case GFX_CMD_CLEAR: {
            fmrb_link_graphics_clear_t c = {
                .canvas_id = cmd->canvas_id,
                .x = 0, .y = 0, .width = 0, .height = 0,
                .color = cmd->params.clear.color
            };
            *sub_cmd_out = FMRB_LINK_GFX_FILL_SCREEN;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_PIXEL: {
            fmrb_link_graphics_pixel_t c = {
                .canvas_id = cmd->canvas_id,
                .x = cmd->params.pixel.x,
                .y = cmd->params.pixel.y,
                .color = cmd->params.pixel.color
            };
            *sub_cmd_out = FMRB_LINK_GFX_DRAW_PIXEL;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_LINE: {
            fmrb_link_graphics_line_t c = {
                .canvas_id = cmd->canvas_id,
                .x1 = cmd->params.line.x1,
                .y1 = cmd->params.line.y1,
                .x2 = cmd->params.line.x2,
                .y2 = cmd->params.line.y2,
                .color = cmd->params.line.color
            };
            *sub_cmd_out = FMRB_LINK_GFX_DRAW_LINE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_RECT: {
            fmrb_link_graphics_rect_t c = {
                .canvas_id = cmd->canvas_id,
                .x = cmd->params.rect.rect.x,
                .y = cmd->params.rect.rect.y,
                .width = cmd->params.rect.rect.width,
                .height = cmd->params.rect.rect.height,
                .color = cmd->params.rect.color,
                .filled = cmd->params.rect.filled
            };
            *sub_cmd_out = cmd->params.rect.filled ? FMRB_LINK_GFX_FILL_RECT : FMRB_LINK_GFX_DRAW_RECT;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_CIRCLE: {
            fmrb_link_graphics_circle_t c = {
                .canvas_id = cmd->canvas_id,
                .x = cmd->params.circle.x,
                .y = cmd->params.circle.y,
                .radius = cmd->params.circle.radius,
                .color = cmd->params.circle.color
            };
            *sub_cmd_out = cmd->params.circle.filled ? FMRB_LINK_GFX_FILL_CIRCLE : FMRB_LINK_GFX_DRAW_CIRCLE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_ROUND_RECT: {
            fmrb_link_graphics_round_rect_t c = {
                .canvas_id = cmd->canvas_id,
                .x = cmd->params.round_rect.x,
                .y = cmd->params.round_rect.y,
                .width = cmd->params.round_rect.w,
                .height = cmd->params.round_rect.h,
                .radius = cmd->params.round_rect.radius,
                .color = cmd->params.round_rect.color
            };
            *sub_cmd_out = cmd->params.round_rect.filled ? FMRB_LINK_GFX_FILL_ROUND_RECT : FMRB_LINK_GFX_DRAW_ROUND_RECT;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_ELLIPSE: {
            fmrb_link_graphics_ellipse_t c = {
                .canvas_id = cmd->canvas_id,
                .x = cmd->params.ellipse.x,
                .y = cmd->params.ellipse.y,
                .rx = cmd->params.ellipse.rx,
                .ry = cmd->params.ellipse.ry,
                .color = cmd->params.ellipse.color
            };
            *sub_cmd_out = cmd->params.ellipse.filled ? FMRB_LINK_GFX_FILL_ELLIPSE : FMRB_LINK_GFX_DRAW_ELLIPSE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_TRIANGLE: {
            fmrb_link_graphics_triangle_t c = {
                .canvas_id = cmd->canvas_id,
                .x0 = cmd->params.triangle.x0,
                .y0 = cmd->params.triangle.y0,
                .x1 = cmd->params.triangle.x1,
                .y1 = cmd->params.triangle.y1,
                .x2 = cmd->params.triangle.x2,
                .y2 = cmd->params.triangle.y2,
                .color = cmd->params.triangle.color
            };
            *sub_cmd_out = cmd->params.triangle.filled ? FMRB_LINK_GFX_FILL_TRIANGLE : FMRB_LINK_GFX_DRAW_TRIANGLE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_ARC: {
            fmrb_link_graphics_arc_t c = {
                .canvas_id = cmd->canvas_id,
                .x = cmd->params.arc.x,
                .y = cmd->params.arc.y,
                .r0 = cmd->params.arc.r0,
                .r1 = cmd->params.arc.r1,
                .angle0 = cmd->params.arc.angle0,
                .angle1 = cmd->params.arc.angle1,
                .color = cmd->params.arc.color
            };
            *sub_cmd_out = cmd->params.arc.filled ? FMRB_LINK_GFX_FILL_ARC : FMRB_LINK_GFX_DRAW_ARC;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_TEXT_SIZE: {
            fmrb_link_graphics_text_size_t c = {
                .canvas_id = cmd->canvas_id,
                .size = cmd->params.text_size.size
            };
            *sub_cmd_out = FMRB_LINK_GFX_SET_TEXT_SIZE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_BLEND_RECT: {
            fmrb_link_graphics_blend_rect_t c = {
                .canvas_id = cmd->canvas_id,
                .x = cmd->params.blend_rect.rect.x,
                .y = cmd->params.blend_rect.rect.y,
                .width = cmd->params.blend_rect.rect.width,
                .height = cmd->params.blend_rect.rect.height,
                .color = cmd->params.blend_rect.color,
                .mode = cmd->params.blend_rect.mode
            };
            *sub_cmd_out = FMRB_LINK_GFX_BLEND_RECT;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_TEXT: {
            size_t text_len = strlen(cmd->params.text.text);
            if (text_len > 255) text_len = 255;
            size_t total = sizeof(fmrb_link_graphics_text_t) + text_len;
            fmrb_link_graphics_text_t *t = (fmrb_link_graphics_text_t *)payload_buf;
            t->canvas_id = cmd->canvas_id;
            t->x = cmd->params.text.x;
            t->y = cmd->params.text.y;
            t->color = cmd->params.text.color;
            t->bg_color = cmd->params.text.bg_color;
            t->bg_transparent = cmd->params.text.bg_transparent ? 1 : 0;
            t->text_len = text_len;
            memcpy(payload_buf + sizeof(fmrb_link_graphics_text_t), cmd->params.text.text, text_len);
            *sub_cmd_out = FMRB_LINK_GFX_DRAW_STRING;
            return (int)total;
        }
        case GFX_CMD_PRESENT: {
            fmrb_link_graphics_push_canvas_t c = {
                .canvas_id = cmd->canvas_id,
                .dest_canvas_id = FMRB_CANVAS_RENDER,
                .x = cmd->params.present.x,
                .y = cmd->params.present.y,
                .transparent_color = cmd->params.present.transparent_color,
                .use_transparency = (cmd->params.present.transparent_color != 0xFF) ? 1 : 0
            };
            *sub_cmd_out = FMRB_LINK_GFX_PUSH_CANVAS;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_DRAW_IMAGE: {
            fmrb_link_graphics_draw_image_t c = {
                .canvas_id = cmd->canvas_id,
                .image_id = cmd->params.draw_image.image_id,
                .x = cmd->params.draw_image.x,
                .y = cmd->params.draw_image.y,
                .flags = cmd->params.draw_image.flags,
                .scale_x_fp8 = cmd->params.draw_image.scale_x_fp8,
                .scale_y_fp8 = cmd->params.draw_image.scale_y_fp8
            };
            *sub_cmd_out = FMRB_LINK_GFX_DRAW_IMAGE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_DELETE_IMAGE: {
            fmrb_link_graphics_delete_image_t c = {
                .image_id = cmd->params.delete_image.image_id
            };
            *sub_cmd_out = FMRB_LINK_GFX_DELETE_IMAGE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_SET_OUTPUT_LEVEL: {
            uint8_t level = cmd->params.set_output_level.level;
            *sub_cmd_out = FMRB_LINK_GFX_SET_OUTPUT_LEVEL;
            memcpy(payload_buf, &level, sizeof(level));
            return sizeof(level);
        }
        case GFX_CMD_SET_CHROMA_LEVEL: {
            uint8_t level = cmd->params.set_chroma_level.level;
            *sub_cmd_out = FMRB_LINK_GFX_SET_CHROMA_LEVEL;
            memcpy(payload_buf, &level, sizeof(level));
            return sizeof(level);
        }
        case GFX_CMD_CREATE_CANVAS: {
            fmrb_link_graphics_create_canvas_t c = {
                .canvas_id = 0,
                .width = cmd->params.create_canvas.width,
                .height = cmd->params.create_canvas.height,
                .z_order = cmd->params.create_canvas.z_order,
                .use_transparent = cmd->params.create_canvas.use_transparent,
                .transparent_color = cmd->params.create_canvas.transparent_color
            };
            *sub_cmd_out = FMRB_LINK_GFX_CREATE_CANVAS;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_CREATE_SPRITE_IMAGE: {
            fmrb_link_graphics_create_sprite_image_t c = {
                .canvas_id = cmd->canvas_id,
                .width = cmd->params.create_sprite_image.width,
                .height = cmd->params.create_sprite_image.height,
                .transparent_color = cmd->params.create_sprite_image.transparent_color,
                .use_transparent = cmd->params.create_sprite_image.use_transparent
            };
            *sub_cmd_out = FMRB_LINK_GFX_CREATE_SPRITE_IMAGE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        case GFX_CMD_CREATE_SPRITE_INSTANCE: {
            fmrb_link_graphics_create_sprite_instance_t c;
            memset(&c, 0, sizeof(c));
            c.canvas_id = cmd->canvas_id;
            c.frame_count = cmd->params.create_sprite_instance.frame_count;
            memcpy(c.image_ids, cmd->params.create_sprite_instance.image_ids,
                   sizeof(uint16_t) * c.frame_count);
            c.x = cmd->params.create_sprite_instance.x;
            c.y = cmd->params.create_sprite_instance.y;
            c.z_order = cmd->params.create_sprite_instance.z_order;
            *sub_cmd_out = FMRB_LINK_GFX_CREATE_SPRITE_INSTANCE;
            memcpy(payload_buf, &c, sizeof(c));
            return sizeof(c);
        }
        default:
            return -1;
    }
}

// Callback for sync GFX command response (called from transport layer context)
static void gfx_sync_response_cb(uint8_t status, const uint8_t *payload,
                                  uint32_t payload_len, void *user_data) {
    gfx_cmd_sync_ctx_t *sc = (gfx_cmd_sync_ctx_t *)user_data;
    if (!sc) return;

    sc->result = (status == 0) ? 0 : -1;
    if (sc->response_buf && payload && payload_len > 0) {
        uint16_t copy_len = (payload_len < sc->response_len) ? (uint16_t)payload_len : sc->response_len;
        memcpy(sc->response_buf, payload, copy_len);
        sc->response_len = copy_len;
    } else {
        sc->response_len = 0;
    }

    if (sc->done) {
        fmrb_semaphore_give(sc->done);
    }
}

// Update GFX statistics
static void gfx_stats_update(int cmd_count, int present_count) {
    g_gfx_total_cmds += cmd_count;
    g_gfx_present_count += present_count;

    fmrb_time_t now_us = fmrb_hal_time_get_us();
    if (g_gfx_stats_last_us == 0) {
        g_gfx_stats_last_us = now_us;
    } else if ((now_us - g_gfx_stats_last_us) >= GFX_STATS_INTERVAL_US) {
        uint64_t elapsed_us = now_us - g_gfx_stats_last_us;
        float elapsed_s = (float)elapsed_us / 1000000.0f;
        FMRB_LOGI(TAG, "GFX STATS: %.1f cmds/s, %.1f presents/s",
                 (float)g_gfx_total_cmds / elapsed_s,
                 (float)g_gfx_present_count / elapsed_s);
        g_gfx_total_cmds = 0;
        g_gfx_present_count = 0;
        g_gfx_stats_last_us = now_us;
    }
}

// ============================================================
// File Transfer State Machine
// ============================================================

// Chunk size derived from SPI frame capacity.
// fmrb_link_file_transfer_data_t header (6 bytes) + msgpack array/bin headers (~10 bytes)
// + COBS overhead (~2 bytes) must fit within FMRB_LINK_FRAME_MAX_DATA.
#define FILE_TRANSFER_OVERHEAD 18
#define FILE_TRANSFER_CHUNK_SIZE (FMRB_LINK_FRAME_MAX_DATA - FILE_TRANSFER_OVERHEAD)

// File transfer state (one active transfer at a time)
typedef struct {
    bool active;
    char path[120];
    uint16_t path_len;
    uint8_t *data;
    uint32_t total_len;
    uint32_t offset;
    file_cmd_result_t *cmd_result;  // Points to caller's stack variable
} file_transfer_state_t;

static file_transfer_state_t g_file_transfer = {0};

// Send BEGIN command for file transfer
static fmrb_err_t file_transfer_send_begin(file_transfer_state_t *ft)
{
    size_t payload_len = sizeof(fmrb_link_file_transfer_begin_t) + ft->path_len;
    uint8_t payload_buf[sizeof(fmrb_link_file_transfer_begin_t) + 120];

    fmrb_link_file_transfer_begin_t *hdr = (fmrb_link_file_transfer_begin_t *)payload_buf;
    hdr->total_size = ft->total_len;
    hdr->path_len = ft->path_len;
    memcpy(payload_buf + sizeof(fmrb_link_file_transfer_begin_t), ft->path, ft->path_len);

    return fmrb_transport_send(
        FMRB_LINK_TYPE_FILE_TRANSFER,
        FMRB_LINK_FILE_TRANSFER_BEGIN,
        payload_buf, payload_len,
        5000);
}

// Send one DATA chunk for file transfer
static fmrb_err_t file_transfer_send_chunk(file_transfer_state_t *ft)
{
    uint32_t remaining = ft->total_len - ft->offset;
    uint16_t chunk_len = (remaining > FILE_TRANSFER_CHUNK_SIZE)
                         ? FILE_TRANSFER_CHUNK_SIZE : (uint16_t)remaining;

    size_t payload_len = sizeof(fmrb_link_file_transfer_data_t) + chunk_len;
    // Allocate on stack for small chunks
    uint8_t *payload_buf = (uint8_t *)fmrb_sys_malloc(payload_len);
    if (!payload_buf) {
        FMRB_LOGE(TAG, "file_transfer: chunk alloc failed (%u bytes)", (unsigned)payload_len);
        return FMRB_ERR_NO_MEMORY;
    }

    fmrb_link_file_transfer_data_t *hdr = (fmrb_link_file_transfer_data_t *)payload_buf;
    hdr->offset = ft->offset;
    hdr->chunk_len = chunk_len;
    memcpy(payload_buf + sizeof(fmrb_link_file_transfer_data_t),
           ft->data + ft->offset, chunk_len);

    fmrb_err_t ret = fmrb_transport_send(
        FMRB_LINK_TYPE_FILE_TRANSFER,
        FMRB_LINK_FILE_TRANSFER_DATA,
        payload_buf, payload_len,
        5000);

    fmrb_sys_free(payload_buf);

    if (ret == FMRB_OK) {
        ft->offset += chunk_len;
    }
    return ret;
}

// Send END command for file transfer
static fmrb_err_t file_transfer_send_end(file_transfer_state_t *ft)
{
    fmrb_link_file_transfer_end_t end_cmd = {
        .total_size = ft->total_len,
        .checksum = 0  // TODO: CRC32 calculation
    };

    return fmrb_transport_send(
        FMRB_LINK_TYPE_FILE_TRANSFER,
        FMRB_LINK_FILE_TRANSFER_END,
        (const uint8_t *)&end_cmd, sizeof(end_cmd),
        5000);
}

// Complete file transfer (success or failure)
static void file_transfer_complete(file_transfer_state_t *ft, int32_t result)
{
    // Free the file data buffer (ownership was transferred from caller)
    if (ft->data) {
        fmrb_sys_free(ft->data);
        ft->data = NULL;
    }
    ft->active = false;
    // Write result and signal completion to caller
    if (ft->cmd_result) {
        ft->cmd_result->result = result;
        fmrb_semaphore_give(ft->cmd_result->done_sem);
        ft->cmd_result = NULL;
    }
}

// Process one step of file transfer (called from main loop)
// Returns true if transfer is active (more work to do)
static bool file_transfer_process_step(void)
{
    file_transfer_state_t *ft = &g_file_transfer;
    if (!ft->active) {
        return false;
    }

    if (ft->offset == 0) {
        // Send BEGIN
        fmrb_err_t ret = file_transfer_send_begin(ft);
        if (ret != FMRB_OK) {
            FMRB_LOGE(TAG, "file_transfer: BEGIN failed: %d", ret);
            file_transfer_complete(ft, -1);
            return false;
        }
        FMRB_LOGI(TAG, "file_transfer: BEGIN sent, path=%.*s, size=%u",
                  (int)ft->path_len, ft->path, (unsigned)ft->total_len);
    }

    if (ft->offset < ft->total_len) {
        // Send one chunk
        fmrb_err_t ret = file_transfer_send_chunk(ft);
        if (ret != FMRB_OK) {
            FMRB_LOGE(TAG, "file_transfer: DATA chunk failed at offset %u: %d",
                      (unsigned)ft->offset, ret);
            file_transfer_complete(ft, -2);
            return false;
        }
    }

    if (ft->offset >= ft->total_len) {
        // Send END
        fmrb_err_t ret = file_transfer_send_end(ft);
        if (ret != FMRB_OK) {
            FMRB_LOGE(TAG, "file_transfer: END failed: %d", ret);
            file_transfer_complete(ft, -3);
            return false;
        }
        FMRB_LOGI(TAG, "file_transfer: complete, %u bytes transferred", (unsigned)ft->total_len);
        file_transfer_complete(ft, 0);
        return false;
    }

    return true;  // More chunks to send
}

// Start a new file transfer from a FILE_CMD_TRANSFER message
static void file_transfer_start(const file_cmd_t *cmd)
{
    file_transfer_state_t *ft = &g_file_transfer;

    if (ft->active) {
        FMRB_LOGE(TAG, "file_transfer: already active, rejecting new transfer");
        if (cmd->result) {
            cmd->result->result = -4;
            fmrb_semaphore_give(cmd->result->done_sem);
        }
        return;
    }

    ft->active = true;
    ft->path_len = cmd->path_len;
    memcpy(ft->path, cmd->path, cmd->path_len);
    ft->data = cmd->params.transfer.data;
    ft->total_len = cmd->params.transfer.data_len;
    ft->offset = 0;
    ft->cmd_result = cmd->result;

    FMRB_LOGI(TAG, "file_transfer: starting, path=%.*s, size=%u",
              (int)ft->path_len, ft->path, (unsigned)ft->total_len);
}

// Callback for FILE_CMD_STATUS async response (runs in host_task context)
static void file_status_response_cb(uint8_t status, const uint8_t *payload,
                                     uint32_t payload_len, void *user_data) {
    file_cmd_result_t *result = (file_cmd_result_t *)user_data;
    if (!result) return;

    if (status == 0 && payload_len >= sizeof(fmrb_link_file_transfer_status_resp_t)) {
        const fmrb_link_file_transfer_status_resp_t *resp =
            (const fmrb_link_file_transfer_status_resp_t *)payload;
        result->data.status.exists = resp->exists;
        result->data.status.file_size = resp->file_size;
        result->data.status.checksum = resp->checksum;
        result->result = 0;
    } else {
        result->data.status.exists = 0;
        result->data.status.file_size = 0;
        result->data.status.checksum = 0;
        result->result = -1;
    }
    fmrb_semaphore_give(result->done_sem);
}

// Handle FILE_CMD_STATUS asynchronously (non-blocking for host_task)
static void file_transfer_handle_status(const file_cmd_t *cmd)
{
    size_t payload_len = sizeof(fmrb_link_file_transfer_status_t) + cmd->path_len;
    uint8_t payload_buf[sizeof(fmrb_link_file_transfer_status_t) + 120];

    fmrb_link_file_transfer_status_t *hdr = (fmrb_link_file_transfer_status_t *)payload_buf;
    hdr->path_len = cmd->path_len;
    memcpy(payload_buf + sizeof(fmrb_link_file_transfer_status_t), cmd->path, cmd->path_len);

    fmrb_err_t ret = fmrb_transport_send_async(
        FMRB_LINK_TYPE_FILE_TRANSFER,
        FMRB_LINK_FILE_TRANSFER_STATUS,
        payload_buf, payload_len,
        file_status_response_cb, cmd->result,
        5000);

    if (ret != FMRB_OK && cmd->result) {
        // Send failed: signal caller immediately
        cmd->result->result = -1;
        fmrb_semaphore_give(cmd->result->done_sem);
    }
    // On success: callback will signal cmd->result->done_sem when response arrives
}

// Handle FILE_CMD_DELETE synchronously
static void file_transfer_handle_delete(const file_cmd_t *cmd)
{
    size_t payload_len = sizeof(fmrb_link_file_transfer_delete_t) + cmd->path_len;
    uint8_t payload_buf[sizeof(fmrb_link_file_transfer_delete_t) + 120];

    fmrb_link_file_transfer_delete_t *hdr = (fmrb_link_file_transfer_delete_t *)payload_buf;
    hdr->path_len = cmd->path_len;
    memcpy(payload_buf + sizeof(fmrb_link_file_transfer_delete_t), cmd->path, cmd->path_len);

    fmrb_err_t ret = fmrb_transport_send(
        FMRB_LINK_TYPE_FILE_TRANSFER,
        FMRB_LINK_FILE_TRANSFER_DELETE,
        payload_buf, payload_len,
        5000);

    if (cmd->result) {
        cmd->result->result = (ret == FMRB_OK) ? 0 : -1;
        fmrb_semaphore_give(cmd->result->done_sem);
    }
}

// Process a FILE_TRANSFER message from the queue
static void host_task_process_file_transfer(const fmrb_msg_t *msg)
{
    file_cmd_t *cmd = (file_cmd_t *)msg->data;

    switch (cmd->cmd_type) {
        case FILE_CMD_TRANSFER:
            file_transfer_start(cmd);
            break;
        case FILE_CMD_STATUS:
            file_transfer_handle_status(cmd);
            break;
        case FILE_CMD_DELETE:
            file_transfer_handle_delete(cmd);
            break;
        default:
            FMRB_LOGW(TAG, "Unknown file command type: %d", cmd->cmd_type);
            break;
    }
}

/**
 * Process GFX command messages with batch optimization.
 *
 * Drains all pending GFX messages from the queue and sends them
 * via fmrb_transport_send_batch() to pack multiple commands into
 * minimal SPI frames.
 */
static void host_task_process_gfx_batch(const fmrb_msg_t *first_msg)
{
    // Collect GFX commands into batch
    gfx_cmd_t cmds[GFX_BATCH_MAX];
    int count = 0;
    gfx_cmd_t *pending_sync_cmd = NULL;  // Sync command found during batching

    // First message is already received
    gfx_cmd_t *first_cmd = (gfx_cmd_t *)first_msg->data;
    if (first_cmd->sync != NULL) {
        // First command is sync - no batch, handle directly
        pending_sync_cmd = first_cmd;
        goto handle_sync;
    }
    cmds[count++] = *first_cmd;

    // Drain additional GFX messages (non-blocking)
    // Stop at sync command - don't skip past it
    fmrb_msg_t next;
    while (count < GFX_BATCH_MAX &&
           fmrb_msg_receive(PROC_ID_HOST, &next, 0) == FMRB_OK) {
        if (next.type == FMRB_MSG_TYPE_APP_GFX) {
            gfx_cmd_t *queued = (gfx_cmd_t *)next.data;
            if (queued->sync != NULL) {
                // Sync command: stop batching, save for after batch send
                // Copy sync pointer before next.data gets reused
                static gfx_cmd_t s_sync_cmd_buf;
                s_sync_cmd_buf = *queued;
                pending_sync_cmd = &s_sync_cmd_buf;
                break;
            }
            cmds[count++] = *queued;
        } else {
            // Non-GFX message: process it immediately, stop batching
            host_task_process_message(&next);
            break;
        }
    }

    // Convert all commands to batch entries
    fmrb_transport_batch_entry_t entries[GFX_BATCH_MAX];
    uint8_t payload_bufs[GFX_BATCH_MAX][GFX_BATCH_PAYLOAD_BUF_SIZE];
    int batch_count = 0;
    int present_count = 0;

    for (int i = 0; i < count; i++) {
        uint8_t sub_cmd;
        int payload_len = gfx_cmd_to_batch_entry(&cmds[i], &sub_cmd, payload_bufs[batch_count]);
        if (payload_len < 0) {
            FMRB_LOGW(TAG, "Unknown GFX command type: %d", cmds[i].cmd_type);
            continue;
        }
        entries[batch_count].link_type = FMRB_LINK_TYPE_GRAPHICS;
        entries[batch_count].sub_cmd = sub_cmd;
        entries[batch_count].payload = payload_bufs[batch_count];
        entries[batch_count].payload_len = (uint32_t)payload_len;
        batch_count++;

        if (cmds[i].cmd_type == GFX_CMD_PRESENT) {
            present_count++;
        }
    }

    // Send batch
    if (batch_count > 0) {
        fmrb_err_t ret = fmrb_transport_send_batch(
            entries, batch_count,
            FMRB_TRANSPORT_TIMEOUT_DEFAULT);

        if (ret != FMRB_OK) {
            FMRB_LOGE(TAG, "Batch send failed: %d (count=%d)", ret, batch_count);
        }

        gfx_stats_update(batch_count, present_count);

        if (batch_count > 1) {
            FMRB_LOGD(TAG, "GFX batch: %d commands sent", batch_count);
        }
    }

    // Handle pending sync command (after batch is sent, so order is preserved)
handle_sync:
    if (pending_sync_cmd != NULL) {
        uint8_t sub_cmd;
        uint8_t sync_payload[GFX_BATCH_PAYLOAD_BUF_SIZE];
        int payload_len = gfx_cmd_to_batch_entry(pending_sync_cmd, &sub_cmd, sync_payload);

        if (payload_len >= 0 && pending_sync_cmd->sync != NULL) {
            gfx_cmd_sync_ctx_t *sc = pending_sync_cmd->sync;
            fmrb_err_t ret = fmrb_transport_send_async(
                FMRB_LINK_TYPE_GRAPHICS | FMRB_LINK_FLAG_ACK_REQUIRED,
                sub_cmd,
                sync_payload,
                (uint32_t)payload_len,
                gfx_sync_response_cb,
                sc,
                5000  // 5 second timeout
            );
            if (ret != FMRB_OK) {
                FMRB_LOGE(TAG, "Sync GFX send_async failed: %d", ret);
                sc->result = -1;
                sc->response_len = 0;
                fmrb_semaphore_give(sc->done);
            }
        }
        // Count sync command for semaphore release
        count++;
    }

    // Release semaphore slots for all processed commands
    if (g_host_gfx_queue_semaphore) {
        for (int i = 0; i < count; i++) {
            fmrb_semaphore_give(g_host_gfx_queue_semaphore);
        }
    }
}

/**
 * Process a host message
 */
static void host_task_process_message(const fmrb_msg_t *hal_msg)
{
    // Check if it's a GFX message first - use batch processing
    if (hal_msg->type == FMRB_MSG_TYPE_APP_GFX) {
        host_task_process_gfx_batch(hal_msg);
        return;
    }

    // File transfer message
    if (hal_msg->type == FMRB_MSG_TYPE_FILE_TRANSFER) {
        host_task_process_file_transfer(hal_msg);
        return;
    }

    // Audio message from kernel
    if (hal_msg->type == FMRB_MSG_TYPE_APP_AUDIO) {
        if (hal_msg->size > 0) {
            FMRB_LOGI(TAG, "Audio command: cmd_type=0x%02x, size=%lu",
                      hal_msg->data[0], (unsigned long)hal_msg->size);
            fmrb_transport_send(
                FMRB_LINK_TYPE_AUDIO, 0,
                hal_msg->data, hal_msg->size,
                FMRB_TRANSPORT_TIMEOUT_DEFAULT);
        }
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
            // Global hotkey: Ctrl+Q (intercept before routing)
            if (msg->type == HOST_MSG_HID_KEY_DOWN) {
                uint8_t mod = (uint8_t)(msg->data.key.modifier & 0xFF);
                uint8_t sc = (uint8_t)(msg->data.key.scancode & 0xFF);
                // Check for Ctrl key: both FMRB format (0x04/0x08) and SDL2 format (0x40/0x80)
                bool has_ctrl = (mod & (FMRB_KEYMAP_MOD_LCTRL | FMRB_KEYMAP_MOD_RCTRL | 0x40 | 0x80)) != 0;
                if (has_ctrl && sc == 0x14) {  // Q scancode
                    FMRB_LOGI(TAG, "Ctrl+Q detected - sending system interrupt");
                    // Send system interrupt to kernel (PID 0)
                    fmrb_msg_t sys_msg = {
                        .type = FMRB_MSG_TYPE_APP_CONTROL,
                        .src_pid = PROC_ID_HOST,
                        .size = 0
                    };
                    // Pack minimal msgpack: {"cmd": "system_interrupt"}
                    const char *payload = "\x81\xa3""cmd\xb0""system_interrupt";
                    sys_msg.size = 22;
                    memcpy(sys_msg.data, payload, sys_msg.size);
                    fmrb_msg_send(0, &sys_msg, 1000);  // PID 0 = kernel
                    break;  // Don't forward to app
                }
            }

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

            // Check if target app is still running (not suspended/stopped)
            {
                fmrb_app_task_context_t* target_ctx = fmrb_app_get_context_by_id(routing.target_pid);
                if (!target_ctx || target_ctx->state != PROC_STATE_RUNNING) {
                    FMRB_LOGW(TAG, "Key event target PID %d not running (state=%d), discarding",
                             routing.target_pid, target_ctx ? target_ctx->state : -1);
                    break;
                }
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

            FMRB_LOGD(TAG, "Mouse click: button=%d, pos=(%d,%d), state=%s - forwarding to Kernel for hit test",
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

        case HOST_MSG_HID_GAMEPAD_BUTTON: {
            FMRB_LOGD(TAG, "Gamepad button: id=%d, button=%d, state=%s",
                     msg->data.gamepad_button.gamepad_id,
                     msg->data.gamepad_button.button_num,
                     msg->data.gamepad_button.state ? "pressed" : "released");

            fmrb_hid_routing_t routing;
            if (fmrb_kernel_get_hid_routing(&routing) != FMRB_OK) {
                break;
            }
            if (!routing.routing_enabled || routing.target_pid == 0xFF) {
                break;
            }

            fmrb_msg_t hid_msg = {
                .type = FMRB_MSG_TYPE_HID_EVENT,
                .src_pid = PROC_ID_HOST,
                .size = sizeof(fmrb_hid_gamepad_button_event_t)
            };
            fmrb_hid_gamepad_button_event_t *btn_event =
                (fmrb_hid_gamepad_button_event_t*)hid_msg.data;
            btn_event->subtype = msg->data.gamepad_button.state
                ? HID_MSG_GAMEPAD_BUTTON_DOWN : HID_MSG_GAMEPAD_BUTTON_UP;
            btn_event->gamepad_id = (uint8_t)msg->data.gamepad_button.gamepad_id;
            btn_event->button_num = (uint8_t)msg->data.gamepad_button.button_num;

            fmrb_err_t ret = FMRB_ERR_TIMEOUT;
            for (int retry = 0; retry < 3; retry++) {
                ret = fmrb_msg_send(routing.target_pid, &hid_msg, 5000);
                if (ret == FMRB_OK) break;
                fmrb_task_delay(FMRB_MS_TO_TICKS(100));
            }
            if (ret != FMRB_OK) {
                FMRB_LOGE(TAG, "Gamepad button event dropped after 3 retries to PID %d",
                         routing.target_pid);
            }
            break;
        }

        case HOST_MSG_HID_GAMEPAD_AXIS: {
            FMRB_LOGD(TAG, "Gamepad axis: id=%d, axis=%d, value=%d",
                     msg->data.gamepad_axis.gamepad_id,
                     msg->data.gamepad_axis.axis_num,
                     msg->data.gamepad_axis.value);

            fmrb_hid_routing_t routing;
            if (fmrb_kernel_get_hid_routing(&routing) != FMRB_OK) {
                break;
            }
            if (!routing.routing_enabled || routing.target_pid == 0xFF) {
                break;
            }

            fmrb_msg_t hid_msg = {
                .type = FMRB_MSG_TYPE_HID_EVENT,
                .src_pid = PROC_ID_HOST,
                .size = sizeof(fmrb_hid_gamepad_axis_event_t)
            };
            fmrb_hid_gamepad_axis_event_t *axis_event =
                (fmrb_hid_gamepad_axis_event_t*)hid_msg.data;
            axis_event->subtype = HID_MSG_GAMEPAD_AXIS;
            axis_event->gamepad_id = (uint8_t)msg->data.gamepad_axis.gamepad_id;
            axis_event->axis_num = (uint8_t)msg->data.gamepad_axis.axis_num;
            axis_event->value = (int16_t)msg->data.gamepad_axis.value;

            // Axis events can be dropped if queue is full (high-frequency)
            fmrb_msg_send(routing.target_pid, &hid_msg, 5000);
            break;
        }

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

        // File transfer interleaving: send one chunk per loop iteration
        // This ensures GFX commands are processed between chunks
        file_transfer_process_step();

        // Process incoming IPC messages (ACK/NACK responses)
        // This MUST be called regularly to receive responses for sync requests
        fmrb_transport_process();

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
    fmrb_base_type_t result = fmrb_task_create_ex(
        fmrb_host_task,
        "fmrb_host",
        FMRB_HOST_TASK_STACK_SIZE,
        NULL,
        FMRB_HOST_TASK_PRIORITY,
        &g_host_task_handle,
        FMRB_HOST_TASK_FLAGS
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
    FMRB_LOGD(TAG, "KEY_DOWN: code=%d scan=%d mod=0x%x", key_code, scancode, modifier);
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
    FMRB_LOGD(TAG, "KEY_UP: code=%d scan=%d mod=0x%x", key_code, scancode, modifier);
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

    FMRB_LOGD(TAG, "MOUSE_MOVE: x=%d y=%d", x, y);
    host_message_t msg = {
        .type = HOST_MSG_HID_MOUSE_MOVE,
        .data.mouse_move.x = x,
        .data.mouse_move.y = y
    };
    return fmrb_host_send_message(&msg);
}

int fmrb_host_send_mouse_click(int x, int y, int button, int state)
{
    FMRB_LOGD(TAG, "MOUSE_CLICK: x=%d y=%d btn=%d state=%d", x, y, button, state);
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
