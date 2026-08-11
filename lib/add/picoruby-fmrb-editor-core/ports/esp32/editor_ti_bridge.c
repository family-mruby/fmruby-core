/*
 * EditorTi: the editor's side of the type inference engine (picoruby-ti).
 *
 * The engine answers "what can follow the cursor" from a flat source string
 * and a byte offset, so this file turns the editor's document (lines, columns
 * in characters) into that shape, calls the engine, and copies the answer out
 * -- the engine hands back pointers into its own 16KB arena, which the next
 * request resets.
 *
 * Slots and the (pointer, length) string convention are EditorCore's, so the
 * mruby binding and the Spinel FFI both reach this the same way they reach the
 * document model (see editor_core_api.h).
 *
 * Memory. A parse is far bigger than the text it parses: measured at ~18x the
 * source on a 64-bit host and ~22x for ordinary app code, with dense literals
 * reaching 100x (doc/editor_ti/report/p2.md). Two consequences shape this file:
 *
 *   - the parse must not come out of the editor app's own heap (500KB shared
 *     with its UI state), so a request borrows a block from the document pool
 *     and runs prism inside it through the scratch allocator hook, then gives
 *     the whole block back;
 *   - it must not run that block dry, because prism aborts the process when a
 *     node allocation fails (pm_node_alloc). So the allocator longjmps back
 *     here instead of returning NULL, and the block is released on the way
 *     out -- an over-large document costs a "no candidates" answer, not a
 *     crash.
 */

#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"

#include "picoruby_ti_suggest.h"

#include "../../include/editor_core_api.h"

static const char *TAG = "editor_ti";

/* The document arena (POOL_ID_EDITOR_DOC), from editor_core.c. Declared here
   rather than in the shared header for the same reason that file declares the
   syntax highlighter: one firmware, one prototype. */
extern int ec_doc_mem_handle(void);

/* Installed while a request runs, by the compiler's prism allocator hook
   (lib/patch/compiler/mruby-compiler2-ccontext.c). */
typedef void *(*fmrb_prism_alloc_fn)(void *ptr, size_t size);
extern void fmrb_prism_set_scratch_allocator(fmrb_prism_alloc_fn fn);

/* Bytes of source the engine is asked to look at. Above this the request is
   refused outright: the parse would not fit the pool, and the engine's own
   arena gives up somewhere past 100KB anyway. Kid-sized programs are far
   below it. */
#define ET_MAX_SOURCE_BYTES (32 * 1024)

/* Room a request asks for, per source byte. Pointers are twice as wide on the
   64-bit Linux build, so the same document needs twice the tree there. Both
   numbers sit above what ordinary code measures and below the worst dense
   literals -- the point is to ask for enough, not to guarantee it, since
   running out is handled. */
#define ET_SCRATCH_PER_BYTE ((sizeof(void *) >= 8) ? 32u : 16u)
#define ET_SCRATCH_SLACK    (64 * 1024)
#define ET_SCRATCH_MIN      (96 * 1024)

#define ET_MAX_SUGGESTIONS  TI_SUGGESTION_CAPACITY
#define ET_LABEL_MAX        48
#define ET_DETAIL_MAX       112
#define ET_DOC_MAX          112

typedef struct {
    char label[ET_LABEL_MAX];
    char detail[ET_DETAIL_MAX];
    char doc[ET_DOC_MAX];
    int  label_len;
} et_item_t;

/* Results outlive the call (the editor reads them field by field afterwards),
   so they live in the document pool rather than in .bss -- 15KB of internal
   RAM is not something the S3 has spare. */
static et_item_t *g_items;
static int        g_count;

/* One request at a time: the engine keeps its working arena in globals, so two
   editors asking at once would tread on each other. The same lock covers the
   scratch allocator, which is a global hook. */
static fmrb_semaphore_t g_lock;
static int              g_lock_ready;

/* Scratch heap for prism, valid only inside a request. */
static fmrb_mem_handle_t g_scratch = -1;
static jmp_buf           g_oom;
static volatile int      g_oom_armed;

static void *et_scratch_realloc(void *ptr, size_t size)
{
    if (g_scratch < 0) return NULL;
    if (size == 0) {
        if (ptr) fmrb_free(g_scratch, ptr);
        return NULL;
    }
    void *p = fmrb_realloc(g_scratch, ptr, size);
    if (!p && g_oom_armed) {
        /* prism dereferences what its node allocator returns, so NULL would
           abort the firmware. Leave the parse here instead; the caller frees
           the whole scratch heap, so nothing prism took is lost. */
        g_oom_armed = 0;
        longjmp(g_oom, 1);
    }
    return p;
}

static int et_lock(void)
{
    if (!g_lock_ready) {
        g_lock = fmrb_semaphore_create_mutex();
        if (!g_lock) return 0;
        g_lock_ready = 1;
    }
    fmrb_semaphore_take(g_lock, FMRB_TICK_MAX);
    return 1;
}

static void et_unlock(void)
{
    if (g_lock_ready) fmrb_semaphore_give(g_lock);
}

/* Byte offset of character index ci within one line, clamped. */
static int32_t et_byte_of(const char *s, int32_t len, int32_t ci)
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

/* Flatten the document into one NUL-terminated buffer, and report where the
   cursor lands in it. Lines come straight from the document model, so nothing
   is copied twice. */
static char *et_serialize(int slot, int y, int x, int total, int *out_cursor)
{
    char *buf = (char *)fmrb_malloc(ec_doc_mem_handle(), (size_t)total + 1);
    if (!buf) return NULL;

    int lines = ec_line_count(slot);
    int pos = 0;
    int cursor = total;
    for (int i = 0; i < lines; i++) {
        int len = 0;
        /* A column count no line can reach, so the whole line comes back. Not
           INT32_MAX: the document model adds it to col0. */
        const char *p = ec_render_text(slot, i, 0, 1 << 24, &len);
        if (i == y) {
            cursor = pos + (int)et_byte_of(p, len, x);
        }
        if (len > 0 && pos + len <= total) {
            memcpy(buf + pos, p, (size_t)len);
            pos += len;
        }
        if (i + 1 < lines && pos < total) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    if (cursor > pos) cursor = pos;
    *out_cursor = cursor;
    return buf;
}

static void et_copy_field(char *dst, size_t cap, const char *src, int len)
{
    if (!src || len <= 0) { dst[0] = '\0'; return; }
    size_t n = (size_t)len;
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Copy the engine's answer into our own storage: its strings point into the
   arena it resets at the start of the next request. Candidates that repeat a
   name already taken are dropped -- overriding a method lists it twice, once
   from the subclass and once from the parent, and the editor has no use for
   the second. */
static void et_take(const TiSuggestionList *out)
{
    g_count = 0;
    for (int i = 0; i < out->count && g_count < ET_MAX_SUGGESTIONS; i++) {
        const TiSuggestion *s = &out->items[i];
        if (!s->contents || s->contents_length <= 0) continue;

        int dup = 0;
        for (int j = 0; j < g_count; j++) {
            if (g_items[j].label_len == s->contents_length &&
                memcmp(g_items[j].label, s->contents, (size_t)s->contents_length) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        et_item_t *it = &g_items[g_count];
        et_copy_field(it->label, ET_LABEL_MAX, s->contents, s->contents_length);
        it->label_len = (int)strlen(it->label);
        et_copy_field(it->detail, ET_DETAIL_MAX, s->detail,
                      s->detail ? (int)strlen(s->detail) : 0);
        et_copy_field(it->doc, ET_DOC_MAX, s->document,
                      s->document ? (int)strlen(s->document) : 0);
        g_count++;
    }
}

int et_suggest(int slot, int y, int x)
{
    int handle = ec_doc_mem_handle();
    if (handle < 0) return EC_ERR_NOMEM;

    int total = ec_doc_bytesize(slot);
    if (total < 0) return EC_ERR_RANGE;
    if (total > ET_MAX_SOURCE_BYTES) return ET_ERR_TOO_LARGE;

    if (!et_lock()) return EC_ERR_NOMEM;

    int    result = 0;
    char  *src = NULL;
    void  *block = NULL;
    int    cursor = 0;
    size_t want = 0;
    fmrb_pool_stats_t st;
    TiSource item;
    TiSourceList list;
    TiSuggestionList out;

    if (!g_items) {
        g_items = (et_item_t *)fmrb_malloc(handle, sizeof(et_item_t) * ET_MAX_SUGGESTIONS);
        if (!g_items) { result = EC_ERR_NOMEM; goto done; }
    }
    g_count = 0;

    src = et_serialize(slot, y, x, total, &cursor);
    if (!src) { result = EC_ERR_NOMEM; goto done; }

    /* Ask for what the parse is likely to need, but never for so much that the
       document itself has nowhere to grow. */
    if (fmrb_mem_get_stats(handle, &st) != 0) { result = EC_ERR_NOMEM; goto done; }
    want = (size_t)total * ET_SCRATCH_PER_BYTE + ET_SCRATCH_SLACK;
    size_t room = st.free_size / 4 * 3;
    if (want > room) want = room;
    if (want < ET_SCRATCH_MIN) { result = EC_ERR_NOMEM; goto done; }

    block = fmrb_malloc(handle, want);
    while (!block && want > ET_SCRATCH_MIN) {
        want /= 2;
        block = fmrb_malloc(handle, want);
    }
    if (!block) { result = EC_ERR_NOMEM; goto done; }

    g_scratch = fmrb_mem_create_handle(block, want, POOL_ID_EDITOR_DOC);
    if (g_scratch < 0) { result = EC_ERR_NOMEM; goto done; }

    item.source = src;
    item.source_byte_length = total;
    list.items = &item;
    list.count = 1;

    fmrb_prism_set_scratch_allocator(et_scratch_realloc);
    if (setjmp(g_oom) == 0) {
        g_oom_armed = 1;
        ti_fill_suggestions_at_cursor(&list, cursor, &out);
        g_oom_armed = 0;
        et_take(&out);
        result = g_count;
    } else {
        FMRB_LOGW(TAG, "parse ran out of scratch memory (%d bytes of source, %zu of heap)",
                  total, want);
        g_count = 0;
        result = 0;
    }
    fmrb_prism_set_scratch_allocator(NULL);

done:
    if (g_scratch >= 0) {
        fmrb_mem_destroy_handle(g_scratch);
        g_scratch = -1;
    }
    if (block) fmrb_free(handle, block);
    if (src) fmrb_free(handle, src);
    et_unlock();
    return result;
}

const char *et_suggestion(int i, int field, int *out_len)
{
    if (out_len) *out_len = 0;
    if (!g_items || i < 0 || i >= g_count) return "";

    const char *s;
    switch (field) {
        case ET_FIELD_LABEL:  s = g_items[i].label;  break;
        case ET_FIELD_DETAIL: s = g_items[i].detail; break;
        case ET_FIELD_DOC:    s = g_items[i].doc;    break;
        default: return "";
    }
    if (out_len) *out_len = (int)strlen(s);
    return s;
}

int et_max_source_bytes(void)
{
    return ET_MAX_SOURCE_BYTES;
}
