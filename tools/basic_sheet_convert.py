#!/usr/bin/env python3
"""Convert a BASIC character sheet between PNG and BMP (host side, needs Pillow).

    tools/basic_sheet_convert.py sheet.png sheet.bmp   # PNG art -> what the firmware loads
    tools/basic_sheet_convert.py sheet.bmp sheet.png   # BMP -> PNG for editing

The firmware only reads the BMP form (tools/basic_sheet.py explains why), so
mastering the artwork in PNG means running this after every edit. Editing the
BMP directly skips the step -- every common editor writes indexed BMP.

Pixels map to palette indices: index 0 is off. A PNG that is already indexed
keeps its indices; an RGB/RGBA PNG maps fully transparent or black pixels to 0
and everything else to the nearest of the reserved ink colours.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import basic_sheet  # noqa: E402

from PIL import Image  # noqa: E402


def png_to_indices(path):
    im = Image.open(path)
    if im.size != (basic_sheet.DIM, basic_sheet.DIM):
        raise SystemExit("%s: sheet must be %dx%d, got %dx%d"
                         % (path, basic_sheet.DIM, basic_sheet.DIM, im.width, im.height))
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
            for idx, (pr, pg, pb) in enumerate(basic_sheet.PALETTE):
                if idx == 0:
                    continue
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if best_d is None or d < best_d:
                    best, best_d = idx, d
            out[y][x] = best
    return out


def indices_to_png(px, path):
    im = Image.new("P", (basic_sheet.DIM, basic_sheet.DIM))
    palette = []
    for r, g, b in basic_sheet.PALETTE:
        palette += [r, g, b]
    palette += [0] * (768 - len(palette))
    im.putpalette(palette)
    im.putdata([px[y][x] for y in range(basic_sheet.DIM) for x in range(basic_sheet.DIM)])
    im.save(path)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    src_ext, dst_ext = os.path.splitext(src)[1].lower(), os.path.splitext(dst)[1].lower()
    if src_ext == ".png" and dst_ext == ".bmp":
        basic_sheet.write_bmp(png_to_indices(src), dst)
    elif src_ext == ".bmp" and dst_ext == ".png":
        indices_to_png(basic_sheet.read_bmp(src), dst)
    else:
        raise SystemExit("convert .png <-> .bmp only (got %s -> %s)" % (src_ext, dst_ext))
    print("wrote %s" % dst)


main()
