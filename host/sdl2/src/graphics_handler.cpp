#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <cstdio>
#include <cstring>
#include <map>

extern "C" {
#include "graphics_handler.h"
#include "fmrb_link_protocol.h"
#include "fmrb_gfx.h"
}

// Graphics handler log levels
typedef enum {
    GFX_LOG_NONE = 0,     // No logging
    GFX_LOG_ERROR = 1,    // Error messages only
    GFX_LOG_INFO = 2,     // Info + Error
    GFX_LOG_DEBUG = 3,    // Debug + Info + Error (verbose)
} gfx_log_level_t;

// Current log level (can be controlled via environment variable or compile-time)
static gfx_log_level_t g_gfx_log_level = GFX_LOG_ERROR;  // Default: errors only

// Log macros
#define GFX_LOG_E(fmt, ...) do { if (g_gfx_log_level >= GFX_LOG_ERROR) { fprintf(stderr, "[GFX_ERR] " fmt "\n", ##__VA_ARGS__); } } while(0)
#define GFX_LOG_I(fmt, ...) do { if (g_gfx_log_level >= GFX_LOG_INFO) { printf("[GFX_INFO] " fmt "\n", ##__VA_ARGS__); } } while(0)
#define GFX_LOG_D(fmt, ...) do { if (g_gfx_log_level >= GFX_LOG_DEBUG) { printf("[GFX_DBG] " fmt "\n", ##__VA_ARGS__); } } while(0)

// Function to set log level at runtime
extern "C" void graphics_handler_set_log_level(int level) {
    if (level >= GFX_LOG_NONE && level <= GFX_LOG_DEBUG) {
        g_gfx_log_level = (gfx_log_level_t)level;
        printf("[GFX] Log level set to %d\n", level);
    }
}

// External reference to LGFX instance created in main.cpp
extern LGFX* g_lgfx;

// Canvas management (LovyanGFX sprites)
static std::map<uint16_t, LGFX_Sprite*> g_canvases;
static uint16_t g_current_target = FMRB_CANVAS_SCREEN;  // 0=screen, other=canvas
static bool g_graphics_initialized = false;  // Flag to prevent multiple initializations

// Get current drawing target (screen or canvas)
static LovyanGFX* get_current_target() {
    if (g_current_target == FMRB_CANVAS_SCREEN) {
        // Draw directly to screen
        return g_lgfx;
    }
    auto it = g_canvases.find(g_current_target);
    if (it != g_canvases.end()) {
        return it->second;  // Canvas sprite
    }
    GFX_LOG_E("Canvas %u not found, using screen", g_current_target);
    return g_lgfx;  // Fallback to screen
}

extern "C" int graphics_handler_init(SDL_Renderer *renderer) {
    (void)renderer; // SDL_Renderer is not used with LovyanGFX

    // Prevent multiple initializations
    if (g_graphics_initialized) {
        GFX_LOG_E("Graphics handler already initialized, ignoring request");
        return 0;  // Return success to avoid breaking caller
    }

    if (!g_lgfx) {
        GFX_LOG_E("LGFX instance not created");
        return -1;
    }

    g_graphics_initialized = true;  // Mark as initialized
    GFX_LOG_I("Graphics handler initialized (using external LGFX instance, direct rendering)");
    return 0;
}

extern "C" void graphics_handler_cleanup(void) {
    // Delete all canvases
    for (auto& pair : g_canvases) {
        delete pair.second;
    }
    g_canvases.clear();
    g_current_target = FMRB_CANVAS_SCREEN;
    g_graphics_initialized = false;  // Reset initialization flag

    // Note: g_lgfx is managed by main.cpp, don't delete here
    GFX_LOG_I("Graphics handler cleaned up");
}

extern "C" SDL_Renderer* graphics_handler_get_renderer(void) {
    return nullptr; // Not used with LovyanGFX
}

// Forward declaration - implemented in socket_server.c
extern "C" int socket_server_send_ack(uint8_t type, uint8_t seq, const uint8_t *response_data, uint16_t response_len);

// Next canvas ID to allocate
static uint16_t g_next_canvas_id = 1;

extern "C" int graphics_handler_process_command(uint8_t msg_type, uint8_t cmd_type, uint8_t seq, const uint8_t *data, size_t size) {
    if (!g_lgfx) {
        return -1;
    }

    // msg_type: message type (for ACK response)
    // cmd_type: graphics command type (from msgpack sub_cmd field)
    // data: structure data only (no cmd_type prefix)

    switch (cmd_type) {
        case FMRB_LINK_GFX_CLEAR:
        case FMRB_LINK_GFX_FILL_SCREEN:
            if (size >= sizeof(fmrb_link_graphics_clear_t)) {
                const fmrb_link_graphics_clear_t *cmd = (const fmrb_link_graphics_clear_t*)data;
                GFX_LOG_D("CLEAR/FILL_SCREEN: canvas_id=%u, color=0x%02x", cmd->canvas_id, cmd->color);

                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                    GFX_LOG_D("CLEAR: Using back buffer");
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                    GFX_LOG_D("CLEAR: Using canvas %u", cmd->canvas_id);
                }
                target->fillScreen(cmd->color);
                GFX_LOG_D("CLEAR: fillScreen executed");
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_PIXEL:
            if (size >= sizeof(fmrb_link_graphics_pixel_t)) {
                const fmrb_link_graphics_pixel_t *cmd = (const fmrb_link_graphics_pixel_t*)data;
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawPixel(cmd->x, cmd->y, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_LINE:
            if (size >= sizeof(fmrb_link_graphics_line_t)) {
                const fmrb_link_graphics_line_t *cmd = (const fmrb_link_graphics_line_t*)data;
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawLine(cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_RECT:
            if (size >= sizeof(fmrb_link_graphics_rect_t)) {
                const fmrb_link_graphics_rect_t *cmd = (const fmrb_link_graphics_rect_t*)data;
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_FILL_RECT:
            if (size >= sizeof(fmrb_link_graphics_rect_t)) {
                const fmrb_link_graphics_rect_t *cmd = (const fmrb_link_graphics_rect_t*)data;
                GFX_LOG_D("FILL_RECT: canvas_id=%u, x=%d, y=%d, w=%d, h=%d, color=0x%02x",
                       cmd->canvas_id, cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                    GFX_LOG_D("FILL_RECT: Using back buffer");
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                    GFX_LOG_D("FILL_RECT: Using canvas %u", cmd->canvas_id);
                }
                target->fillRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                GFX_LOG_D("FILL_RECT: fillRect executed");
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_ROUND_RECT:
            if (size >= sizeof(fmrb_link_graphics_round_rect_t)) {
                const fmrb_link_graphics_round_rect_t *cmd = (const fmrb_link_graphics_round_rect_t*)data;
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_FILL_ROUND_RECT:
            if (size >= sizeof(fmrb_link_graphics_round_rect_t)) {
                const fmrb_link_graphics_round_rect_t *cmd = (const fmrb_link_graphics_round_rect_t*)data;
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->fillRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_CIRCLE:
            if (size >= sizeof(fmrb_link_graphics_circle_t)) {
                const fmrb_link_graphics_circle_t *cmd = (const fmrb_link_graphics_circle_t*)data;
                GFX_LOG_D("DRAW_CIRCLE: canvas_id=%u, x=%d, y=%d, r=%d, color=0x%02x",
                       cmd->canvas_id, cmd->x, cmd->y, cmd->radius, cmd->color);
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                    GFX_LOG_D("DRAW_CIRCLE: Using back buffer");
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                    GFX_LOG_D("DRAW_CIRCLE: Using canvas %u", cmd->canvas_id);
                }
                target->drawCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
                GFX_LOG_D("DRAW_CIRCLE: drawCircle executed");
                return 0;
            }
            break;

        case FMRB_LINK_GFX_FILL_CIRCLE:
            if (size >= sizeof(fmrb_link_graphics_circle_t)) {
                const fmrb_link_graphics_circle_t *cmd = (const fmrb_link_graphics_circle_t*)data;
                GFX_LOG_D("FILL_CIRCLE: canvas_id=%u, x=%d, y=%d, r=%d, color=0x%02x",
                       cmd->canvas_id, cmd->x, cmd->y, cmd->radius, cmd->color);
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                    GFX_LOG_D("FILL_CIRCLE: Using back buffer");
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                    GFX_LOG_D("FILL_CIRCLE: Using canvas %u", cmd->canvas_id);
                }
                target->fillCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
                GFX_LOG_D("FILL_CIRCLE: fillCircle executed");
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_ELLIPSE:
            if (size >= sizeof(fmrb_link_graphics_ellipse_t)) {
                const fmrb_link_graphics_ellipse_t *cmd = (const fmrb_link_graphics_ellipse_t*)data;
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_FILL_ELLIPSE:
            if (size >= sizeof(fmrb_link_graphics_ellipse_t)) {
                const fmrb_link_graphics_ellipse_t *cmd = (const fmrb_link_graphics_ellipse_t*)data;
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->fillEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_TRIANGLE:
            if (size >= sizeof(fmrb_link_graphics_triangle_t)) {
                const fmrb_link_graphics_triangle_t *cmd = (const fmrb_link_graphics_triangle_t*)data;
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_FILL_TRIANGLE:
            if (size >= sizeof(fmrb_link_graphics_triangle_t)) {
                const fmrb_link_graphics_triangle_t *cmd = (const fmrb_link_graphics_triangle_t*)data;
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->fillTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DRAW_STRING:
            // Use structure from fmrb_link_protocol.h (no cmd_type in data)
            if (size < sizeof(fmrb_link_graphics_text_t)) {
                GFX_LOG_E("String command too small: size=%zu, expected>=%zu", size, sizeof(fmrb_link_graphics_text_t));
                break;
            }
            {
                const fmrb_link_graphics_text_t *text_cmd = (const fmrb_link_graphics_text_t*)data;

                size_t expected_size = sizeof(fmrb_link_graphics_text_t) + text_cmd->text_len;
                if (size < expected_size) {
                    GFX_LOG_E("String command size mismatch: expected=%zu, actual=%zu, text_len=%u",
                            expected_size, size, text_cmd->text_len);
                    break;
                }

                // Text data follows the structure
                const char *text_data = (const char*)(data + sizeof(fmrb_link_graphics_text_t));
                char text_buf[256];
                size_t len = text_cmd->text_len < 255 ? text_cmd->text_len : 255;
                memcpy(text_buf, text_data, len);
                text_buf[len] = '\0';

                GFX_LOG_D("DRAW_STRING: canvas_id=%u, x=%d, y=%d, color=0x%02x, text='%s'",
                       text_cmd->canvas_id, text_cmd->x, text_cmd->y, text_cmd->color, text_buf);

                // Get target from command
                LovyanGFX* target;
                if (text_cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = g_lgfx;
                    GFX_LOG_D("DRAW_STRING: Using back buffer");
                } else {
                    auto it = g_canvases.find(text_cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found", text_cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                    GFX_LOG_D("DRAW_STRING: Using canvas %u", text_cmd->canvas_id);
                }
                target->setTextColor(text_cmd->color);
                target->setCursor(text_cmd->x, text_cmd->y);
                target->print(text_buf);
                GFX_LOG_D("DRAW_STRING: Text drawn");
                return 0;
            }

        case FMRB_LINK_GFX_PRESENT:
            if (size >= sizeof(fmrb_link_graphics_present_t)) {
                const fmrb_link_graphics_present_t *cmd = (const fmrb_link_graphics_present_t*)data;
                GFX_LOG_D("PRESENT: canvas_id=%u", cmd->canvas_id);

                // Simply update the display - all drawing is already done on g_lgfx
                g_lgfx->display();
                GFX_LOG_D("PRESENT: display() called");
                return 0;
            }
            break;

        // Canvas management commands
        case FMRB_LINK_GFX_CREATE_CANVAS:
            if (size >= sizeof(fmrb_link_graphics_create_canvas_t)) {
                const fmrb_link_graphics_create_canvas_t *cmd = (const fmrb_link_graphics_create_canvas_t*)data;

                // Allocate new canvas ID (ignore cmd->canvas_id from client)
                uint16_t canvas_id = g_next_canvas_id++;
                if (canvas_id == 0xFFFF) {  // FMRB_CANVAS_INVALID
                    canvas_id = g_next_canvas_id++;  // Skip invalid value
                }

                // Create new sprite
                LGFX_Sprite* sprite = new LGFX_Sprite(g_lgfx);
                sprite->setColorDepth(8);  // RGB332
                if (!sprite->createSprite(cmd->width, cmd->height)) {
                    GFX_LOG_E("Failed to create sprite %u (%dx%d)",
                            canvas_id, cmd->width, cmd->height);
                    delete sprite;
                    return -1;
                }

                g_canvases[canvas_id] = sprite;
                GFX_LOG_I("Canvas created: ID=%u, %dx%d", canvas_id, cmd->width, cmd->height);

                // Send ACK with canvas_id
                socket_server_send_ack(msg_type, seq, (const uint8_t*)&canvas_id, sizeof(canvas_id));
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DELETE_CANVAS:
            if (size >= sizeof(fmrb_link_graphics_delete_canvas_t)) {
                const fmrb_link_graphics_delete_canvas_t *cmd = (const fmrb_link_graphics_delete_canvas_t*)data;

                auto it = g_canvases.find(cmd->canvas_id);
                if (it == g_canvases.end()) {
                    GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                    return -1;
                }

                // If deleting current target, switch back to screen
                if (g_current_target == cmd->canvas_id) {
                    g_current_target = FMRB_CANVAS_SCREEN;
                }

                delete it->second;
                g_canvases.erase(it);
                GFX_LOG_I("Canvas deleted: ID=%u", cmd->canvas_id);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_SET_TARGET:
            if (size >= sizeof(fmrb_link_graphics_set_target_t)) {
                const fmrb_link_graphics_set_target_t *cmd = (const fmrb_link_graphics_set_target_t*)data;

                // Validate target
                if (cmd->target_id != FMRB_CANVAS_SCREEN) {
                    if (g_canvases.find(cmd->target_id) == g_canvases.end()) {
                        GFX_LOG_E("Canvas %u not found for set_target", cmd->target_id);
                        return -1;
                    }
                }

                g_current_target = cmd->target_id;
                GFX_LOG_D("Drawing target set: ID=%u %s", cmd->target_id,
                       cmd->target_id == FMRB_CANVAS_SCREEN ? "(screen)" : "(canvas)");
                return 0;
            }
            break;

        case FMRB_LINK_GFX_PUSH_CANVAS:
            if (size >= sizeof(fmrb_link_graphics_push_canvas_t)) {
                const fmrb_link_graphics_push_canvas_t *cmd = (const fmrb_link_graphics_push_canvas_t*)data;

                // Find source canvas
                auto it = g_canvases.find(cmd->canvas_id);
                if (it == g_canvases.end()) {
                    GFX_LOG_E("Canvas %u not found for push", cmd->canvas_id);
                    return -1;
                }

                // Determine destination (screen or canvas)
                LovyanGFX* dst;
                const char* dst_name;
                if (cmd->dest_canvas_id == FMRB_CANVAS_SCREEN) {
                    dst = g_lgfx;
                    dst_name = "screen";
                } else {
                    auto dst_it = g_canvases.find(cmd->dest_canvas_id);
                    if (dst_it == g_canvases.end()) {
                        GFX_LOG_E("Destination canvas %u not found for push", cmd->dest_canvas_id);
                        return -1;
                    }
                    dst = dst_it->second;
                    dst_name = "canvas";
                }

                // Push sprite to destination
                LGFX_Sprite* src_sprite = it->second;
                GFX_LOG_D("PUSH_CANVAS: src=%p (%dx%d), dst=%p (%s), pos=(%d,%d)",
                       src_sprite, src_sprite->width(), src_sprite->height(),
                       dst, dst_name, cmd->x, cmd->y);
                if (cmd->use_transparency) {
                    src_sprite->pushSprite(dst, cmd->x, cmd->y, cmd->transparent_color);
                    GFX_LOG_D("Canvas pushed with transparency: ID=%u to %s %u at (%d,%d), transp=0x%02x",
                           cmd->canvas_id, dst_name, cmd->dest_canvas_id, cmd->x, cmd->y, cmd->transparent_color);
                } else {
                    src_sprite->pushSprite(dst, cmd->x, cmd->y);
                    GFX_LOG_D("Canvas pushed: ID=%u to %s %u at (%d,%d)",
                           cmd->canvas_id, dst_name, cmd->dest_canvas_id, cmd->x, cmd->y);
                }
                return 0;
            }
            break;

        default:
            GFX_LOG_E("Unknown graphics command: 0x%02x", cmd_type);
            return -1;
    }

    GFX_LOG_E("Invalid command size for type 0x%02x (size=%zu)", cmd_type, size);
    return -1;
}
