#include "graphics_handler.h"
#include <stdio.h>
#include <string.h>

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

static void rgb565_to_rgb888(fmrb_color_t color565, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = ((color565 & 0xF800) >> 11) << 3;
    *g = ((color565 & 0x07E0) >> 5) << 2;
    *b = (color565 & 0x001F) << 3;
}

static int process_clear_command(const fmrb_gfx_clear_cmd_t *cmd) {
    uint8_t r, g, b;
    rgb565_to_rgb888(cmd->color, &r, &g, &b);

    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    SDL_RenderClear(g_renderer);

    return 0;
}

static int process_pixel_command(const fmrb_gfx_pixel_cmd_t *cmd) {
    uint8_t r, g, b;
    rgb565_to_rgb888(cmd->color, &r, &g, &b);

    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    SDL_RenderDrawPoint(g_renderer, cmd->x, cmd->y);

    return 0;
}

static int process_line_command(const fmrb_gfx_line_cmd_t *cmd) {
    uint8_t r, g, b;
    rgb565_to_rgb888(cmd->color, &r, &g, &b);

    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    SDL_RenderDrawLine(g_renderer, cmd->x1, cmd->y1, cmd->x2, cmd->y2);

    return 0;
}

static int process_rect_command(const fmrb_gfx_rect_cmd_t *cmd) {
    uint8_t r, g, b;
    rgb565_to_rgb888(cmd->color, &r, &g, &b);

    SDL_Rect rect = {cmd->x, cmd->y, cmd->width, cmd->height};
    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);

    if (cmd->cmd_type == FMRB_GFX_CMD_FILL_RECT) {
        SDL_RenderFillRect(g_renderer, &rect);
    } else {
        SDL_RenderDrawRect(g_renderer, &rect);
    }

    return 0;
}

static int process_text_command(const fmrb_gfx_text_cmd_t *cmd, const uint8_t *text_data) {
    // Simple text rendering - for now just draw a placeholder rectangle
    uint8_t r, g, b;
    rgb565_to_rgb888(cmd->color, &r, &g, &b);

    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);

    // Draw a simple text placeholder (8x8 per character)
    int char_width = 8;
    int char_height = 8;
    for (int i = 0; i < cmd->text_len && i < 64; i++) {
        SDL_Rect char_rect = {
            cmd->x + i * char_width,
            cmd->y,
            char_width,
            char_height
        };
        SDL_RenderDrawRect(g_renderer, &char_rect);
    }

    printf("Text: %.*s\n", cmd->text_len, text_data);
    return 0;
}

static int process_present_command(void) {
    SDL_RenderPresent(g_renderer);
    return 0;
}

int graphics_handler_process_command(const uint8_t *data, size_t size) {
    if (!g_renderer || !data || size == 0) {
        return -1;
    }

    uint8_t cmd_type = data[0];

    switch (cmd_type) {
        case FMRB_GFX_CMD_CLEAR:
            if (size >= sizeof(fmrb_gfx_clear_cmd_t)) {
                return process_clear_command((const fmrb_gfx_clear_cmd_t*)data);
            }
            break;

        case FMRB_GFX_CMD_DRAW_PIXEL:
            if (size >= sizeof(fmrb_gfx_pixel_cmd_t)) {
                return process_pixel_command((const fmrb_gfx_pixel_cmd_t*)data);
            }
            break;

        case FMRB_GFX_CMD_DRAW_LINE:
            if (size >= sizeof(fmrb_gfx_line_cmd_t)) {
                return process_line_command((const fmrb_gfx_line_cmd_t*)data);
            }
            break;

        case FMRB_GFX_CMD_DRAW_RECT:
        case FMRB_GFX_CMD_FILL_RECT:
            if (size >= sizeof(fmrb_gfx_rect_cmd_t)) {
                return process_rect_command((const fmrb_gfx_rect_cmd_t*)data);
            }
            break;

        case FMRB_GFX_CMD_DRAW_TEXT:
            if (size >= sizeof(fmrb_gfx_text_cmd_t)) {
                const fmrb_gfx_text_cmd_t *text_cmd = (const fmrb_gfx_text_cmd_t*)data;
                const uint8_t *text_data = data + sizeof(fmrb_gfx_text_cmd_t);
                if (size >= sizeof(fmrb_gfx_text_cmd_t) + text_cmd->text_len) {
                    return process_text_command(text_cmd, text_data);
                }
            }
            break;

        case FMRB_GFX_CMD_PRESENT:
            return process_present_command();

        default:
            fprintf(stderr, "Unknown graphics command: 0x%02x\n", cmd_type);
            return -1;
    }

    fprintf(stderr, "Invalid command size for type 0x%02x\n", cmd_type);
    return -1;
}