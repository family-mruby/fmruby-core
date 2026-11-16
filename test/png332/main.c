// Host test of the sprite editor's PNG reader and writer
// (lib/gems/picoruby-fmrb-bmp332/ports/esp32/png332.c).
//
// usage: png332_test <out_dir> <png> <ref.raw> [<png> <ref.raw> ...]
//
// Each <png> is decoded and compared with <ref.raw> (w*h RGB332 bytes from
// tools/rgb332_png.rb, test/png332/run.rb writes them), then encoded again,
// written to <out_dir>/<n>.png for run.rb to read back with the Ruby reader,
// and decoded again to check the round trip. Synthetic cases cover the
// 8-bit depth, the stored-block fallback and the malformed inputs.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fmrb_png332.h"

static size_t g_live;   // allocations not yet freed

static void *t_alloc(void *ud, size_t n)
{
    (void)ud;
    g_live++;
    return malloc(n ? n : 1);
}

static void t_free(void *ud, void *p)
{
    (void)ud;
    if (p) g_live--;
    free(p);
}

static const fmrb_png332_allocator_t A = { t_alloc, t_free, NULL };
static int g_fail;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } } while (0)

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

// Encode px, decode the result, compare. Returns the PNG size (0 on failure).
static size_t round_trip(const char *name, const uint8_t *px, int w, int h, const char *save_as)
{
    uint8_t *png = NULL;
    size_t png_len = 0;
    int r = fmrb_png332_encode(px, (uint16_t)w, (uint16_t)h, &A, &png, &png_len);
    CHECK(r == FMRB_PNG332_OK, "%s: encode %s", name, fmrb_png332_error(r));
    if (r != FMRB_PNG332_OK) return 0;
    uint16_t w2 = 0, h2 = 0;
    uint8_t *px2 = NULL;
    r = fmrb_png332_decode(png, png_len, &A, &w2, &h2, &px2);
    CHECK(r == FMRB_PNG332_OK, "%s: decode of own PNG %s", name, fmrb_png332_error(r));
    if (r == FMRB_PNG332_OK) {
        CHECK(w2 == w && h2 == h, "%s: size %dx%d after round trip", name, w2, h2);
        CHECK(memcmp(px, px2, (size_t)w * h) == 0, "%s: pixels differ after round trip", name);
        t_free(NULL, px2);
    }
    if (save_as) {
        FILE *f = fopen(save_as, "wb");
        fwrite(png, 1, png_len, f);
        fclose(f);
    }
    t_free(NULL, png);
    return png_len;
}

static void synthetic(const char *out_dir)
{
    char path[512];
    uint32_t seed = 12345;

    // 256 colours, 160x120 (>= 16 K pixels: 8 bits per pixel), noisy enough
    // that the fixed codes lose to stored blocks.
    int w = 160, h = 120;
    uint8_t *px = malloc((size_t)w * h);
    for (int i = 0; i < w * h; i++) {
        seed = seed * 1103515245u + 12345u;
        px[i] = (uint8_t)(seed >> 16);
    }
    snprintf(path, sizeof(path), "%s/synth_noise.png", out_dir);
    size_t n = round_trip("noise", px, w, h, path);
    CHECK(n > 0 && n < (size_t)(w + 1) * h + 1200, "noise: %zu bytes (stored fallback expected)", n);

    // A large picture with few colours and long runs: compresses well.
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) px[y * w + x] = (uint8_t)(((x / 16) + (y / 16)) & 1 ? 0xE3 : 0x1C);
    snprintf(path, sizeof(path), "%s/synth_checker.png", out_dir);
    n = round_trip("checker", px, w, h, path);
    CHECK(n > 0 && n < 600, "checker: %zu bytes", n);

    // One pixel, one colour (1 bit).
    uint8_t one = 0x48;
    round_trip("1x1", &one, 1, 1, NULL);

    // A width that does not fill the last byte at 1/2/4 bits.
    int ws[3] = { 13, 7, 3 };
    int cols[3] = { 2, 4, 16 };
    for (int k = 0; k < 3; k++) {
        int ww = ws[k], hh = 5;
        for (int i = 0; i < ww * hh; i++) px[i] = (uint8_t)((i * 37) % cols[k] * 13);
        char name[32];
        snprintf(name, sizeof(name), "odd width %d", ww);
        round_trip(name, px, ww, hh, NULL);
    }
    free(px);

    // Malformed input never reads past the buffer or leaks.
    static const uint8_t junk[16] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    uint16_t w2, h2;
    uint8_t *p2 = NULL;
    CHECK(fmrb_png332_decode(junk, sizeof(junk), &A, &w2, &h2, &p2) != FMRB_PNG332_OK, "junk accepted");
    CHECK(fmrb_png332_decode((const uint8_t *)"BM......", 8, &A, &w2, &h2, &p2) == FMRB_PNG332_E_NOT_PNG, "BMP taken as PNG");
}

int main(int argc, char **argv)
{
    if (argc < 2 || (argc - 2) % 2 != 0) {
        fprintf(stderr, "usage: %s <out_dir> [<png> <ref.raw>]...\n", argv[0]);
        return 2;
    }
    const char *out_dir = argv[1];
    size_t in_total = 0, out_total = 0;
    int files = 0;
    for (int i = 2; i < argc; i += 2) {
        size_t len = 0, ref_len = 0;
        uint8_t *buf = read_file(argv[i], &len);
        uint8_t *ref = read_file(argv[i + 1], &ref_len);
        CHECK(buf && ref, "%s: cannot read", argv[i]);
        if (!buf || !ref) continue;
        uint16_t w = 0, h = 0;
        uint8_t *px = NULL;
        int r = fmrb_png332_decode(buf, len, &A, &w, &h, &px);
        CHECK(r == FMRB_PNG332_OK, "%s: %s", argv[i], fmrb_png332_error(r));
        if (r == FMRB_PNG332_OK) {
            CHECK((size_t)w * h == ref_len && memcmp(px, ref, ref_len) == 0,
                  "%s: pixels differ from tools/rgb332_png.rb", argv[i]);
            char path[512];
            snprintf(path, sizeof(path), "%s/%d.png", out_dir, files);
            size_t n = round_trip(argv[i], px, w, h, path);
            in_total += len;
            out_total += n;
            t_free(NULL, px);
        }
        files++;
        free(buf);
        free(ref);
    }
    synthetic(out_dir);
    CHECK(g_live == 0, "%zu allocations leaked", g_live);
    printf("png332: %d pictures read as tools/rgb332_png.rb reads them; written again %zu B (were %zu B)\n",
           files, out_total, in_total);
    if (g_fail) {
        printf("png332: %d failures\n", g_fail);
        return 1;
    }
    printf("png332: OK\n");
    return 0;
}
