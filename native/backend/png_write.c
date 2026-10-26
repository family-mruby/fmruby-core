/*
 * png_write.c - see png_write.h. Runs on the main thread (outside FreeRTOS),
 * hence libc malloc.
 */

#include "png_write.h"

#include <stdlib.h>
#include <string.h>

static uint32_t s_crc_table[256];
static int s_crc_ready = 0;

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        s_crc_table[n] = c;
    }
    s_crc_ready = 1;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!s_crc_ready) crc_init();
    for (size_t i = 0; i < n; i++) {
        crc = s_crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    }
    return crc;
}

static uint8_t *put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return p + 4;
}

/* Write one chunk: length, type, data, CRC over type + data. */
static uint8_t *put_chunk(uint8_t *p, const char type[4], const uint8_t *data, size_t len)
{
    p = put_be32(p, (uint32_t)len);
    uint8_t *crc_from = p;
    memcpy(p, type, 4);
    p += 4;
    if (len) {
        memmove(p, data, len);
        p += len;
    }
    uint32_t crc = crc_update(0xffffffffu, crc_from, len + 4) ^ 0xffffffffu;
    return put_be32(p, crc);
}

uint8_t *png_write_rgb565(const uint16_t *px, int w, int h, size_t *out_len)
{
    if (px == NULL || w <= 0 || h <= 0) return NULL;

    /* The filtered image: one filter byte (0, none) per row, then RGB. */
    const size_t row = 1 + (size_t)w * 3;
    const size_t raw_len = row * (size_t)h;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (raw == NULL) return NULL;
    for (int y = 0; y < h; y++) {
        uint8_t *d = raw + row * (size_t)y;
        *d++ = 0;
        const uint16_t *s = px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint16_t v = s[x];
            uint8_t r5 = (v >> 11) & 0x1f, g6 = (v >> 5) & 0x3f, b5 = v & 0x1f;
            *d++ = (uint8_t)((r5 << 3) | (r5 >> 2));
            *d++ = (uint8_t)((g6 << 2) | (g6 >> 4));
            *d++ = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }

    /* zlib stream of stored blocks: 2-byte header, blocks of at most 65535
     * bytes (5-byte header each), Adler-32. */
    const size_t nblocks = raw_len / 65535 + 1;
    const size_t z_len = 2 + raw_len + nblocks * 5 + 4;
    /* signature + IHDR(25) + IDAT(12 + z_len) + IEND(12) */
    const size_t total = 8 + 25 + 12 + z_len + 12;
    uint8_t *out = (uint8_t *)malloc(total + z_len);
    if (out == NULL) {
        free(raw);
        return NULL;
    }
    /* Build the zlib stream at the end of the buffer, then let put_chunk
     * move it into place. */
    uint8_t *z = out + total;
    uint8_t *zp = z;
    *zp++ = 0x78;
    *zp++ = 0x01;
    uint32_t a = 1, b = 0;
    size_t left = raw_len;
    const uint8_t *rp = raw;
    while (1) {
        size_t n = left > 65535 ? 65535 : left;
        *zp++ = (left == n) ? 1 : 0;   /* BFINAL, BTYPE=00 */
        *zp++ = (uint8_t)(n & 0xff);
        *zp++ = (uint8_t)(n >> 8);
        *zp++ = (uint8_t)(~n & 0xff);
        *zp++ = (uint8_t)((~n >> 8) & 0xff);
        memcpy(zp, rp, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + rp[i]) % 65521;
            b = (b + a) % 65521;
        }
        zp += n;
        rp += n;
        left -= n;
        if (left == 0) break;
    }
    zp = put_be32(zp, (b << 16) | a);
    const size_t z_used = (size_t)(zp - z);
    free(raw);

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    uint8_t *p = out;
    memcpy(p, sig, 8);
    p += 8;
    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 2;    /* truecolour */
    ihdr[10] = 0;   /* deflate */
    ihdr[11] = 0;   /* adaptive filtering */
    ihdr[12] = 0;   /* no interlace */
    p = put_chunk(p, "IHDR", ihdr, sizeof(ihdr));
    p = put_chunk(p, "IDAT", z, z_used);
    p = put_chunk(p, "IEND", NULL, 0);
    *out_len = (size_t)(p - out);
    return out;
}
