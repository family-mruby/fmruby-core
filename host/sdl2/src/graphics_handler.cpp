#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include "../common/graphics_commands.h"
#include <cstdio>
#include <cstring>
#include <map>

extern "C" {
#include "graphics_handler.h"
}

// External reference to LGFX instance created in main.cpp
extern LGFX* g_lgfx;

// Back buffer for double buffering
static LGFX_Sprite* g_back_buffer = nullptr;

// Canvas management (LovyanGFX sprites)
static std::map<uint16_t, LGFX_Sprite*> g_canvases;
static uint16_t g_current_target = FMRB_CANVAS_SCREEN;  // 0=screen, other=canvas

// Get current drawing target (back buffer or canvas)
static LovyanGFX* get_current_target() {
    if (g_current_target == FMRB_CANVAS_SCREEN) {
        // Draw to back buffer for double buffering
        return g_back_buffer ? static_cast<LovyanGFX*>(g_back_buffer) : g_lgfx;
    }
    auto it = g_canvases.find(g_current_target);
    if (it != g_canvases.end()) {
        return it->second;  // Canvas sprite
    }
    fprintf(stderr, "Warning: Canvas %u not found, using back buffer\n", g_current_target);
    return g_back_buffer ? static_cast<LovyanGFX*>(g_back_buffer) : g_lgfx;  // Fallback to back buffer
}

extern "C" int graphics_handler_init(SDL_Renderer *renderer) {
    (void)renderer; // SDL_Renderer is not used with LovyanGFX

    if (!g_lgfx) {
        fprintf(stderr, "LGFX instance not created\n");
        return -1;
    }

    // Create back buffer sprite for double buffering
    g_back_buffer = new LGFX_Sprite(g_lgfx);
    if (!g_back_buffer) {
        fprintf(stderr, "Failed to allocate back buffer sprite\n");
        return -1;
    }

    g_back_buffer->setColorDepth(8);  // RGB332 to match main screen
    if (!g_back_buffer->createSprite(480, 320)) {
        fprintf(stderr, "Failed to create back buffer sprite (480x320)\n");
        delete g_back_buffer;
        g_back_buffer = nullptr;
        return -1;
    }

    g_back_buffer->fillScreen(FMRB_COLOR_BLACK);  // Initialize to black
    printf("Graphics handler initialized (using external LGFX instance with back buffer)\n");
    printf("Back buffer created: %p, size: 480x320, color depth: 8\n", (void*)g_back_buffer);
    return 0;
}

extern "C" void graphics_handler_cleanup(void) {
    // Delete back buffer
    if (g_back_buffer) {
        delete g_back_buffer;
        g_back_buffer = nullptr;
    }

    // Delete all canvases
    for (auto& pair : g_canvases) {
        delete pair.second;
    }
    g_canvases.clear();
    g_current_target = FMRB_CANVAS_SCREEN;

    // Note: g_lgfx is managed by main.cpp, don't delete here
    printf("Graphics handler cleaned up\n");
}

extern "C" SDL_Renderer* graphics_handler_get_renderer(void) {
    return nullptr; // Not used with LovyanGFX
}

// Forward declaration - implemented in socket_server.c
extern "C" int socket_server_send_ack(uint8_t type, uint8_t seq, const uint8_t *response_data, uint16_t response_len);

// Next canvas ID to allocate
static uint16_t g_next_canvas_id = 1;

extern "C" int graphics_handler_process_command(uint8_t type, uint8_t seq, const uint8_t *data, size_t size) {
    if (!data || size < 1 || !g_lgfx) {
        return -1;
    }

    uint8_t cmd_type = data[0];

    switch (cmd_type) {
        case FMRB_GFX_CMD_CLEAR:
        case FMRB_GFX_CMD_FILL_SCREEN:
            if (size >= sizeof(fmrb_gfx_clear_cmd_t)) {
                const fmrb_gfx_clear_cmd_t *cmd = (const fmrb_gfx_clear_cmd_t*)data;
                printf("FILL_SCREEN: canvas_id=%u, color=0x%02x\n", cmd->canvas_id, cmd->color);
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    // Draw to back buffer for double buffering
                    target = g_back_buffer ? static_cast<LovyanGFX*>(g_back_buffer) : g_lgfx;
                    printf("FILL_SCREEN: Drawing to back buffer %p\n", (void*)target);
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        fprintf(stderr, "Canvas %u not found\n", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->fillScreen(cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_PIXEL:
            if (size >= sizeof(fmrb_gfx_pixel_cmd_t)) {
                const fmrb_gfx_pixel_cmd_t *cmd = (const fmrb_gfx_pixel_cmd_t*)data;
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    // Draw to back buffer for double buffering
                    target = g_back_buffer ? static_cast<LovyanGFX*>(g_back_buffer) : g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        fprintf(stderr, "Canvas %u not found\n", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawPixel(cmd->x, cmd->y, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_LINE:
            if (size >= sizeof(fmrb_gfx_line_cmd_t)) {
                const fmrb_gfx_line_cmd_t *cmd = (const fmrb_gfx_line_cmd_t*)data;
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = (g_back_buffer) ? static_cast<LovyanGFX*>(g_back_buffer) : g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        fprintf(stderr, "Canvas %u not found\n", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawLine(cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_RECT:
            if (size >= sizeof(fmrb_gfx_rect_cmd_t)) {
                const fmrb_gfx_rect_cmd_t *cmd = (const fmrb_gfx_rect_cmd_t*)data;
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = (g_back_buffer) ? static_cast<LovyanGFX*>(g_back_buffer) : g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        fprintf(stderr, "Canvas %u not found\n", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->drawRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_RECT:
            if (size >= sizeof(fmrb_gfx_rect_cmd_t)) {
                const fmrb_gfx_rect_cmd_t *cmd = (const fmrb_gfx_rect_cmd_t*)data;
                // Get target from command (thread-safe)
                LovyanGFX* target;
                if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
                    target = (g_back_buffer) ? static_cast<LovyanGFX*>(g_back_buffer) : g_lgfx;
                } else {
                    auto it = g_canvases.find(cmd->canvas_id);
                    if (it == g_canvases.end()) {
                        fprintf(stderr, "Canvas %u not found\n", cmd->canvas_id);
                        return -1;
                    }
                    target = it->second;
                }
                target->fillRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_ROUND_RECT:
            if (size >= sizeof(fmrb_gfx_round_rect_cmd_t)) {
                const fmrb_gfx_round_rect_cmd_t *cmd = (const fmrb_gfx_round_rect_cmd_t*)data;
                get_current_target()->drawRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_ROUND_RECT:
            if (size >= sizeof(fmrb_gfx_round_rect_cmd_t)) {
                const fmrb_gfx_round_rect_cmd_t *cmd = (const fmrb_gfx_round_rect_cmd_t*)data;
                get_current_target()->fillRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_CIRCLE:
            if (size >= sizeof(fmrb_gfx_circle_cmd_t)) {
                const fmrb_gfx_circle_cmd_t *cmd = (const fmrb_gfx_circle_cmd_t*)data;
                get_current_target()->drawCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_CIRCLE:
            if (size >= sizeof(fmrb_gfx_circle_cmd_t)) {
                const fmrb_gfx_circle_cmd_t *cmd = (const fmrb_gfx_circle_cmd_t*)data;
                get_current_target()->fillCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_ELLIPSE:
            if (size >= sizeof(fmrb_gfx_ellipse_cmd_t)) {
                const fmrb_gfx_ellipse_cmd_t *cmd = (const fmrb_gfx_ellipse_cmd_t*)data;
                get_current_target()->drawEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_ELLIPSE:
            if (size >= sizeof(fmrb_gfx_ellipse_cmd_t)) {
                const fmrb_gfx_ellipse_cmd_t *cmd = (const fmrb_gfx_ellipse_cmd_t*)data;
                get_current_target()->fillEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_TRIANGLE:
            if (size >= sizeof(fmrb_gfx_triangle_cmd_t)) {
                const fmrb_gfx_triangle_cmd_t *cmd = (const fmrb_gfx_triangle_cmd_t*)data;
                get_current_target()->drawTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_TRIANGLE:
            if (size >= sizeof(fmrb_gfx_triangle_cmd_t)) {
                const fmrb_gfx_triangle_cmd_t *cmd = (const fmrb_gfx_triangle_cmd_t*)data;
                get_current_target()->fillTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_STRING:
            // cmd_type is already extracted in data[0]
            // Payload format: int32_t x, int32_t y, uint8_t color, uint16_t text_len, char[text_len] text
            if (size < 1 + 4 + 4 + 1 + 2) { // cmd_type + x + y + color + text_len
                break;
            }
            {
                const uint8_t *payload = data + 1;  // Skip cmd_type
                int32_t x, y;
                uint8_t color;
                uint16_t text_len;

                memcpy(&x, payload, sizeof(int32_t)); payload += sizeof(int32_t);
                memcpy(&y, payload, sizeof(int32_t)); payload += sizeof(int32_t);
                memcpy(&color, payload, sizeof(uint8_t)); payload += sizeof(uint8_t);
                memcpy(&text_len, payload, sizeof(uint16_t)); payload += sizeof(uint16_t);

                size_t expected_size = 1 + 4 + 4 + 1 + 2 + text_len;
                if (size < expected_size) {
                    fprintf(stderr, "String command size mismatch: expected=%zu, actual=%zu, text_len=%u\n",
                            expected_size, size, text_len);
                    break;
                }

                const char *text_data = (const char*)payload;
                char text_buf[256];
                size_t len = text_len < 255 ? text_len : 255;
                memcpy(text_buf, text_data, len);
                text_buf[len] = '\0';
#ifdef FMRB_IPC_DEBUG
                printf("Drawing string at (%d,%d) color=0x%08x: %s\n",
                       x, y, color, text_buf);
#endif

                LovyanGFX* target = get_current_target();
                target->setTextColor(color);
                target->setCursor(x, y);
                target->print(text_buf);
                return 0;
            }

        case FMRB_GFX_CMD_PRESENT:
            // Swap buffers: copy back buffer to front buffer
            if (g_back_buffer) {
                g_back_buffer->pushSprite(g_lgfx, 0, 0);
            }
            g_lgfx->display();
            return 0;

        // Canvas management commands
        case FMRB_GFX_CMD_CREATE_CANVAS:
            if (size >= sizeof(fmrb_gfx_create_canvas_cmd_t)) {
                const fmrb_gfx_create_canvas_cmd_t *cmd = (const fmrb_gfx_create_canvas_cmd_t*)data;

                // Allocate new canvas ID (ignore cmd->canvas_id from client)
                uint16_t canvas_id = g_next_canvas_id++;
                if (canvas_id == 0xFFFF) {  // FMRB_CANVAS_INVALID
                    canvas_id = g_next_canvas_id++;  // Skip invalid value
                }

                // Create new sprite
                LGFX_Sprite* sprite = new LGFX_Sprite(g_lgfx);
                sprite->setColorDepth(8);  // RGB332
                if (!sprite->createSprite(cmd->width, cmd->height)) {
                    fprintf(stderr, "Failed to create sprite %u (%dx%d)\n",
                            canvas_id, cmd->width, cmd->height);
                    delete sprite;
                    return -1;
                }

                g_canvases[canvas_id] = sprite;
                printf("Canvas created: ID=%u, %dx%d\n", canvas_id, cmd->width, cmd->height);

                // Send ACK with canvas_id
                socket_server_send_ack(type, seq, (const uint8_t*)&canvas_id, sizeof(canvas_id));
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DELETE_CANVAS:
            if (size >= sizeof(fmrb_gfx_delete_canvas_cmd_t)) {
                const fmrb_gfx_delete_canvas_cmd_t *cmd = (const fmrb_gfx_delete_canvas_cmd_t*)data;

                auto it = g_canvases.find(cmd->canvas_id);
                if (it == g_canvases.end()) {
                    fprintf(stderr, "Canvas %u not found\n", cmd->canvas_id);
                    return -1;
                }

                // If deleting current target, switch back to screen
                if (g_current_target == cmd->canvas_id) {
                    g_current_target = FMRB_CANVAS_SCREEN;
                }

                delete it->second;
                g_canvases.erase(it);
                printf("Canvas deleted: ID=%u\n", cmd->canvas_id);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_SET_TARGET:
            if (size >= sizeof(fmrb_gfx_set_target_cmd_t)) {
                const fmrb_gfx_set_target_cmd_t *cmd = (const fmrb_gfx_set_target_cmd_t*)data;

                // Validate target
                if (cmd->target_id != FMRB_CANVAS_SCREEN) {
                    if (g_canvases.find(cmd->target_id) == g_canvases.end()) {
                        fprintf(stderr, "Canvas %u not found for set_target\n", cmd->target_id);
                        return -1;
                    }
                }

                g_current_target = cmd->target_id;
                printf("Drawing target set: ID=%u %s\n", cmd->target_id,
                       cmd->target_id == FMRB_CANVAS_SCREEN ? "(screen)" : "(canvas)");
                return 0;
            }
            break;

        case FMRB_GFX_CMD_PUSH_CANVAS:
            if (size >= sizeof(fmrb_gfx_push_canvas_cmd_t)) {
                const fmrb_gfx_push_canvas_cmd_t *cmd = (const fmrb_gfx_push_canvas_cmd_t*)data;

                // Find source canvas
                auto it = g_canvases.find(cmd->canvas_id);
                if (it == g_canvases.end()) {
                    fprintf(stderr, "Canvas %u not found for push\n", cmd->canvas_id);
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
                        fprintf(stderr, "Destination canvas %u not found for push\n", cmd->dest_canvas_id);
                        return -1;
                    }
                    dst = dst_it->second;
                    dst_name = "canvas";
                }

                // Push sprite to destination
                LGFX_Sprite* src_sprite = it->second;
                if (cmd->use_transparency) {
                    src_sprite->pushSprite(dst, cmd->x, cmd->y, cmd->transparent_color);
                    printf("Canvas pushed with transparency: ID=%u to %s %u at (%d,%d), transp=0x%02x\n",
                           cmd->canvas_id, dst_name, cmd->dest_canvas_id, cmd->x, cmd->y, cmd->transparent_color);
                } else {
                    src_sprite->pushSprite(dst, cmd->x, cmd->y);
                    printf("Canvas pushed: ID=%u to %s %u at (%d,%d)\n",
                           cmd->canvas_id, dst_name, cmd->dest_canvas_id, cmd->x, cmd->y);
                }
                return 0;
            }
            break;

        default:
            fprintf(stderr, "Unknown graphics command: 0x%02x\n", cmd_type);
            return -1;
    }

    fprintf(stderr, "Invalid command size for type 0x%02x (size=%zu)\n", cmd_type, size);
    return -1;
}
