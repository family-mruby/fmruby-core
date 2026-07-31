# FmrbGfx - drawing API for Family mruby Python applications.
#
# Transcribed from the Ruby version (picoruby-fmrb-app mrblib/fmrb-gfx.rb plus
# the primitives gfx.c defines directly on the class), so method names,
# argument order and colour values match. The Ruby class keeps the low-level
# calls as methods on a C object; here they are flat functions in _fmrb taking
# the canvas id, and this class supplies the object shape.
#
# Not in this first stage (see doc/micropython/phase3.md): sprites, images,
# masks, tiles, arcs, blend_rect, get_pixel, font selection, text size,
# composite regions and viewports.

import _fmrb


class FmrbGfx:
    # RGB332: 3-bit red, 3-bit green, 2-bit blue.
    BLACK = 0x00
    WHITE = 0xFF
    RED = 0xE0
    GREEN = 0x1C
    BLUE = 0x03
    YELLOW = 0xFC
    CYAN = 0x1F
    MAGENTA = 0xE3
    GRAY = 0x6D

    # The default font is fixed-pitch 6x8, which is all this stage renders.
    CHAR_W = 6
    LINE_H = 8

    def __init__(self, canvas_id, width=0, height=0):
        self.canvas_id = canvas_id
        self.canvas_width = width
        self.canvas_height = height

    def clear(self, color=BLACK):
        _fmrb.gfx_clear(self.canvas_id, color)
        return self

    def present(self, x=None, y=None):
        _fmrb.gfx_present(self.canvas_id, x, y)
        return self

    def set_pixel(self, x, y, color):
        _fmrb.gfx_set_pixel(self.canvas_id, x, y, color)
        return self

    def draw_line(self, x0, y0, x1, y1, color):
        _fmrb.gfx_draw_line(self.canvas_id, x0, y0, x1, y1, color)
        return self

    def draw_rect(self, x, y, w, h, color):
        _fmrb.gfx_draw_rect(self.canvas_id, x, y, w, h, color)
        return self

    def fill_rect(self, x, y, w, h, color):
        _fmrb.gfx_fill_rect(self.canvas_id, x, y, w, h, color)
        return self

    def draw_circle(self, x, y, r, color):
        _fmrb.gfx_draw_circle(self.canvas_id, x, y, r, color)
        return self

    def fill_circle(self, x, y, r, color):
        _fmrb.gfx_fill_circle(self.canvas_id, x, y, r, color)
        return self

    def draw_round_rect(self, x, y, w, h, r, color):
        _fmrb.gfx_draw_round_rect(self.canvas_id, x, y, w, h, r, color)
        return self

    def fill_round_rect(self, x, y, w, h, r, color):
        _fmrb.gfx_fill_round_rect(self.canvas_id, x, y, w, h, r, color)
        return self

    def draw_ellipse(self, x, y, rx, ry, color):
        _fmrb.gfx_draw_ellipse(self.canvas_id, x, y, rx, ry, color)
        return self

    def fill_ellipse(self, x, y, rx, ry, color):
        _fmrb.gfx_fill_ellipse(self.canvas_id, x, y, rx, ry, color)
        return self

    def draw_triangle(self, x0, y0, x1, y1, x2, y2, color):
        _fmrb.gfx_draw_triangle(self.canvas_id, x0, y0, x1, y1, x2, y2, color)
        return self

    def fill_triangle(self, x0, y0, x1, y1, x2, y2, color):
        _fmrb.gfx_fill_triangle(self.canvas_id, x0, y0, x1, y1, x2, y2, color)
        return self

    def draw_text(self, x, y, text, color, bg_color=None):
        _fmrb.gfx_draw_text(self.canvas_id, x, y, str(text), color, bg_color)
        return self

    # Rendered width of a string with the default font. ASCII only at this
    # stage; a multi-byte run would need the hybrid renderer.
    def text_width(self, text):
        return len(text) * self.CHAR_W

    def font_height(self):
        return self.LINE_H
