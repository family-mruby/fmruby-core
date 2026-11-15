# FmrbGfx - drawing API for Family mruby Python applications.
#
# Transcribed from the Ruby version (picoruby-fmrb-app mrblib/fmrb-gfx.rb plus
# the primitives gfx.c defines directly on the class), so method names,
# argument order and colour values match. The methods on the app boundary are
# generated from sig/ into _FmrbGfxBoundary (fmrb_boundary_py.h, doc/
# app_boundary), which passes the object itself to _fmrb. The object is a
# drawer of the firmware (_drawer, its number): the canvas, the drawing origin
# and the area are kept there and the firmware adds the origin, so nothing
# here adds it. This class adds what the Ruby mrblib adds: the wrappers with
# default arguments over the *_raw methods, and the text metrics.
#
# Not provided (see doc/micropython/README.md): GfxBlock, video.

import _fmrb


class FmrbGfx(_FmrbGfxBoundary):
    # Font families for set_font. JA carries the Japanese glyphs; the default
    # one is ASCII only.
    FONT_DEFAULT = 0
    FONT_JA = 1
    FONT_JA_BOLD = 2
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
        # The firmware's drawer on that canvas. Its drawing origin and the
        # size of its area (doc/codebase_tidy T4) start at (0, 0) and the
        # canvas size; FmrbApp gives the app's gfx the area inside the frame
        # (_layout_user_area). canvas_id, origin_x, origin_y, area_width and
        # area_height read them back (properties over the boundary).
        self._drawer = _fmrb.gfx_drawer_open(canvas_id, width, height)
        self.canvas_width = width
        self.canvas_height = height
        # What set_font last selected, so a caller that has to draw in another
        # font (the window frame does) can put this one back.
        self.current_font = (FmrbGfx.FONT_DEFAULT, None)

    canvas_id = property(_FmrbGfxBoundary.canvas_id)
    origin_x = property(_FmrbGfxBoundary.origin_x)
    origin_y = property(_FmrbGfxBoundary.origin_y)
    area_width = property(_FmrbGfxBoundary.area_width)
    area_height = property(_FmrbGfxBoundary.area_height)

    def _set_origin(self, x, y, w, h):
        _fmrb.gfx_set_origin(self, x, y, w, h)
        return self

    # The wrappers: the same defaults as Ruby's.

    # present(x, y) puts the canvas at (x, y) on the screen; without them it
    # goes where the app's window is.
    def present(self, x=None, y=None):
        if x is None or y is None:
            return self.present_raw(0, 0, False)
        return self.present_raw(x, y, True)

    # mixed=True draws ASCII with the built-in 6x8 font and multi-byte runs
    # with the Japanese one, in a single line. Without it, a line is drawn
    # entirely in whatever set_font selected.
    def draw_text(self, x, y, text, color, bg_color=None, mixed=False):
        return self.draw_text_raw(x, y, str(text), color, -1 if bg_color is None else bg_color, mixed)

    # family is FONT_DEFAULT, FONT_JA or FONT_JA_BOLD (or its name, "ja");
    # size is the pixel height: 8, 12 or 16 for FONT_JA (8 when left out),
    # 12 for FONT_JA_BOLD.
    def set_font(self, family, size=None):
        self.current_font = (family, size)
        if size is None:
            size = 12 if family in (FmrbGfx.FONT_DEFAULT, FmrbGfx.FONT_JA_BOLD, "default", "ja_bold") else 8
        return self.set_font_raw(family, size)

    # Scale on top of the font, 1 to 4.
    def set_text_size(self, size):
        return self.set_text_size_raw(size)

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
        return self.sync_file_raw(path, dest if dest else path)

    def transfer_file(self, path, dest=None):
        return self.transfer_file_raw(path, dest if dest else path)

    # create_image(path) -> {"id":, "width":, "height":} or None (generated).
    # The path is a graphics-side one, so sync_file it there first.

    def draw_image(self, image_id, x=0, y=0, scale_x=1.0, scale_y=0.0):
        return self.draw_image_raw(image_id, x, y, scale_x, scale_y)

    # Stamp a rectangle of a sprite image straight onto the canvas. No instance
    # is allocated, so this is the cheap way to draw a fixed background out of
    # a tile sheet.
    def draw_tile(self, image_id, src_x, src_y, w, h, dst_x=0, dst_y=0):
        return self.draw_tile_raw(image_id, src_x, src_y, w, h, dst_x, dst_y)

    def draw_image_masked(self, image_id, mask_id, x=0, y=0):
        return self.draw_image_masked_raw(image_id, mask_id, x, y)

    # regions: dicts with src_x, src_y, dst_x, dst_y, w, h, transparent; what
    # is left out is filled in as Ruby's set_composite_regions does.
    def set_composite_regions(self, regions):
        out = []
        for r in regions or []:
            dx = r.get("dst_x", 0)
            dy = r.get("dst_y", 0)
            out.append({"src_x": r.get("src_x", dx), "src_y": r.get("src_y", dy),
                        "dst_x": dx, "dst_y": dy, "w": r.get("w", 0), "h": r.get("h", 0),
                        "transparent": bool(r.get("transparent", False))})
        return self.set_composite_regions_raw(out)

    def delete_all_sprites(self):
        _fmrb.gfx_delete_all_sprites(self)
        return self


# A block of pixels held on the graphics side. Either drawn into (with the
# canvas drawing methods, while it is the target) or loaded from a BMP.
class SpriteImage:
    def __init__(self, gfx, width, height, transparent_color=0, use_transparent=False):
        self.gfx = gfx
        self.width = width
        self.height = height
        self.id = _fmrb.gfx_create_sprite_image(gfx, width, height,
                                                transparent_color, use_transparent)

    # Between set_target and reset_target, the parent FmrbGfx draws into this
    # image instead of onto the canvas (in the image's own coordinates: the
    # firmware adds no origin meanwhile).
    def set_target(self):
        _fmrb.gfx_set_sprite_target(self.gfx, self.id)
        return self

    def reset_target(self):
        _fmrb.gfx_set_sprite_target(self.gfx, 0)
        return self

    def load_bmp(self, path):
        _fmrb.gfx_load_sprite_image_bmp(self.gfx, self.id, path)
        return self

    def destroy(self):
        if self.id:
            _fmrb.gfx_delete_sprite_image(self.gfx, self.id)
            self.id = None


# A placement of one or more SpriteImages on the canvas. Moving one costs a
# single command, and the compositing happens on the graphics side, which is
# why anything that moves every frame should be a sprite. Its coordinates are
# from the gfx's origin (the firmware adds it).
class SpriteInstance:
    def __init__(self, gfx, images, x, y, z=0):
        self.gfx = gfx
        frames = images if isinstance(images, list) else [images]
        self.id = _fmrb.gfx_create_sprite_instance(gfx, [f.id for f in frames], x, y, z)

    def move(self, x, y):
        _fmrb.gfx_sprite_move(self.gfx, self.id, x, y)
        return self

    def set_visible(self, visible):
        _fmrb.gfx_sprite_visible(self.gfx, self.id, visible)
        return self

    def set_frame(self, index):
        _fmrb.gfx_sprite_frame(self.gfx, self.id, index)
        return self

    def destroy(self):
        if self.id:
            _fmrb.gfx_delete_sprite_instance(self.gfx, self.id)
            self.id = None
