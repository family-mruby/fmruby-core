#!/usr/bin/env python3
"""Make a Motion-JPEG file to try the video player with.

The player takes a file of JPEG frames written back to back (the format
`ffmpeg -f mjpeg` produces), and it has to come from somewhere for a test that
does not need the network or a video to convert. This draws its own frames, so
the file is reproducible and every part of it is known: a moving bar, a frame
counter and a colour sweep, which between them show whether the picture is
being decoded, whether frames are being dropped, and whether the rows are in
the right order.

Pillow is one of the exceptions to writing tools in Ruby (CLAUDE.md: image
generation stays in Python).

    python3 tools/gen_test_mjpg.py out.mjpg [--size 320x176] [--frames 60]
"""

import argparse
import io
import sys

from PIL import Image, ImageDraw


def frame(i, n, w, h):
    img = Image.new("RGB", (w, h), (16, 16, 24))
    d = ImageDraw.Draw(img)

    # Colour sweep across the top: a solid band whose hue walks with the frame
    # number, so a stuck picture is obvious at a glance.
    for x in range(w):
        hue = (x * 255 // w + i * 4) % 255
        d.line([(x, 0), (x, h // 8)], fill=(hue, 255 - hue, (hue * 2) % 255))

    # A bar that crosses the picture and back. Its position is the frame
    # number, so dropped frames show as a jump.
    span = w - 40
    pos = i * 2 % (2 * span)
    if pos > span:
        pos = 2 * span - pos
    d.rectangle([pos, h // 4, pos + 40, h // 4 + h // 6], fill=(240, 200, 40))

    # Corner marks: if the rows or columns are out of order, these move.
    for (cx, cy) in ((4, 4), (w - 12, 4), (4, h - 12), (w - 12, h - 12)):
        d.rectangle([cx, cy, cx + 8, cy + 8], fill=(255, 0, 0))

    d.text((8, h - 28), f"frame {i + 1}/{n}", fill=(255, 255, 255))
    d.text((8, h - 16), f"{w}x{h}", fill=(160, 160, 160))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--size", default="320x176",
                    help="WxH (keep both a multiple of 16)")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--quality", type=int, default=80)
    a = ap.parse_args()

    w, h = (int(v) for v in a.size.lower().split("x"))
    if w % 16 or h % 16:
        print(f"warning: {w}x{h} is not a multiple of 16; the device decoder "
              f"works on that grid", file=sys.stderr)

    total = 0
    with open(a.out, "wb") as f:
        for i in range(a.frames):
            buf = io.BytesIO()
            frame(i, a.frames, w, h).save(buf, format="JPEG",
                                          quality=a.quality, subsampling=0)
            b = buf.getvalue()
            # Each frame must stand alone as SOI..EOI: that is what the
            # player's scanner looks for.
            assert b[:2] == b"\xff\xd8" and b[-2:] == b"\xff\xd9"
            f.write(b)
            total += len(b)

    print(f"{a.out}: {a.frames} frames, {w}x{h}, {total} bytes "
          f"({total // a.frames} bytes a frame)")


if __name__ == "__main__":
    main()
