#!/usr/bin/env python3
# Generate the web build's wallpapers (doc/wasm/).
#
# Two families, one file per resolution the page offers:
#   bg_cyber_WxH.png  a night street, composed from assets/cyberpunk-street.png
#                     (Luis Zuno / Ansimuz, CC0 -- see ASSETS.md).
#   bg_WxH.png        the western scene, scaled from the hand-drawn 426x240
#                     original with nearest-neighbour (852x480 is an exact 2x;
#                     640x360 is 1.5x and 1280x720 just over 3x, both slightly
#                     uneven, accepted).
#
# Both families are built once at 426x240 and scaled from there, so every size
# is the same picture rather than a different amount of it.
#
# Each file is a palette PNG of exactly the colours it uses (4 and 28): the
# same pixels as an RGB PNG, about half the bytes, and 3-5x quicker for the
# display side to decode (Retro decodes the wallpaper on every redraw).
#
# Python/Pillow by exception to the Ruby-tools rule: image GENERATION is the
# documented Pillow carve-out. Deterministic, so re-running produces
# identical files.
#
#   python3 tools/web/gen_backgrounds.py   # writes into flash/usr/share/backgrounds/

import os
import sys

from PIL import Image

OUT = os.path.join(os.path.dirname(__file__), "..", "..",
                   "flash", "usr", "share", "backgrounds")
SIZES = [(426, 240), (640, 360), (852, 480), (1280, 720)]

ART = os.path.join(os.path.dirname(__file__), "assets", "cyberpunk-street.png")

# The artwork is 608x192: wider than the screen and the same shape all the way
# across, since it is drawn to loop. So the screen's shape is taken out of it
# by cutting the right-hand side -- never by adding sky above, which would put
# an invented band over someone else's picture -- and what is left is scaled
# to the screen. Full height, so the road still sits on the bottom edge.
#
# The scale is not a whole number (1.25x, 1.875x, 2.5x, 3.75x) and NEAREST is used
# anyway, as it is for the western one: some pixels come out one wide and some
# two, which pixel art carries better than a blur does.
def draw_cyber(w, h):
    art = Image.open(ART).convert("RGB")
    aw, ah = art.size
    keep = min(aw, int(round(ah * w / float(h))))
    return art.crop((0, 0, keep, ah)).resize((w, h), Image.NEAREST)


# An RGB picture as a palette PNG of its own colours, in a fixed order (the
# colours sorted), so the pixels are exactly the same. More than 256 colours
# stays RGB.
def save_png(img, path):
    img = img.convert("RGB")
    colours = img.getcolors(256)
    if colours is None:
        img.save(path, optimize=True)
        return
    order = sorted(c for _n, c in colours)
    index = {c: i for i, c in enumerate(order)}
    pal = Image.new("P", img.size)
    pal.putpalette([v for c in order for v in c])
    pal.putdata([index[c] for c in img.get_flattened_data()])
    pal.save(path, optimize=True)


def main():
    western = Image.open(os.path.join(OUT, "bg_426x240.png")).convert("RGB")
    for w, h in SIZES:
        cyber = draw_cyber(w, h)
        save_png(cyber, os.path.join(OUT, f"bg_cyber_{w}x{h}.png"))
        if (w, h) == (426, 240):
            # The hand-drawn original, written again as a palette PNG.
            save_png(western, os.path.join(OUT, "bg_426x240.png"))
        else:
            save_png(western.resize((w, h), Image.NEAREST),
                     os.path.join(OUT, f"bg_{w}x{h}.png"))
        print(f"{w}x{h} done")


if __name__ == "__main__":
    sys.exit(main())
