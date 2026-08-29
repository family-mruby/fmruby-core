#!/usr/bin/env python3
# Generate the web build's wallpapers (doc/wasm/).
#
# Two families, one file per resolution the page offers:
#   bg_cyber_WxH.png  the neon skyline, DRAWN NATIVELY at each size -- a
#                     bigger framebuffer gets more skyline, not bigger pixels.
#   bg_WxH.png        the western scene, scaled from the hand-drawn 426x240
#                     original with nearest-neighbour (852x480 is an exact 2x;
#                     640x360 is 1.5x and slightly uneven, accepted).
#
# Python/Pillow by exception to the Ruby-tools rule: image GENERATION is the
# documented Pillow carve-out. Deterministic (fixed seeds), so re-running
# produces identical files.
#
#   python3 tool/web/gen_backgrounds.py   # writes into flash/usr/share/backgrounds/

import os
import random
import sys

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "..",
                   "flash", "usr", "share", "backgrounds")
SIZES = [(426, 240), (640, 360), (852, 480)]

NEON = [(34, 211, 238), (236, 72, 153), (250, 204, 21), (52, 211, 153)]


def draw_cyber(w, h):
    s = w / 426.0                       # proportions follow the base design
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)

    def put(x, y, c):
        if 0 <= x < w and 0 <= y < h:
            img.putpixel((x, y), c)

    horizon = int(h * 150 / 240)

    for y in range(h):
        t = min(y / horizon, 1.0)
        d.line([(0, y), (w, y)],
               fill=(int(8 + 34 * t), int(4 + 8 * t), int(28 + 52 * t)))

    random.seed(1120)
    for _ in range(int(90 * s * s)):
        put(random.randrange(w), random.randrange(0, horizon - int(30 * s)),
            random.choice([(120, 140, 180), (180, 200, 230), (90, 110, 150)]))

    mx, my, mr = int(340 * s), int(44 * s), int(26 * s)
    for y in range(-mr, mr + 1):
        for x in range(-mr, mr + 1):
            r2 = x * x + y * y
            if r2 <= mr * mr:
                edge = r2 > (mr - max(3, int(3 * s))) ** 2
                put(mx + x, my + y, (236, 72, 153) if edge else (190, 44, 120))
    for cx, cy, cr in [(332, 38, 5), (350, 52, 4), (342, 58, 3)]:
        cx, cy, cr = int(cx * s), int(cy * s), max(2, int(cr * s))
        for y in range(-cr, cr + 1):
            for x in range(-cr, cr + 1):
                if x * x + y * y <= cr * cr:
                    put(cx + x, cy + y, (160, 32, 100))

    def building(x, bw, top, body, lit):
        d.rectangle([max(0, x), top, min(w - 1, x + bw - 1), horizon], fill=body)
        for wy in range(top + 3, horizon - 2, 4):
            for wx in range(x + 2, x + bw - 3, 4):
                if 0 <= wx < w - 1 and random.random() < lit:
                    c = random.choice(NEON) if random.random() < 0.25 \
                        else (240, 220, 140)
                    d.rectangle([wx, wy, wx + 1, wy + 1], fill=c)

    random.seed(7)
    x = 0
    while x < w:
        bw = random.randrange(14, 30)
        building(x, bw, random.randrange(int(70 * s), int(110 * s)),
                 (24, 16, 48), 0.10)
        x += bw
    x = -6
    while x < w:
        bw = random.randrange(20, 44)
        top = random.randrange(int(95 * s), int(135 * s))
        building(x, max(6, bw), top, (14, 10, 32), 0.30)
        if random.random() < 0.4:
            ax = x + bw // 2
            if 0 <= ax < w:
                d.line([(ax, top), (ax, max(0, top - 8))], fill=(30, 26, 60))
                put(ax, top - 9, random.choice(NEON))
        x += bw - random.randrange(0, 8)

    d.line([(0, horizon), (w, horizon)], fill=(236, 72, 153))
    d.line([(0, horizon + 1), (w, horizon + 1)], fill=(120, 30, 90))

    gc, gcd = (34, 211, 238), (16, 90, 110)
    ys, yy, step = [], float(horizon + 2), 2.0
    while yy < h:
        ys.append(int(yy))
        yy += step
        step *= 1.35
    for i, y in enumerate(ys):
        d.line([(0, y), (w, y)], fill=gc if i % 2 == 0 else gcd)
    cx = w // 2
    for k in range(-12, 13):
        d.line([(cx + int(k * 18 * s), horizon + 2), (cx + int(k * 90 * s), h - 1)],
               fill=gcd)
    return img


def main():
    western = Image.open(os.path.join(OUT, "bg_426x240.png")).convert("RGB")
    for w, h in SIZES:
        cyber = draw_cyber(w, h)
        cyber.save(os.path.join(OUT, f"bg_cyber_{w}x{h}.png"), optimize=True)
        if (w, h) != (426, 240):
            western.resize((w, h), Image.NEAREST) \
                   .save(os.path.join(OUT, f"bg_{w}x{h}.png"), optimize=True)
        print(f"{w}x{h} done")


if __name__ == "__main__":
    sys.exit(main())
