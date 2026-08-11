/*
 * EditorCore: the editor's document model, in C.
 *
 * Why this exists (doc/editor_serious_mode/instruction_p4.md): with the document
 * held as an mruby Array of Strings, a 53KB file killed the editor's VM outright
 * and a fullscreen editor plus a 14KB windowed app ran the app pool out of
 * memory. Lines now live in a flat arena taken from POOL_ID_EDITOR_DOC -- its own
 * 1MB PSRAM pool, deliberately neither the app's mruby pool (which would cap the
 * file size and feed the GC's malloc accounting) nor the shared SYSTEM pool
 * (the drivers' wallet, and too small anyway).
 *
 * Columns are UTF-8 CHARACTER indices, matching what the Ruby editor did with
 * String#[] -- not bytes. Every entry point that takes an x converts.
 *
 * Documents are addressed by an int slot (ec_open_slot), which is what lets the
 * same code back both the mruby binding and the Spinel FFI. Each editor instance
 * holds one slot; a fourth editor gets a negative slot and reports it.
 */

#include <string.h>
#include <stdint.h>

#include "fmrb_mem.h"
#include "fmrb_hal_file.h"
#include "fmrb_log.h"

#include "../../include/editor_core_api.h"

static const char *TAG = "editor_core";

/* Per-byte highlight categories for one line of Ruby, from the Prism lexer in
   picoruby-syntax-highlight. Declared here rather than in a shared header: both
   gems are linked into the same firmware, and adding an exported include path
   to the mruby gem build is more machinery than one prototype deserves. */
extern int fmrb_syntax_highlight_line(const char *src, size_t len, uint8_t *out_map);

#define ED_ERR_RANGE  EC_ERR_RANGE
#define ED_ERR_NOMEM  EC_ERR_NOMEM
#define ED_ERR_IO     EC_ERR_IO

#define ED_MAX_DOCS      3      /* == FMRB_USER_APP_COUNT: only user apps edit */
#define ED_LINE_CHUNK    32     /* line capacity granularity, bytes */
#define ED_LINES_CHUNK   64     /* line-array growth, entries */
#define ED_IO_CHUNK      2048   /* file read/write staging buffer */

typedef struct {
    char    *buf;       /* line bytes, no terminator */
    int32_t  len;       /* bytes used */
    int32_t  cap;       /* bytes allocated */
    uint8_t *hl;        /* per-character categories, or NULL when not cached */
    int32_t  hl_chars;  /* entries valid in hl */
} ed_line_t;

typedef struct {
    int        used;
    ed_line_t *lines;
    int32_t    count;
    int32_t    cap;
    char      *clip;     /* clipboard bytes ('\n' separated) */
    int32_t    clip_len;
    int        hl_on;
} ed_doc_t;

static fmrb_mem_handle_t g_handle = -1;
static ed_doc_t          g_docs[ED_MAX_DOCS];

/* ---- arena ------------------------------------------------------------- */

static int ed_arena_ready(void)
{
    if (g_handle >= 0) return 1;
    void  *pool = fmrb_get_mempool_ptr(POOL_ID_EDITOR_DOC);
    size_t size = fmrb_get_mempool_size(POOL_ID_EDITOR_DOC);
    if (!pool || size == 0) {
        FMRB_LOGE(TAG, "editor doc pool unavailable");
        return 0;
    }
    g_handle = fmrb_mem_create_handle(pool, size, POOL_ID_EDITOR_DOC);
    if (g_handle < 0) {
        FMRB_LOGE(TAG, "editor doc pool handle failed");
        return 0;
    }
    FMRB_LOGI(TAG, "document arena ready: %zu bytes (POOL_ID_EDITOR_DOC)", size);
    return 1;
}

static void *ed_alloc(size_t n)
{
    if (!ed_arena_ready()) return NULL;
    return fmrb_malloc(g_handle, n);
}

static void *ed_realloc(void *p, size_t n)
{
    if (!ed_arena_ready()) return NULL;
    return fmrb_realloc(g_handle, p, n);
}

static void ed_free(void *p)
{
    if (p && g_handle >= 0) fmrb_free(g_handle, p);
}

/* ---- documents --------------------------------------------------------- */

static void doc_clear(ed_doc_t *d)
{
    if (d->lines) {
        for (int32_t i = 0; i < d->count; i++) {
            ed_free(d->lines[i].buf);
            ed_free(d->lines[i].hl);
        }
        ed_free(d->lines);
    }
    d->lines = NULL;
    d->count = 0;
    d->cap = 0;
}

static ed_doc_t *doc_of(int slot)
{
    if (slot < 0 || slot >= ED_MAX_DOCS) return NULL;
    if (!g_docs[slot].used) return NULL;
    return &g_docs[slot];
}

int ec_open_slot(void)
{
    for (int i = 0; i < ED_MAX_DOCS; i++) {
        if (!g_docs[i].used) {
            g_docs[i].used = 1;
            g_docs[i].hl_on = 1;
            return i;
        }
    }
    return EC_ERR_NOMEM;  /* more editors than slots: the caller reports it */
}

void ec_close_slot(int slot)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return;
    doc_clear(d);
    ed_free(d->clip);
    d->clip = NULL;
    d->clip_len = 0;
    d->used = 0;
}

/* ---- UTF-8 ------------------------------------------------------------- */

static int32_t u8_chars(const char *s, int32_t len)
{
    int32_t n = 0;
    for (int32_t i = 0; i < len; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
    }
    return n;
}

/* Byte offset of character index ci, clamped to len. */
static int32_t u8_byte_of(const char *s, int32_t len, int32_t ci)
{
    if (ci <= 0) return 0;
    int32_t n = 0;
    for (int32_t i = 0; i < len; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {
            if (n == ci) return i;
            n++;
        }
    }
    return len;
}

/* ---- line primitives --------------------------------------------------- */

/* Bumped by anything that changes a line's contents or moves lines around.
   The wrap cache is keyed by it, so one counter invalidates every entry -- the
   conservative choice, and an edit already forces a repaint of the lines it
   touched. */
static uint32_t g_wrap_stamp = 1;

static void line_drop_hl(ed_line_t *l)
{
    ed_free(l->hl);
    l->hl = NULL;
    l->hl_chars = 0;
    g_wrap_stamp++;
}

static int line_reserve(ed_line_t *l, int32_t need)
{
    if (l->cap >= need) return 0;
    int32_t cap = ((need + ED_LINE_CHUNK) / ED_LINE_CHUNK) * ED_LINE_CHUNK;
    char *buf = (char *)ed_realloc(l->buf, (size_t)cap);
    if (!buf) return ED_ERR_NOMEM;
    l->buf = buf;
    l->cap = cap;
    return 0;
}

static int lines_reserve(ed_doc_t *d, int32_t need)
{
    if (d->cap >= need) return 0;
    int32_t cap = ((need + ED_LINES_CHUNK) / ED_LINES_CHUNK) * ED_LINES_CHUNK;
    ed_line_t *p = (ed_line_t *)ed_realloc(d->lines, (size_t)cap * sizeof(ed_line_t));
    if (!p) return ED_ERR_NOMEM;
    memset(&p[d->cap], 0, (size_t)(cap - d->cap) * sizeof(ed_line_t));
    d->lines = p;
    d->cap = cap;
    return 0;
}

/* Insert an empty line at index y. */
static int lines_insert(ed_doc_t *d, int32_t y)
{
    g_wrap_stamp++;
    int r = lines_reserve(d, d->count + 1);
    if (r < 0) return r;
    if (y < d->count) {
        memmove(&d->lines[y + 1], &d->lines[y],
                (size_t)(d->count - y) * sizeof(ed_line_t));
    }
    memset(&d->lines[y], 0, sizeof(ed_line_t));
    d->count++;
    return 0;
}

static void lines_remove(ed_doc_t *d, int32_t y)
{
    if (y < 0 || y >= d->count) return;
    g_wrap_stamp++;
    ed_free(d->lines[y].buf);
    ed_free(d->lines[y].hl);
    if (y < d->count - 1) {
        memmove(&d->lines[y], &d->lines[y + 1],
                (size_t)(d->count - y - 1) * sizeof(ed_line_t));
    }
    d->count--;
    memset(&d->lines[d->count], 0, sizeof(ed_line_t));
}

/* Empty one-line document. */
static int doc_reset(ed_doc_t *d)
{
    doc_clear(d);
    int r = lines_reserve(d, 1);
    if (r < 0) return r;
    memset(&d->lines[0], 0, sizeof(ed_line_t));
    d->count = 1;
    return 0;
}

static int line_insert_bytes(ed_line_t *l, int32_t at, const char *src, int32_t n)
{
    int r = line_reserve(l, l->len + n);
    if (r < 0) return r;
    if (at < l->len) {
        memmove(l->buf + at + n, l->buf + at, (size_t)(l->len - at));
    }
    memcpy(l->buf + at, src, (size_t)n);
    l->len += n;
    line_drop_hl(l);
    return 0;
}

static void line_delete_bytes(ed_line_t *l, int32_t at, int32_t n)
{
    if (at >= l->len || n <= 0) return;
    if (at + n > l->len) n = l->len - at;
    memmove(l->buf + at, l->buf + at + n, (size_t)(l->len - at - n));
    l->len -= n;
    line_drop_hl(l);
}

/* ---- highlight cache --------------------------------------------------- */

/* Per-character categories for line y, computed once and kept until the line
   changes. This is where the old "tokenize the line on every draw, leaving a
   String on the mruby heap each time" went. */
static const uint8_t *line_hl(ed_doc_t *d, int32_t y, int32_t *out_chars)
{
    ed_line_t *l = &d->lines[y];
    if (l->hl) {
        *out_chars = l->hl_chars;
        return l->hl;
    }
    if (!d->hl_on || l->len == 0) return NULL;

    int32_t nchars = u8_chars(l->buf, l->len);
    uint8_t *bytemap = (uint8_t *)ed_alloc((size_t)l->len);
    if (!bytemap) return NULL;
    if (fmrb_syntax_highlight_line(l->buf, (size_t)l->len, bytemap) != 0) {
        ed_free(bytemap);
        return NULL;
    }
    uint8_t *chmap = (uint8_t *)ed_alloc((size_t)nchars);
    if (!chmap) {
        ed_free(bytemap);
        return NULL;
    }
    /* Take each character's category from its first byte. */
    int32_t ci = 0;
    for (int32_t i = 0; i < l->len && ci < nchars; i++) {
        if (((unsigned char)l->buf[i] & 0xC0) != 0x80) chmap[ci++] = bytemap[i];
    }
    ed_free(bytemap);
    l->hl = chmap;
    l->hl_chars = nchars;
    *out_chars = nchars;
    return chmap;
}

static int doc_delete_range(ed_doc_t *d, int32_t sy, int32_t sx, int32_t ey, int32_t ex)
{
    if (sy < 0 || ey >= d->count || sy > ey) return ED_ERR_RANGE;
    if (sy == ey) {
        ed_line_t *l = &d->lines[sy];
        int32_t b0 = u8_byte_of(l->buf, l->len, sx);
        int32_t b1 = u8_byte_of(l->buf, l->len, ex);
        if (b1 > b0) line_delete_bytes(l, b0, b1 - b0);
        return 0;
    }
    /* head keeps [0,sx), tail of the last line is appended to it */
    ed_line_t *head = &d->lines[sy];
    ed_line_t *last = &d->lines[ey];
    int32_t hb = u8_byte_of(head->buf, head->len, sx);
    int32_t lb = u8_byte_of(last->buf, last->len, ex);
    int32_t tail_len = last->len - lb;
    head->len = hb;
    line_drop_hl(head);
    if (tail_len > 0) {
        int r = line_insert_bytes(head, head->len, last->buf + lb, tail_len);
        if (r < 0) return r;
    }
    for (int32_t i = ey; i > sy; i--) lines_remove(d, i);
    return 0;
}

static int doc_insert_multiline(ed_doc_t *d, int32_t y, int32_t x,
                                const char *s, int32_t n,
                                int32_t *out_y, int32_t *out_x)
{
    /* First segment goes into the current line; the rest become new lines, and
       what followed the cursor rides on the last one. */
    int32_t seg_end = 0;
    while (seg_end < n && s[seg_end] != '\n') seg_end++;

    ed_line_t *l = &d->lines[y];
    int32_t at = u8_byte_of(l->buf, l->len, x);
    int32_t tail_len = l->len - at;
    char *tail = NULL;
    if (seg_end < n && tail_len > 0) {
        tail = (char *)ed_alloc((size_t)tail_len);
        if (!tail) return ED_ERR_NOMEM;
        memcpy(tail, l->buf + at, (size_t)tail_len);
        l->len = at;
        line_drop_hl(l);
    }
    int r = line_insert_bytes(&d->lines[y], at, s, seg_end);
    if (r < 0) { ed_free(tail); return r; }

    int32_t cy = y;
    int32_t pos = seg_end;
    while (pos < n) {
        pos++;  /* skip '\n' */
        int32_t e = pos;
        while (e < n && s[e] != '\n') e++;
        r = lines_insert(d, cy + 1);
        if (r < 0) { ed_free(tail); return r; }
        cy++;
        if (e > pos) {
            r = line_insert_bytes(&d->lines[cy], 0, s + pos, e - pos);
            if (r < 0) { ed_free(tail); return r; }
        }
        pos = e;
    }
    *out_y = cy;
    *out_x = u8_chars(d->lines[cy].buf, d->lines[cy].len);
    if (tail) {
        r = line_insert_bytes(&d->lines[cy], d->lines[cy].len, tail, tail_len);
        ed_free(tail);
        if (r < 0) return r;
    }
    return 0;
}

/* ---- packed records ---------------------------------------------------- */

static uint8_t g_rec[16];   /* the only shared return buffer; one call at a time */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static const char *pos_record(int32_t y, int32_t x, int *out_len)
{
    put_u32(&g_rec[0], (uint32_t)y);
    put_u32(&g_rec[4], (uint32_t)x);
    if (out_len) *out_len = 8;
    return (const char *)g_rec;
}

static int y_ok(ed_doc_t *d, int y) { return y >= 0 && y < d->count; }

/* ---- API: reading ------------------------------------------------------ */

int ec_reset(int slot)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;
    int r = doc_reset(d);
    return r < 0 ? r : 0;
}

int ec_line_count(int slot)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return 0;
    if (d->count == 0) doc_reset(d);
    return d->count;
}

int ec_line_length(int slot, int y)
{
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y)) return 0;
    return u8_chars(d->lines[y].buf, d->lines[y].len);
}

/* Visible slice of a line: characters [col0, col0+max_cols). Points straight
   into the line buffer -- valid until that line changes. */
const char *ec_render_text(int slot, int y, int col0, int max_cols, int *out_len)
{
    if (out_len) *out_len = 0;
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y) || max_cols <= 0) return "";
    ed_line_t *l = &d->lines[y];
    int32_t b0 = u8_byte_of(l->buf, l->len, col0);
    int32_t b1 = u8_byte_of(l->buf, l->len, col0 + max_cols);
    if (b1 <= b0) return "";
    if (out_len) *out_len = (int)(b1 - b0);
    return l->buf + b0;
}

/* Highlight categories for the same slice, one byte per character. Empty when
   highlighting is off for this buffer or the line has nothing to colour. */
const char *ec_render_hl(int slot, int y, int col0, int max_cols, int *out_len)
{
    if (out_len) *out_len = 0;
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y) || max_cols <= 0) return "";
    int32_t nchars = 0;
    const uint8_t *map = line_hl(d, y, &nchars);
    if (!map || col0 >= nchars) return "";
    int32_t n = nchars - col0;
    if (n > max_cols) n = max_cols;
    if (out_len) *out_len = (int)n;
    return (const char *)(map + col0);
}

/* Display width of one Unicode code point, in terminal cells.
 *
 * East Asian Wide and Fullwidth take two cells; everything else takes one.
 * This is not a complete Unicode width table and does not need to be -- it has
 * to agree with what the fonts on this machine actually draw, and misaki and
 * efontJA are exactly 1:2 across the ranges below. The one that catches people
 * out is half-width katakana (U+FF61-U+FF9F): multi-byte, but one cell.
 */
static int cp_cells(uint32_t cp)
{
    if (cp < 0x1100) return 1;                          /* ASCII and Latin */
    if (cp <= 0x115F) return 2;                         /* Hangul Jamo */
    if (cp >= 0x2E80 && cp <= 0xA4CF) return 2;         /* CJK radicals..Yi */
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 2;         /* Hangul syllables */
    if (cp >= 0xF900 && cp <= 0xFAFF) return 2;         /* CJK compatibility */
    if (cp >= 0xFF00 && cp <= 0xFF60) return 2;         /* fullwidth forms */
    /* Half-width katakana is one cell by name and in misaki, but efontJA_12 --
       the font the edit area uses -- has no glyph for it and draws a
       full-width box. Measured, not assumed: flash/app/test/ja_width.app.rb
       renders "ｱｲｳｴｵ|" 66px wide in efont, exactly as five kanji. The grid has
       to agree with what appears on screen or the cursor drifts. */
    if (cp >= 0xFF61 && cp <= 0xFF9F) return 2;         /* half-width katakana */
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 2;         /* fullwidth signs */
    return 1;
}

/* Decode the UTF-8 sequence starting at s (len bytes available). */
static uint32_t u8_decode(const char *s, int32_t len, int32_t *out_bytes)
{
    unsigned char b0 = (unsigned char)s[0];
    int32_t need = 1;
    uint32_t cp = b0;
    if (b0 >= 0xF0)      { need = 4; cp = b0 & 0x07; }
    else if (b0 >= 0xE0) { need = 3; cp = b0 & 0x0F; }
    else if (b0 >= 0xC0) { need = 2; cp = b0 & 0x1F; }
    if (need > len) need = 1;                 /* truncated: treat as one byte */
    for (int32_t i = 1; i < need; i++) {
        cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
    }
    if (out_bytes) *out_bytes = need;
    return (need == 1 && b0 >= 0x80) ? 0xFFFD : cp;
}

/* Cell width per character for the same slice ec_render_text returns, one byte
   per character, in the shape ec_render_hl uses. The editor turns this into the
   prefix sums it needs to place a cursor, a selection box or a click.
   Recomputed per call rather than cached: a screen row is at most a couple of
   hundred characters and the walk is a byte scan. */
const char *ec_render_width(int slot, int y, int col0, int max_cols, int *out_len)
{
    if (out_len) *out_len = 0;
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y) || max_cols <= 0) return "";
    ed_line_t *l = &d->lines[y];
    int32_t b0 = u8_byte_of(l->buf, l->len, col0);
    if (b0 >= l->len) return "";

    static uint8_t widths[EC_WIDTH_MAX_COLS];
    int32_t n = 0;
    int32_t i = b0;
    while (i < l->len && n < max_cols && n < EC_WIDTH_MAX_COLS) {
        int32_t used = 1;
        uint32_t cp = u8_decode(l->buf + i, l->len - i, &used);
        widths[n++] = (uint8_t)cp_cells(cp);
        i += used;
    }
    if (out_len) *out_len = (int)n;
    return (const char *)widths;
}

/* ---- line wrapping ----------------------------------------------------- */
/*
 * Where a line breaks when folded into a window +view_cells+ wide.
 *
 * Two entry points rather than one that returns a list: an int in and an int
 * out needs no record format and no binary parsing on either binding, and the
 * cache below makes the repeated calls a lookup. A screen row asks for the
 * start of its own segment and of the next one.
 *
 * The whole document is never measured. Drawing walks forward from the anchor
 * and only ever asks about the lines it is about to put on screen.
 */
#define EC_WRAP_CACHE_N    24     /* a screenful of rows, plus slack */
#define EC_WRAP_CACHE_SEGS 32     /* starts kept per line; ~2000 ASCII chars */

typedef struct {
    int      used;
    int      slot;
    int32_t  y;
    int32_t  cells;
    uint32_t stamp;
    int32_t  count;                        /* segments in the whole line */
    int32_t  cached;                       /* entries valid in starts[] */
    uint16_t starts[EC_WRAP_CACHE_SEGS];
} ed_wrap_entry_t;

static ed_wrap_entry_t g_wrap[EC_WRAP_CACHE_N];
static int             g_wrap_next;        /* round-robin victim */

/* Walk the line once, filling in the entry. A line longer than
   EC_WRAP_CACHE_SEGS segments still gets a correct count; only the tail of its
   starts[] is missing, and ec_wrap_start walks for those. */
static void wrap_fill(ed_doc_t *d, int slot, int32_t y, int32_t cells,
                      ed_wrap_entry_t *e)
{
    ed_line_t *l = &d->lines[y];
    e->used = 1;
    e->slot = slot;
    e->y = y;
    e->cells = cells;
    e->stamp = g_wrap_stamp;
    e->count = 1;
    e->cached = 1;
    e->starts[0] = 0;

    int32_t used = 0;
    int32_t ci = 0;
    int32_t i = 0;
    while (i < l->len) {
        int32_t nb = 1;
        uint32_t cp = u8_decode(l->buf + i, l->len - i, &nb);
        int32_t w = cp_cells(cp);
        /* Break before the character that does not fit, so a two-cell glyph is
           never split. A character wider than the whole window still has to go
           somewhere: it stays on an otherwise empty segment. */
        if (used > 0 && used + w > cells) {
            e->count++;
            if (e->cached < EC_WRAP_CACHE_SEGS) {
                e->starts[e->cached++] = (uint16_t)ci;
            }
            used = 0;
        }
        used += w;
        i += nb;
        ci++;
    }
}

static ed_wrap_entry_t *wrap_entry(int slot, int y, int cells)
{
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y)) return NULL;
    if (cells < 1) cells = 1;

    for (int i = 0; i < EC_WRAP_CACHE_N; i++) {
        ed_wrap_entry_t *e = &g_wrap[i];
        if (e->used && e->slot == slot && e->y == y &&
            e->cells == cells && e->stamp == g_wrap_stamp) {
            return e;
        }
    }
    ed_wrap_entry_t *e = &g_wrap[g_wrap_next];
    g_wrap_next = (g_wrap_next + 1) % EC_WRAP_CACHE_N;
    wrap_fill(d, slot, y, cells, e);
    return e;
}

/* Number of screen rows line y occupies at this width (at least 1). */
int ec_wrap_count(int slot, int y, int view_cells)
{
    ed_wrap_entry_t *e = wrap_entry(slot, y, view_cells);
    return e ? (int)e->count : 1;
}

/* Character index where segment +seg+ starts. seg >= count answers the line
   length, so a caller can ask for "the start of the next one" to get an end. */
int ec_wrap_start(int slot, int y, int view_cells, int seg)
{
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y)) return 0;
    ed_line_t *l = &d->lines[y];
    if (seg <= 0) return 0;

    ed_wrap_entry_t *e = wrap_entry(slot, y, view_cells);
    if (!e) return 0;
    if (seg >= e->count) return (int)u8_chars(l->buf, l->len);
    if (seg < e->cached) return (int)e->starts[seg];

    /* Past what the entry kept: walk to it. Only pathologically long lines
       reach this, and only for their tail. */
    int32_t cells = e->cells;
    int32_t used = 0, ci = 0, i = 0, s = 0;
    while (i < l->len) {
        int32_t nb = 1;
        uint32_t cp = u8_decode(l->buf + i, l->len - i, &nb);
        int32_t w = cp_cells(cp);
        if (used > 0 && used + w > cells) {
            s++;
            if (s == seg) return (int)ci;
            used = 0;
        }
        used += w;
        i += nb;
        ci++;
    }
    return (int)ci;
}

/* The character under the cursor ("" past end of line). */
const char *ec_char_at(int slot, int y, int x, int *out_len)
{
    if (out_len) *out_len = 0;
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y)) return "";
    ed_line_t *l = &d->lines[y];
    int32_t b0 = u8_byte_of(l->buf, l->len, x);
    if (b0 >= l->len) return "";
    int32_t b1 = u8_byte_of(l->buf, l->len, x + 1);
    if (out_len) *out_len = (int)(b1 - b0);
    return l->buf + b0;
}

void ec_set_hl(int slot, int on)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return;
    on = on ? 1 : 0;
    if (d->hl_on == on) return;
    d->hl_on = on;
    for (int32_t i = 0; i < d->count; i++) line_drop_hl(&d->lines[i]);
}

int ec_doc_bytesize(int slot)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return 0;
    int32_t total = 0;
    for (int32_t i = 0; i < d->count; i++) total += d->lines[i].len;
    total += d->count - 1;  /* newlines between lines */
    return total;
}

int ec_mem_used(void)
{
    fmrb_pool_stats_t st;
    if (g_handle < 0) return 0;
    if (fmrb_mem_get_stats(g_handle, &st) != 0) return 0;
    return (int)st.used_size;
}

/* ---- API: editing ------------------------------------------------------ */

int ec_insert_text(int slot, int y, int x, const char *s, int len)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;
    if (!y_ok(d, y)) return EC_ERR_RANGE;
    ed_line_t *l = &d->lines[y];
    int32_t at = u8_byte_of(l->buf, l->len, x);
    int r = line_insert_bytes(l, at, s, len);
    if (r < 0) return r;
    return x + u8_chars(s, len);
}

int ec_split_line(int slot, int y, int x)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;
    if (!y_ok(d, y)) return EC_ERR_RANGE;
    int32_t at = u8_byte_of(d->lines[y].buf, d->lines[y].len, x);
    int32_t tail = d->lines[y].len - at;
    int r = lines_insert(d, y + 1);
    if (r < 0) return r;
    ed_line_t *src = &d->lines[y];       /* the array may have moved */
    ed_line_t *dst = &d->lines[y + 1];
    if (tail > 0) {
        r = line_insert_bytes(dst, 0, src->buf + at, tail);
        if (r < 0) return r;
        src->len = at;
        line_drop_hl(src);
    }
    return 0;
}

/* Append line y+1 to line y and drop it. Returns the character length line y
   had before the join, which is where the cursor belongs. */
int ec_join_line(int slot, int y)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;
    if (!y_ok(d, y) || y + 1 >= d->count) return EC_ERR_RANGE;
    ed_line_t *a = &d->lines[y];
    ed_line_t *b = &d->lines[y + 1];
    int32_t prev_chars = u8_chars(a->buf, a->len);
    if (b->len > 0) {
        int r = line_insert_bytes(a, a->len, b->buf, b->len);
        if (r < 0) return r;
    }
    lines_remove(d, y + 1);
    return prev_chars;
}

int ec_delete_char(int slot, int y, int x)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;
    if (!y_ok(d, y)) return EC_ERR_RANGE;
    ed_line_t *l = &d->lines[y];
    int32_t b0 = u8_byte_of(l->buf, l->len, x);
    if (b0 >= l->len) return 0;
    int32_t b1 = u8_byte_of(l->buf, l->len, x + 1);
    line_delete_bytes(l, b0, b1 - b0);
    return 0;
}

int ec_delete_range(int slot, int sy, int sx, int ey, int ex)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;
    return doc_delete_range(d, sy, sx, ey, ex);
}

const char *ec_insert_multiline(int slot, int y, int x, const char *s, int len,
                                int *out_len)
{
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y)) return pos_record(y, x, out_len);
    int32_t ny = y, nx = x;
    int r = doc_insert_multiline(d, y, x, s, len, &ny, &nx);
    if (r < 0) return pos_record(y, x, out_len);
    return pos_record(ny, nx, out_len);
}

/* ---- API: file I/O ----------------------------------------------------- */

/* Read in chunks and append lines as they complete, so peak memory is the
   document itself -- not the document plus a whole-file copy. */
int ec_load_file(int slot, const char *path)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;

    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &f) != FMRB_OK) return EC_ERR_IO;
    if (doc_reset(d) < 0) {
        fmrb_hal_file_close(f);
        return EC_ERR_NOMEM;
    }
    char *chunk = (char *)ed_alloc(ED_IO_CHUNK);
    if (!chunk) {
        fmrb_hal_file_close(f);
        return EC_ERR_NOMEM;
    }

    int32_t cur = 0;
    int rc = 0;
    for (;;) {
        size_t got = 0;
        if (fmrb_hal_file_read(f, chunk, ED_IO_CHUNK, &got) != FMRB_OK) { rc = EC_ERR_IO; break; }
        if (got == 0) break;
        int32_t start = 0;
        for (size_t i = 0; i < got; i++) {
            if (chunk[i] == '\n') {
                if ((int32_t)i > start) {
                    rc = line_insert_bytes(&d->lines[cur], d->lines[cur].len,
                                           chunk + start, (int32_t)i - start);
                    if (rc < 0) break;
                }
                rc = lines_insert(d, cur + 1);
                if (rc < 0) break;
                cur++;
                start = (int32_t)i + 1;
            }
        }
        if (rc < 0) break;
        if ((size_t)start < got) {
            rc = line_insert_bytes(&d->lines[cur], d->lines[cur].len,
                                   chunk + start, (int32_t)(got - (size_t)start));
            if (rc < 0) break;
        }
    }
    ed_free(chunk);
    fmrb_hal_file_close(f);

    if (rc < 0) {
        doc_reset(d);   /* leave a usable empty buffer, not a half-read one */
        return rc;
    }
    /* A trailing newline leaves an empty last line, which the Ruby split("\n")
       did not produce -- drop it so line counts match. */
    if (d->count > 1 && d->lines[d->count - 1].len == 0) {
        lines_remove(d, d->count - 1);
    }
    return d->count;
}

int ec_save_file(int slot, const char *path)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;

    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(path, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &f) != FMRB_OK) {
        return EC_ERR_IO;
    }
    int32_t written = 0;
    int rc = 0;
    for (int32_t i = 0; i < d->count; i++) {
        size_t w = 0;
        if (d->lines[i].len > 0) {
            if (fmrb_hal_file_write(f, d->lines[i].buf, (size_t)d->lines[i].len, &w) != FMRB_OK) {
                rc = EC_ERR_IO; break;
            }
            written += (int32_t)w;
        }
        if (i + 1 < d->count) {
            if (fmrb_hal_file_write(f, "\n", 1, &w) != FMRB_OK) { rc = EC_ERR_IO; break; }
            written += (int32_t)w;
        }
    }
    fmrb_hal_file_close(f);
    return rc < 0 ? rc : written;
}

/* ---- API: search ------------------------------------------------------- */

/* Forward search from (from_y, from_x), wrapping once. +after+ skips the
   character at the cursor (Find Next). Queries do not span lines: the search
   dialog is a single-line field. */
const char *ec_find(int slot, const char *q, int qlen, int from_y, int from_x,
                    int after, int *out_len)
{
    memset(g_rec, 0, 9);
    if (out_len) *out_len = 9;
    ed_doc_t *d = doc_of(slot);
    if (!d || qlen <= 0) return (const char *)g_rec;

    int32_t start_y = from_y;
    if (start_y < 0) start_y = 0;
    if (start_y >= d->count) start_y = d->count - 1;
    int32_t start_x = from_x + (after ? 1 : 0);

    for (int pass = 0; pass < 2; pass++) {
        int32_t y0 = (pass == 0) ? start_y : 0;
        int32_t y1 = (pass == 0) ? d->count - 1 : start_y;
        for (int32_t y = y0; y <= y1 && y < d->count; y++) {
            ed_line_t *l = &d->lines[y];
            int32_t from_b = 0;
            if (pass == 0 && y == start_y) from_b = u8_byte_of(l->buf, l->len, start_x);
            for (int32_t b = from_b; b + qlen <= l->len; b++) {
                if (memcmp(l->buf + b, q, (size_t)qlen) == 0) {
                    g_rec[0] = 1;
                    put_u32(&g_rec[1], (uint32_t)y);
                    put_u32(&g_rec[5], (uint32_t)u8_chars(l->buf, b));
                    return (const char *)g_rec;
                }
            }
        }
    }
    return (const char *)g_rec;
}

/* ---- API: clipboard ---------------------------------------------------- */

int ec_copy_range(int slot, int sy, int sx, int ey, int ex)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return EC_ERR_NOMEM;
    if (sy < 0 || ey >= d->count || sy > ey) return EC_ERR_RANGE;

    int32_t total = 0;
    for (int32_t y = sy; y <= ey; y++) {
        ed_line_t *l = &d->lines[y];
        int32_t b0 = (y == sy) ? u8_byte_of(l->buf, l->len, sx) : 0;
        int32_t b1 = (y == ey) ? u8_byte_of(l->buf, l->len, ex) : l->len;
        if (b1 > b0) total += b1 - b0;
        if (y < ey) total += 1;
    }
    char *buf = (total > 0) ? (char *)ed_alloc((size_t)total) : NULL;
    if (total > 0 && !buf) return EC_ERR_NOMEM;

    int32_t at = 0;
    for (int32_t y = sy; y <= ey; y++) {
        ed_line_t *l = &d->lines[y];
        int32_t b0 = (y == sy) ? u8_byte_of(l->buf, l->len, sx) : 0;
        int32_t b1 = (y == ey) ? u8_byte_of(l->buf, l->len, ex) : l->len;
        if (b1 > b0) {
            memcpy(buf + at, l->buf + b0, (size_t)(b1 - b0));
            at += b1 - b0;
        }
        if (y < ey) buf[at++] = '\n';
    }
    ed_free(d->clip);
    d->clip = buf;
    d->clip_len = total;
    return total;
}

const char *ec_paste_at(int slot, int y, int x, int *out_len)
{
    ed_doc_t *d = doc_of(slot);
    if (!d || !y_ok(d, y) || d->clip_len == 0) return pos_record(y, x, out_len);
    int32_t ny = y, nx = x;
    int r = doc_insert_multiline(d, y, x, d->clip, d->clip_len, &ny, &nx);
    if (r < 0) return pos_record(y, x, out_len);
    return pos_record(ny, nx, out_len);
}

int ec_clipboard_length(int slot)
{
    ed_doc_t *d = doc_of(slot);
    if (!d) return 0;
    return d->clip_len;
}
