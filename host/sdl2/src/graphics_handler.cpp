#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include "../common/graphics_commands.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include "graphics_handler.h"
}

// External reference to LGFX instance created in main.cpp
extern LGFX* g_lgfx;

extern "C" int graphics_handler_init(SDL_Renderer *renderer) {
    (void)renderer; // SDL_Renderer is not used with LovyanGFX

    if (!g_lgfx) {
        fprintf(stderr, "LGFX instance not created\n");
        return -1;
    }

    printf("Graphics handler initialized (using external LGFX instance)\n");
    return 0;
}

extern "C" void graphics_handler_cleanup(void) {
    // Note: g_lgfx is managed by main.cpp, don't delete here
    printf("Graphics handler cleaned up\n");
}

extern "C" SDL_Renderer* graphics_handler_get_renderer(void) {
    return nullptr; // Not used with LovyanGFX
}

extern "C" int graphics_handler_process_command(const uint8_t *data, size_t size) {
    if (!data || size < 1 || !g_lgfx) {
        return -1;
    }

    uint8_t cmd_type = data[0];

    switch (cmd_type) {
        case FMRB_GFX_CMD_CLEAR:
        case FMRB_GFX_CMD_FILL_SCREEN:
            if (size >= sizeof(fmrb_gfx_clear_cmd_t)) {
                const fmrb_gfx_clear_cmd_t *cmd = (const fmrb_gfx_clear_cmd_t*)data;
#ifdef FMRB_IPC_DEBUG
                printf("FILL_SCREEN: color=0x%08x\n", cmd->color);
#endif
                g_lgfx->fillScreen(cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_PIXEL:
            if (size >= sizeof(fmrb_gfx_pixel_cmd_t)) {
                const fmrb_gfx_pixel_cmd_t *cmd = (const fmrb_gfx_pixel_cmd_t*)data;
                g_lgfx->drawPixel(cmd->x, cmd->y, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_LINE:
            if (size >= sizeof(fmrb_gfx_line_cmd_t)) {
                const fmrb_gfx_line_cmd_t *cmd = (const fmrb_gfx_line_cmd_t*)data;
                g_lgfx->drawLine(cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_RECT:
            if (size >= sizeof(fmrb_gfx_rect_cmd_t)) {
                const fmrb_gfx_rect_cmd_t *cmd = (const fmrb_gfx_rect_cmd_t*)data;
                g_lgfx->drawRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_RECT:
            if (size >= sizeof(fmrb_gfx_rect_cmd_t)) {
                const fmrb_gfx_rect_cmd_t *cmd = (const fmrb_gfx_rect_cmd_t*)data;
                g_lgfx->fillRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_ROUND_RECT:
            if (size >= sizeof(fmrb_gfx_round_rect_cmd_t)) {
                const fmrb_gfx_round_rect_cmd_t *cmd = (const fmrb_gfx_round_rect_cmd_t*)data;
                g_lgfx->drawRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_ROUND_RECT:
            if (size >= sizeof(fmrb_gfx_round_rect_cmd_t)) {
                const fmrb_gfx_round_rect_cmd_t *cmd = (const fmrb_gfx_round_rect_cmd_t*)data;
                g_lgfx->fillRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_CIRCLE:
            if (size >= sizeof(fmrb_gfx_circle_cmd_t)) {
                const fmrb_gfx_circle_cmd_t *cmd = (const fmrb_gfx_circle_cmd_t*)data;
                g_lgfx->drawCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_CIRCLE:
            if (size >= sizeof(fmrb_gfx_circle_cmd_t)) {
                const fmrb_gfx_circle_cmd_t *cmd = (const fmrb_gfx_circle_cmd_t*)data;
                g_lgfx->fillCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_ELLIPSE:
            if (size >= sizeof(fmrb_gfx_ellipse_cmd_t)) {
                const fmrb_gfx_ellipse_cmd_t *cmd = (const fmrb_gfx_ellipse_cmd_t*)data;
                g_lgfx->drawEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_ELLIPSE:
            if (size >= sizeof(fmrb_gfx_ellipse_cmd_t)) {
                const fmrb_gfx_ellipse_cmd_t *cmd = (const fmrb_gfx_ellipse_cmd_t*)data;
                g_lgfx->fillEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_TRIANGLE:
            if (size >= sizeof(fmrb_gfx_triangle_cmd_t)) {
                const fmrb_gfx_triangle_cmd_t *cmd = (const fmrb_gfx_triangle_cmd_t*)data;
                g_lgfx->drawTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_FILL_TRIANGLE:
            if (size >= sizeof(fmrb_gfx_triangle_cmd_t)) {
                const fmrb_gfx_triangle_cmd_t *cmd = (const fmrb_gfx_triangle_cmd_t*)data;
                g_lgfx->fillTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_STRING:
            // cmd_type is already extracted in data[0]
            // Payload format: int32_t x, int32_t y, uint32_t color, uint16_t text_len, char[text_len] text
            if (size < 1 + 4 + 4 + 4 + 2) { // cmd_type + x + y + color + text_len
                break;
            }
            {
                const uint8_t *payload = data + 1;  // Skip cmd_type
                int32_t x, y;
                uint32_t color;
                uint16_t text_len;

                memcpy(&x, payload, sizeof(int32_t)); payload += sizeof(int32_t);
                memcpy(&y, payload, sizeof(int32_t)); payload += sizeof(int32_t);
                memcpy(&color, payload, sizeof(uint32_t)); payload += sizeof(uint32_t);
                memcpy(&text_len, payload, sizeof(uint16_t)); payload += sizeof(uint16_t);

                size_t expected_size = 1 + 4 + 4 + 4 + 2 + text_len;
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

                g_lgfx->setTextColor(color);
                g_lgfx->setCursor(x, y);
                g_lgfx->print(text_buf);
                return 0;
            }

        case FMRB_GFX_CMD_PRESENT:
            g_lgfx->display();
            return 0;

        default:
            fprintf(stderr, "Unknown graphics command: 0x%02x\n", cmd_type);
            return -1;
    }

    fprintf(stderr, "Invalid command size for type 0x%02x (size=%zu)\n", cmd_type, size);
    return -1;
}
