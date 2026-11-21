// Host test of the display sides' PNG reader
// (lib/gems/picoruby-fmrb-bmp332/include/fmrb_png332_pngle.h: LovyanGFX's
// pngle with the colour rule of fmrb_png332.h), against the same references
// main.c checks the app side's reader with.
//
// usage: png332_pngle_test <png> <ref.raw> [<png> <ref.raw> ...]
//
// Each <png> is decoded and must give exactly <ref.raw> (w*h RGB332 bytes from
// tools/rgb332_png.rb, test/png332/run.rb writes them): the sprite a display
// side loads holds what BMP332.load reads, transparency included.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fmrb_png332_pngle.h"

typedef struct {
    uint8_t *px;
    uint32_t w, h;
    uint32_t puts;
} sink_t;

static void put(void *ud, uint32_t x, uint32_t y, uint8_t c)
{
    sink_t *s = (sink_t *)ud;
    s->px[(size_t)y * s->w + x] = c;
    s->puts++;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? (size_t)n : 1);
    *len = fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int main(int argc, char **argv)
{
    int fail = 0, n = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        size_t len = 0, ref_len = 0;
        uint8_t *png = read_file(argv[i], &len);
        uint8_t *ref = read_file(argv[i + 1], &ref_len);
        if (!png || !ref || len < 24) {
            printf("FAIL: %s: cannot read\n", argv[i]);
            fail++;
            free(png);
            free(ref);
            continue;
        }
        sink_t s = { NULL, be32(png + 16), be32(png + 20), 0 };
        s.px = calloc((size_t)s.w * s.h, 1);
        memset(s.px, 0xAA, (size_t)s.w * s.h);
        uint32_t w = 0, h = 0;
        int r = fmrb_png332_pngle_decode(png, len, s.w, s.h, put, &s, &w, &h);
        if (r != FMRB_PNG332_PNGLE_OK) {
            printf("FAIL: %s: decode %d\n", argv[i], r);
            fail++;
        } else if (w != s.w || h != s.h || ref_len != (size_t)w * h) {
            printf("FAIL: %s: %ux%u, reference %zu bytes\n", argv[i], w, h, ref_len);
            fail++;
        } else if (s.puts != w * h) {
            printf("FAIL: %s: %u pixels written of %u\n", argv[i], s.puts, w * h);
            fail++;
        } else if (memcmp(s.px, ref, ref_len) != 0) {
            size_t k = 0;
            while (s.px[k] == ref[k]) k++;
            printf("FAIL: %s: pixel %zu is 0x%02X, the app side reads 0x%02X\n", argv[i], k, s.px[k], ref[k]);
            fail++;
        }
        n++;
        free(s.px);
        free(png);
        free(ref);
    }
    if (fail) {
        printf("png332 (display side): %d of %d failed\n", fail, n);
        return 1;
    }
    printf("png332: %d pictures read the same on the display sides (pngle + fmrb_png332.h)\n", n);
    return 0;
}
