#include "graphics_handler.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static SDL_Renderer *g_renderer = NULL;

int graphics_handler_init(SDL_Renderer *renderer) {
    if (!renderer) {
        fprintf(stderr, "Invalid renderer\n");
        return -1;
    }

    g_renderer = renderer;

    // Set initial render color to black
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);

    printf("Graphics handler initialized\n");
    return 0;
}

void graphics_handler_cleanup(void) {
    g_renderer = NULL;
    printf("Graphics handler cleaned up\n");
}

SDL_Renderer* graphics_handler_get_renderer(void) {
    return g_renderer;
}

static void color_to_rgb(fmrb_color_t color, uint8_t *r, uint8_t *g, uint8_t *b) {
    // Color format: 0xAARRGGBB (ARGB8888)
    *r = (color >> 16) & 0xFF;
    *g = (color >> 8) & 0xFF;
    *b = color & 0xFF;
}

// Helper to draw filled circle using scanlines
static void draw_filled_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    for (int y = -radius; y <= radius; y++) {
        int x = (int)sqrt(radius * radius - y * y);
        SDL_RenderDrawLine(g_renderer, cx - x, cy + y, cx + x, cy + y);
    }
}

// Helper to draw circle outline
static void draw_circle_outline(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    while (y >= x) {
        SDL_RenderDrawPoint(g_renderer, cx + x, cy + y);
        SDL_RenderDrawPoint(g_renderer, cx - x, cy + y);
        SDL_RenderDrawPoint(g_renderer, cx + x, cy - y);
        SDL_RenderDrawPoint(g_renderer, cx - x, cy - y);
        SDL_RenderDrawPoint(g_renderer, cx + y, cy + x);
        SDL_RenderDrawPoint(g_renderer, cx - y, cy + x);
        SDL_RenderDrawPoint(g_renderer, cx + y, cy - x);
        SDL_RenderDrawPoint(g_renderer, cx - y, cy - x);

        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

int graphics_handler_process_command(const uint8_t *data, size_t size) {
    if (!g_renderer || !data || size == 0) {
        return -1;
    }

    uint8_t cmd_type = data[0];
    uint8_t r, g, b;

    switch (cmd_type) {
        case FMRB_GFX_CMD_CLEAR:
        case FMRB_GFX_CMD_FILL_SCREEN:
            if (size >= sizeof(fmrb_gfx_clear_cmd_t)) {
                const fmrb_gfx_clear_cmd_t *cmd = (const fmrb_gfx_clear_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                printf("FILL_SCREEN: color=0x%08x -> RGB(%d,%d,%d)\n", cmd->color, r, g, b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                SDL_RenderClear(g_renderer);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_PIXEL:
            if (size >= sizeof(fmrb_gfx_pixel_cmd_t)) {
                const fmrb_gfx_pixel_cmd_t *cmd = (const fmrb_gfx_pixel_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                SDL_RenderDrawPoint(g_renderer, cmd->x, cmd->y);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_LINE:
            if (size >= sizeof(fmrb_gfx_line_cmd_t)) {
                const fmrb_gfx_line_cmd_t *cmd = (const fmrb_gfx_line_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                SDL_RenderDrawLine(g_renderer, cmd->x1, cmd->y1, cmd->x2, cmd->y2);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_FAST_VLINE:
            if (size >= sizeof(fmrb_gfx_vline_cmd_t)) {
                const fmrb_gfx_vline_cmd_t *cmd = (const fmrb_gfx_vline_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                SDL_RenderDrawLine(g_renderer, cmd->x, cmd->y, cmd->x, cmd->y + cmd->h);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_FAST_HLINE:
            if (size >= sizeof(fmrb_gfx_hline_cmd_t)) {
                const fmrb_gfx_hline_cmd_t *cmd = (const fmrb_gfx_hline_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                SDL_RenderDrawLine(g_renderer, cmd->x, cmd->y, cmd->x + cmd->w, cmd->y);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_RECT:
        case FMRB_GFX_CMD_FILL_RECT:
            if (size >= sizeof(fmrb_gfx_rect_cmd_t)) {
                const fmrb_gfx_rect_cmd_t *cmd = (const fmrb_gfx_rect_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_Rect rect = {cmd->x, cmd->y, cmd->width, cmd->height};
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                if (cmd_type == FMRB_GFX_CMD_FILL_RECT) {
                    SDL_RenderFillRect(g_renderer, &rect);
                } else {
                    SDL_RenderDrawRect(g_renderer, &rect);
                }
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_ROUND_RECT:
        case FMRB_GFX_CMD_FILL_ROUND_RECT:
            if (size >= sizeof(fmrb_gfx_round_rect_cmd_t)) {
                const fmrb_gfx_round_rect_cmd_t *cmd = (const fmrb_gfx_round_rect_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_Rect rect = {cmd->x, cmd->y, cmd->width, cmd->height};
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                if (cmd_type == FMRB_GFX_CMD_FILL_ROUND_RECT) {
                    SDL_RenderFillRect(g_renderer, &rect);
                } else {
                    SDL_RenderDrawRect(g_renderer, &rect);
                }
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_CIRCLE:
        case FMRB_GFX_CMD_FILL_CIRCLE:
            if (size >= sizeof(fmrb_gfx_circle_cmd_t)) {
                const fmrb_gfx_circle_cmd_t *cmd = (const fmrb_gfx_circle_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                if (cmd_type == FMRB_GFX_CMD_FILL_CIRCLE) {
                    draw_filled_circle(cmd->x, cmd->y, cmd->radius, r, g, b);
                } else {
                    draw_circle_outline(cmd->x, cmd->y, cmd->radius, r, g, b);
                }
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_ELLIPSE:
        case FMRB_GFX_CMD_FILL_ELLIPSE:
            if (size >= sizeof(fmrb_gfx_ellipse_cmd_t)) {
                const fmrb_gfx_ellipse_cmd_t *cmd = (const fmrb_gfx_ellipse_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                // Simplified ellipse
                for (int angle = 0; angle < 360; angle += 2) {
                    float rad = angle * 3.14159f / 180.0f;
                    int x = (int)(cmd->x + cmd->rx * cos(rad));
                    int y = (int)(cmd->y + cmd->ry * sin(rad));
                    SDL_RenderDrawPoint(g_renderer, x, y);
                }
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_TRIANGLE:
        case FMRB_GFX_CMD_FILL_TRIANGLE:
            if (size >= sizeof(fmrb_gfx_triangle_cmd_t)) {
                const fmrb_gfx_triangle_cmd_t *cmd = (const fmrb_gfx_triangle_cmd_t*)data;
                color_to_rgb(cmd->color, &r, &g, &b);
                SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
                SDL_RenderDrawLine(g_renderer, cmd->x0, cmd->y0, cmd->x1, cmd->y1);
                SDL_RenderDrawLine(g_renderer, cmd->x1, cmd->y1, cmd->x2, cmd->y2);
                SDL_RenderDrawLine(g_renderer, cmd->x2, cmd->y2, cmd->x0, cmd->y0);
                return 0;
            }
            break;

        case FMRB_GFX_CMD_DRAW_STRING:
            printf("DRAW_STRING: size=%zu, sizeof(cmd)=%zu\n", size, sizeof(fmrb_gfx_string_cmd_t));
            if (size >= sizeof(fmrb_gfx_string_cmd_t)) {
                const fmrb_gfx_string_cmd_t *cmd = (const fmrb_gfx_string_cmd_t*)data;
                printf("cmd: x=%d y=%d color=0x%08x font_size=%d text_len=%d\n",
                       cmd->x, cmd->y, cmd->color, cmd->font_size, cmd->text_len);
                const uint8_t *text_data = data + sizeof(fmrb_gfx_string_cmd_t);
                if (size >= sizeof(fmrb_gfx_string_cmd_t) + cmd->text_len) {
                    char text_buf[256];
                    size_t len = cmd->text_len < 255 ? cmd->text_len : 255;
                    memcpy(text_buf, text_data, len);
                    text_buf[len] = '\0';
                    printf("Drawing string at (%d,%d) color=0x%08x size=%d: %s\n",
                           cmd->x, cmd->y, cmd->color, cmd->font_size, text_buf);

                    // TODO: Implement actual SDL2 text rendering
                    // For now, just acknowledge the command was received
                    return 0;
                }
            }
            break;

        case FMRB_GFX_CMD_DRAW_TEXT:
            if (size >= sizeof(fmrb_gfx_text_cmd_t)) {
                const fmrb_gfx_text_cmd_t *cmd = (const fmrb_gfx_text_cmd_t*)data;
                const uint8_t *text_data = data + sizeof(fmrb_gfx_text_cmd_t);
                if (size >= sizeof(fmrb_gfx_text_cmd_t) + cmd->text_len) {
                    printf("Text: %.*s\n", cmd->text_len, text_data);
                    return 0;
                }
            }
            break;

        case FMRB_GFX_CMD_PRESENT:
            SDL_RenderPresent(g_renderer);
            return 0;

        default:
            fprintf(stderr, "Unknown graphics command: 0x%02x\n", cmd_type);
            return -1;
    }

    fprintf(stderr, "Invalid command size for type 0x%02x (size=%zu)\n", cmd_type, size);
    return -1;
}
