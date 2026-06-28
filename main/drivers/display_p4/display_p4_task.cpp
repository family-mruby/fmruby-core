// Family mruby Modern (ESP32-P4 / M5Stack Tab5) display task.
//
// Receives msgpack-encoded GFX/CONTROL commands from the hal_link_local TX
// buffer, dispatches them, and sends ACK responses via the RX buffer.
//
// Phase 2: Full GFX rendering via LGFX_Sprite canvas compositing.
//   - Each canvas is an 8bpp (RGB332) LGFX_Sprite allocated in PSRAM.
//   - SpriteImages are separate 8bpp sprites for bitmap assets.
//   - SpriteInstances are placed on canvases and composited after the canvas.
//   - PUSH_CANVAS: sort by z-order → pushSprite to g_lcd → composite instances.
//   - GfxBlock VM programs are executed locally (display_p4_vm).
//   - Pixel-perfect 1:1 scale (no upscaling). Tab5 LCD-size profile TBD.

#include "display_p4_task.h"
#include "display_p4_vm.h"
#include "display_p4_sprite.h"
#include "lgfx_tab5.hpp"

#include "fmrb_log.h"
#include "fmrb_hal_link.h"
#include "fmrb_link_protocol.h"
#include "fmrb_link_types.h"
#include "fmrb_mem.h"
#include "fmrb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

extern "C" {
#include "fmrb_bmp332.h"
}

#include <msgpack.h>
#include <cstring>
#include <cstdlib>  // qsort

static const char *TAG = "display_p4";

// Tab5 board pins / I2C (see fmrb_pin_assign.h; kept local to the driver).
#define TAB5_I2C_PORT   I2C_NUM_1
#define TAB5_I2C_SDA    GPIO_NUM_31
#define TAB5_I2C_SCL    GPIO_NUM_32
#define TAB5_TP_INT     GPIO_NUM_23
#define PI4IO1_ADDR     0x43   // PI4IO GPIO expander #1 (LCD/touch reset)
#define PI4IO2_ADDR     0x44   // PI4IO GPIO expander #2 (power rails)

static LGFX_Tab5 g_lcd;

// Receive buffer for local link commands
#define DISPLAY_P4_RECV_BUF_SIZE 4096
static uint8_t g_recv_buf[DISPLAY_P4_RECV_BUF_SIZE];

// ============================================================
// Canvas management
// Each canvas is an 8bpp (RGB332) LGFX_Sprite allocated in PSRAM.
// ============================================================

#define DISPLAY_P4_MAX_CANVAS    8
#define DISPLAY_P4_CANVAS_SCREEN  ((uint16_t)0x0000)
#define DISPLAY_P4_CANVAS_RENDER  ((uint16_t)0xFFF0)
#define DISPLAY_P4_CANVAS_INVALID ((uint16_t)0xFFFF)

typedef struct {
    uint16_t     canvas_id;
    LGFX_Sprite *sprite;
    int16_t      z_order;
    int16_t      push_x, push_y;
    bool         is_visible;
    uint8_t      transparent_color;
    bool         use_transparency;
    uint16_t     width, height;
} p4_canvas_t;

static p4_canvas_t g_canvases[DISPLAY_P4_MAX_CANVAS];
static size_t      g_canvas_count = 0;
static uint16_t    g_next_canvas_id = 1;

// When non-zero, draw commands are redirected to this SpriteImage's sprite.
static uint16_t    g_sprite_target_id = 0;

static p4_canvas_t* canvas_find(uint16_t canvas_id) {
    for (size_t i = 0; i < g_canvas_count; i++) {
        if (g_canvases[i].canvas_id == canvas_id)
            return &g_canvases[i];
    }
    return nullptr;
}

static p4_canvas_t* canvas_alloc(uint16_t canvas_id,
                                  uint16_t width, uint16_t height, int16_t z_order,
                                  uint8_t transparent_color, bool use_transparency) {
    if (g_canvas_count >= DISPLAY_P4_MAX_CANVAS) {
        FMRB_LOGE(TAG, "Canvas pool full (max %d)", DISPLAY_P4_MAX_CANVAS);
        return nullptr;
    }

    auto *sprite = new LGFX_Sprite(&g_lcd);
    sprite->setColorDepth(8);  // 8bpp = RGB332, matches kernel color format
    sprite->setPsram(true);
    if (!sprite->createSprite(width, height)) {
        FMRB_LOGE(TAG, "Sprite alloc failed: %dx%d", (int)width, (int)height);
        delete sprite;
        return nullptr;
    }
    sprite->clear(0);

    p4_canvas_t *c       = &g_canvases[g_canvas_count++];
    c->canvas_id         = canvas_id;
    c->sprite            = sprite;
    c->z_order           = z_order;
    c->push_x            = 0;
    c->push_y            = 0;
    c->is_visible        = false;
    c->transparent_color = transparent_color;
    c->use_transparency  = use_transparency;
    c->width             = width;
    c->height            = height;

    FMRB_LOGI(TAG, "Canvas alloc: id=%u %dx%d z=%d transp=%u/%u",
              canvas_id, width, height, z_order, transparent_color, (uint8_t)use_transparency);
    return c;
}

static void canvas_free(p4_canvas_t *c) {
    if (!c) return;
    FMRB_LOGI(TAG, "Canvas free: id=%u", c->canvas_id);

    display_p4_vm_delete_progs_by_canvas(c->canvas_id);
    display_p4_sprite_delete_all_for_canvas(c->canvas_id);

    if (c->sprite) {
        c->sprite->deleteSprite();
        delete c->sprite;
        c->sprite = nullptr;
    }

    size_t idx = (size_t)(c - g_canvases);
    if (idx < g_canvas_count - 1) {
        memmove(&g_canvases[idx], &g_canvases[idx + 1],
                (g_canvas_count - idx - 1) * sizeof(p4_canvas_t));
    }
    g_canvas_count--;
}

static int canvas_cmp_zorder(const void *a, const void *b) {
    return (int)((const p4_canvas_t *)a)->z_order
         - (int)((const p4_canvas_t *)b)->z_order;
}

// Return the drawing target sprite. When g_sprite_target_id is set, drawing
// commands are redirected to that SpriteImage rather than the canvas sprite.
static LGFX_Sprite* get_sprite(uint16_t canvas_id) {
    if (g_sprite_target_id != 0) {
        LGFX_Sprite *s = display_p4_sprite_image_get(g_sprite_target_id);
        if (s) return s;
        // Target image gone; fall through to canvas
    }
    p4_canvas_t *c = canvas_find(canvas_id);
    return c ? c->sprite : nullptr;
}

// ============================================================
// Mouse cursor (16x16 sprite, drawn last on each render)
// ============================================================

static LGFX_Sprite *g_cursor_sprite  = nullptr;
static bool         g_cursor_visible = false;
static int          g_cursor_x = 0;
static int          g_cursor_y = 0;

#define CURSOR_TRANSPARENT 0xFF00FF

static void cursor_init(void) {
    g_cursor_sprite = new LGFX_Sprite(&g_lcd);
    g_cursor_sprite->setColorDepth(16);
    g_cursor_sprite->setPsram(false);
    g_cursor_sprite->createSprite(16, 16);
    g_cursor_sprite->clear((uint32_t)CURSOR_TRANSPARENT);

    static const uint8_t pat[16][16] = {
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,1,1,1,1,1,1,0,0,0,0},
        {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
        {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
        {0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0},
    };
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint32_t color;
            switch (pat[y][x]) {
                case 1:  color = 0xFFFFFF; break;
                case 2:  color = 0x000000; break;
                default: color = CURSOR_TRANSPARENT; break;
            }
            g_cursor_sprite->drawPixel(x, y, color);
        }
    }
}

// ============================================================
// Render frame: composite all visible canvases onto g_lcd
// Sprite instances for each canvas are composited onto the canvas
// sprite before it is pushed to g_lcd.
// ============================================================

static void render_frame(void) {
    if (g_canvas_count == 0) return;

    if (g_canvas_count > 1) {
        qsort(g_canvases, g_canvas_count, sizeof(p4_canvas_t), canvas_cmp_zorder);
    }

    for (size_t i = 0; i < g_canvas_count; i++) {
        p4_canvas_t *c = &g_canvases[i];
        if (!c->is_visible || !c->sprite) continue;

        // Composite sprite instances into the canvas sprite first.
        display_p4_sprite_composite(c->canvas_id, c->sprite);

        // Push canvas sprite to g_lcd.
        if (c->use_transparency) {
            c->sprite->pushSprite(&g_lcd, c->push_x, c->push_y,
                                  (uint32_t)c->transparent_color);
        } else {
            c->sprite->pushSprite(&g_lcd, c->push_x, c->push_y);
        }
    }

    if (g_cursor_visible && g_cursor_sprite) {
        g_cursor_sprite->pushSprite(&g_lcd, g_cursor_x, g_cursor_y,
                                    (uint32_t)CURSOR_TRANSPARENT);
    }
}

// ============================================================
// PI4IO I2C helper
// ============================================================

static inline void pi4io_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

// ============================================================
// Power/reset sequence for the Tab5 panel + touch.
// ============================================================

static void tab5_power_on(void) {
    // TP INT high during reset selects GT911 touch I2C address 0x14.
    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = 1ULL << TAB5_TP_INT;
    int_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&int_cfg);
    gpio_set_level(TAB5_TP_INT, 1);

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = TAB5_I2C_PORT;
    bus_cfg.sda_io_num = TAB5_I2C_SDA;
    bus_cfg.scl_io_num = TAB5_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        FMRB_LOGE(TAG, "PI4IO i2c bus init failed");
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t io1 = NULL, io2 = NULL;
    dev_cfg.device_address = PI4IO1_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io1);
    dev_cfg.device_address = PI4IO2_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io2);

    // PI4IO #1: directions/pulls, resets driven LOW
    pi4io_write(io1, 0x03, 0x7F);
    pi4io_write(io1, 0x05, 0x46);
    pi4io_write(io1, 0x07, 0x00);
    pi4io_write(io1, 0x0D, 0x7F);
    pi4io_write(io1, 0x0B, 0x7F);
    // PI4IO #2: power rails / IO config
    pi4io_write(io2, 0x03, 0xB9);
    pi4io_write(io2, 0x07, 0x06);
    pi4io_write(io2, 0x0D, 0xB9);
    pi4io_write(io2, 0x0B, 0xF9);
    pi4io_write(io2, 0x09, 0x40);
    pi4io_write(io2, 0x11, 0xBF);
    pi4io_write(io2, 0x05, 0x89);

    vTaskDelay(pdMS_TO_TICKS(10));
    pi4io_write(io1, 0x05, 0x76);  // release LCD + touch resets

    gpio_set_direction(TAB5_TP_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    i2c_master_bus_rm_device(io1);
    i2c_master_bus_rm_device(io2);
    i2c_del_master_bus(bus);
    // Backlight: LEDC PWM via Light_PWM in LGFX_Tab5 (GPIO22, ch7, 44100Hz).
}

// ============================================================
// ACK response sender
// ============================================================

static void send_ack(uint8_t msg_type, uint8_t seq, const uint8_t *data, size_t data_len) {
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
        .data = (uint8_t *)sbuf.data,
        .size = sbuf.size,
    };
    fmrb_hal_link_local_send_response(FMRB_LINK_CHANNEL_DEFAULT, &resp, 1000);
    msgpack_sbuffer_destroy(&sbuf);
}

// ============================================================
// GFX command dispatcher
// Returns: 0 = success, no ACK sent
//          1 = ACK with data already sent
//         -1 = error / unimplemented
// ============================================================

static int process_gfx_command(uint8_t msg_type, uint8_t sub_cmd, uint8_t seq,
                                const uint8_t *data, size_t size) {
    switch (sub_cmd) {

    // --- Screen-level fill ---

    case FMRB_LINK_GFX_CLEAR:
    case FMRB_LINK_GFX_FILL_SCREEN: {
        if (size < sizeof(fmrb_link_graphics_clear_t)) break;
        const auto *cmd = (const fmrb_link_graphics_clear_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillScreen(cmd->color);
        return 0;
    }

    // --- Pixel ---

    case FMRB_LINK_GFX_DRAW_PIXEL: {
        if (size < sizeof(fmrb_link_graphics_pixel_t)) break;
        const auto *cmd = (const fmrb_link_graphics_pixel_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawPixel(cmd->x, cmd->y, cmd->color);
        return 0;
    }

    // --- Lines ---

    case FMRB_LINK_GFX_DRAW_LINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto *cmd = (const fmrb_link_graphics_line_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawLine(cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_FAST_VLINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto *cmd = (const fmrb_link_graphics_line_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawFastVLine(cmd->x1, cmd->y1, cmd->y2 - cmd->y1, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_FAST_HLINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto *cmd = (const fmrb_link_graphics_line_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawFastHLine(cmd->x1, cmd->y1, cmd->x2 - cmd->x1, cmd->color);
        return 0;
    }

    // --- Rectangles ---

    case FMRB_LINK_GFX_DRAW_RECT: {
        if (size < sizeof(fmrb_link_graphics_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_RECT: {
        if (size < sizeof(fmrb_link_graphics_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_ROUND_RECT: {
        if (size < sizeof(fmrb_link_graphics_round_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_round_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ROUND_RECT: {
        if (size < sizeof(fmrb_link_graphics_round_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_round_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
        return 0;
    }

    // --- Circles / Ellipses ---

    case FMRB_LINK_GFX_DRAW_CIRCLE: {
        if (size < sizeof(fmrb_link_graphics_circle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_circle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_CIRCLE: {
        if (size < sizeof(fmrb_link_graphics_circle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_circle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_ELLIPSE: {
        if (size < sizeof(fmrb_link_graphics_ellipse_t)) break;
        const auto *cmd = (const fmrb_link_graphics_ellipse_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ELLIPSE: {
        if (size < sizeof(fmrb_link_graphics_ellipse_t)) break;
        const auto *cmd = (const fmrb_link_graphics_ellipse_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
        return 0;
    }

    // --- Triangles ---

    case FMRB_LINK_GFX_DRAW_TRIANGLE: {
        if (size < sizeof(fmrb_link_graphics_triangle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_triangle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_TRIANGLE: {
        if (size < sizeof(fmrb_link_graphics_triangle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_triangle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    // --- Arcs ---

    case FMRB_LINK_GFX_DRAW_ARC: {
        if (size < sizeof(fmrb_link_graphics_arc_t)) break;
        const auto *cmd = (const fmrb_link_graphics_arc_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawArc(cmd->x, cmd->y, cmd->r0, cmd->r1, cmd->angle0, cmd->angle1, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ARC: {
        if (size < sizeof(fmrb_link_graphics_arc_t)) break;
        const auto *cmd = (const fmrb_link_graphics_arc_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillArc(cmd->x, cmd->y, cmd->r0, cmd->r1, cmd->angle0, cmd->angle1, cmd->color);
        return 0;
    }

    // --- Text ---

    case FMRB_LINK_GFX_DRAW_STRING: {
        if (size < sizeof(fmrb_link_graphics_text_t)) break;
        const auto *cmd = (const fmrb_link_graphics_text_t *)data;
        if (size < sizeof(fmrb_link_graphics_text_t) + cmd->text_len) break;
        const char *text_data = (const char *)(data + sizeof(fmrb_link_graphics_text_t));
        char buf[256];
        size_t len = cmd->text_len < 255u ? cmd->text_len : 255u;
        memcpy(buf, text_data, len);
        buf[len] = '\0';
        auto *s = get_sprite(cmd->canvas_id);
        if (s) {
            if (cmd->bg_transparent) {
                s->setTextColor(cmd->color);
            } else {
                s->setTextColor(cmd->color, cmd->bg_color);
            }
            s->setCursor(cmd->x, cmd->y);
            s->print(buf);
        }
        return 0;
    }

    case FMRB_LINK_GFX_SET_TEXT_SIZE: {
        if (size < sizeof(fmrb_link_graphics_text_size_t)) break;
        const auto *cmd = (const fmrb_link_graphics_text_size_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->setTextSize(cmd->size);
        return 0;
    }

    case FMRB_LINK_GFX_SET_TEXT_COLOR: {
        if (size < 4) break;  // canvas_id(2) + fg(1) + bg(1)
        uint16_t canvas_id = *(const uint16_t *)data;
        uint8_t fg = data[2];
        uint8_t bg = data[3];
        auto *s = get_sprite(canvas_id);
        if (s) s->setTextColor(fg, bg);
        return 0;
    }

    case FMRB_LINK_GFX_SET_FONT: {
        if (size < sizeof(fmrb_link_graphics_set_font_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_font_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s && cmd->family == FMRB_LINK_GFX_FONT_FAMILY_DEFAULT) {
            s->setFont(&fonts::Font0);
        }
        // JA font support is a future task.
        return 0;
    }

    // --- Canvas management ---

    case FMRB_LINK_GFX_CREATE_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_create_canvas_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_canvas_t *)data;

        uint16_t cid = g_next_canvas_id++;
        if (cid == DISPLAY_P4_CANVAS_INVALID) cid = g_next_canvas_id++;

        p4_canvas_t *c = canvas_alloc(cid,
                                      (uint16_t)cmd->width, (uint16_t)cmd->height,
                                      cmd->z_order,
                                      cmd->transparent_color,
                                      (bool)cmd->use_transparent);
        if (!c) {
            uint16_t err = DISPLAY_P4_CANVAS_INVALID;
            send_ack(msg_type, seq, (const uint8_t *)&err, sizeof(err));
        } else {
            FMRB_LOGI(TAG, "CREATE_CANVAS: id=%u %dx%d z=%d",
                      cid, (int)cmd->width, (int)cmd->height, cmd->z_order);
            send_ack(msg_type, seq, (const uint8_t *)&cid, sizeof(cid));
        }
        return 1;  // ACK already sent
    }

    case FMRB_LINK_GFX_DELETE_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_delete_canvas_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_canvas_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) canvas_free(c);
        return 0;
    }

    case FMRB_LINK_GFX_SET_WINDOW_ORDER: {
        if (size < sizeof(fmrb_link_graphics_set_window_order_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_window_order_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) c->z_order = cmd->z_order;
        return 0;
    }

    case FMRB_LINK_GFX_UPDATE_WINDOW: {
        if (size < sizeof(fmrb_link_graphics_update_window_t)) break;
        const auto *cmd = (const fmrb_link_graphics_update_window_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) {
            c->push_x = (int16_t)cmd->x;
            c->push_y = (int16_t)cmd->y;
            c->width  = (uint16_t)cmd->width;
            c->height = (uint16_t)cmd->height;
        }
        return 0;
    }

    case FMRB_LINK_GFX_SET_TARGET:
        return 0;  // Not used in sprite-per-canvas approach

    case FMRB_LINK_GFX_PUSH_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_push_canvas_t)) break;
        const auto *cmd = (const fmrb_link_graphics_push_canvas_t *)data;
        p4_canvas_t *src = canvas_find(cmd->canvas_id);
        if (!src) return -1;

        if (cmd->dest_canvas_id == DISPLAY_P4_CANVAS_RENDER ||
            cmd->dest_canvas_id == DISPLAY_P4_CANVAS_SCREEN) {
            src->push_x           = (int16_t)cmd->x;
            src->push_y           = (int16_t)cmd->y;
            src->transparent_color = cmd->transparent_color;
            src->use_transparency  = (bool)cmd->use_transparency;
            src->is_visible        = true;
            render_frame();
        } else {
            // Push canvas-to-canvas
            p4_canvas_t *dst = canvas_find(cmd->dest_canvas_id);
            if (!dst || !src->sprite || !dst->sprite) return -1;
            if (cmd->use_transparency) {
                src->sprite->pushSprite(dst->sprite, cmd->x, cmd->y,
                                        (uint32_t)cmd->transparent_color);
            } else {
                src->sprite->pushSprite(dst->sprite, cmd->x, cmd->y);
            }
        }
        return 0;
    }

    case FMRB_LINK_GFX_SET_CANVAS_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_set_canvas_visible_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_canvas_visible_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) c->is_visible = (cmd->visible != 0);
        return 0;
    }

    case FMRB_LINK_GFX_PRESENT:
        render_frame();
        return 0;

    case FMRB_LINK_GFX_GET_PIXEL: {
        if (size < sizeof(fmrb_link_graphics_get_pixel_t)) break;
        const auto *cmd = (const fmrb_link_graphics_get_pixel_t *)data;
        fmrb_link_graphics_pixel_value_t resp = {};
        auto *s = get_sprite(cmd->canvas_id);
        if (s && cmd->x >= 0 && cmd->x < (int16_t)s->width() &&
                 cmd->y >= 0 && cmd->y < (int16_t)s->height()) {
            // readPixelValue returns the raw palette index = RGB332 value for 8bpp
            resp.color  = (uint8_t)s->readPixelValue(cmd->x, cmd->y);
            resp.status = 0;
        } else {
            resp.color  = 0;
            resp.status = s ? 1 : 0xFF;
        }
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    // --- Cursor ---

    case FMRB_LINK_GFX_CURSOR_SET_POSITION: {
        if (size < sizeof(fmrb_link_graphics_cursor_position_t)) break;
        const auto *cmd = (const fmrb_link_graphics_cursor_position_t *)data;
        g_cursor_x = cmd->x;
        g_cursor_y = cmd->y;
        return 0;
    }

    case FMRB_LINK_GFX_CURSOR_SET_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_cursor_visible_t)) break;
        const auto *cmd = (const fmrb_link_graphics_cursor_visible_t *)data;
        g_cursor_visible = cmd->visible;
        return 0;
    }

    // --- GfxBlock VM ---

    case FMRB_LINK_GFX_DEFINE_PROG: {
        if (size < sizeof(fmrb_link_graphics_define_prog_t)) break;
        const auto *cmd = (const fmrb_link_graphics_define_prog_t *)data;
        size_t off = sizeof(fmrb_link_graphics_define_prog_t);
        if (size < off + cmd->bytecode_len + cmd->strtable_len) break;

        const uint8_t *bytecode = data + off;
        const uint8_t *strtable = bytecode + cmd->bytecode_len;
        uint8_t prog_id = display_p4_vm_define_prog(
            cmd->canvas_id, bytecode, cmd->bytecode_len,
            strtable, cmd->strtable_len);
        FMRB_LOGI(TAG, "DEFINE_PROG: canvas=%u bc=%u st=%u -> id=%u",
                  cmd->canvas_id, cmd->bytecode_len, cmd->strtable_len, prog_id);
        send_ack(msg_type, seq, &prog_id, sizeof(prog_id));
        return 1;
    }

    case FMRB_LINK_GFX_EXEC_PROG: {
        if (size < sizeof(fmrb_link_graphics_exec_prog_t)) break;
        const auto *cmd = (const fmrb_link_graphics_exec_prog_t *)data;
        size_t off = sizeof(fmrb_link_graphics_exec_prog_t);
        if (size < off + (size_t)cmd->reg_count * 3) break;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) {
            display_p4_vm_exec_prog(cmd->canvas_id, cmd->prog_id,
                                    data + off, cmd->reg_count, s);
        }
        return 0;
    }

    case FMRB_LINK_GFX_DELETE_PROG: {
        if (size < sizeof(fmrb_link_graphics_delete_prog_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_prog_t *)data;
        display_p4_vm_delete_prog(cmd->prog_id);
        return 0;
    }

    // --- Sprite image management ---

    case FMRB_LINK_GFX_CREATE_SPRITE_IMAGE: {
        if (size < sizeof(fmrb_link_graphics_create_sprite_image_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_sprite_image_t *)data;
        uint16_t img_id = display_p4_sprite_image_create(
            cmd->canvas_id, cmd->width, cmd->height,
            cmd->transparent_color, (bool)cmd->use_transparent);
        FMRB_LOGI(TAG, "CREATE_SPRITE_IMAGE: %ux%u -> id=%u",
                  (unsigned)cmd->width, (unsigned)cmd->height, img_id);
        fmrb_link_graphics_sprite_image_created_t resp = { .image_id = img_id };
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_DELETE_SPRITE_IMAGE: {
        if (size < sizeof(fmrb_link_graphics_delete_sprite_image_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_sprite_image_t *)data;
        display_p4_sprite_image_delete(cmd->image_id);
        return 0;
    }

    case FMRB_LINK_GFX_SET_SPRITE_IMAGE_TARGET: {
        if (size < sizeof(fmrb_link_graphics_set_sprite_image_target_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_sprite_image_target_t *)data;
        g_sprite_target_id = cmd->image_id;  // 0 = reset to canvas target
        FMRB_LOGD(TAG, "SET_SPRITE_IMAGE_TARGET: image_id=%u", cmd->image_id);
        return 0;
    }

    case FMRB_LINK_GFX_LOAD_SPRITE_IMAGE_BMP: {
        if (size < sizeof(fmrb_link_graphics_load_sprite_image_bmp_t)) break;
        const auto *cmd = (const fmrb_link_graphics_load_sprite_image_bmp_t *)data;
        if (size < sizeof(*cmd) + cmd->path_len) break;

        // Build VFS path (strip leading '/', prepend /flash/)
        const char *p  = (const char *)(data + sizeof(*cmd));
        int         pl = (int)cmd->path_len;
        if (pl > 0 && p[0] == '/') { p++; pl--; }
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "/flash/%.*s", pl, p);

        LGFX_Sprite *spr = display_p4_sprite_image_get(cmd->image_id);
        if (!spr) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: image %u not found", cmd->image_id);
            return -1;
        }

        FILE *fp = fopen(full_path, "rb");
        if (!fp) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: cannot open %s", full_path);
            return -1;
        }
        fseek(fp, 0, SEEK_END);
        long fsz = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fsz <= 0 || fsz > 65536) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: invalid file size %ld", fsz);
            fclose(fp);
            return -1;
        }

        uint8_t *bmp_buf = (uint8_t *)fmrb_sys_malloc((size_t)fsz);
        if (!bmp_buf) {
            fclose(fp);
            return -1;
        }
        fread(bmp_buf, 1, (size_t)fsz, fp);
        fclose(fp);

        fmrb_bmp332_t bmp;
        if (fmrb_bmp332_parse(bmp_buf, (size_t)fsz, &bmp) != 0) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: parse failed: %s", full_path);
            fmrb_sys_free(bmp_buf);
            return -1;
        }

        uint16_t copy_w = (uint16_t)((bmp.width  < (uint16_t)spr->width())  ? bmp.width  : spr->width());
        uint16_t copy_h = (uint16_t)((bmp.height < (uint16_t)spr->height()) ? bmp.height : spr->height());
        for (uint16_t y = 0; y < copy_h; y++) {
            for (uint16_t x = 0; x < copy_w; x++) {
                spr->drawPixel(x, y, bmp.pixels[(size_t)y * bmp.width + x]);
            }
        }
        fmrb_sys_free(bmp_buf);
        FMRB_LOGI(TAG, "LOAD_SPRITE_BMP: loaded %s -> image %u (%ux%u)",
                  full_path, cmd->image_id, bmp.width, bmp.height);
        return 0;
    }

    // --- Sprite instance management ---

    case FMRB_LINK_GFX_CREATE_SPRITE_INSTANCE: {
        if (size < sizeof(fmrb_link_graphics_create_sprite_instance_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_sprite_instance_t *)data;
        uint16_t inst_id = display_p4_sprite_instance_create(
            cmd->canvas_id, cmd->image_ids, cmd->frame_count,
            cmd->x, cmd->y, cmd->z_order);
        FMRB_LOGI(TAG, "CREATE_SPRITE_INSTANCE: canvas=%u frames=%u -> id=%u",
                  cmd->canvas_id, cmd->frame_count, inst_id);
        fmrb_link_graphics_sprite_instance_created_t resp = { .instance_id = inst_id };
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_DELETE_SPRITE_INSTANCE: {
        if (size < sizeof(fmrb_link_graphics_delete_sprite_instance_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_sprite_instance_t *)data;
        display_p4_sprite_instance_delete(cmd->instance_id);
        return 0;
    }

    case FMRB_LINK_GFX_SPRITE_INSTANCE_MOVE: {
        if (size < sizeof(fmrb_link_graphics_sprite_instance_move_t)) break;
        const auto *cmd = (const fmrb_link_graphics_sprite_instance_move_t *)data;
        display_p4_sprite_instance_move(cmd->instance_id, cmd->x, cmd->y);
        return 0;
    }

    case FMRB_LINK_GFX_SPRITE_INSTANCE_SET_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_sprite_instance_set_visible_t)) break;
        const auto *cmd = (const fmrb_link_graphics_sprite_instance_set_visible_t *)data;
        display_p4_sprite_instance_set_visible(cmd->instance_id, (bool)cmd->visible);
        return 0;
    }

    case FMRB_LINK_GFX_SPRITE_INSTANCE_SET_FRAME: {
        if (size < sizeof(fmrb_link_graphics_sprite_instance_set_frame_t)) break;
        const auto *cmd = (const fmrb_link_graphics_sprite_instance_set_frame_t *)data;
        display_p4_sprite_instance_set_frame(cmd->instance_id, cmd->frame_index);
        return 0;
    }

    case FMRB_LINK_GFX_DELETE_ALL_SPRITES: {
        if (size < sizeof(fmrb_link_graphics_delete_all_sprites_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_all_sprites_t *)data;
        display_p4_sprite_delete_all_for_canvas(cmd->canvas_id);
        return 0;
    }

    // --- Draw image (SpriteImage → canvas) ---

    case FMRB_LINK_GFX_DRAW_IMAGE: {
        if (size < sizeof(fmrb_link_graphics_draw_image_t)) break;
        const auto *cmd = (const fmrb_link_graphics_draw_image_t *)data;
        LGFX_Sprite *src = display_p4_sprite_image_get(cmd->image_id);
        if (!src) return -1;
        auto *dst = get_sprite(cmd->canvas_id);
        if (!dst) return -1;

        uint8_t trans_color = 0;
        bool use_trans = display_p4_sprite_image_get_transparent(cmd->image_id, &trans_color);
        // 1:1 scale only (scale_x_fp8=256 means 1.0); ignoring scale for now.
        if (use_trans) {
            src->pushSprite(dst, cmd->x, cmd->y, (uint32_t)trans_color);
        } else {
            src->pushSprite(dst, cmd->x, cmd->y);
        }
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_TILE: {
        if (size < sizeof(fmrb_link_graphics_draw_tile_t)) break;
        const auto *cmd = (const fmrb_link_graphics_draw_tile_t *)data;
        LGFX_Sprite *src = display_p4_sprite_image_get(cmd->image_id);
        if (!src) return -1;
        auto *dst = get_sprite(cmd->canvas_id);
        if (!dst) return -1;

        uint8_t trans_color = 0;
        bool use_trans = display_p4_sprite_image_get_transparent(cmd->image_id, &trans_color);
        int sw = src->width();
        int sh = src->height();
        for (int yy = 0; yy < (int)cmd->h; yy++) {
            int sy = cmd->src_y + yy;
            if (sy < 0 || sy >= sh) continue;
            for (int xx = 0; xx < (int)cmd->w; xx++) {
                int sx = cmd->src_x + xx;
                if (sx < 0 || sx >= sw) continue;
                uint8_t pixel = (uint8_t)src->readPixelValue(sx, sy);
                if (use_trans && pixel == trans_color) continue;
                dst->drawPixel(cmd->dst_x + xx, cmd->dst_y + yy, pixel);
            }
        }
        return 0;
    }

    // --- Mask operations ---

    case FMRB_LINK_GFX_CREATE_MASK: {
        if (size < sizeof(fmrb_link_graphics_create_mask_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_mask_t *)data;
        uint16_t mask_id = display_p4_mask_create(cmd->canvas_id, cmd->width, cmd->height);
        fmrb_link_graphics_mask_created_t resp = { .mask_id = mask_id };
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_MASK_DATA: {
        if (size < sizeof(fmrb_link_graphics_mask_data_t)) break;
        const auto *cmd = (const fmrb_link_graphics_mask_data_t *)data;
        size_t off = sizeof(fmrb_link_graphics_mask_data_t);
        if (size < off + cmd->chunk_len) break;
        display_p4_mask_data(cmd->mask_id, data + off, cmd->chunk_len, cmd->offset);
        return 0;
    }

    case FMRB_LINK_GFX_DELETE_MASK: {
        if (size < sizeof(fmrb_link_graphics_delete_mask_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_mask_t *)data;
        display_p4_mask_delete(cmd->mask_id);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_IMAGE_MASKED: {
        if (size < sizeof(fmrb_link_graphics_draw_image_masked_t)) break;
        const auto *cmd = (const fmrb_link_graphics_draw_image_masked_t *)data;
        LGFX_Sprite *src = display_p4_sprite_image_get(cmd->image_id);
        if (!src) return -1;
        auto *dst = get_sprite(cmd->canvas_id);
        if (!dst) return -1;
        display_p4_mask_blit(cmd->mask_id, src, dst, cmd->x, cmd->y);
        return 0;
    }

    // --- Blend / output-level (no-op stubs for API completeness) ---

    case FMRB_LINK_GFX_BLEND_RECT:
    case FMRB_LINK_GFX_SET_OUTPUT_LEVEL:
    case FMRB_LINK_GFX_SET_CHROMA_LEVEL:
    case FMRB_LINK_GFX_SET_COMPOSITE_REGIONS:
    case FMRB_LINK_GFX_SET_TARGET:
    case FMRB_LINK_GFX_DRAW_CHAR:
    case FMRB_LINK_GFX_DRAW_BITMAP:
    case FMRB_LINK_GFX_CREATE_IMAGE_FROM_MEM:
    case FMRB_LINK_GFX_CREATE_IMAGE_FROM_FILE:
    case FMRB_LINK_GFX_DELETE_IMAGE:
        FMRB_LOGD(TAG, "GFX cmd 0x%02x: ACK only (not implemented)", sub_cmd);
        return 0;

    default:
        FMRB_LOGW(TAG, "Unknown GFX cmd: 0x%02x", sub_cmd);
        return -1;
    }

    FMRB_LOGE(TAG, "GFX cmd 0x%02x: payload too small (size=%zu)", sub_cmd, size);
    return -1;
}

// ============================================================
// Message dispatcher
// msgpack format: [type(u8), seq(u8), sub_cmd(u8), payload(bin|nil)]
// ============================================================

static void process_message(const uint8_t *msgpack_data, size_t msgpack_len) {
    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);

    msgpack_unpack_return ret = msgpack_unpack_next(
        &msg, (const char *)msgpack_data, msgpack_len, NULL);
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

    uint8_t type    = (uint8_t)root.via.array.ptr[0].via.u64;
    uint8_t seq     = (uint8_t)root.via.array.ptr[1].via.u64;
    uint8_t sub_cmd = (uint8_t)root.via.array.ptr[2].via.u64;
    uint8_t base_type    = type & FMRB_LINK_TYPE_MASK;
    bool    ack_required = (type & FMRB_LINK_FLAG_ACK_REQUIRED) != 0;

    const uint8_t *payload     = nullptr;
    size_t         payload_len = 0;
    if (root.via.array.ptr[3].type == MSGPACK_OBJECT_BIN) {
        payload     = (const uint8_t *)root.via.array.ptr[3].via.bin.ptr;
        payload_len = root.via.array.ptr[3].via.bin.size;
    }

    switch (base_type) {
    case FMRB_LINK_TYPE_CONTROL:
        if (sub_cmd == FMRB_LINK_CONTROL_VERSION &&
            payload_len >= sizeof(fmrb_control_version_req_t)) {
            uint8_t local_ver = FMRB_LINK_VERSION;
            FMRB_LOGI(TAG, "VERSION check: remote=%u local=%u seq=%u",
                      payload[0], local_ver, seq);
            send_ack(type, seq, &local_ver, sizeof(local_ver));
        } else if (sub_cmd == FMRB_LINK_CONTROL_INIT_DISPLAY &&
                   payload_len >= sizeof(fmrb_control_init_display_t)) {
            const auto *init_cmd = (const fmrb_control_init_display_t *)payload;
            FMRB_LOGI(TAG, "INIT_DISPLAY: %dx%d %d-bit margin=%d,%d",
                      init_cmd->width, init_cmd->height, init_cmd->color_depth,
                      init_cmd->margin_x, init_cmd->margin_y);
            send_ack(type, seq, nullptr, 0);
        } else if (sub_cmd == FMRB_LINK_CONTROL_GA_VERSION) {
            fmrb_control_ga_version_resp_t resp = {};
            strncpy(resp.version, FMRB_GA_VERSION, sizeof(resp.version) - 1);
            FMRB_LOGI(TAG, "GA_VERSION: responding with \"%s\"", resp.version);
            send_ack(type, seq, (const uint8_t *)&resp, sizeof(resp));
        } else {
            if (ack_required) send_ack(type, seq, nullptr, 0);
        }
        break;

    case FMRB_LINK_TYPE_GRAPHICS:
        if (payload && payload_len > 0) {
            int result = process_gfx_command(type, sub_cmd, seq, payload, payload_len);
            // 0 = success, no ACK yet; 1 = ACK already sent; -1 = error
            if (result == 0 && ack_required) {
                send_ack(type, seq, nullptr, 0);
            }
        } else if (ack_required) {
            send_ack(type, seq, nullptr, 0);
        }
        break;

    default:
        if (ack_required) send_ack(type, seq, nullptr, 0);
        break;
    }

    msgpack_unpacked_destroy(&msg);
}

// ============================================================
// Main task
// ============================================================

static void display_p4_task(void *arg) {
    (void)arg;
    FMRB_LOGI(TAG, "Tab5 display: power on");
    tab5_power_on();

    FMRB_LOGI(TAG, "Tab5 display: VM + cursor init");
    display_p4_vm_init();
    cursor_init();

    FMRB_LOGI(TAG, "Tab5 display: LGFX init");
    if (!g_lcd.init()) {
        FMRB_LOGE(TAG, "LGFX init failed");
    } else {
        g_lcd.setRotation(1); // landscape: 1280x720 (native portrait 720x1280 rotated 90deg CW)
        FMRB_LOGI(TAG, "LGFX init OK (%dx%d)", g_lcd.width(), g_lcd.height());
        g_lcd.fillScreen(g_lcd.color888(0, 0, 64));
        g_lcd.setTextColor(g_lcd.color888(255, 255, 255));
        g_lcd.setTextSize(4);
        g_lcd.setCursor(20, 20);
        g_lcd.print("Family mruby");
        g_lcd.setCursor(20, 80);
        g_lcd.print("Modern (P4)");
        g_lcd.setTextSize(2);
        g_lcd.setCursor(20, 160);
        g_lcd.print("Initializing...");
    }

    FMRB_LOGI(TAG, "Tab5 display: entering command receive loop");

    while (1) {
        fmrb_link_message_t msg = {
            .data = g_recv_buf,
            .size = sizeof(g_recv_buf),
        };
        fmrb_err_t err = fmrb_hal_link_local_receive_cmd(
            FMRB_LINK_CHANNEL_DEFAULT, &msg, 100);
        if (err == FMRB_OK && msg.size > 0) {
            process_message(g_recv_buf, msg.size);
        }
    }
}

extern "C" fmrb_err_t display_p4_task_init(void) {
    BaseType_t ok = xTaskCreatePinnedToCore(display_p4_task, "display_p4",
                                            8192, NULL, 5, NULL, 1);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create display_p4 task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

extern "C" fmrb_err_t display_p4_task_deinit(void) {
    return FMRB_OK;
}
