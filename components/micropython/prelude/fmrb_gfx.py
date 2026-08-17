# FmrbGfx - drawing API for Family mruby Python applications.
#
# Transcribed from the Ruby version (picoruby-fmrb-app mrblib/fmrb-gfx.rb plus
# the primitives gfx.c defines directly on the class), so method names,
# argument order and colour values match. The Ruby class keeps the low-level
# calls as methods on a C object; here they are flat functions in _fmrb taking
# the canvas id, and this class supplies the object shape.
#
# Not provided (see doc/micropython/README.md): masks, arcs, blend_rect,
# get_pixel, GfxBlock, composite regions and viewports.

import _fmrb


class FmrbGfx:
    # Font families for set_font. JA carries the Japanese glyphs; the default
    # one is ASCII only.
    FONT_DEFAULT = 0
    FONT_JA = 1
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
        # What set_font last selected, so a caller that has to draw in another
        # font (the window frame does) can put this one back.
        self.current_font = (FmrbGfx.FONT_DEFAULT, None)

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

    # mixed=True draws ASCII with the built-in 6x8 font and multi-byte runs
    # with the Japanese one, in a single line. Without it, a line is drawn
    # entirely in whatever set_font selected.
    def draw_text(self, x, y, text, color, bg_color=None, mixed=False):
        _fmrb.gfx_draw_text(self.canvas_id, x, y, str(text), color, bg_color, mixed)
        return self

    # family is FONT_DEFAULT or FONT_JA; size is the pixel height (the JA font
    # has one size, so it can be left out).
    def set_font(self, family, size=None):
        self.current_font = (family, size)
        _fmrb.gfx_set_font(self.canvas_id, family, size)
        return self

    # Scale on top of the font, 1 to 4.
    def set_text_size(self, size):
        _fmrb.gfx_set_text_size(self.canvas_id, size)
        return self

    # Rendered width of a string drawn with mixed=True, in pixels.
    #
    # Walks UTF-8 by hand: an ASCII byte is 6 px, a three-byte sequence is a
    # full-width glyph at 8 px, and the two-byte range holds half-width
    # katakana at 4 px. Strings are byte strings here (the build does not
    # enable the unicode string type), so indexing gives bytes either way --
    # encode() keeps that true if the setting ever changes.
    def text_width(self, text):
        if not text:
            return 0
        data = str(text).encode()
        w = 0
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b < 0x80:
                w += self.CHAR_W
                i += 1
            elif b < 0xC0:      # continuation byte, already counted
                i += 1
            elif b < 0xE0:
                w += 4
                i += 2
            else:
                w += 8
                i += 3
        return w

    def font_height(self):
        return self.LINE_H

    # ---- images (decoded on the graphics side; the pixels never reach here) ----

    # Copy a file to the graphics side, unless the copy there already matches.
    def sync_file(self, path, dest=None):
        return _fmrb.gfx_sync_file(path, dest if dest else path)

    # -> {"id":, "width":, "height":} or None. The path is a graphics-side one,
    # so sync_file it there first.
    def create_image(self, path):
        return _fmrb.gfx_create_image(self.canvas_id, path)

    def draw_image(self, image_id, x=0, y=0, scale_x=1.0, scale_y=0.0):
        # Scale is fixed point on the wire: 256 = 1.0, and 0 for y means
        # "same as x".
        _fmrb.gfx_draw_image(self.canvas_id, image_id, x, y,
                             int(scale_x * 256), int(scale_y * 256))
        return self

    def delete_image(self, image_id):
        _fmrb.gfx_delete_image(self.canvas_id, image_id)
        return self

    # Stamp a rectangle of a sprite image straight onto the canvas. No instance
    # is allocated, so this is the cheap way to draw a fixed background out of
    # a tile sheet.
    def draw_tile(self, image_id, src_x, src_y, w, h, dst_x=0, dst_y=0):
        _fmrb.gfx_draw_tile(self.canvas_id, image_id, src_x, src_y, w, h, dst_x, dst_y)
        return self

    def delete_all_sprites(self):
        _fmrb.gfx_delete_all_sprites(self.canvas_id)
        return self


# A block of pixels held on the graphics side. Either drawn into (with the
# canvas drawing methods, while it is the target) or loaded from a BMP.
class SpriteImage:
    def __init__(self, gfx, width, height, transparent_color=0, use_transparent=False):
        self.gfx = gfx
        self.width = width
        self.height = height
        self.id = _fmrb.gfx_create_sprite_image(gfx.canvas_id, width, height,
                                                transparent_color, use_transparent)

    # Between set_target and reset_target, the parent FmrbGfx draws into this
    # image instead of onto the canvas.
    def set_target(self):
        _fmrb.gfx_set_sprite_target(self.gfx.canvas_id, self.id)
        return self

    def reset_target(self):
        _fmrb.gfx_set_sprite_target(self.gfx.canvas_id, 0)
        return self

    def load_bmp(self, path):
        _fmrb.gfx_load_sprite_image_bmp(self.gfx.canvas_id, self.id, path)
        return self

    def destroy(self):
        if self.id:
            _fmrb.gfx_delete_sprite_image(self.gfx.canvas_id, self.id)
            self.id = None


# A placement of one or more SpriteImages on the canvas. Moving one costs a
# single command, and the compositing happens on the graphics side, which is
# why anything that moves every frame should be a sprite.
class SpriteInstance:
    def __init__(self, gfx, images, x, y, z=0):
        self.gfx = gfx
        frames = images if isinstance(images, list) else [images]
        self.id = _fmrb.gfx_create_sprite_instance(gfx.canvas_id,
                                                   [f.id for f in frames], x, y, z)

    def move(self, x, y):
        _fmrb.gfx_sprite_move(self.gfx.canvas_id, self.id, x, y)
        return self

    def set_visible(self, visible):
        _fmrb.gfx_sprite_visible(self.gfx.canvas_id, self.id, visible)
        return self

    def set_frame(self, index):
        _fmrb.gfx_sprite_frame(self.gfx.canvas_id, self.id, index)
        return self

    def destroy(self):
        if self.id:
            _fmrb.gfx_delete_sprite_instance(self.gfx.canvas_id, self.id)
            self.id = None
