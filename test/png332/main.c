// Host test of the sprite editor's PNG reader and writer
// (lib/gems/picoruby-fmrb-bmp332/ports/esp32/png332.c) and of BMP332.info /
// BMP332.to_png's C (picture332.c).
//
// usage: png332_test <out_dir> <png> <ref.raw> [<png> <ref.raw> ...]
//
// Each <png> is decoded and compared with <ref.raw> (w*h RGB332 bytes from
// tools/rgb332_png.rb, test/png332/run.rb writes them), then encoded again,
// written to <out_dir>/<n>.png for run.rb to read back with the Ruby reader,
// and decoded again to check the round trip. A <ref.raw> of "-" is a BMP
// with no reference: only the checks of picture332.c run on it. Synthetic
// cases cover the 8-bit depth, the stored-block fallback and the malformed
// inputs.
//
// picture332.c on every picture: the head gives the decoder's size and kind,
// and to_png gives exactly the bytes of encode over the decoder's pixels
// (BMP332.to_png must equal BMP332.encode_png(BMP332.parse(...)), so a
// chat stamp keeps its sha256); its refusals come with the sizes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fmrb_png332.h"
#include "fmrb_picture332.h"
#include "fmrb_bmp332.h"

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


// picture332.c over one picture file's bytes (a PNG or an 8-bit BMP).
static int g_pictures;

static void check_picture(const char *name, const uint8_t *buf, size_t len)
{
    // What the decoders give (BMP332.parse).
    uint16_t w = 0, h = 0;
    uint8_t *px = NULL, *copy = NULL;
    const uint8_t *pixels = NULL;
    int is_png = fmrb_png332_is_png(buf, len);
    if (is_png) {
        if (fmrb_png332_decode(buf, len, &A, &w, &h, &px) != FMRB_PNG332_OK) return;
        pixels = px;
    } else {
        copy = t_alloc(NULL, len);
        memcpy(copy, buf, len);
        fmrb_bmp332_t bmp;
        if (fmrb_bmp332_parse(copy, len, &bmp) != 0) {
            // A BMP that BMP332 does not read (1 or 4 bits): neither do these.
            fmrb_picture332_head_t hd;
            uint8_t *o = NULL;
            size_t ol = 0;
            uint16_t ww, hh;
            CHECK(fmrb_picture332_head(buf, len, &hd) != 0, "%s: head of an unreadable BMP", name);
            CHECK(fmrb_picture332_to_png(buf, len, 2048, 2048, 0, &A, &o, &ol, &ww, &hh) ==
                      FMRB_PICTURE332_E_UNREADABLE, "%s: an unreadable BMP rewritten", name);
            t_free(NULL, copy);
            g_pictures++;
            return;
        }
        w = bmp.width;
        h = bmp.height;
        pixels = bmp.pixels;
    }
    uint8_t *want = NULL;
    size_t want_len = 0;
    int r = fmrb_png332_encode(pixels, w, h, &A, &want, &want_len);
    CHECK(r == FMRB_PNG332_OK, "%s: encode", name);
    if (px) t_free(NULL, px);
    if (copy) t_free(NULL, copy);
    if (r != FMRB_PNG332_OK) return;

    fmrb_picture332_head_t head;
    CHECK(fmrb_picture332_head(buf, len, &head) == 0, "%s: head not read", name);
    CHECK(head.width == w && head.height == h, "%s: head says %dx%d, decoded %dx%d", name,
          head.width, head.height, w, h);
    CHECK(head.format == (is_png ? FMRB_PICTURE332_PNG : FMRB_PICTURE332_BMP), "%s: head format %d", name,
          head.format);

    // to_png: the same bytes as encode over the decoded pixels.
    uint8_t *png = NULL;
    size_t png_len = 0;
    uint16_t w2 = 0, h2 = 0;
    r = fmrb_picture332_to_png(buf, len, 2048, 2048, 0, &A, &png, &png_len, &w2, &h2);
    CHECK(r == FMRB_PICTURE332_OK, "%s: to_png %d", name, r);
    if (r == FMRB_PICTURE332_OK) {
        CHECK(w2 == w && h2 == h, "%s: to_png size %dx%d", name, w2, h2);
        CHECK(png_len == want_len && memcmp(png, want, want_len) == 0,
              "%s: to_png differs from encode_png(parse) (%zu B against %zu B)", name, png_len, want_len);
        t_free(NULL, png);
    }
    // Exactly the limits pass; one less is refused, with the sizes.
    r = fmrb_picture332_to_png(buf, len, w, h, want_len, &A, &png, &png_len, &w2, &h2);
    CHECK(r == FMRB_PICTURE332_OK, "%s: to_png at its own limits %d", name, r);
    if (r == FMRB_PICTURE332_OK) t_free(NULL, png);
    r = fmrb_picture332_to_png(buf, len, w, (uint16_t)(h - 1), 0, &A, &png, &png_len, &w2, &h2);
    CHECK(r == FMRB_PICTURE332_E_TOO_BIG && png == NULL && w2 == w && h2 == h, "%s: too_big %d %dx%d", name,
          r, w2, h2);
    r = fmrb_picture332_to_png(buf, len, 2048, 2048, want_len - 1, &A, &png, &png_len, &w2, &h2);
    CHECK(r == FMRB_PICTURE332_E_TOO_HEAVY && png == NULL && png_len == want_len, "%s: too_heavy %d %zu", name,
          r, png_len);
    t_free(NULL, want);
    g_pictures++;
}

// An 8-bit BMP of w x h (bottom-up, or top-down with a negative height).
static uint8_t *make_bmp(int w, int h, int top_down, const uint8_t *px, size_t *len)
{
    int row = (w + 3) & ~3;
    size_t off = 14 + 40 + 1024, n = off + (size_t)row * h;
    uint8_t *b = calloc(1, n);
    b[0] = 'B';
    b[1] = 'M';
    b[2] = (uint8_t)n;
    b[3] = (uint8_t)(n >> 8);
    b[10] = (uint8_t)off;
    b[11] = (uint8_t)(off >> 8);
    b[14] = 40;
    b[18] = (uint8_t)w;
    int32_t hh = top_down ? -h : h;
    memcpy(b + 22, &hh, 4);
    b[26] = 1;
    b[28] = 8;
    for (int y = 0; y < h; y++) {
        int src = top_down ? y : h - 1 - y;
        memcpy(b + off + (size_t)y * row, px + (size_t)src * w, (size_t)w);
    }
    *len = n;
    return b;
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

    // BMPs: an odd width bottom-up and top-down, and a head that is not one.
    uint8_t bpx[5 * 3];
    for (int i = 0; i < 15; i++) bpx[i] = (uint8_t)(i * 17);
    for (int td = 0; td < 2; td++) {
        size_t blen;
        uint8_t *b = make_bmp(5, 3, td, bpx, &blen);
        check_picture(td ? "bmp top-down" : "bmp bottom-up", b, blen);
        fmrb_picture332_head_t hd;
        b[28] = 24;
        CHECK(fmrb_picture332_head(b, blen, &hd) != 0, "24-bit BMP head accepted");
        free(b);
    }
    {
        fmrb_picture332_head_t hd;
        uint8_t *o = NULL;
        size_t ol = 0;
        uint16_t ww, hh;
        CHECK(fmrb_picture332_head((const uint8_t *)"GIF89a", 6, &hd) != 0, "GIF head accepted");
        CHECK(fmrb_picture332_to_png((const uint8_t *)"GIF89a", 6, 2048, 2048, 0, &A, &o, &ol, &ww, &hh) ==
                  FMRB_PICTURE332_E_UNREADABLE, "GIF rewritten");
    }

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
        if (strcmp(argv[i + 1], "-") == 0) {
            CHECK(buf != NULL, "%s: cannot read", argv[i]);
            if (buf) check_picture(argv[i], buf, len);
            free(buf);
            continue;
        }
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
            check_picture(argv[i], buf, len);
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
    printf("png332: %d pictures: BMP332.info's head and BMP332.to_png's bytes as parse + encode_png\n",
           g_pictures);
    if (g_fail) {
        printf("png332: %d failures\n", g_fail);
        return 1;
    }
    printf("png332: OK\n");
    return 0;
}
