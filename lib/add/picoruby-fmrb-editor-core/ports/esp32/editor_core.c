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
 * One document per VM: the slot is keyed by mrb_state, so a second editor
 * instance gets its own document without the API carrying a handle. A VM beyond
 * ED_MAX_DOCS gets -ENOMEM from every call, which the Ruby side reports.
 */

#include <string.h>
#include <stdint.h>

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/class.h>

#include "fmrb_mem.h"
#include "fmrb_hal_file.h"
#include "fmrb_log.h"

#include "../../include/picoruby_fmrb_editor_core.h"

static const char *TAG = "editor_core";

/* Per-byte highlight categories for one line of Ruby, from the Prism lexer in
   picoruby-syntax-highlight. Declared here rather than in a shared header: both
   gems are linked into the same firmware, and adding an exported include path
   to the mruby gem build is more machinery than one prototype deserves. */
extern int fmrb_syntax_highlight_line(const char *src, size_t len, uint8_t *out_map);

#define ED_ERR_RANGE  (-1)
#define ED_ERR_NOMEM  (-2)
#define ED_ERR_IO     (-3)

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
    mrb_state *owner;
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

static ed_doc_t *doc_of(mrb_state *mrb)
{
    for (int i = 0; i < ED_MAX_DOCS; i++) {
        if (g_docs[i].owner == mrb) return &g_docs[i];
    }
    for (int i = 0; i < ED_MAX_DOCS; i++) {
        if (g_docs[i].owner == NULL) {
            g_docs[i].owner = mrb;
            g_docs[i].hl_on = 1;
            return &g_docs[i];
        }
    }
    return NULL;  /* more editors than slots: callers report -ENOMEM */
}

static void doc_release(mrb_state *mrb)
{
    for (int i = 0; i < ED_MAX_DOCS; i++) {
        if (g_docs[i].owner == mrb) {
            doc_clear(&g_docs[i]);
            ed_free(g_docs[i].clip);
            g_docs[i].clip = NULL;
            g_docs[i].clip_len = 0;
            g_docs[i].owner = NULL;
            return;
        }
    }
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

static void line_drop_hl(ed_line_t *l)
{
    ed_free(l->hl);
    l->hl = NULL;
    l->hl_chars = 0;
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

/* ---- packed records ---------------------------------------------------- */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static mrb_value pos_record(mrb_state *mrb, int32_t y, int32_t x)
{
    uint8_t rec[8];
    put_u32(&rec[0], (uint32_t)y);
    put_u32(&rec[4], (uint32_t)x);
    return mrb_str_new(mrb, (const char *)rec, sizeof(rec));
}

/* ---- guards ------------------------------------------------------------ */

#define DOC_OR_RETURN(d, mrb, retval)          \
    ed_doc_t *d = doc_of(mrb);                 \
    if (!d || d->count == 0) return (retval);

static int y_ok(ed_doc_t *d, mrb_int y) { return y >= 0 && y < d->count; }

/* ---- API: reading ------------------------------------------------------ */

static mrb_value ec_reset(mrb_state *mrb, mrb_value self)
{
    ed_doc_t *d = doc_of(mrb);
    if (!d) return mrb_fixnum_value(ED_ERR_NOMEM);
    int r = doc_reset(d);
    return mrb_fixnum_value(r < 0 ? r : 0);
}

static mrb_value ec_line_count(mrb_state *mrb, mrb_value self)
{
    ed_doc_t *d = doc_of(mrb);
    if (!d) return mrb_fixnum_value(0);
    if (d->count == 0) doc_reset(d);
    return mrb_fixnum_value(d->count);
}

static mrb_value ec_line_length(mrb_state *mrb, mrb_value self)
{
    mrb_int y;
    mrb_get_args(mrb, "i", &y);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(0))
    if (!y_ok(d, y)) return mrb_fixnum_value(0);
    return mrb_fixnum_value(u8_chars(d->lines[y].buf, d->lines[y].len));
}

/* Visible slice of a line: characters [col0, col0+max_cols). */
static mrb_value ec_render_text(mrb_state *mrb, mrb_value self)
{
    mrb_int y, col0, max_cols;
    mrb_get_args(mrb, "iii", &y, &col0, &max_cols);
    DOC_OR_RETURN(d, mrb, mrb_str_new(mrb, "", 0))
    if (!y_ok(d, y) || max_cols <= 0) return mrb_str_new(mrb, "", 0);
    ed_line_t *l = &d->lines[y];
    int32_t b0 = u8_byte_of(l->buf, l->len, (int32_t)col0);
    int32_t b1 = u8_byte_of(l->buf, l->len, (int32_t)(col0 + max_cols));
    if (b1 <= b0) return mrb_str_new(mrb, "", 0);
    return mrb_str_new(mrb, l->buf + b0, (size_t)(b1 - b0));
}

/* Highlight categories for the same slice, one byte per character. Empty when
   highlighting is off for this buffer or the line has nothing to colour. */
static mrb_value ec_render_hl(mrb_state *mrb, mrb_value self)
{
    mrb_int y, col0, max_cols;
    mrb_get_args(mrb, "iii", &y, &col0, &max_cols);
    DOC_OR_RETURN(d, mrb, mrb_str_new(mrb, "", 0))
    if (!y_ok(d, y) || max_cols <= 0) return mrb_str_new(mrb, "", 0);
    int32_t nchars = 0;
    const uint8_t *map = line_hl(d, (int32_t)y, &nchars);
    if (!map) return mrb_str_new(mrb, "", 0);
    if (col0 >= nchars) return mrb_str_new(mrb, "", 0);
    int32_t n = nchars - (int32_t)col0;
    if (n > max_cols) n = (int32_t)max_cols;
    return mrb_str_new(mrb, (const char *)(map + col0), (size_t)n);
}

/* The character under the cursor, as a 1-character String ("" past EOL). */
static mrb_value ec_char_at(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    DOC_OR_RETURN(d, mrb, mrb_str_new(mrb, "", 0))
    if (!y_ok(d, y)) return mrb_str_new(mrb, "", 0);
    ed_line_t *l = &d->lines[y];
    int32_t b0 = u8_byte_of(l->buf, l->len, (int32_t)x);
    if (b0 >= l->len) return mrb_str_new(mrb, "", 0);
    int32_t b1 = u8_byte_of(l->buf, l->len, (int32_t)x + 1);
    return mrb_str_new(mrb, l->buf + b0, (size_t)(b1 - b0));
}

static mrb_value ec_hl_set(mrb_state *mrb, mrb_value self)
{
    mrb_bool on;
    mrb_get_args(mrb, "b", &on);
    ed_doc_t *d = doc_of(mrb);
    if (!d) return mrb_nil_value();
    if (d->hl_on != (on ? 1 : 0)) {
        d->hl_on = on ? 1 : 0;
        for (int32_t i = 0; i < d->count; i++) line_drop_hl(&d->lines[i]);
    }
    return mrb_nil_value();
}

static mrb_value ec_doc_bytesize(mrb_state *mrb, mrb_value self)
{
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(0))
    int32_t total = 0;
    for (int32_t i = 0; i < d->count; i++) total += d->lines[i].len;
    total += d->count - 1;  /* newlines between lines */
    return mrb_fixnum_value(total);
}

static mrb_value ec_mem_used(mrb_state *mrb, mrb_value self)
{
    fmrb_pool_stats_t st;
    if (g_handle < 0) return mrb_fixnum_value(0);
    if (fmrb_mem_get_stats(g_handle, &st) != 0) return mrb_fixnum_value(0);
    return mrb_fixnum_value((mrb_int)st.used_size);
}

/* ---- API: editing ------------------------------------------------------ */

static mrb_value ec_insert_text(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_value str;
    mrb_get_args(mrb, "iiS", &y, &x, &str);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(ED_ERR_NOMEM))
    if (!y_ok(d, y)) return mrb_fixnum_value(ED_ERR_RANGE);
    ed_line_t *l = &d->lines[y];
    int32_t at = u8_byte_of(l->buf, l->len, (int32_t)x);
    int32_t n  = (int32_t)RSTRING_LEN(str);
    int r = line_insert_bytes(l, at, RSTRING_PTR(str), n);
    if (r < 0) return mrb_fixnum_value(r);
    return mrb_fixnum_value(x + u8_chars(RSTRING_PTR(str), n));
}

static mrb_value ec_split_line(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(ED_ERR_NOMEM))
    if (!y_ok(d, y)) return mrb_fixnum_value(ED_ERR_RANGE);
    int32_t at = u8_byte_of(d->lines[y].buf, d->lines[y].len, (int32_t)x);
    int32_t tail = d->lines[y].len - at;
    int r = lines_insert(d, (int32_t)y + 1);
    if (r < 0) return mrb_fixnum_value(r);
    ed_line_t *src = &d->lines[y];        /* after insert: array may have moved */
    ed_line_t *dst = &d->lines[y + 1];
    if (tail > 0) {
        r = line_insert_bytes(dst, 0, src->buf + at, tail);
        if (r < 0) return mrb_fixnum_value(r);
        src->len = at;
        line_drop_hl(src);
    }
    return mrb_fixnum_value(0);
}

/* Append line y+1 to line y and drop it. Returns the character length line y
   had before the join, which is where the cursor belongs. */
static mrb_value ec_join_line(mrb_state *mrb, mrb_value self)
{
    mrb_int y;
    mrb_get_args(mrb, "i", &y);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(ED_ERR_NOMEM))
    if (!y_ok(d, y) || y + 1 >= d->count) return mrb_fixnum_value(ED_ERR_RANGE);
    ed_line_t *a = &d->lines[y];
    ed_line_t *b = &d->lines[y + 1];
    int32_t prev_chars = u8_chars(a->buf, a->len);
    if (b->len > 0) {
        int r = line_insert_bytes(a, a->len, b->buf, b->len);
        if (r < 0) return mrb_fixnum_value(r);
    }
    lines_remove(d, (int32_t)y + 1);
    return mrb_fixnum_value(prev_chars);
}

static mrb_value ec_delete_char(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(ED_ERR_NOMEM))
    if (!y_ok(d, y)) return mrb_fixnum_value(ED_ERR_RANGE);
    ed_line_t *l = &d->lines[y];
    int32_t b0 = u8_byte_of(l->buf, l->len, (int32_t)x);
    if (b0 >= l->len) return mrb_fixnum_value(0);
    int32_t b1 = u8_byte_of(l->buf, l->len, (int32_t)x + 1);
    line_delete_bytes(l, b0, b1 - b0);
    return mrb_fixnum_value(0);
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

static mrb_value ec_delete_range(mrb_state *mrb, mrb_value self)
{
    mrb_int sy, sx, ey, ex;
    mrb_get_args(mrb, "iiii", &sy, &sx, &ey, &ex);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(ED_ERR_NOMEM))
    int r = doc_delete_range(d, (int32_t)sy, (int32_t)sx, (int32_t)ey, (int32_t)ex);
    return mrb_fixnum_value(r);
}

/* Insert text that may contain newlines. Returns packed [new_y, new_x]. */
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

static mrb_value ec_insert_multiline(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_value str;
    mrb_get_args(mrb, "iiS", &y, &x, &str);
    DOC_OR_RETURN(d, mrb, pos_record(mrb, 0, 0))
    if (!y_ok(d, y)) return pos_record(mrb, 0, 0);
    int32_t ny = (int32_t)y, nx = (int32_t)x;
    int r = doc_insert_multiline(d, (int32_t)y, (int32_t)x,
                                RSTRING_PTR(str), (int32_t)RSTRING_LEN(str),
                                &ny, &nx);
    if (r < 0) return pos_record(mrb, (uint32_t)y, (uint32_t)x);
    return pos_record(mrb, ny, nx);
}

/* ---- API: file I/O ----------------------------------------------------- */

/* Read the file in chunks and append lines as they complete, so peak memory is
   the document itself -- not the document plus a whole-file copy. */
static mrb_value ec_load_file(mrb_state *mrb, mrb_value self)
{
    mrb_value path;
    mrb_get_args(mrb, "S", &path);
    ed_doc_t *d = doc_of(mrb);
    if (!d) return mrb_fixnum_value(ED_ERR_NOMEM);

    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(RSTRING_CSTR(mrb, path), FMRB_O_RDONLY, &f) != FMRB_OK) {
        return mrb_fixnum_value(ED_ERR_IO);
    }
    if (doc_reset(d) < 0) {
        fmrb_hal_file_close(f);
        return mrb_fixnum_value(ED_ERR_NOMEM);
    }

    char *chunk = (char *)ed_alloc(ED_IO_CHUNK);
    if (!chunk) {
        fmrb_hal_file_close(f);
        return mrb_fixnum_value(ED_ERR_NOMEM);
    }

    int32_t cur = 0;           /* index of the line being filled */
    int rc = 0;
    for (;;) {
        size_t got = 0;
        if (fmrb_hal_file_read(f, chunk, ED_IO_CHUNK, &got) != FMRB_OK) { rc = ED_ERR_IO; break; }
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
        /* Leave a usable empty buffer rather than a half-read one. */
        doc_reset(d);
        return mrb_fixnum_value(rc);
    }
    /* A trailing newline leaves an empty last line, which is what the Ruby
       split("\n") did not produce -- drop it to keep line counts identical. */
    if (d->count > 1 && d->lines[d->count - 1].len == 0) {
        lines_remove(d, d->count - 1);
    }
    return mrb_fixnum_value(d->count);
}

static mrb_value ec_save_file(mrb_state *mrb, mrb_value self)
{
    mrb_value path;
    mrb_get_args(mrb, "S", &path);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(ED_ERR_NOMEM))

    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(RSTRING_CSTR(mrb, path),
                           FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &f) != FMRB_OK) {
        return mrb_fixnum_value(ED_ERR_IO);
    }
    int32_t written = 0;
    int rc = 0;
    for (int32_t i = 0; i < d->count; i++) {
        size_t w = 0;
        if (d->lines[i].len > 0) {
            if (fmrb_hal_file_write(f, d->lines[i].buf, (size_t)d->lines[i].len, &w) != FMRB_OK) {
                rc = ED_ERR_IO; break;
            }
            written += (int32_t)w;
        }
        if (i + 1 < d->count) {
            if (fmrb_hal_file_write(f, "\n", 1, &w) != FMRB_OK) { rc = ED_ERR_IO; break; }
            written += (int32_t)w;
        }
    }
    fmrb_hal_file_close(f);
    if (rc < 0) return mrb_fixnum_value(rc);
    return mrb_fixnum_value(written);
}

/* ---- API: search ------------------------------------------------------- */

/* Forward search from (from_y, from_x), wrapping once. +after+ skips the
   character at the cursor (Find Next). Queries do not span lines: the search
   dialog is a single-line field. Returns packed [found, y, x] (found in the
   first byte of the y slot is avoided -- see below: separate 1-byte flag). */
static mrb_value ec_find(mrb_state *mrb, mrb_value self)
{
    mrb_value q;
    mrb_int from_y, from_x;
    mrb_bool after;
    mrb_get_args(mrb, "Siib", &q, &from_y, &from_x, &after);
    DOC_OR_RETURN(d, mrb, mrb_str_new(mrb, "\0\0\0\0\0\0\0\0\0", 9))

    const char *qs = RSTRING_PTR(q);
    int32_t ql = (int32_t)RSTRING_LEN(q);
    uint8_t rec[9];
    memset(rec, 0, sizeof(rec));
    if (ql == 0) return mrb_str_new(mrb, (const char *)rec, sizeof(rec));

    int32_t start_y = (int32_t)from_y;
    if (start_y < 0) start_y = 0;
    if (start_y >= d->count) start_y = d->count - 1;
    int32_t start_x = (int32_t)from_x + (after ? 1 : 0);

    for (int32_t pass = 0; pass < 2; pass++) {
        int32_t y0 = (pass == 0) ? start_y : 0;
        int32_t y1 = (pass == 0) ? d->count - 1 : start_y;
        for (int32_t y = y0; y <= y1 && y < d->count; y++) {
            ed_line_t *l = &d->lines[y];
            int32_t from_b = 0;
            if (pass == 0 && y == start_y) from_b = u8_byte_of(l->buf, l->len, start_x);
            for (int32_t b = from_b; b + ql <= l->len; b++) {
                if (memcmp(l->buf + b, qs, (size_t)ql) == 0) {
                    rec[0] = 1;
                    put_u32(&rec[1], (uint32_t)y);
                    put_u32(&rec[5], (uint32_t)u8_chars(l->buf, b));
                    return mrb_str_new(mrb, (const char *)rec, sizeof(rec));
                }
            }
        }
    }
    return mrb_str_new(mrb, (const char *)rec, sizeof(rec));
}

/* ---- API: clipboard ---------------------------------------------------- */

static mrb_value ec_copy_range(mrb_state *mrb, mrb_value self)
{
    mrb_int sy, sx, ey, ex;
    mrb_get_args(mrb, "iiii", &sy, &sx, &ey, &ex);
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(ED_ERR_NOMEM))
    if (sy < 0 || ey >= d->count || sy > ey) return mrb_fixnum_value(ED_ERR_RANGE);

    /* size first, then one allocation */
    int32_t total = 0;
    for (int32_t y = (int32_t)sy; y <= (int32_t)ey; y++) {
        ed_line_t *l = &d->lines[y];
        int32_t b0 = (y == sy) ? u8_byte_of(l->buf, l->len, (int32_t)sx) : 0;
        int32_t b1 = (y == ey) ? u8_byte_of(l->buf, l->len, (int32_t)ex) : l->len;
        if (b1 > b0) total += b1 - b0;
        if (y < ey) total += 1;  /* newline */
    }
    char *buf = (total > 0) ? (char *)ed_alloc((size_t)total) : NULL;
    if (total > 0 && !buf) return mrb_fixnum_value(ED_ERR_NOMEM);

    int32_t at = 0;
    for (int32_t y = (int32_t)sy; y <= (int32_t)ey; y++) {
        ed_line_t *l = &d->lines[y];
        int32_t b0 = (y == sy) ? u8_byte_of(l->buf, l->len, (int32_t)sx) : 0;
        int32_t b1 = (y == ey) ? u8_byte_of(l->buf, l->len, (int32_t)ex) : l->len;
        if (b1 > b0) {
            memcpy(buf + at, l->buf + b0, (size_t)(b1 - b0));
            at += b1 - b0;
        }
        if (y < ey) buf[at++] = '\n';
    }
    ed_free(d->clip);
    d->clip = buf;
    d->clip_len = total;
    return mrb_fixnum_value(total);
}

static mrb_value ec_paste_at(mrb_state *mrb, mrb_value self)
{
    mrb_int y, x;
    mrb_get_args(mrb, "ii", &y, &x);
    DOC_OR_RETURN(d, mrb, pos_record(mrb, 0, 0))
    if (!y_ok(d, y) || d->clip_len == 0) return pos_record(mrb, (int32_t)y, (int32_t)x);
    int32_t ny = (int32_t)y, nx = (int32_t)x;
    int r = doc_insert_multiline(d, (int32_t)y, (int32_t)x, d->clip, d->clip_len, &ny, &nx);
    if (r < 0) return pos_record(mrb, (int32_t)y, (int32_t)x);
    return pos_record(mrb, ny, nx);
}

static mrb_value ec_clipboard_length(mrb_state *mrb, mrb_value self)
{
    DOC_OR_RETURN(d, mrb, mrb_fixnum_value(0))
    return mrb_fixnum_value(d->clip_len);
}

/* ---- gem hooks --------------------------------------------------------- */

void mrb_picoruby_fmrb_editor_core_init_impl(mrb_state *mrb)
{
    struct RClass *mod = mrb_define_module(mrb, "EditorCore");

    mrb_define_module_function(mrb, mod, "reset",       ec_reset,       MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "line_count",  ec_line_count,  MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "line_length", ec_line_length, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "render_text", ec_render_text, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "render_hl",   ec_render_hl,   MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "char_at",     ec_char_at,     MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "hl_enabled=", ec_hl_set,      MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "doc_bytesize", ec_doc_bytesize, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mod, "mem_used",    ec_mem_used,    MRB_ARGS_NONE());

    mrb_define_module_function(mrb, mod, "insert_text", ec_insert_text, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, mod, "split_line",  ec_split_line,  MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "join_line",   ec_join_line,   MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "delete_char", ec_delete_char, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "delete_range", ec_delete_range, MRB_ARGS_REQ(4));
    mrb_define_module_function(mrb, mod, "insert_multiline", ec_insert_multiline, MRB_ARGS_REQ(3));

    mrb_define_module_function(mrb, mod, "load_file",   ec_load_file,   MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "save_file",   ec_save_file,   MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mod, "find",        ec_find,        MRB_ARGS_REQ(4));

    mrb_define_module_function(mrb, mod, "copy_range",  ec_copy_range,  MRB_ARGS_REQ(4));
    mrb_define_module_function(mrb, mod, "paste_at",    ec_paste_at,    MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, mod, "clipboard_length", ec_clipboard_length, MRB_ARGS_NONE());
}

void mrb_picoruby_fmrb_editor_core_final_impl(mrb_state *mrb)
{
    doc_release(mrb);
}
