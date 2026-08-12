/*
 * EditorTi: the editor's side of the type inference engine (picoruby-ti).
 *
 * The engine answers three questions -- what can follow the cursor, what is
 * under it, and what is wrong with the file -- from a flat source string and
 * byte offsets. So this file turns the editor's document (lines, columns in
 * characters) into that shape, calls the engine, and copies the answers out:
 * the engine hands back pointers into its own 16KB arena, which the next
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
 *     to the request instead of returning NULL, and the block is released on
 *     the way out -- an over-large document costs an empty answer, not a
 *     crash.
 *
 * All three entry points share that scaffolding through et_begin / et_end;
 * each one keeps its own setjmp, since that has to sit in the frame the
 * allocator jumps back to.
 */

#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"

#include "picoruby_ti_suggest.h"
#include "picoruby_ti_hover.h"
#include "picoruby_ti_diagnostic.h"
#include "picoruby_ti_call_context.h"

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
/* How long a summary the editor keeps. Anything past it is dropped without a
   word, and a summary carries two languages at once (the <<en>> marker), so
   this is a rule for whoever writes sig/*.rbs as much as a buffer size here:
   the two halves together must fit. It is written down in sig/README.md, where
   the doc comments are. 64 candidates x (48 + 112 + 112) sits in the document
   pool, so raising it costs the editor's memory, not flash. */
#define ET_DOC_MAX          112

#define ET_MAX_DIAGNOSTICS  TI_DIAGNOSTIC_CAPACITY
#define ET_MESSAGE_MAX      160

typedef struct {
    char label[ET_LABEL_MAX];
    char detail[ET_DETAIL_MAX];
    char doc[ET_DOC_MAX];
    int  label_len;
} et_item_t;

typedef struct {
    int  start_y, start_x, end_y, end_x;
    char message[ET_MESSAGE_MAX];
} et_diag_t;

typedef struct {
    char name[TI_VARIABLE_NAME_CAPACITY];
    char type[TI_TYPE_NAME_CAPACITY];
    char signature[ET_DETAIL_MAX];
    char doc[ET_DOC_MAX];
    int  is_method;
    int  found;
} et_hover_t;

typedef struct {
    char signature[ET_DETAIL_MAX];
    char name[TI_CALL_ARGUMENT_NAME_CAPACITY];
    char type[TI_CALL_ARGUMENT_TYPE_CAPACITY];
    int  index;
    int  found;
} et_call_t;

/* Answers outlive the call (the editor reads them field by field afterwards),
   so they live in the document pool rather than in .bss -- 15KB plus of
   internal RAM is not something the S3 has spare. */
static et_item_t *g_items;
static int        g_count;
static et_diag_t *g_diags;
static int        g_diag_count;
static et_hover_t g_hover;   /* one record, small enough to keep here */
static et_call_t  g_call;    /* likewise */

/* One request at a time: the engine keeps its working arena in globals, so two
   editors asking at once would tread on each other. The same lock covers the
   scratch allocator, which is a per-task hook installed for the length of a
   request. */
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

/* The other direction: where a byte offset in the flattened document lands,
   as a line and a CHARACTER column -- the coordinates the editor uses. Walked
   per call rather than kept as a table: a request has at most 64 answers to
   place and the document is capped at 32KB. */
static void et_pos_of_offset(const char *src, int len, int off, int *out_y, int *out_x)
{
    if (off < 0) off = 0;
    if (off > len) off = len;
    int y = 0, x = 0;
    for (int i = 0; i < off; i++) {
        if (src[i] == '\n') {
            y++;
            x = 0;
        } else if (((unsigned char)src[i] & 0xC0) != 0x80) {
            x++;
        }
    }
    *out_y = y;
    *out_x = x;
}

static void et_copy_field(char *dst, size_t cap, const char *src, int len)
{
    if (!src || len <= 0) { dst[0] = '\0'; return; }
    size_t n = (size_t)len;
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void et_copy_str(char *dst, size_t cap, const char *src)
{
    et_copy_field(dst, cap, src, src ? (int)strlen(src) : 0);
}

/* ---- request scaffolding ------------------------------------------------ */

typedef struct {
    int    handle;      /* document pool */
    char  *src;         /* flattened document, NUL terminated */
    int    src_len;
    int    cursor;      /* byte offset of (y, x) in src */
    void  *block;       /* scratch heap backing store */
    size_t block_size;
    int    locked;
} et_run_t;

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

static void et_end(et_run_t *r)
{
    fmrb_prism_set_scratch_allocator(NULL);
    if (g_scratch >= 0) {
        fmrb_mem_destroy_handle(g_scratch);
        g_scratch = -1;
    }
    if (r->block) { fmrb_free(r->handle, r->block); r->block = NULL; }
    if (r->src)   { fmrb_free(r->handle, r->src);   r->src = NULL; }
    if (r->locked) { et_unlock(); r->locked = 0; }
}

/* Take the lock, flatten the document, borrow a scratch heap and point prism
   at it. Returns 0 on success (the caller must call et_end), or a negative
   error with everything already released. */
static int et_begin(et_run_t *r, int slot, int y, int x)
{
    memset(r, 0, sizeof(*r));
    r->handle = ec_doc_mem_handle();
    if (r->handle < 0) return EC_ERR_NOMEM;

    int total = ec_doc_bytesize(slot);
    if (total < 0) return EC_ERR_RANGE;
    if (total > ET_MAX_SOURCE_BYTES) return ET_ERR_TOO_LARGE;

    if (!et_lock()) return EC_ERR_NOMEM;
    r->locked = 1;
    r->src_len = total;

    r->src = et_serialize(slot, y, x, total, &r->cursor);
    if (!r->src) { et_end(r); return EC_ERR_NOMEM; }

    /* Ask for what the parse is likely to need, but never for so much that the
       document itself has nowhere to grow. */
    fmrb_pool_stats_t st;
    if (fmrb_mem_get_stats(r->handle, &st) != 0) { et_end(r); return EC_ERR_NOMEM; }
    size_t want = (size_t)total * ET_SCRATCH_PER_BYTE + ET_SCRATCH_SLACK;
    if (want < ET_SCRATCH_MIN) want = ET_SCRATCH_MIN;
    size_t room = st.free_size / 4 * 3;
    if (want > room) want = room;
    if (want < ET_SCRATCH_MIN) {
        /* Only when the pool itself is nearly full -- a small document asks
           for the floor, not for its own size. */
        FMRB_LOGW(TAG, "no room for a parse: %zu bytes free in the document pool",
                  st.free_size);
        et_end(r);
        return EC_ERR_NOMEM;
    }

    /* Fragmentation can refuse one big block while smaller ones are there.
       Halve, but never past the floor -- below it the parse would fail so
       often that asking is a waste. */
    r->block = fmrb_malloc(r->handle, want);
    while (!r->block && want / 2 >= ET_SCRATCH_MIN) {
        want /= 2;
        r->block = fmrb_malloc(r->handle, want);
    }
    if (!r->block) { et_end(r); return EC_ERR_NOMEM; }
    r->block_size = want;

    /* Quiet: one of these is born and dies per completion/hover/diagnose
       request, and the lifecycle logs would drown the device log. */
    g_scratch = fmrb_mem_create_handle_quiet(r->block, want, POOL_ID_EDITOR_DOC);
    if (g_scratch < 0) { et_end(r); return EC_ERR_NOMEM; }

    fmrb_prism_set_scratch_allocator(et_scratch_realloc);
    return 0;
}

static void et_sources(et_run_t *r, TiSource *item, TiSourceList *list)
{
    item->source = r->src;
    item->source_byte_length = r->src_len;
    list->items = item;
    list->count = 1;
}

/* ---- completion --------------------------------------------------------- */

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
        et_copy_str(it->detail, ET_DETAIL_MAX, s->detail);
        et_copy_str(it->doc, ET_DOC_MAX, s->document);
        g_count++;
    }
}

int et_suggest(int slot, int y, int x)
{
    et_run_t r;
    int rc = et_begin(&r, slot, y, x);
    if (rc < 0) return rc;

    int result;
    if (!g_items) {
        g_items = (et_item_t *)fmrb_malloc(r.handle, sizeof(et_item_t) * ET_MAX_SUGGESTIONS);
    }
    g_count = 0;

    if (!g_items) {
        result = EC_ERR_NOMEM;
    } else if (setjmp(g_oom) == 0) {
        TiSource item;
        TiSourceList list;
        TiSuggestionList out;
        et_sources(&r, &item, &list);
        g_oom_armed = 1;
        ti_fill_suggestions_at_cursor(&list, r.cursor, &out);
        g_oom_armed = 0;
        et_take(&out);
        result = g_count;
    } else {
        FMRB_LOGW(TAG, "parse ran out of scratch memory (%d bytes of source, %zu of heap)",
                  r.src_len, r.block_size);
        g_count = 0;
        result = 0;
    }

    et_end(&r);
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

/* ---- hover -------------------------------------------------------------- */

int et_hover(int slot, int y, int x)
{
    memset(&g_hover, 0, sizeof(g_hover));

    et_run_t r;
    int rc = et_begin(&r, slot, y, x);
    if (rc < 0) return rc;

    int result;
    if (setjmp(g_oom) == 0) {
        TiSource item;
        TiSourceList list;
        TiHoverInfo out;
        et_sources(&r, &item, &list);
        g_oom_armed = 1;
        ti_find_hover_at_cursor(&list, r.cursor, &out);
        g_oom_armed = 0;

        if (out.found) {
            et_copy_str(g_hover.name, sizeof(g_hover.name), out.variable_name);
            et_copy_str(g_hover.type, sizeof(g_hover.type), out.type_name);
            et_copy_str(g_hover.signature, sizeof(g_hover.signature), out.method_signature);
            et_copy_str(g_hover.doc, sizeof(g_hover.doc), out.method_document);
            g_hover.is_method = out.is_method;
            g_hover.found = 1;
        }
        result = g_hover.found;
    } else {
        FMRB_LOGW(TAG, "hover ran out of scratch memory (%d bytes of source)", r.src_len);
        result = 0;
    }

    et_end(&r);
    return result;
}

const char *et_hover_field(int field, int *out_len)
{
    if (out_len) *out_len = 0;
    if (!g_hover.found) return "";

    const char *s;
    switch (field) {
        case ET_HOVER_NAME:      s = g_hover.name;      break;
        case ET_HOVER_TYPE:      s = g_hover.type;      break;
        case ET_HOVER_SIGNATURE: s = g_hover.signature; break;
        case ET_HOVER_DOC:       s = g_hover.doc;       break;
        default: return "";
    }
    if (out_len) *out_len = (int)strlen(s);
    return s;
}

int et_hover_is_method(void)
{
    return g_hover.found ? g_hover.is_method : 0;
}

/* ---- signature help ----------------------------------------------------- */

int et_call_context(int slot, int y, int x)
{
    memset(&g_call, 0, sizeof(g_call));
    g_call.index = -1;

    et_run_t r;
    int rc = et_begin(&r, slot, y, x);
    if (rc < 0) return rc;

    int result;
    if (setjmp(g_oom) == 0) {
        TiSource item;
        TiSourceList list;
        TiCallContext out;
        et_sources(&r, &item, &list);
        g_oom_armed = 1;
        ti_find_call_context(&list, r.cursor, &out);
        g_oom_armed = 0;

        if (out.found) {
            et_copy_str(g_call.signature, sizeof(g_call.signature), out.method_signature);
            et_copy_str(g_call.name, sizeof(g_call.name), out.argument_name);
            et_copy_str(g_call.type, sizeof(g_call.type), out.argument_type);
            g_call.index = out.argument_index;
            g_call.found = 1;
        }
        result = g_call.found;
    } else {
        FMRB_LOGW(TAG, "call context ran out of scratch memory (%d bytes of source)",
                  r.src_len);
        result = 0;
    }

    et_end(&r);
    return result;
}

const char *et_call_field(int field, int *out_len)
{
    if (out_len) *out_len = 0;
    if (!g_call.found) return "";

    const char *s;
    switch (field) {
        case ET_CALL_SIGNATURE: s = g_call.signature; break;
        case ET_CALL_ARG_NAME:  s = g_call.name;      break;
        case ET_CALL_ARG_TYPE:  s = g_call.type;      break;
        default: return "";
    }
    if (out_len) *out_len = (int)strlen(s);
    return s;
}

int et_call_argument_index(void)
{
    return g_call.found ? g_call.index : -1;
}

/* ---- diagnostics -------------------------------------------------------- */

int et_diagnose(int slot)
{
    et_run_t r;
    /* No cursor involved; the engine reads the whole document. */
    int rc = et_begin(&r, slot, 0, 0);
    if (rc < 0) return rc;

    int result;
    if (!g_diags) {
        g_diags = (et_diag_t *)fmrb_malloc(r.handle, sizeof(et_diag_t) * ET_MAX_DIAGNOSTICS);
    }
    g_diag_count = 0;

    if (!g_diags) {
        result = EC_ERR_NOMEM;
    } else if (setjmp(g_oom) == 0) {
        TiSource item;
        TiSourceList list;
        TiDiagnosticList out;
        et_sources(&r, &item, &list);
        g_oom_armed = 1;
        ti_fill_diagnostics(&list, &out);
        g_oom_armed = 0;

        for (int i = 0; i < out.count && g_diag_count < ET_MAX_DIAGNOSTICS; i++) {
            const TiDiagnostic *d = &out.items[i];
            et_diag_t *slot_out = &g_diags[g_diag_count];
            /* Byte offsets are only meaningful against the buffer they were
               produced from, so convert while it is still alive. */
            et_pos_of_offset(r.src, r.src_len, d->start_byte_offset,
                             &slot_out->start_y, &slot_out->start_x);
            et_pos_of_offset(r.src, r.src_len, d->end_byte_offset,
                             &slot_out->end_y, &slot_out->end_x);
            et_copy_str(slot_out->message, ET_MESSAGE_MAX, d->message);
            g_diag_count++;
        }
        result = g_diag_count;
    } else {
        FMRB_LOGW(TAG, "diagnostics ran out of scratch memory (%d bytes of source)",
                  r.src_len);
        g_diag_count = 0;
        result = 0;
    }

    et_end(&r);
    return result;
}

int et_diagnostic_pos(int i, int field)
{
    if (!g_diags || i < 0 || i >= g_diag_count) return -1;
    switch (field) {
        case ET_DIAG_START_Y: return g_diags[i].start_y;
        case ET_DIAG_START_X: return g_diags[i].start_x;
        case ET_DIAG_END_Y:   return g_diags[i].end_y;
        case ET_DIAG_END_X:   return g_diags[i].end_x;
        default: return -1;
    }
}

const char *et_diagnostic_message(int i, int *out_len)
{
    if (out_len) *out_len = 0;
    if (!g_diags || i < 0 || i >= g_diag_count) return "";
    if (out_len) *out_len = (int)strlen(g_diags[i].message);
    return g_diags[i].message;
}
