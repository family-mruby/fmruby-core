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

// Canvas state structure
typedef struct {
    uint16_t canvas_id;
    LGFX_Sprite* draw_buffer;      // Drawing buffer (front buffer for user drawing)
    LGFX_Sprite* render_buffer;    // Rendering buffer (back buffer for composition)
    int16_t z_order;               // Z-order (0=bottom, higher=front, SystemApp=0 fixed)
    int16_t push_x, push_y;        // Position to push to screen
    bool is_visible;               // Visibility flag
    uint16_t width, height;        // Canvas dimensions
    bool dirty;                    // Redraw flag
} canvas_state_t;

// Maximum number of canvases
#define MAX_CANVAS_COUNT 16

// Canvas management
static canvas_state_t g_canvases[MAX_CANVAS_COUNT];
static size_t g_canvas_count = 0;
static uint16_t g_current_target = FMRB_CANVAS_SCREEN;  // 0=screen, other=canvas
static bool g_graphics_initialized = false;  // Flag to prevent multiple initializations

// Canvas helper functions
static canvas_state_t* canvas_state_find(uint16_t canvas_id) {
    for (size_t i = 0; i < g_canvas_count; i++) {
        if (g_canvases[i].canvas_id == canvas_id) {
            return &g_canvases[i];
        }
    }
    return nullptr;
}

static canvas_state_t* canvas_state_alloc(uint16_t canvas_id, uint16_t width, uint16_t height) {
    if (g_canvas_count >= MAX_CANVAS_COUNT) {
        GFX_LOG_E("Maximum canvas count reached (%d)", MAX_CANVAS_COUNT);
        return nullptr;
    }

    canvas_state_t* canvas = &g_canvases[g_canvas_count++];
    canvas->canvas_id = canvas_id;
    canvas->width = width;
    canvas->height = height;
    canvas->z_order = 0;
    canvas->push_x = 0;
    canvas->push_y = 0;
    canvas->is_visible = true;
    canvas->dirty = false;

    // Create draw buffer (front buffer for drawing)
    canvas->draw_buffer = new LGFX_Sprite(g_lgfx);
    canvas->draw_buffer->setColorDepth(8);  // RGB332
    if (!canvas->draw_buffer->createSprite(width, height)) {
        GFX_LOG_E("Failed to create draw buffer for canvas %u", canvas_id);
        delete canvas->draw_buffer;
        g_canvas_count--;
        return nullptr;
    }

    // Create render buffer (back buffer for composition)
    canvas->render_buffer = new LGFX_Sprite(g_lgfx);
    canvas->render_buffer->setColorDepth(8);  // RGB332
    if (!canvas->render_buffer->createSprite(width, height)) {
        GFX_LOG_E("Failed to create render buffer for canvas %u", canvas_id);
        delete canvas->draw_buffer;
        delete canvas->render_buffer;
        g_canvas_count--;
        return nullptr;
    }

    GFX_LOG_I("Canvas allocated: ID=%u, size=%dx%d, z_order=%d",
              canvas_id, width, height, canvas->z_order);
    return canvas;
}

static void canvas_state_free(canvas_state_t* canvas) {
    if (!canvas) return;

    GFX_LOG_I("Freeing canvas ID=%u", canvas->canvas_id);

    if (canvas->draw_buffer) {
        delete canvas->draw_buffer;
        canvas->draw_buffer = nullptr;
    }
    if (canvas->render_buffer) {
        delete canvas->render_buffer;
        canvas->render_buffer = nullptr;
    }

    // Remove from array by shifting remaining elements
    size_t index = canvas - g_canvases;
    if (index < g_canvas_count - 1) {
        memmove(&g_canvases[index], &g_canvases[index + 1],
                (g_canvas_count - index - 1) * sizeof(canvas_state_t));
    }
    g_canvas_count--;
}

// Compare function for qsort (sort by z_order ascending)
static int canvas_compare_zorder(const void* a, const void* b) {
    const canvas_state_t* ca = (const canvas_state_t*)a;
    const canvas_state_t* cb = (const canvas_state_t*)b;
    return ca->z_order - cb->z_order;
}

static void canvas_sort_by_zorder() {
    if (g_canvas_count > 1) {
        qsort(g_canvases, g_canvas_count, sizeof(canvas_state_t), canvas_compare_zorder);
    }
}

// Render all canvases to screen in Z-order
static void graphics_handler_render_frame_internal() {
    if (g_canvas_count == 0) {
        return;  // No canvases to render
    }

    // Sort canvases by Z-order (low to high)
    canvas_sort_by_zorder();

    // Composite all visible canvases to screen in Z-order
    for (size_t i = 0; i < g_canvas_count; i++) {
        canvas_state_t* canvas = &g_canvases[i];
        if (canvas->is_visible && canvas->render_buffer) {
            if(canvas->dirty){
                GFX_LOG_D("Push canvas ID=%u at (%d,%d), z_order=%d",
                      canvas->canvas_id, canvas->push_x, canvas->push_y, canvas->z_order);
                canvas->dirty = false;
                canvas->render_buffer->pushSprite(g_lgfx, canvas->push_x, canvas->push_y);
            }
        }
    }
}

// Get current drawing target (screen or canvas)
static LovyanGFX* get_current_target() {
    if (g_current_target == FMRB_CANVAS_SCREEN) {
        // Draw directly to screen
        return g_lgfx;
    }
    canvas_state_t* canvas = canvas_state_find(g_current_target);
    if (canvas) {
        return canvas->draw_buffer;  // Draw to draw_buffer
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

    g_lgfx->setAutoDisplay(false);

    g_graphics_initialized = true;  // Mark as initialized
    GFX_LOG_I("Graphics handler initialized (using external LGFX instance, direct rendering)");
    return 0;
}

extern "C" void graphics_handler_cleanup(void) {
    // Delete all canvases
    while (g_canvas_count > 0) {
        canvas_state_free(&g_canvases[0]);
    }
    g_current_target = FMRB_CANVAS_SCREEN;
    g_graphics_initialized = false;  // Reset initialization flag

    // Note: g_lgfx is managed by main.cpp, don't delete here
    GFX_LOG_I("Graphics handler cleaned up");
}

extern "C" SDL_Renderer* graphics_handler_get_renderer(void) {
    return nullptr; // Not used with LovyanGFX
}

extern "C" void graphics_handler_render_frame(void) {
    if (!g_lgfx) {
        return;
    }
    graphics_handler_render_frame_internal();
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
                    GFX_LOG_D("CLEAR: Using screen");
                } else {
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    GFX_LOG_D("FILL_RECT: Using screen");
                } else {
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    GFX_LOG_D("DRAW_CIRCLE: Using screen");
                } else {
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    GFX_LOG_D("FILL_CIRCLE: Using screen");
                } else {
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
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
                    GFX_LOG_D("DRAW_STRING: Using screen");
                } else {
                    canvas_state_t* canvas = canvas_state_find(text_cmd->canvas_id);
                    if (!canvas) {
                        GFX_LOG_E("Canvas %u not found", text_cmd->canvas_id);
                        return -1;
                    }
                    target = canvas->draw_buffer;
                    canvas->dirty = true;
                    GFX_LOG_D("DRAW_STRING: Using canvas %u", text_cmd->canvas_id);
                }
                target->setTextColor(text_cmd->color);
                target->setCursor(text_cmd->x, text_cmd->y);
                target->print(text_buf);
                GFX_LOG_D("DRAW_STRING: Text drawn");
                return 0;
            }

        // case FMRB_LINK_GFX_PRESENT:
        //     if (size >= sizeof(fmrb_link_graphics_present_t)) {
        //         const fmrb_link_graphics_present_t *cmd = (const fmrb_link_graphics_present_t*)data;
        //         GFX_LOG_D("PRESENT: canvas_id=%u", cmd->canvas_id);

        //         if (cmd->canvas_id == FMRB_CANVAS_SCREEN) {
        //             // Direct screen update - nothing to do, main loop handles rendering
        //             GFX_LOG_D("PRESENT: Screen - will be rendered in main loop");
        //         } else {
        //             // Push draw_buffer to render_buffer for the specified canvas
        //             canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
        //             if (!canvas) {
        //                 GFX_LOG_E("Canvas %u not found for present", cmd->canvas_id);
        //                 return -1;
        //             }

        //             // Copy draw_buffer to render_buffer (double buffering)
        //             GFX_LOG_D("PRESENT: Copying draw_buffer to render_buffer for canvas %u", cmd->canvas_id);
        //             canvas->draw_buffer->pushSprite(canvas->render_buffer, 0, 0);
        //             canvas->dirty = true;
        //         }

        //         // Note: Rendering and display() are handled by main loop at ~60fps
        //         GFX_LOG_D("PRESENT: Canvas updated, will be rendered in main loop");
        //         return 0;
        //     }
        //     break;

        // Canvas management commands
        case FMRB_LINK_GFX_CREATE_CANVAS:
            if (size >= sizeof(fmrb_link_graphics_create_canvas_t)) {
                const fmrb_link_graphics_create_canvas_t *cmd = (const fmrb_link_graphics_create_canvas_t*)data;

                // Allocate new canvas ID (ignore cmd->canvas_id from client)
                uint16_t canvas_id = g_next_canvas_id++;
                if (canvas_id == 0xFFFF) {  // FMRB_CANVAS_INVALID
                    canvas_id = g_next_canvas_id++;  // Skip invalid value
                }

                // Allocate canvas state
                canvas_state_t* canvas = canvas_state_alloc(canvas_id, cmd->width, cmd->height);
                if (!canvas) {
                    GFX_LOG_E("Failed to allocate canvas %u (%dx%d)",
                            canvas_id, cmd->width, cmd->height);
                    return -1;
                }

                GFX_LOG_I("Canvas created: ID=%u, %dx%d", canvas_id, cmd->width, cmd->height);

                // Send ACK with canvas_id
                socket_server_send_ack(msg_type, seq, (const uint8_t*)&canvas_id, sizeof(canvas_id));
                return 0;
            }
            break;

        case FMRB_LINK_GFX_DELETE_CANVAS:
            if (size >= sizeof(fmrb_link_graphics_delete_canvas_t)) {
                const fmrb_link_graphics_delete_canvas_t *cmd = (const fmrb_link_graphics_delete_canvas_t*)data;

                canvas_state_t* canvas = canvas_state_find(cmd->canvas_id);
                if (!canvas) {
                    GFX_LOG_E("Canvas %u not found", cmd->canvas_id);
                    return -1;
                }

                // If deleting current target, switch back to screen
                if (g_current_target == cmd->canvas_id) {
                    g_current_target = FMRB_CANVAS_SCREEN;
                }

                canvas_state_free(canvas);
                GFX_LOG_I("Canvas deleted: ID=%u", cmd->canvas_id);
                return 0;
            }
            break;

        case FMRB_LINK_GFX_SET_TARGET:
            if (size >= sizeof(fmrb_link_graphics_set_target_t)) {
                const fmrb_link_graphics_set_target_t *cmd = (const fmrb_link_graphics_set_target_t*)data;

                // Validate target
                if (cmd->target_id != FMRB_CANVAS_SCREEN) {
                    if (!canvas_state_find(cmd->target_id)) {
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
                canvas_state_t* src_canvas = canvas_state_find(cmd->canvas_id);
                if (!src_canvas) {
                    GFX_LOG_E("Canvas %u not found for push", cmd->canvas_id);
                    return -1;
                }

                // Determine destination (screen or canvas)
                LovyanGFX* dst;
                const char* dst_name;
                if (cmd->dest_canvas_id == FMRB_CANVAS_RENDER) {
                    dst = src_canvas->render_buffer;
                    dst_name = "render_canvas";
                    src_canvas->push_x = cmd->x;
                    src_canvas->push_y = cmd->y;
                } else if(cmd->dest_canvas_id == 0) {
                    dst = g_lgfx;
                    dst_name = "screen";
                } else {
                    GFX_LOG_E("Destination canvas %u is not supported yet...", cmd->dest_canvas_id);
                    // Store push position in canvas state (for render_frame)
                    // TODO: how to handle chird canvas??
                    // src_canvas->push_x = cmd->x;
                    // src_canvas->push_y = cmd->y;
                    return -1;
                }


                // Push render_buffer to destination
                LGFX_Sprite* src_sprite = src_canvas->draw_buffer;
                GFX_LOG_D("PUSH_CANVAS: src=%p (%dx%d), dst=%p (%s), pos=(%d,%d)",
                       src_sprite, src_sprite->width(), src_sprite->height(), dst, dst_name, cmd->x, cmd->y);
                if (cmd->use_transparency) {
                    src_sprite->pushSprite(dst, 0, 0, cmd->transparent_color);
                    GFX_LOG_D("Canvas pushed with transparency: ID=%u to %s %u at (%d,%d), transp=0x%02x",
                           cmd->canvas_id, dst_name, cmd->dest_canvas_id, cmd->x, cmd->y, cmd->transparent_color);
                } else {
                    src_sprite->pushSprite(dst, 0, 0);
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
