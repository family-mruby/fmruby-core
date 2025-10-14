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
                printf("FILL_SCREEN: color=0x%08x\n", cmd->color);
                g_lgfx->fillScreen(cmd->color);
                g_lgfx->display();
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
                // For now, always use filled rect
                g_lgfx->fillRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_CIRCLE:
            if (size >= sizeof(fmrb_gfx_circle_cmd_t)) {
                const fmrb_gfx_circle_cmd_t *cmd = (const fmrb_gfx_circle_cmd_t*)data;
                // For now, always use filled circle
                g_lgfx->fillCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_ELLIPSE:
            if (size >= sizeof(fmrb_gfx_ellipse_cmd_t)) {
                const fmrb_gfx_ellipse_cmd_t *cmd = (const fmrb_gfx_ellipse_cmd_t*)data;
                // For now, always use filled ellipse
                g_lgfx->fillEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_TRIANGLE:
            if (size >= sizeof(fmrb_gfx_triangle_cmd_t)) {
                const fmrb_gfx_triangle_cmd_t *cmd = (const fmrb_gfx_triangle_cmd_t*)data;
                g_lgfx->fillTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_STRING:
            if (size >= sizeof(fmrb_gfx_string_cmd_t)) {
                const fmrb_gfx_string_cmd_t *cmd = (const fmrb_gfx_string_cmd_t*)data;
                const uint8_t *text_data = data + sizeof(fmrb_gfx_string_cmd_t);
                if (size >= sizeof(fmrb_gfx_string_cmd_t) + cmd->text_len) {
                    char text_buf[256];
                    size_t len = cmd->text_len < 255 ? cmd->text_len : 255;
                    memcpy(text_buf, text_data, len);
                    text_buf[len] = '\0';
                    printf("Drawing string at (%d,%d) color=0x%08x size=%d: %s\n",
                           cmd->x, cmd->y, cmd->color, cmd->font_size, text_buf);

                    g_lgfx->setTextColor(cmd->color);
                    g_lgfx->setTextSize(cmd->font_size / 8.0f); // Approximate scaling
                    g_lgfx->setCursor(cmd->x, cmd->y);
                    g_lgfx->print(text_buf);
                    g_lgfx->display();
                    return 0;
                }
            }
            break;

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
