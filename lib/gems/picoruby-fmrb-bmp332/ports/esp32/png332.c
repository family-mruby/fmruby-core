// PNG reader and writer for the sprite pictures (fmrb_png332.h).
//
// Pure C over buffers: no mruby, no platform headers, no static state, so the
// host test (test/png332) builds this very file. The inflate side decodes into
// a buffer that holds the whole picture, so it needs no 32 KB window of its
// own; the deflate side is a small LZ77 with per-block Huffman codes (a
// sprite-sized picture comes out as small as zlib's best; see the deflate
// section for what it costs).

#include "fmrb_png332.h"
#include <string.h>

static const uint8_t PNG_SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

// ---------------------------------------------------------------- common --

static const uint16_t LEN_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t LEN_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void wr32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    // Half-byte table: 64 bytes of constant data instead of 1 KB.
    static const uint32_t T[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
        0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ T[crc & 15];
        crc = (crc >> 4) ^ T[crc & 15];
    }
    return ~crc;
}

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n > 0) {
        size_t k = n < 5552 ? n : 5552;
        n -= k;
        while (k--) {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

int fmrb_png332_is_png(const uint8_t *buf, size_t len)
{
    return buf && len >= 8 && memcmp(buf, PNG_SIG, 8) == 0;
}

const char *fmrb_png332_error(int code)
{
    switch (code) {
    case FMRB_PNG332_OK: return "ok";
    case FMRB_PNG332_E_NOT_PNG: return "not a PNG";
    case FMRB_PNG332_E_FORMAT: return "broken PNG";
    case FMRB_PNG332_E_UNSUPPORTED: return "PNG kind not handled (interlaced, 16-bit or too large)";
    case FMRB_PNG332_E_DATA: return "PNG image data does not decode";
    case FMRB_PNG332_E_NOMEM: return "out of memory";
    default: return "PNG error";
    }
}

// ---------------------------------------------------------------- inflate --

typedef struct {
    uint16_t counts[16];    // codes of each length
    uint16_t symbols[288];  // symbols ordered by code
} huff_t;

typedef struct {
    const uint8_t *in;
    size_t in_len, in_pos;
    uint32_t bits;
    int nbits;
    uint8_t *out;
    size_t out_len, out_pos;
    huff_t lit, dist;
    uint8_t lengths[288 + 32];
} inflate_t;

static int getbit(inflate_t *s)
{
    if (s->nbits == 0) {
        if (s->in_pos >= s->in_len) return -1;
        s->bits = s->in[s->in_pos++];
        s->nbits = 8;
    }
    int b = s->bits & 1;
    s->bits >>= 1;
    s->nbits--;
    return b;
}

static int getbits(inflate_t *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        int b = getbit(s);
        if (b < 0) return -1;
        v |= b << i;
    }
    return v;
}

static int build_tree(huff_t *t, const uint8_t *lengths, int n)
{
    uint16_t offs[16];
    memset(t->counts, 0, sizeof(t->counts));
    for (int i = 0; i < n; i++) t->counts[lengths[i]]++;
    t->counts[0] = 0;
    uint16_t sum = 0;
    int left = 1;
    for (int i = 1; i < 16; i++) {
        offs[i] = sum;
        sum += t->counts[i];
        left <<= 1;
        left -= t->counts[i];
        if (left < 0) return -1;   // over-subscribed
    }
    for (int i = 0; i < n; i++) {
        if (lengths[i]) t->symbols[offs[lengths[i]]++] = (uint16_t)i;
    }
    return 0;
}

static int decode_sym(inflate_t *s, const huff_t *t)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        int b = getbit(s);
        if (b < 0) return -1;
        code |= b;
        int count = t->counts[len];
        if (code - first < count) return t->symbols[index + code - first];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int fixed_trees(inflate_t *s)
{
    int i = 0;
    for (; i < 144; i++) s->lengths[i] = 8;
    for (; i < 256; i++) s->lengths[i] = 9;
    for (; i < 280; i++) s->lengths[i] = 7;
    for (; i < 288; i++) s->lengths[i] = 8;
    if (build_tree(&s->lit, s->lengths, 288) != 0) return -1;
    for (i = 0; i < 30; i++) s->lengths[i] = 5;
    return build_tree(&s->dist, s->lengths, 30);
}

static int dynamic_trees(inflate_t *s)
{
    static const uint8_t ORDER[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int hlit = getbits(s, 5), hdist = getbits(s, 5), hclen = getbits(s, 4);
    if (hlit < 0 || hdist < 0 || hclen < 0) return -1;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 30) return -1;
    memset(s->lengths, 0, 19);
    for (int i = 0; i < hclen; i++) {
        int v = getbits(s, 3);
        if (v < 0) return -1;
        s->lengths[ORDER[i]] = (uint8_t)v;
    }
    // The code-length code goes in the literal tree for the moment.
    if (build_tree(&s->lit, s->lengths, 19) != 0) return -1;
    int n = 0;
    while (n < hlit + hdist) {
        int sym = decode_sym(s, &s->lit);
        int rep, val = 0;
        if (sym < 0) return -1;
        if (sym < 16) {
            s->lengths[n++] = (uint8_t)sym;
            continue;
        } else if (sym == 16) {
            if (n == 0) return -1;
            val = s->lengths[n - 1];
            rep = getbits(s, 2);
            if (rep < 0) return -1;
            rep += 3;
        } else if (sym == 17) {
            rep = getbits(s, 3);
            if (rep < 0) return -1;
            rep += 3;
        } else {
            rep = getbits(s, 7);
            if (rep < 0) return -1;
            rep += 11;
        }
        if (n + rep > hlit + hdist) return -1;
        while (rep--) s->lengths[n++] = (uint8_t)val;
    }
    if (s->lengths[256] == 0) return -1;
    if (build_tree(&s->lit, s->lengths, hlit) != 0) return -1;
    return build_tree(&s->dist, s->lengths + hlit, hdist);
}

static int inflate_block(inflate_t *s)
{
    for (;;) {
        int sym = decode_sym(s, &s->lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->out_pos >= s->out_len) return -1;
            s->out[s->out_pos++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) return -1;
        int e = getbits(s, LEN_EXTRA[sym]);
        if (e < 0) return -1;
        size_t len = LEN_BASE[sym] + (size_t)e;
        int ds = decode_sym(s, &s->dist);
        if (ds < 0 || ds >= 30) return -1;
        e = getbits(s, DIST_EXTRA[ds]);
        if (e < 0) return -1;
        size_t dist = DIST_BASE[ds] + (size_t)e;
        if (dist > s->out_pos || len > s->out_len - s->out_pos) return -1;
        uint8_t *d = s->out + s->out_pos;
        const uint8_t *src = d - dist;
        for (size_t i = 0; i < len; i++) d[i] = src[i];
        s->out_pos += len;
    }
}

static int inflate_stored(inflate_t *s)
{
    // The rest of the current byte is dropped.
    s->nbits = 0;
    if (s->in_len - s->in_pos < 4) return -1;
    const uint8_t *p = s->in + s->in_pos;
    size_t len = p[0] | ((size_t)p[1] << 8);
    size_t nlen = p[2] | ((size_t)p[3] << 8);
    if ((len ^ 0xFFFF) != nlen) return -1;
    s->in_pos += 4;
    if (len > s->in_len - s->in_pos || len > s->out_len - s->out_pos) return -1;
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

// A zlib stream that must fill out exactly.
static int zlib_inflate(inflate_t *s)
{
    if (s->in_len < 6) return -1;
    uint8_t cmf = s->in[0], flg = s->in[1];
    if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) return -1;
    s->in_pos = 2;
    int final;
    do {
        final = getbit(s);
        int type = getbits(s, 2);
        if (final < 0 || type < 0) return -1;
        int r;
        if (type == 0) r = inflate_stored(s);
        else if (type == 1) r = fixed_trees(s) == 0 ? inflate_block(s) : -1;
        else if (type == 2) r = dynamic_trees(s) == 0 ? inflate_block(s) : -1;
        else r = -1;
        if (r != 0) return -1;
    } while (!final);
    if (s->out_pos != s->out_len) return -1;
    if (s->in_len - s->in_pos < 4) return -1;
    if (rd32be(s->in + s->in_pos) != adler32(s->out, s->out_len)) return -1;
    return 0;
}

// ----------------------------------------------------------------- decode --

static uint8_t to332(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint8_t)((r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6));
}

// A pixel at least this transparent becomes the transparent colour: alpha
// below half, and the tRNS entries below half for a palette. Half-transparent
// edges go one way or the other, since a key colour has no in-between.
#define ALPHA_OPAQUE_MIN 128

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

int fmrb_png332_decode(const uint8_t *buf, size_t len,
                       const fmrb_png332_allocator_t *a,
                       uint16_t *width, uint16_t *height, uint8_t **pixels)
{
    if (!fmrb_png332_is_png(buf, len)) return FMRB_PNG332_E_NOT_PNG;

    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1;
    const uint8_t *plte = NULL;
    const uint8_t *trns = NULL;
    size_t plte_n = 0, trns_n = 0, idat_total = 0;
    int have_ihdr = 0;

    // First walk: the header, the palette and how much image data there is.
    size_t pos = 8;
    while (pos + 12 <= len) {
        uint32_t clen = rd32be(buf + pos);
        const uint8_t *type = buf + pos + 4;
        const uint8_t *body = buf + pos + 8;
        if (clen > len - pos - 12) return FMRB_PNG332_E_FORMAT;
        if (memcmp(type, "IHDR", 4) == 0) {
            if (clen != 13) return FMRB_PNG332_E_FORMAT;
            w = rd32be(body);
            h = rd32be(body + 4);
            depth = body[8];
            ctype = body[9];
            if (body[10] != 0 || body[11] != 0) return FMRB_PNG332_E_FORMAT;
            if (body[12] != 0) return FMRB_PNG332_E_UNSUPPORTED;
            have_ihdr = 1;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (clen % 3 != 0 || clen > 768) return FMRB_PNG332_E_FORMAT;
            plte = body;
            plte_n = clen / 3;
        } else if (memcmp(type, "tRNS", 4) == 0) {
            trns = body;
            trns_n = clen;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat_total += clen;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + clen;
    }
    if (!have_ihdr || idat_total == 0) return FMRB_PNG332_E_FORMAT;
    if (w == 0 || h == 0 || w > FMRB_PNG332_MAX_DIM || h > FMRB_PNG332_MAX_DIM) {
        return FMRB_PNG332_E_UNSUPPORTED;
    }

    int channels;
    switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return FMRB_PNG332_E_FORMAT;
    }
    if (ctype == 0 || ctype == 3) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return FMRB_PNG332_E_UNSUPPORTED;
    } else if (depth != 8) {
        return FMRB_PNG332_E_UNSUPPORTED;
    }
    if (ctype == 3 && plte == NULL) return FMRB_PNG332_E_FORMAT;

    size_t bpp_bits = (size_t)depth * channels;
    size_t stride = ((size_t)w * bpp_bits + 7) / 8;
    size_t step = bpp_bits < 8 ? 1 : bpp_bits / 8;
    size_t raw_len = (stride + 1) * h;

    // The colour of each sample value (palette index or grey level). A
    // palette entry or grey level that tRNS makes transparent maps to the
    // transparent colour. tRNS is one alpha per palette entry (the rest are
    // opaque), one grey sample, or one RGB sample (16 bits each, of which an
    // 8-bit picture uses the low byte); any other shape is ignored.
    uint8_t map[256];
    int key_rgb = 0;
    uint8_t key_r = 0, key_g = 0, key_b = 0;
    if (ctype == 3) {
        memset(map, 0, sizeof(map));
        for (size_t i = 0; i < plte_n; i++) {
            map[i] = to332(plte[i * 3], plte[i * 3 + 1], plte[i * 3 + 2]);
            if (trns && i < trns_n && trns[i] < ALPHA_OPAQUE_MIN) map[i] = FMRB_PNG332_TRANSPARENT;
        }
    } else if (ctype == 0) {
        int maxv = (1 << depth) - 1;
        for (int i = 0; i <= maxv; i++) {
            uint8_t g = (uint8_t)(i * 255 / maxv);
            map[i] = to332(g, g, g);
        }
        if (trns && trns_n == 2 && trns[0] == 0 && trns[1] <= maxv) {
            map[trns[1]] = FMRB_PNG332_TRANSPARENT;
        }
    } else if (ctype == 2 && trns && trns_n == 6 &&
               trns[0] == 0 && trns[2] == 0 && trns[4] == 0) {
        key_rgb = 1;
        key_r = trns[1];
        key_g = trns[3];
        key_b = trns[5];
    }

    uint8_t *idat = (uint8_t *)a->alloc(a->ud, idat_total);
    uint8_t *raw = (uint8_t *)a->alloc(a->ud, raw_len);
    inflate_t *st = (inflate_t *)a->alloc(a->ud, sizeof(inflate_t));
    uint8_t *px = (uint8_t *)a->alloc(a->ud, (size_t)w * h);
    int ret = FMRB_PNG332_OK;
    if (!idat || !raw || !st || !px) {
        ret = FMRB_PNG332_E_NOMEM;
        goto done;
    }

    // Second walk: the image data in one piece.
    {
        size_t at = 0;
        pos = 8;
        while (pos + 12 <= len) {
            uint32_t clen = rd32be(buf + pos);
            const uint8_t *type = buf + pos + 4;
            if (memcmp(type, "IDAT", 4) == 0) {
                memcpy(idat + at, buf + pos + 8, clen);
                at += clen;
            } else if (memcmp(type, "IEND", 4) == 0) {
                break;
            }
            pos += 12 + clen;
        }
    }

    memset(st, 0, sizeof(*st));
    st->in = idat;
    st->in_len = idat_total;
    st->out = raw;
    st->out_len = raw_len;
    if (zlib_inflate(st) != 0) {
        ret = FMRB_PNG332_E_DATA;
        goto done;
    }

    // Undo the filters in place; each line's previous line is the one above.
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *line = raw + (size_t)y * (stride + 1);
        uint8_t f = line[0];
        uint8_t *cur = line + 1;
        const uint8_t *prev = y > 0 ? cur - (stride + 1) : NULL;
        if (f > 4) {
            ret = FMRB_PNG332_E_DATA;
            goto done;
        }
        for (size_t i = 0; i < stride; i++) {
            int left = i >= step ? cur[i - step] : 0;
            int up = prev ? prev[i] : 0;
            int ul = (prev && i >= step) ? prev[i - step] : 0;
            int add;
            switch (f) {
            case 1: add = left; break;
            case 2: add = up; break;
            case 3: add = (left + up) / 2; break;
            case 4: add = paeth(left, up, ul); break;
            default: add = 0; break;
            }
            cur[i] = (uint8_t)(cur[i] + add);
        }
        uint8_t *o = px + (size_t)y * w;
        for (uint32_t x = 0; x < w; x++) {
            if (ctype == 3 || ctype == 0) {
                int v;
                if (depth == 8) {
                    v = cur[x];
                } else {
                    size_t bit = (size_t)x * depth;
                    v = (cur[bit / 8] >> (8 - depth - (int)(bit % 8))) & ((1 << depth) - 1);
                }
                o[x] = map[v];
            } else if (ctype == 4) {
                uint8_t g = cur[x * 2];
                o[x] = cur[x * 2 + 1] < ALPHA_OPAQUE_MIN ? FMRB_PNG332_TRANSPARENT : to332(g, g, g);
            } else {
                const uint8_t *c = cur + (size_t)x * channels;
                if ((ctype == 6 && c[3] < ALPHA_OPAQUE_MIN) ||
                    (key_rgb && c[0] == key_r && c[1] == key_g && c[2] == key_b)) {
                    o[x] = FMRB_PNG332_TRANSPARENT;
                } else {
                    o[x] = to332(c[0], c[1], c[2]);
                }
            }
        }
    }

    *width = (uint16_t)w;
    *height = (uint16_t)h;
    *pixels = px;
    px = NULL;

done:
    if (px) a->free(a->ud, px);
    if (st) a->free(a->ud, st);
    if (raw) a->free(a->ud, raw);
    if (idat) a->free(a->ud, idat);
    return ret;
}

// ---------------------------------------------------------------- deflate --
//
// LZ77 with hash chains and one step of lazy matching (as zlib), a window of
// up to 32 KB but never more than the data, the tokens of up to BLOCK_TOKENS
// at a time, and each block written with whichever of the fixed codes and its
// own Huffman codes is shorter (the latter matter: a 4-colour picture's
// pixels cost 2-3 bits there instead of 8). The tables grow with the picture
// and come from the allocator: a 16x16 sprite needs about 10 KB in all.

#define MAX_HASH_BITS 13
#define MAX_WIN 32768u
#define MAX_CHAIN 128
#define MIN_MATCH 3
#define MAX_MATCH 258
#define BLOCK_TOKENS 4096
#define NLIT 286
#define NDIST 30
#define NCLEN 19

static const uint8_t CLEN_ORDER[NCLEN] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

typedef struct {
    uint8_t *out;
    size_t cap, pos;
    uint32_t bits;
    int nbits;
} bitw_t;

typedef struct {
    bitw_t bw;
    int32_t *head;        // 1 << hash_bits: the latest position of each hash
    uint16_t *prev;       // per position (mod prev_size): back to the previous
    uint32_t *tok;        // literal, or 0x80000000 | len << 16 | dist
    size_t ntok, tok_cap;
    size_t prev_size;     // the window: positions this far back are found
    int hash_bits;
    uint32_t lfreq[NLIT], dfreq[NDIST], cfreq[NCLEN];
    uint32_t lwork[NLIT], dwork[NDIST];   // counts huff_lengths may bend
    uint8_t llen[NLIT], dlen[NDIST], clen[NCLEN];
    uint16_t lcode[NLIT], dcode[NDIST], ccode[NCLEN];
    uint8_t rle_sym[NLIT + NDIST], rle_extra[NLIT + NDIST];
    uint8_t seq[NLIT + NDIST];
    // Huffman tree building
    uint32_t weight[2 * NLIT];
    int16_t parent[2 * NLIT];
    uint8_t alive[2 * NLIT];
} deflate_t;

static void putbits(bitw_t *b, uint32_t v, int n)
{
    b->bits |= v << b->nbits;
    b->nbits += n;
    while (b->nbits >= 8) {
        if (b->pos < b->cap) b->out[b->pos] = (uint8_t)b->bits;
        b->pos++;
        b->bits >>= 8;
        b->nbits -= 8;
    }
}

// Huffman codes go most significant bit first.
static void putcode(bitw_t *b, uint32_t code, int n)
{
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r = (r << 1) | (code & 1);
        code >>= 1;
    }
    putbits(b, r, n);
}

static int len_sym(size_t len)
{
    int s = 28;
    while (LEN_BASE[s] > len) s--;
    return s;
}

static int dist_sym(size_t dist)
{
    int s = 29;
    while (DIST_BASE[s] > dist) s--;
    return s;
}

// Code lengths for freq[0..n-1], none longer than maxbits. Unused symbols get
// 0; at least two symbols get a code, so the code is complete.
static void huff_lengths(deflate_t *d, uint32_t *freq, int n, int maxbits, uint8_t *len)
{
    int used = 0;
    for (int i = 0; i < n; i++) used += freq[i] != 0;
    for (int i = 0; used < 2 && i < n; i++) {
        if (freq[i] == 0) {
            freq[i] = 1;
            used++;
        }
    }
    for (;;) {
        int nodes = n;
        for (int i = 0; i < n; i++) {
            d->weight[i] = freq[i];
            d->parent[i] = -1;
            d->alive[i] = freq[i] != 0;
        }
        for (int left = used; left > 1; left--) {
            int m1 = -1, m2 = -1;
            for (int i = 0; i < nodes; i++) {
                if (!d->alive[i]) continue;
                if (m1 < 0 || d->weight[i] < d->weight[m1]) {
                    m2 = m1;
                    m1 = i;
                } else if (m2 < 0 || d->weight[i] < d->weight[m2]) {
                    m2 = i;
                }
            }
            d->weight[nodes] = d->weight[m1] + d->weight[m2];
            d->parent[nodes] = -1;
            d->alive[nodes] = 1;
            d->alive[m1] = d->alive[m2] = 0;
            d->parent[m1] = d->parent[m2] = (int16_t)nodes;
            nodes++;
        }
        int too_long = 0;
        for (int i = 0; i < n; i++) {
            int l = 0;
            if (freq[i]) {
                for (int p = i; d->parent[p] >= 0; p = d->parent[p]) l++;
            }
            if (l > maxbits) too_long = 1;
            len[i] = (uint8_t)l;
        }
        if (!too_long) return;
        // Flatten the counts and build again.
        for (int i = 0; i < n; i++) {
            if (freq[i]) freq[i] = (freq[i] >> 1) | 1;
        }
    }
}

static void huff_codes(const uint8_t *len, int n, uint16_t *code)
{
    uint16_t count[16] = { 0 }, next[16];
    for (int i = 0; i < n; i++) count[len[i]]++;
    count[0] = 0;
    uint16_t c = 0;
    for (int b = 1; b < 16; b++) {
        c = (uint16_t)((c + count[b - 1]) << 1);
        next[b] = c;
    }
    for (int i = 0; i < n; i++) {
        if (len[i]) code[i] = next[len[i]]++;
    }
}

// The run-length form of the code lengths (symbols 0-18 of RFC 1951 3.2.7).
static int rle_lengths(deflate_t *d, int total)
{
    int k = 0;
    for (int i = 0; i < total;) {
        uint8_t v = d->seq[i];
        int run = 1;
        while (i + run < total && d->seq[i + run] == v) run++;
        if (v == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            if (r >= 11) {
                d->rle_sym[k] = 18;
                d->rle_extra[k++] = (uint8_t)(r - 11);
            } else {
                d->rle_sym[k] = 17;
                d->rle_extra[k++] = (uint8_t)(r - 3);
            }
            i += r;
        } else if (v != 0 && run >= 4) {
            d->rle_sym[k] = v;
            d->rle_extra[k++] = 0;
            int r = run - 1 > 6 ? 6 : run - 1;
            d->rle_sym[k] = 16;
            d->rle_extra[k++] = (uint8_t)(r - 3);
            i += 1 + r;
        } else {
            d->rle_sym[k] = v;
            d->rle_extra[k++] = 0;
            i++;
        }
    }
    return k;
}

static uint8_t fixed_lit_len(int s)
{
    return s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
}

static void put_fixed_lit(bitw_t *b, int s)
{
    if (s < 144) putcode(b, 0x30 + s, 8);
    else if (s < 256) putcode(b, 0x190 + (s - 144), 9);
    else if (s < 280) putcode(b, s - 256, 7);
    else putcode(b, 0xC0 + (s - 280), 8);
}

static void flush_block(deflate_t *d, int final)
{
    memset(d->lfreq, 0, sizeof(d->lfreq));
    memset(d->dfreq, 0, sizeof(d->dfreq));
    size_t extra_bits = 0;
    for (size_t i = 0; i < d->ntok; i++) {
        uint32_t t = d->tok[i];
        if (t & 0x80000000u) {
            int ls = len_sym((t >> 16) & 0x1FF), ds = dist_sym(t & 0xFFFF);
            d->lfreq[257 + ls]++;
            d->dfreq[ds]++;
            extra_bits += LEN_EXTRA[ls] + DIST_EXTRA[ds];
        } else {
            d->lfreq[t]++;
        }
    }
    d->lfreq[256] = 1;

    // The fixed codes' cost, from the counts as they are.
    size_t fixed_bits = 3 + extra_bits;
    for (int i = 0; i < NLIT; i++) fixed_bits += (size_t)d->lfreq[i] * fixed_lit_len(i);
    for (int i = 0; i < NDIST; i++) fixed_bits += (size_t)d->dfreq[i] * 5;

    // This block's own codes (huff_lengths may bend the counts: cost them
    // with copies taken first).
    memcpy(d->lwork, d->lfreq, sizeof(d->lwork));
    memcpy(d->dwork, d->dfreq, sizeof(d->dwork));
    huff_lengths(d, d->lwork, NLIT, 15, d->llen);
    huff_lengths(d, d->dwork, NDIST, 15, d->dlen);
    int hlit = NLIT, hdist = NDIST;
    while (hlit > 257 && d->llen[hlit - 1] == 0) hlit--;
    while (hdist > 1 && d->dlen[hdist - 1] == 0) hdist--;
    memcpy(d->seq, d->llen, (size_t)hlit);
    memcpy(d->seq + hlit, d->dlen, (size_t)hdist);
    int nrle = rle_lengths(d, hlit + hdist);
    memset(d->cfreq, 0, sizeof(d->cfreq));
    for (int i = 0; i < nrle; i++) d->cfreq[d->rle_sym[i]]++;
    huff_lengths(d, d->cfreq, NCLEN, 7, d->clen);
    int hclen = NCLEN;
    while (hclen > 4 && d->clen[CLEN_ORDER[hclen - 1]] == 0) hclen--;
    size_t dyn_bits = 3 + 14 + 3 * (size_t)hclen + extra_bits;
    for (int i = 0; i < nrle; i++) {
        uint8_t s = d->rle_sym[i];
        dyn_bits += d->clen[s] + (s == 16 ? 2 : s == 17 ? 3 : s == 18 ? 7 : 0);
    }
    for (int i = 0; i < NLIT; i++) dyn_bits += (size_t)d->lfreq[i] * d->llen[i];
    for (int i = 0; i < NDIST; i++) dyn_bits += (size_t)d->dfreq[i] * d->dlen[i];

    bitw_t *b = &d->bw;
    int dynamic = dyn_bits < fixed_bits;
    putbits(b, final ? 1 : 0, 1);
    if (dynamic) {
        putbits(b, 2, 2);
        huff_codes(d->llen, NLIT, d->lcode);
        huff_codes(d->dlen, NDIST, d->dcode);
        huff_codes(d->clen, NCLEN, d->ccode);
        putbits(b, (uint32_t)(hlit - 257), 5);
        putbits(b, (uint32_t)(hdist - 1), 5);
        putbits(b, (uint32_t)(hclen - 4), 4);
        for (int i = 0; i < hclen; i++) putbits(b, d->clen[CLEN_ORDER[i]], 3);
        for (int i = 0; i < nrle; i++) {
            uint8_t s = d->rle_sym[i];
            putcode(b, d->ccode[s], d->clen[s]);
            if (s == 16) putbits(b, d->rle_extra[i], 2);
            else if (s == 17) putbits(b, d->rle_extra[i], 3);
            else if (s == 18) putbits(b, d->rle_extra[i], 7);
        }
    } else {
        putbits(b, 1, 2);
    }
    for (size_t i = 0; i < d->ntok; i++) {
        uint32_t t = d->tok[i];
        if (t & 0x80000000u) {
            size_t len = (t >> 16) & 0x1FF, dist = t & 0xFFFF;
            int ls = len_sym(len), ds = dist_sym(dist);
            if (dynamic) putcode(b, d->lcode[257 + ls], d->llen[257 + ls]);
            else put_fixed_lit(b, 257 + ls);
            putbits(b, (uint32_t)(len - LEN_BASE[ls]), LEN_EXTRA[ls]);
            if (dynamic) putcode(b, d->dcode[ds], d->dlen[ds]);
            else putcode(b, (uint32_t)ds, 5);
            putbits(b, (uint32_t)(dist - DIST_BASE[ds]), DIST_EXTRA[ds]);
        } else if (dynamic) {
            putcode(b, d->lcode[t], d->llen[t]);
        } else {
            put_fixed_lit(b, (int)t);
        }
    }
    if (dynamic) putcode(b, d->lcode[256], d->llen[256]);
    else put_fixed_lit(b, 256);
    d->ntok = 0;
}

static uint32_t hash3(const deflate_t *d, const uint8_t *p)
{
    uint32_t h = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return (h * 2654435761u) >> (32 - d->hash_bits);
}

// Puts positions *next .. upto-1 in the hash chains.
static void insert_upto(deflate_t *d, const uint8_t *data, size_t n, size_t *next, size_t upto)
{
    for (size_t i = *next; i < upto; i++) {
        if (i + MIN_MATCH > n) continue;
        uint32_t hv = hash3(d, data + i);
        int32_t old = d->head[hv];
        size_t back = old >= 0 ? i - (size_t)old : 0;
        d->prev[i % d->prev_size] = (uint16_t)(back < d->prev_size ? back : 0);
        d->head[hv] = (int32_t)i;
    }
    if (upto > *next) *next = upto;
}

// The longest match for position i among the positions already inserted.
static size_t find_match(const deflate_t *d, const uint8_t *data, size_t n, size_t i, size_t *dist_out)
{
    size_t best_len = 0;
    if (i + MIN_MATCH > n) return 0;
    int32_t cand = d->head[hash3(d, data + i)];
    size_t limit = n - i < MAX_MATCH ? n - i : MAX_MATCH;
    for (int chain = 0; cand >= 0 && chain < MAX_CHAIN; chain++) {
        size_t dist = i - (size_t)cand;
        if (dist >= d->prev_size) break;
        if (data[cand + best_len] == data[i + best_len] || best_len == 0) {
            size_t l = 0;
            while (l < limit && data[cand + l] == data[i + l]) l++;
            if (l > best_len) {
                best_len = l;
                *dist_out = dist;
                if (l == limit) break;
            }
        }
        uint16_t back = d->prev[(size_t)cand % d->prev_size];
        if (back == 0) break;
        cand -= back;
    }
    return best_len >= MIN_MATCH ? best_len : 0;
}

static void emit(deflate_t *d, uint32_t t)
{
    d->tok[d->ntok++] = t;
    if (d->ntok == d->tok_cap) flush_block(d, 0);
}

// The deflate blocks of data into d->bw. Returns the bytes written (which
// may be more than its cap: then the caller stores the data instead).
static size_t deflate_data(deflate_t *d, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < ((size_t)1 << d->hash_bits); i++) d->head[i] = -1;
    memset(d->prev, 0, d->prev_size * sizeof(uint16_t));
    d->ntok = 0;
    size_t i = 0, next = 0;
    while (i < n) {
        size_t dist = 0, dist2 = 0;
        insert_upto(d, data, n, &next, i);
        size_t len = find_match(d, data, n, i, &dist);
        if (len && len < MAX_MATCH && i + 1 < n) {
            // Lazy: a longer match one byte on makes this byte a literal.
            insert_upto(d, data, n, &next, i + 1);
            size_t len2 = find_match(d, data, n, i + 1, &dist2);
            if (len2 > len) len = 0;
        }
        if (len) {
            emit(d, 0x80000000u | ((uint32_t)len << 16) | (uint32_t)dist);
            i += len;
        } else {
            emit(d, data[i]);
            i++;
        }
    }
    flush_block(d, 1);
    if (d->bw.nbits > 0) putbits(&d->bw, 0, 8 - d->bw.nbits);
    return d->bw.pos;
}

static uint8_t *put_chunk(uint8_t *p, const char *type, const uint8_t *data, size_t n)
{
    wr32be(p, (uint32_t)n);
    memcpy(p + 4, type, 4);
    if (n) memmove(p + 8, data, n);
    wr32be(p + 8 + n, crc32_update(0, p + 4, n + 4));
    return p + 12 + n;
}

int fmrb_png332_encode(const uint8_t *pixels, uint16_t width, uint16_t height,
                       const fmrb_png332_allocator_t *a,
                       uint8_t **out, size_t *out_len)
{
    if (!pixels || width == 0 || height == 0 ||
        width > FMRB_PNG332_MAX_DIM || height > FMRB_PNG332_MAX_DIM) {
        return FMRB_PNG332_E_UNSUPPORTED;
    }
    size_t w = width, h = height, n = w * h;

    // The colours used, in ascending order (as tools/rgb332_png.rb). The
    // tables come from the allocator to keep the app task's stack small.
    uint8_t *tables = (uint8_t *)a->alloc(a->ud, 256 + 256 + 256 * 3);
    if (!tables) return FMRB_PNG332_E_NOMEM;
    uint8_t *used = tables, *index = tables + 256, *plte = tables + 512;
    memset(used, 0, 256);
    for (size_t i = 0; i < n; i++) used[pixels[i]] = 1;
    int ncol = 0;
    for (int c = 0; c < 256; c++) {
        if (!used[c]) continue;
        index[c] = (uint8_t)ncol;
        plte[ncol * 3] = (uint8_t)(((c >> 5) & 7) * 255 / 7);
        plte[ncol * 3 + 1] = (uint8_t)(((c >> 2) & 7) * 255 / 7);
        plte[ncol * 3 + 2] = (uint8_t)((c & 3) * 255 / 3);
        ncol++;
    }
    int depth = (n >= 16 * 1024 || ncol > 16) ? 8 : ncol > 4 ? 4 : ncol > 2 ? 2 : 1;
    size_t stride = (w * depth + 7) / 8;
    size_t raw_len = (stride + 1) * h;

    // Stored blocks: 5 bytes per 65535. The compressed attempt gets that much
    // room and gives way to them when it does not fit.
    size_t stored_len = raw_len + 5 * ((raw_len + 65534) / 65535 + (raw_len == 0));
    size_t z_cap = stored_len + 6;   // zlib header and Adler-32 around either
    size_t png_cap = 8 + 25 + 12 + (size_t)ncol * 3 + 12 + z_cap + 12;

    uint8_t *raw = (uint8_t *)a->alloc(a->ud, raw_len);
    deflate_t *d = (deflate_t *)a->alloc(a->ud, sizeof(deflate_t));
    if (d) {
        memset(d, 0, sizeof(*d));
        d->prev_size = raw_len < MAX_WIN ? raw_len : MAX_WIN;
        d->hash_bits = 8;
        while (d->hash_bits < MAX_HASH_BITS && ((size_t)1 << d->hash_bits) < raw_len) d->hash_bits++;
        d->tok_cap = raw_len < BLOCK_TOKENS ? raw_len + 1 : BLOCK_TOKENS;
        d->head = (int32_t *)a->alloc(a->ud, ((size_t)1 << d->hash_bits) * sizeof(int32_t));
        d->prev = (uint16_t *)a->alloc(a->ud, d->prev_size * sizeof(uint16_t));
        d->tok = (uint32_t *)a->alloc(a->ud, d->tok_cap * sizeof(uint32_t));
    }
    uint8_t *png = (uint8_t *)a->alloc(a->ud, png_cap);
    int ret = FMRB_PNG332_OK;
    if (!raw || !d || !d->head || !d->prev || !d->tok || !png) {
        ret = FMRB_PNG332_E_NOMEM;
        goto done;
    }

    for (size_t y = 0; y < h; y++) {
        uint8_t *line = raw + y * (stride + 1);
        line[0] = 0;   // filter: none
        memset(line + 1, 0, stride);
        const uint8_t *src = pixels + y * w;
        for (size_t x = 0; x < w; x++) {
            uint8_t v = index[src[x]];
            if (depth == 8) {
                line[1 + x] = v;
            } else {
                size_t bit = x * depth;
                line[1 + bit / 8] |= (uint8_t)(v << (8 - depth - (int)(bit % 8)));
            }
        }
    }

    // The zlib stream goes where IDAT's body will be.
    uint8_t *z = png + 8 + 25 + 12 + (size_t)ncol * 3 + 8;
    z[0] = 0x78;
    z[1] = 0xDA;   // 32 K window, "best" (a hint only)
    d->bw.out = z + 2;
    d->bw.cap = stored_len;
    d->bw.pos = 0;
    d->bw.bits = 0;
    d->bw.nbits = 0;
    size_t zlen = deflate_data(d, raw, raw_len);
    if (zlen > stored_len) {
        size_t at = 0, p = 0;
        do {
            size_t k = raw_len - at < 65535 ? raw_len - at : 65535;
            uint8_t *s = z + 2 + p;
            s[0] = (at + k >= raw_len) ? 1 : 0;
            s[1] = (uint8_t)k;
            s[2] = (uint8_t)(k >> 8);
            s[3] = (uint8_t)~k;
            s[4] = (uint8_t)(~k >> 8);
            memcpy(s + 5, raw + at, k);
            p += 5 + k;
            at += k;
        } while (at < raw_len);
        zlen = p;
    }
    wr32be(z + 2 + zlen, adler32(raw, raw_len));
    zlen += 6;

    uint8_t ihdr[13];
    wr32be(ihdr, (uint32_t)w);
    wr32be(ihdr + 4, (uint32_t)h);
    ihdr[8] = (uint8_t)depth;
    ihdr[9] = 3;    // palette
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    memcpy(png, PNG_SIG, 8);
    uint8_t *p = put_chunk(png + 8, "IHDR", ihdr, 13);
    p = put_chunk(p, "PLTE", plte, (size_t)ncol * 3);
    p = put_chunk(p, "IDAT", z, zlen);   // z is already in place
    p = put_chunk(p, "IEND", NULL, 0);

    *out = png;
    *out_len = (size_t)(p - png);
    png = NULL;

done:
    if (png) a->free(a->ud, png);
    if (d) {
        if (d->tok) a->free(a->ud, d->tok);
        if (d->prev) a->free(a->ud, d->prev);
        if (d->head) a->free(a->ud, d->head);
        a->free(a->ud, d);
    }
    if (raw) a->free(a->ud, raw);
    a->free(a->ud, tables);
    return ret;
}
