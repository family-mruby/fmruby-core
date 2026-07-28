#!/usr/bin/env python3
"""Read and write the BASIC character sheets (128x128 indexed BMP).

The sheet holds 16 x 16 cells of 8x8 pixels; the character code is
row * 16 + column. This is the format the firmware loads at app start
(components/basic/assets/basic_assets.c), so the artwork can be edited in a
graphics editor. Palette index 0 is off (background / transparent), 1-3 are on;
the extra indices exist so four-colour artwork can be stored before the
renderer grows 2bpp colour attribute support.

No third party module is used here on purpose: the generators must run in a
bare checkout. tools/basic_sheet_convert.py does PNG <-> BMP with Pillow.
"""

CELL = 8
COLS = 16
DIM = CELL * COLS  # 128

# Index 0 is the background. 1 is the ink the renderer draws today; 2 and 3 are
# reserved for the colour attributes and are given visible colours so artwork
# using them is not invisible in an editor.
PALETTE = [
    (0x00, 0x00, 0x00),  # 0: off
    (0xFF, 0xFF, 0xFF),  # 1: on
    (0xE0, 0x40, 0x40),  # 2: reserved (colour attribute 2)
    (0x40, 0xC0, 0xE0),  # 3: reserved (colour attribute 3)
]


def _u16(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def _u32(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))


def glyphs_to_pixels(glyphs):
    """[256][8] row bitmaps -> [128][128] palette indices."""
    px = [[0] * DIM for _ in range(DIM)]
    for code in range(256):
        ox = (code % COLS) * CELL
        oy = (code // COLS) * CELL
        for row in range(CELL):
            bits = glyphs[code][row]
            for col in range(CELL):
                if bits & (0x80 >> col):
                    px[oy + row][ox + col] = 1
    return px


def pixels_to_glyphs(px):
    """[128][128] palette indices -> [256][8] row bitmaps (index != 0 = on)."""
    glyphs = [[0] * CELL for _ in range(256)]
    for y in range(DIM):
        for x in range(DIM):
            if px[y][x] == 0:
                continue
            code = (y // CELL) * COLS + (x // CELL)
            glyphs[code][y % CELL] |= 0x80 >> (x % CELL)
    return glyphs


def write_bmp(px, path):
    """Write [128][128] palette indices as an 8bpp bottom-up indexed BMP."""
    stride = DIM  # 128 is already a multiple of 4, so no row padding
    palette = b"".join(bytes((b, g, r, 0)) for (r, g, b) in PALETTE)
    palette += bytes(4 * (256 - len(PALETTE)))
    pixel_offset = 14 + 40 + len(palette)
    body = b"".join(bytes(px[y]) for y in range(DIM - 1, -1, -1))  # bottom-up

    header = b"BM" + _u32(pixel_offset + len(body)) + _u32(0) + _u32(pixel_offset)
    dib = (
        _u32(40) + _u32(DIM) + _u32(DIM) + _u16(1) + _u16(8) + _u32(0)
        + _u32(len(body)) + _u32(2835) + _u32(2835) + _u32(len(PALETTE) // 4) + _u32(0)
    )
    with open(path, "wb") as fp:
        fp.write(header + dib + palette + body)


def read_bmp(path):
    """Read an indexed BMP sheet back into [128][128] palette indices.

    Mirrors the firmware loader: 1, 4 and 8 bits per pixel, either row order.
    """
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise ValueError("%s: not a BMP" % path)
    rd32 = lambda o: int.from_bytes(data[o:o + 4], "little")  # noqa: E731
    bits_offset = rd32(10)
    dib_size = rd32(14)
    width = int.from_bytes(data[18:22], "little", signed=True)
    raw_height = int.from_bytes(data[22:26], "little", signed=True)
    bpp = int.from_bytes(data[28:30], "little")
    compression = rd32(30)
    top_down = raw_height < 0
    height = -raw_height if top_down else raw_height
    if dib_size < 40 or compression != 0:
        raise ValueError("%s: need an uncompressed BITMAPINFOHEADER BMP" % path)
    if (width, height) != (DIM, DIM):
        raise ValueError("%s: sheet must be %dx%d, got %dx%d" % (path, DIM, DIM, width, height))
    if bpp not in (1, 4, 8):
        raise ValueError("%s: need 1, 4 or 8 bits per pixel, got %d" % (path, bpp))

    stride = ((width * bpp + 31) // 32) * 4
    px = [[0] * DIM for _ in range(DIM)]
    for y in range(DIM):
        file_row = y if top_down else (DIM - 1 - y)
        row = data[bits_offset + file_row * stride:][:stride]
        for x in range(DIM):
            if bpp == 8:
                px[y][x] = row[x]
            elif bpp == 4:
                px[y][x] = (row[x >> 1] & 0x0F) if (x & 1) else (row[x >> 1] >> 4)
            else:
                px[y][x] = (row[x >> 3] >> (7 - (x & 7))) & 1
    return px
