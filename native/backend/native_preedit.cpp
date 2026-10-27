/*
 * native_preedit.cpp - the text an IME is composing, drawn in Family mruby's
 * own font (native_ime.h, doc/linux_native L12b).
 *
 * The outputs lay it over the picture at the focused app's text cursor (the
 * compositor as a scene buffer, the SDL window as a texture), at the
 * picture's scale, so it reads as the app's own text being typed. The app
 * is not involved: it only says where its cursor is, and gets the text when
 * the IME commits it.
 *
 * A sprite of LovyanGFX over a buffer of our own (16-bit: LovyanGFX keeps
 * RGB565 with its bytes swapped), the efontJA fonts the display task draws
 * the apps' Japanese with. The colours are given as uint16_t (LovyanGFX
 * reads an int as RGB888 and a uint8_t as RGB332).
 */

#include <M5GFX.h>
#include <string.h>

#include "native_ime.h"

#include "fonts/misaki/lgfx_misaki_fonts.hpp"

/* The widest text drawn, and the tallest line (efontJA 16 plus the line). */
#define PREEDIT_MAX_W 1024
#define PREEDIT_MAX_H 18

static uint16_t s_px[PREEDIT_MAX_W * PREEDIT_MAX_H];

/* RGB565 */
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_BLACK = 0x0000;
static const uint16_t C_SEL = 0x0011;   /* dark blue: the part being converted */

static uint32_t argb(uint16_t sw)
{
    uint16_t c = (uint16_t)((sw >> 8) | (sw << 8));
    uint32_t r = ((c >> 11) & 31) * 255 / 31;
    uint32_t g = ((c >> 5) & 63) * 255 / 63;
    uint32_t b = (c & 31) * 255 / 31;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* The width of the first n bytes of utf8 in the sprite's font. */
static int width_of(lgfx::LGFX_Sprite &sp, const char *utf8, int n)
{
    if (n <= 0) return 0;
    char buf[512];
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    memcpy(buf, utf8, (size_t)n);
    buf[n] = '\0';
    return (int)sp.textWidth(buf);
}

extern "C" bool native_preedit_render(const char *utf8, int sel_begin, int sel_end, int line_h,
                                      uint32_t *out, int cap, int *w, int *h)
{
    if (utf8 == NULL || utf8[0] == '\0' || out == NULL) return false;
    int len = (int)strlen(utf8);
    if (len >= 512) len = 511;

    static lgfx::LGFX_Sprite sp;
    const lgfx::IFont *font = &fonts::efontJA_12;
    int font_h = 12;
    if (line_h >= 16) {
        font = &fonts::efontJA_16;
        font_h = 16;
    } else if (line_h < 12) {
        font = &fonts::misaki_8;
        font_h = 8;
    }
    sp.setBuffer(s_px, 1, 1, 16);
    sp.setFont(font);
    sp.setTextSize(1);
    char text[512];
    memcpy(text, utf8, (size_t)len);
    text[len] = '\0';
    int tw = (int)sp.textWidth(text);
    int bw = tw + 2;                    /* a pixel either side, room for the caret */
    int bh = font_h + 2;                /* the line under it, and a pixel above */
    if (bw > PREEDIT_MAX_W) bw = PREEDIT_MAX_W;
    if (bw * bh > cap || bh > PREEDIT_MAX_H) return false;

    sp.setBuffer(s_px, bw, bh, 16);
    sp.setFont(font);
    sp.setTextSize(1);
    sp.fillScreen(C_WHITE);
    sp.setTextColor(C_BLACK, C_WHITE);
    sp.setTextDatum(lgfx::top_left);
    sp.drawString(text, 1, 1);
    /* A cursor of -1 is a hidden one. */
    bool caret = sel_begin >= 0;
    if (sel_begin < 0) sel_begin = 0;
    if (sel_end > len) sel_end = len;
    if (caret && sel_begin < sel_end) {
        /* The part being converted: white on blue. */
        int x0 = 1 + width_of(sp, text, sel_begin);
        int x1 = 1 + width_of(sp, text, sel_end);
        sp.fillRect(x0, 0, x1 - x0, bh - 1, C_SEL);
        sp.setTextColor(C_WHITE, C_SEL);
        char part[512];
        memcpy(part, text + sel_begin, (size_t)(sel_end - sel_begin));
        part[sel_end - sel_begin] = '\0';
        sp.drawString(part, x0, 1);
    } else if (caret && sel_begin < len) {
        int x = 1 + width_of(sp, text, sel_begin);
        sp.drawFastVLine(x - 1, 0, bh - 1, C_BLACK);
    }
    sp.drawFastHLine(0, bh - 1, bw, C_BLACK);

    for (int i = 0; i < bw * bh; i++) out[i] = argb(s_px[i]);
    *w = bw;
    *h = bh;
    return true;
}
