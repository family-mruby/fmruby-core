#!/usr/bin/env python3
"""Convert a BASIC character sheet between PNG and BMP (host side, needs Pillow).

    tool/basic/basic_sheet_convert.py sheet.png sheet.bmp  # PNG art -> what the firmware loads
    tool/basic/basic_sheet_convert.py sheet.bmp sheet.png  # BMP -> PNG for editing

The firmware only reads the BMP form (tool/basic/basic_sheet.rb explains why), so
mastering the artwork in PNG means running this after every edit. Editing the
BMP directly skips the step -- every common editor writes indexed BMP.

This is the one tool here that stays Python: reading whatever PNG a graphics
editor produced (colour type, bit depth, interlacing) is what Pillow is for, and
re-implementing that in Ruby would be the opposite of a small tool. Pillow reads
and writes the BMP side as well, so nothing is shared with the Ruby generators
beyond the palette below.

Pixels map to palette indices: index 0 is off. A PNG that is already indexed
keeps its indices; an RGB/RGBA PNG maps fully transparent or black pixels to 0
and everything else to the nearest of the reserved ink colours.
"""

import os
import sys

from PIL import Image

# Must match BasicSheet::PALETTE in basic_sheet.rb (index 0 off, 1 ink, 2-3
# reserved for the colour attributes).
PALETTE = [
    (0x00, 0x00, 0x00),
    (0xFF, 0xFF, 0xFF),
    (0xE0, 0x40, 0x40),
    (0x40, 0xC0, 0xE0),
]
DIM = 128


def load_indices(path):
    """Any supported sheet file -> [128][128] palette indices."""
    im = Image.open(path)
    if im.size != (DIM, DIM):
        raise SystemExit("%s: sheet must be %dx%d, got %dx%d"
                         % (path, DIM, DIM, im.width, im.height))
    if im.mode == "P":
        px = im.load()
        return [[px[x, y] for x in range(im.width)] for y in range(im.height)]

    im = im.convert("RGBA")
    px = im.load()
    out = [[0] * im.width for _ in range(im.height)]
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a < 128 or (r, g, b) == (0, 0, 0):
                continue
            best, best_d = 1, None
            for idx, (pr, pg, pb) in enumerate(PALETTE):
                if idx == 0:
                    continue
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if best_d is None or d < best_d:
                    best, best_d = idx, d
            out[y][x] = best
    return out


def save_indices(px, path):
    """[128][128] palette indices -> an indexed PNG or BMP, by extension."""
    im = Image.new("P", (DIM, DIM))
    palette = []
    for r, g, b in PALETTE:
        palette += [r, g, b]
    palette += [0] * (768 - len(palette))
    im.putpalette(palette)
    im.putdata([px[y][x] for y in range(DIM) for x in range(DIM)])
    im.save(path)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    src_ext, dst_ext = os.path.splitext(src)[1].lower(), os.path.splitext(dst)[1].lower()
    if {src_ext, dst_ext} != {".png", ".bmp"}:
        raise SystemExit("convert .png <-> .bmp only (got %s -> %s)" % (src_ext, dst_ext))
    save_indices(load_indices(src), dst)
    print("wrote %s" % dst)


main()
