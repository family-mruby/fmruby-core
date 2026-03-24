// M5GFX graphics handler for Atom Display (HDMI output)
// Receives GFX commands via fmrb_hal_link (local Message Buffer),
// decodes msgpack, and renders using M5Unified + M5AtomDisplay API.
//
// M5Unified provides:
// - M5.Display   : primary display (Atom Display HDMI, set via setPrimaryDisplayType)
// - M5.Displays(0): built-in LCD (if present, e.g. AtomS3R)
// - M5.Displays(N): additional external displays
// GFX commands are rendered to M5.Display (HDMI).
// Built-in LCD can be used for status display in the future.

#include <cstdio>
#include <cstring>
#include <cstdlib>

// M5AtomDisplay.h must be included BEFORE M5Unified.h
// so that __M5GFX_M5ATOMDISPLAY__ is defined and config_t includes atom_display member.
#include <M5AtomDisplay.h>
#include <M5Unified.h>

extern "C" {
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_task_config.h"
#include "fmrb_hal_link.h"
#include "fmrb_link_types.h"
#include "fmrb_link_protocol.h"
#include "fmrb_mem.h"
#include "m5gfx_task.h"
#include <msgpack.h>
}

static const char *TAG = "m5gfx";

// ============================================================
// Display and rendering state
// ============================================================

// g_display points to M5.Display (primary = Atom Display HDMI).
// M5Unified manages display lifecycle; we just hold a pointer.
static M5GFX* g_display = nullptr;
static bool g_display_initialized = false;

// Sprite resolution (defined in m5gfx_task.h, may be overridden by INIT_DISPLAY)
static uint16_t g_sprite_width = M5GFX_SPRITE_WIDTH;
static uint16_t g_sprite_height = M5GFX_SPRITE_HEIGHT;

// Canvas state structure (same as graphics_handler.cpp in fmruby-graphics-audio)
typedef struct {
    uint16_t canvas_id;
    LGFX_Sprite* draw_buffer;
    LGFX_Sprite* render_buffer;
    void* draw_buffer_mem;
    void* render_buffer_mem;
    int16_t z_order;
    int16_t push_x, push_y;
    bool is_visible;
    uint16_t width, height;
    uint16_t active_width, active_height;
    bool dirty;
} canvas_state_t;

#define MAX_CANVAS_COUNT 16
#define FMRB_CANVAS_SCREEN 0x0000
#define FMRB_CANVAS_RENDER 0xFFF0
#define FMRB_CANVAS_INVALID 0xFFFF

static canvas_state_t g_canvases[MAX_CANVAS_COUNT];
static size_t g_canvas_count = 0;
static uint16_t g_next_canvas_id = 1;
static uint16_t g_current_target = FMRB_CANVAS_SCREEN;

// Cursor
static LGFX_Sprite* g_cursor_sprite = nullptr;
static LGFX_Sprite* g_cursor_save = nullptr;
static bool g_cursor_visible = false;
static bool g_cursor_drawn = false;
static int g_cursor_x = 240;
static int g_cursor_y = 160;
static int g_cursor_save_x = 0;
static int g_cursor_save_y = 0;
static const uint32_t CURSOR_TRANSPARENT_COLOR = 0xFF00FF;

static const uint8_t cursor_pattern[16][16] = {
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {1, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
};

// Mutex for canvas state protection
static SemaphoreHandle_t g_canvas_mutex = nullptr;

// Task state
static fmrb_task_handle_t g_task_handle = nullptr;
static volatile bool g_running = false;

// Receive buffer
#define M5GFX_RECV_BUF_SIZE 4096
static uint8_t g_recv_buf[M5GFX_RECV_BUF_SIZE];

// ============================================================
// Canvas memory pool (PSRAM via EXT_RAM_BSS_ATTR)
// ============================================================
// Statically allocated on PSRAM to avoid heap fragmentation.
// Slot size and count defined in m5gfx_task.h.

EXT_RAM_BSS_ATTR static uint8_t __attribute__((aligned(8)))
    g_canvas_pool[M5GFX_CANVAS_MAX_SLOTS][M5GFX_CANVAS_SLOT_SIZE];
static bool g_canvas_pool_used[M5GFX_CANVAS_MAX_SLOTS] = {};

static void canvas_pool_init(void) {
    memset(g_canvas_pool_used, 0, sizeof(g_canvas_pool_used));
    FMRB_LOGI(TAG, "Canvas pool: %d slots x %d bytes = %zu bytes (PSRAM static)",
               M5GFX_CANVAS_MAX_SLOTS, M5GFX_CANVAS_SLOT_SIZE,
               (size_t)M5GFX_CANVAS_MAX_SLOTS * M5GFX_CANVAS_SLOT_SIZE);
}

static void canvas_pool_deinit(void) {
    memset(g_canvas_pool_used, 0, sizeof(g_canvas_pool_used));
}

static void* canvas_pool_alloc(void) {
    for (int i = 0; i < M5GFX_CANVAS_MAX_SLOTS; i++) {
        if (!g_canvas_pool_used[i]) {
            g_canvas_pool_used[i] = true;
            return g_canvas_pool[i];
        }
    }
    FMRB_LOGE(TAG, "Canvas pool exhausted");
    return nullptr;
}

static void canvas_pool_free(void* ptr) {
    if (!ptr) return;
    for (int i = 0; i < M5GFX_CANVAS_MAX_SLOTS; i++) {
        if ((void*)g_canvas_pool[i] == ptr) {
            g_canvas_pool_used[i] = false;
            return;
        }
    }
    FMRB_LOGE(TAG, "canvas_pool_free: invalid pointer %p", ptr);
}

// ============================================================
// Canvas helpers
// ============================================================

static canvas_state_t* canvas_find(uint16_t canvas_id) {
    for (size_t i = 0; i < g_canvas_count; i++) {
        if (g_canvases[i].canvas_id == canvas_id) {
            return &g_canvases[i];
        }
    }
    return nullptr;
}

static canvas_state_t* canvas_alloc(uint16_t canvas_id, uint16_t req_w, uint16_t req_h) {
    if (g_canvas_count >= MAX_CANVAS_COUNT) {
        FMRB_LOGE(TAG, "Max canvas count reached");
        return nullptr;
    }

    canvas_state_t* c = &g_canvases[g_canvas_count];
    c->canvas_id = canvas_id;
    c->width = g_sprite_width;
    c->height = g_sprite_height;
    c->active_width = req_w;
    c->active_height = req_h;
    c->z_order = canvas_id;
    c->push_x = 0;
    c->push_y = 0;
    c->is_visible = false;
    c->dirty = false;

    c->draw_buffer_mem = canvas_pool_alloc();
    if (!c->draw_buffer_mem) {
        FMRB_LOGE(TAG, "Failed to alloc draw buffer from canvas pool");
        return nullptr;
    }

    c->render_buffer_mem = canvas_pool_alloc();
    if (!c->render_buffer_mem) {
        FMRB_LOGE(TAG, "Failed to alloc render buffer from canvas pool");
        canvas_pool_free(c->draw_buffer_mem);
        c->draw_buffer_mem = nullptr;
        return nullptr;
    }

    c->draw_buffer = new LGFX_Sprite(g_display);
    c->draw_buffer->setColorDepth(M5GFX_CANVAS_COLOR_DEPTH);
    c->draw_buffer->setBuffer(c->draw_buffer_mem, req_w, req_h, M5GFX_CANVAS_COLOR_DEPTH);

    c->render_buffer = new LGFX_Sprite(g_display);
    c->render_buffer->setColorDepth(M5GFX_CANVAS_COLOR_DEPTH);
    c->render_buffer->setBuffer(c->render_buffer_mem, req_w, req_h, M5GFX_CANVAS_COLOR_DEPTH);

    g_canvas_count++;

    FMRB_LOGI(TAG, "Canvas alloc: id=%u, active=%dx%d, alloc=%dx%d",
              canvas_id, req_w, req_h, c->width, c->height);
    return c;
}

static void canvas_free(canvas_state_t* c) {
    if (!c) return;
    FMRB_LOGI(TAG, "Canvas free: id=%u", c->canvas_id);

    delete c->draw_buffer;
    c->draw_buffer = nullptr;
    delete c->render_buffer;
    c->render_buffer = nullptr;

    if (c->draw_buffer_mem) {
        canvas_pool_free(c->draw_buffer_mem);
        c->draw_buffer_mem = nullptr;
    }
    if (c->render_buffer_mem) {
        canvas_pool_free(c->render_buffer_mem);
        c->render_buffer_mem = nullptr;
    }

    size_t index = c - g_canvases;
    if (index < g_canvas_count - 1) {
        memmove(&g_canvases[index], &g_canvases[index + 1],
                (g_canvas_count - index - 1) * sizeof(canvas_state_t));
    }
    g_canvas_count--;
}

static int canvas_cmp_zorder(const void* a, const void* b) {
    return ((const canvas_state_t*)a)->z_order - ((const canvas_state_t*)b)->z_order;
}

// Get drawing target for a command
// Direct draw mode: all drawing goes to g_display (HDMI FPGA) directly.
// Canvas compositing via PSRAM Sprite pushSprite causes display corruption
// due to PSRAM/SPI bus contention. Direct draw is stable.
// TODO: Implement line-buffer based compositing for Canvas support.
static LovyanGFX* get_target(uint16_t canvas_id, bool mark_dirty = true) {
    (void)canvas_id;
    (void)mark_dirty;
    return g_display;
}

// ============================================================
// Render frame: push each canvas directly to g_display via SPI
// No intermediate screen_buffer (avoids PSRAM→PSRAM→SPI issue)
// ============================================================

static void render_frame() {
    if (!g_display_initialized || g_canvas_count == 0) return;

    if (xSemaphoreTake(g_canvas_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    // Sort by z-order
    if (g_canvas_count > 1) {
        qsort(g_canvases, g_canvas_count, sizeof(canvas_state_t), canvas_cmp_zorder);
    }

    // Hold SPI bus for the entire render pass (same pattern as official sample)
    g_display->startWrite();

    // Push each visible canvas render_buffer directly to display
    for (size_t i = 0; i < g_canvas_count; i++) {
        canvas_state_t* c = &g_canvases[i];
        if (c->is_visible && c->render_buffer) {
            c->render_buffer->pushSprite(g_display, c->push_x, c->push_y);
        }
    }

    // Draw cursor directly to display
    if (g_cursor_visible && g_cursor_sprite) {
        g_cursor_sprite->pushSprite(g_display, g_cursor_x, g_cursor_y, CURSOR_TRANSPARENT_COLOR);
    }

    g_display->endWrite();

    xSemaphoreGive(g_canvas_mutex);
}

// ============================================================
// Display initialization
// ============================================================

static void init_display(uint16_t width, uint16_t height, uint8_t color_depth) {
    if (g_display_initialized) {
        FMRB_LOGW(TAG, "Display already initialized");
        return;
    }

    g_sprite_width = width;
    g_sprite_height = height;

    // Initialize canvas memory pool on PSRAM
    canvas_pool_init();

    FMRB_LOGI(TAG, "Display init: sprite=%dx%d, display=%dx%d (hw scaling)",
              width, height, g_display->width(), g_display->height());

    // Create cursor sprites
    g_cursor_sprite = new LGFX_Sprite(g_display);
    g_cursor_sprite->setColorDepth(16);
    g_cursor_sprite->createSprite(16, 16);
    g_cursor_sprite->clear(CURSOR_TRANSPARENT_COLOR);

    g_cursor_save = new LGFX_Sprite(g_display);
    g_cursor_save->setColorDepth(16);
    g_cursor_save->createSprite(16, 16);

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint32_t color;
            switch (cursor_pattern[y][x]) {
                case 1: color = 0xFFFFFF; break;
                case 2: color = 0x000000; break;
                default: color = CURSOR_TRANSPARENT_COLOR; break;
            }
            g_cursor_sprite->drawPixel(x, y, color);
        }
    }

    g_display_initialized = true;
    FMRB_LOGI(TAG, "Display initialized");
}

// Forward declaration
static void send_ack(uint8_t msg_type, uint8_t seq, const uint8_t* data, size_t data_len);

// ============================================================
// Command processing
// ============================================================

static int process_gfx_command(uint8_t msg_type, uint8_t cmd_type, uint8_t seq, const uint8_t* data, size_t size) {
    switch (cmd_type) {
    case FMRB_LINK_GFX_CLEAR:
    case FMRB_LINK_GFX_FILL_SCREEN: {
        if (size < sizeof(fmrb_link_graphics_clear_t)) break;
        const auto* cmd = (const fmrb_link_graphics_clear_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->fillScreen(cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_PIXEL: {
        if (size < sizeof(fmrb_link_graphics_pixel_t)) break;
        const auto* cmd = (const fmrb_link_graphics_pixel_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawPixel(cmd->x, cmd->y, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_LINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto* cmd = (const fmrb_link_graphics_line_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawLine(cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_FAST_VLINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto* cmd = (const fmrb_link_graphics_line_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawFastVLine(cmd->x1, cmd->y1, cmd->y2 - cmd->y1, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_FAST_HLINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto* cmd = (const fmrb_link_graphics_line_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawFastHLine(cmd->x1, cmd->y1, cmd->x2 - cmd->x1, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_RECT: {
        if (size < sizeof(fmrb_link_graphics_rect_t)) break;
        const auto* cmd = (const fmrb_link_graphics_rect_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_RECT: {
        if (size < sizeof(fmrb_link_graphics_rect_t)) break;
        const auto* cmd = (const fmrb_link_graphics_rect_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->fillRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_ROUND_RECT: {
        if (size < sizeof(fmrb_link_graphics_round_rect_t)) break;
        const auto* cmd = (const fmrb_link_graphics_round_rect_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ROUND_RECT: {
        if (size < sizeof(fmrb_link_graphics_round_rect_t)) break;
        const auto* cmd = (const fmrb_link_graphics_round_rect_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->fillRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_CIRCLE: {
        if (size < sizeof(fmrb_link_graphics_circle_t)) break;
        const auto* cmd = (const fmrb_link_graphics_circle_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_CIRCLE: {
        if (size < sizeof(fmrb_link_graphics_circle_t)) break;
        const auto* cmd = (const fmrb_link_graphics_circle_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->fillCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_ELLIPSE: {
        if (size < sizeof(fmrb_link_graphics_ellipse_t)) break;
        const auto* cmd = (const fmrb_link_graphics_ellipse_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ELLIPSE: {
        if (size < sizeof(fmrb_link_graphics_ellipse_t)) break;
        const auto* cmd = (const fmrb_link_graphics_ellipse_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->fillEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_TRIANGLE: {
        if (size < sizeof(fmrb_link_graphics_triangle_t)) break;
        const auto* cmd = (const fmrb_link_graphics_triangle_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->drawTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_TRIANGLE: {
        if (size < sizeof(fmrb_link_graphics_triangle_t)) break;
        const auto* cmd = (const fmrb_link_graphics_triangle_t*)data;
        auto* t = get_target(cmd->canvas_id);
        if (t) t->fillTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_STRING: {
        if (size < sizeof(fmrb_link_graphics_text_t)) break;
        const auto* cmd = (const fmrb_link_graphics_text_t*)data;
        size_t expected = sizeof(fmrb_link_graphics_text_t) + cmd->text_len;
        if (size < expected) break;

        const char* text_data = (const char*)(data + sizeof(fmrb_link_graphics_text_t));
        char text_buf[256];
        size_t len = cmd->text_len < 255 ? cmd->text_len : 255;
        memcpy(text_buf, text_data, len);
        text_buf[len] = '\0';

        auto* t = get_target(cmd->canvas_id);
        if (t) {
            if (cmd->bg_transparent) {
                t->setTextColor(cmd->color);
            } else {
                t->setTextColor(cmd->color, cmd->bg_color);
            }
            t->setCursor(cmd->x, cmd->y);
            t->print(text_buf);
        }
        return 0;
    }

    case FMRB_LINK_GFX_SET_TEXT_SIZE: {
        if (size < 3) break;  // canvas_id(2) + size(1)
        uint16_t canvas_id = *(const uint16_t*)data;
        uint8_t text_size = data[2];
        auto* t = get_target(canvas_id, false);
        if (t) t->setTextSize(text_size);
        return 0;
    }

    case FMRB_LINK_GFX_SET_TEXT_COLOR: {
        if (size < 4) break;  // canvas_id(2) + fg(1) + bg(1)
        uint16_t canvas_id = *(const uint16_t*)data;
        uint8_t fg = data[2];
        uint8_t bg = data[3];
        auto* t = get_target(canvas_id, false);
        if (t) t->setTextColor(fg, bg);
        return 0;
    }

    // Canvas management
    case FMRB_LINK_GFX_CREATE_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_create_canvas_t)) break;
        const auto* cmd = (const fmrb_link_graphics_create_canvas_t*)data;

        uint16_t cid = g_next_canvas_id++;
        if (cid == FMRB_CANVAS_INVALID) cid = g_next_canvas_id++;

        canvas_state_t* c = canvas_alloc(cid, cmd->width, cmd->height);
        if (!c) return -1;
        c->z_order = cmd->z_order;

        FMRB_LOGI(TAG, "Canvas created: id=%u, %dx%d, z=%d",
                  cid, (int)cmd->width, (int)cmd->height, (int)cmd->z_order);

        // Send ACK with canvas_id
        send_ack(msg_type, seq, (const uint8_t*)&cid, sizeof(cid));
        return 1;  // ACK already sent
    }

    case FMRB_LINK_GFX_DELETE_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_delete_canvas_t)) break;
        const auto* cmd = (const fmrb_link_graphics_delete_canvas_t*)data;

        canvas_state_t* c = canvas_find(cmd->canvas_id);
        if (!c) return -1;

        if (g_current_target == cmd->canvas_id) {
            g_current_target = FMRB_CANVAS_SCREEN;
        }
        canvas_free(c);
        return 0;
    }

    case FMRB_LINK_GFX_SET_WINDOW_ORDER: {
        if (size < sizeof(fmrb_link_graphics_set_window_order_t)) break;
        const auto* cmd = (const fmrb_link_graphics_set_window_order_t*)data;

        canvas_state_t* c = canvas_find(cmd->canvas_id);
        if (!c) return -1;
        c->z_order = cmd->z_order;
        return 0;
    }

    case FMRB_LINK_GFX_UPDATE_WINDOW: {
        if (size < sizeof(fmrb_link_graphics_update_window_t)) break;
        const auto* cmd = (const fmrb_link_graphics_update_window_t*)data;

        canvas_state_t* c = canvas_find(cmd->canvas_id);
        if (!c) return -1;

        xSemaphoreTake(g_canvas_mutex, portMAX_DELAY);
        c->push_x = cmd->x;
        c->push_y = cmd->y;
        c->active_width = (uint16_t)cmd->width;
        c->active_height = (uint16_t)cmd->height;
        c->draw_buffer->setBuffer(c->draw_buffer_mem, c->active_width, c->active_height, M5GFX_CANVAS_COLOR_DEPTH);
        c->render_buffer->setBuffer(c->render_buffer_mem, c->active_width, c->active_height, M5GFX_CANVAS_COLOR_DEPTH);
        c->dirty = true;
        xSemaphoreGive(g_canvas_mutex);
        return 0;
    }

    case FMRB_LINK_GFX_SET_TARGET: {
        if (size < sizeof(fmrb_link_graphics_set_target_t)) break;
        const auto* cmd = (const fmrb_link_graphics_set_target_t*)data;

        if (cmd->target_id != FMRB_CANVAS_SCREEN && !canvas_find(cmd->target_id)) {
            FMRB_LOGE(TAG, "Canvas %u not found for set_target", cmd->target_id);
            return -1;
        }
        g_current_target = cmd->target_id;
        return 0;
    }

    case FMRB_LINK_GFX_PUSH_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_push_canvas_t)) break;
        const auto* cmd = (const fmrb_link_graphics_push_canvas_t*)data;

        canvas_state_t* src = canvas_find(cmd->canvas_id);
        if (!src) return -1;

        LGFX_Sprite* dst;
        int px, py;

        if (cmd->dest_canvas_id == FMRB_CANVAS_RENDER) {
            xSemaphoreTake(g_canvas_mutex, portMAX_DELAY);
            dst = src->render_buffer;
            src->push_x = cmd->x;
            src->push_y = cmd->y;
            src->is_visible = true;
            px = 0;
            py = 0;
        } else if (cmd->dest_canvas_id == FMRB_CANVAS_SCREEN) {
            dst = (g_canvas_count > 0) ? g_canvases[0].render_buffer : nullptr;
            px = cmd->x;
            py = cmd->y;
        } else {
            FMRB_LOGE(TAG, "Dest canvas %u not supported", cmd->dest_canvas_id);
            return -1;
        }

        if (dst) {
            if (cmd->use_transparency) {
                src->draw_buffer->pushSprite(dst, px, py, cmd->transparent_color);
            } else {
                src->draw_buffer->pushSprite(dst, px, py);
            }
        }

        if (cmd->dest_canvas_id == FMRB_CANVAS_RENDER) {
            xSemaphoreGive(g_canvas_mutex);
        }
        return 0;
    }

    case FMRB_LINK_GFX_CURSOR_SET_POSITION: {
        if (size < sizeof(fmrb_link_graphics_cursor_position_t)) break;
        const auto* cmd = (const fmrb_link_graphics_cursor_position_t*)data;
        g_cursor_x = cmd->x;
        g_cursor_y = cmd->y;
        return 0;
    }

    case FMRB_LINK_GFX_CURSOR_SET_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_cursor_visible_t)) break;
        const auto* cmd = (const fmrb_link_graphics_cursor_visible_t*)data;
        g_cursor_visible = cmd->visible;
        return 0;
    }

    default:
        FMRB_LOGW(TAG, "Unknown GFX cmd: 0x%02x", cmd_type);
        return -1;
    }

    FMRB_LOGE(TAG, "Invalid cmd size for 0x%02x (size=%zu)", cmd_type, size);
    return -1;
}

// ============================================================
// ACK response sender (m5gfx -> Core via RX buffer)
// ============================================================

static void send_ack(uint8_t msg_type, uint8_t seq, const uint8_t* data, size_t data_len) {
    // Build ACK as msgpack: [type|ACK_REQUIRED, seq, RESPONSE_MSG_ACK, payload]
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, msg_type);
    msgpack_pack_uint8(&pk, seq);
    msgpack_pack_uint8(&pk, FMRB_LINK_RESPONSE_MSG_ACK);

    if (data && data_len > 0) {
        msgpack_pack_bin(&pk, data_len);
        msgpack_pack_bin_body(&pk, data, data_len);
    } else {
        msgpack_pack_nil(&pk);
    }

    fmrb_link_message_t resp = {
        .data = (uint8_t*)sbuf.data,
        .size = sbuf.size,
    };
    fmrb_hal_link_local_send_response(FMRB_LINK_GRAPHICS, &resp, 1000);

    msgpack_sbuffer_destroy(&sbuf);
}

// ============================================================
// Message decode (msgpack: [type, seq, sub_cmd, payload])
// ============================================================

static void process_message(const uint8_t* msgpack_data, size_t msgpack_len) {
    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);

    msgpack_unpack_return ret = msgpack_unpack_next(&msg, (const char*)msgpack_data, msgpack_len, NULL);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        FMRB_LOGE(TAG, "msgpack unpack failed");
        msgpack_unpacked_destroy(&msg);
        return;
    }

    msgpack_object root = msg.data;
    if (root.type != MSGPACK_OBJECT_ARRAY || root.via.array.size < 4) {
        FMRB_LOGE(TAG, "Invalid msgpack format");
        msgpack_unpacked_destroy(&msg);
        return;
    }

    uint8_t type = (uint8_t)root.via.array.ptr[0].via.u64;
    uint8_t seq = (uint8_t)root.via.array.ptr[1].via.u64;
    uint8_t sub_cmd = (uint8_t)root.via.array.ptr[2].via.u64;

    uint8_t base_type = type & 0x1F;  // Strip flags

    const uint8_t* payload = nullptr;
    size_t payload_len = 0;

    if (root.via.array.ptr[3].type == MSGPACK_OBJECT_BIN) {
        payload = (const uint8_t*)root.via.array.ptr[3].via.bin.ptr;
        payload_len = root.via.array.ptr[3].via.bin.size;
    }

    switch (base_type) {
    case FMRB_LINK_TYPE_CONTROL:
        if (sub_cmd == FMRB_LINK_CONTROL_VERSION && payload_len >= sizeof(fmrb_control_version_req_t)) {
            uint8_t remote_ver = payload[0];
            uint8_t local_ver = FMRB_LINK_PROTOCOL_VERSION;
            FMRB_LOGI(TAG, "VERSION check: remote=%d, local=%d, seq=%u", remote_ver, local_ver, seq);
            send_ack(type, seq, &local_ver, sizeof(local_ver));
        } else if (sub_cmd == FMRB_LINK_CONTROL_INIT_DISPLAY && payload_len >= sizeof(fmrb_control_init_display_t)) {
            const auto* init_cmd = (const fmrb_control_init_display_t*)payload;
            FMRB_LOGI(TAG, "INIT_DISPLAY: %dx%d, %d-bit", init_cmd->width, init_cmd->height, init_cmd->color_depth);
            init_display(init_cmd->width, init_cmd->height, init_cmd->color_depth);
            send_ack(type, seq, nullptr, 0);
        }
        break;

    case FMRB_LINK_TYPE_GRAPHICS:
        if (payload && payload_len > 0) {
            int result = process_gfx_command(type, sub_cmd, seq, payload, payload_len);
            // result == 0: success, ACK not yet sent
            // result > 0: ACK already sent by handler (e.g. CREATE_CANVAS)
            if (result == 0) {
                send_ack(type, seq, nullptr, 0);
            }
        }
        break;

    default:
        FMRB_LOGD(TAG, "Ignoring msg type=%u", base_type);
        break;
    }

    msgpack_unpacked_destroy(&msg);
}

// ============================================================
// Main task
// ============================================================

static void m5gfx_task(void* arg) {
    FMRB_LOGI(TAG, "M5GFX task started");

    // Initialize M5Unified (auto-detects AtomS3/AtomS3R + Atom Display)
    auto cfg = M5.config();
    cfg.output_power = true;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.external_imu = false;
    cfg.external_rtc = false;
    cfg.external_display.atom_display = true;
    // Set Atom Display logical resolution for hardware scaling
    cfg.atom_display.logical_width = g_sprite_width;
    cfg.atom_display.logical_height = g_sprite_height;
    M5.begin(cfg);

    // Set Atom Display (HDMI) as primary display
    M5.setPrimaryDisplayType({
        m5::board_t::board_M5AtomDisplay,
    });

    g_display = &M5.Display;
    g_display->setAutoDisplay(false);

    int display_count = M5.getDisplayCount();
    FMRB_LOGI(TAG, "M5Unified: %d display(s) detected", display_count);
    for (int i = 0; i < display_count; i++) {
        FMRB_LOGI(TAG, "  Display[%d]: %dx%d", i,
                   M5.Displays(i).width(), M5.Displays(i).height());
    }
    FMRB_LOGI(TAG, "Primary display: %dx%d", g_display->width(), g_display->height());

    g_canvas_mutex = xSemaphoreCreateMutex();

    // Direct draw mode: hold SPI bus for stable HDMI output
    g_display->startWrite();
    FMRB_LOGI(TAG, "Direct draw mode: SPI bus acquired");

    while (g_running) {
        // Drain all pending commands (non-blocking after first)
        bool first = true;
        while (true) {
            fmrb_link_message_t msg = {
                .data = g_recv_buf,
                .size = sizeof(g_recv_buf),
            };
            fmrb_err_t err = fmrb_hal_link_local_receive_cmd(
                FMRB_LINK_GRAPHICS, &msg, first ? 33 : 0);
            if (err != FMRB_OK || msg.size == 0) break;
            process_message(g_recv_buf, msg.size);
            first = false;
        }
    }

    g_display->endWrite();

    // Cleanup
    while (g_canvas_count > 0) {
        canvas_free(&g_canvases[0]);
    }

    canvas_pool_deinit();

    delete g_cursor_sprite;
    g_cursor_sprite = nullptr;
    delete g_cursor_save;
    g_cursor_save = nullptr;

    // g_display is managed by M5Unified, do not delete
    g_display = nullptr;

    if (g_canvas_mutex) {
        vSemaphoreDelete(g_canvas_mutex);
        g_canvas_mutex = nullptr;
    }

    FMRB_LOGI(TAG, "M5GFX task stopped");
    fmrb_task_delete(NULL);
}

// ============================================================
// Public API
// ============================================================

extern "C" fmrb_err_t m5gfx_task_init(void) {
    if (g_task_handle) {
        FMRB_LOGW(TAG, "Already initialized");
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Initializing M5GFX...");
    g_running = true;

    fmrb_base_type_t result = fmrb_task_create_ex(
        m5gfx_task,
        "m5gfx",
        FMRB_M5GFX_TASK_STACK_SIZE,
        NULL,
        FMRB_M5GFX_TASK_PRIORITY,
        &g_task_handle,
        FMRB_M5GFX_TASK_FLAGS
    );

    if (result != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create M5GFX task");
        g_running = false;
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "M5GFX initialized");
    return FMRB_OK;
}

extern "C" fmrb_err_t m5gfx_task_deinit(void) {
    if (!g_task_handle) return FMRB_OK;

    FMRB_LOGI(TAG, "Deinitializing M5GFX...");
    g_running = false;
    fmrb_task_delay_ms(1500);
    g_task_handle = nullptr;
    g_display_initialized = false;

    FMRB_LOGI(TAG, "M5GFX deinitialized");
    return FMRB_OK;
}
