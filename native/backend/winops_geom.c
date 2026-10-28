/*
 * winops_geom.c - see winops_geom.h.
 */

#include "winops_geom.h"

winops_rect_t winops_layout_rect(winops_layout_t layout, int screen_w, int screen_h)
{
    winops_rect_t r = { 0, WINOPS_MENU_BAR_H, screen_w, screen_h - WINOPS_MENU_BAR_H };
    if (r.h < 0) r.h = 0;
    int half = screen_w / 2;
    if (layout == WINOPS_LEFT) {
        r.w = half;
    } else if (layout == WINOPS_RIGHT) {
        r.x = half;
        r.w = screen_w - half;
    }
    return r;
}

bool winops_on_title(int rx, int ry, int w)
{
    return ry >= 0 && ry < WINOPS_TITLE_H && rx >= WINOPS_TITLE_LEFT && rx < w - WINOPS_TITLE_RIGHT;
}

bool winops_double(unsigned t1, int x1, int y1, unsigned t2, int x2, int y2)
{
    unsigned dt = t2 - t1;
    int dx = x2 - x1, dy = y2 - y1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dt <= WINOPS_DOUBLE_MS && dx <= WINOPS_DOUBLE_NEAR && dy <= WINOPS_DOUBLE_NEAR;
}
