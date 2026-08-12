/*
 * The browser's side of the type inference engine (picoruby-ti).
 *
 * This is the WebConsole's counterpart to editor_ti_bridge.c: the same four
 * engine entry points, wrapped so JavaScript can reach them. The device bridge
 * exists because the editor has a document model and an allocator of its own;
 * here there is neither. The page hands over the whole text, prism allocates
 * with plain malloc (no PRISM_XALLOCATOR is defined for this build, so
 * prism/defines.h falls back to it), and the browser grows the heap when it
 * needs to. So there is no pool to borrow from, no lock -- one thread -- and no
 * longjmp on out of memory.
 *
 * What is kept from the device side is the shape of the answers. The engine
 * returns pointers into its own 16KB arena, which the next request resets, so
 * every string is copied into storage here and read afterwards field by field
 * through the getters. Diagnostics are converted to line and CHARACTER column
 * while the source they were measured against is still around, since that is
 * what a text editor can place.
 *
 * Built by `rake ti:wasm` into tool/web/wasm/ti.js + ti.wasm; the export list
 * is exports.txt next to this file.
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "picoruby_ti_suggest.h"
#include "picoruby_ti_hover.h"
#include "picoruby_ti_diagnostic.h"
#include "picoruby_ti_call_context.h"

/* Bytes of source a request looks at. Far above the device's 32KB cap (a page
   has a real heap), but not unbounded: the engine's arena gives up somewhere
   past 100KB anyway, and a runaway request should answer nothing rather than
   freeze the tab. */
#define TW_MAX_SOURCE_BYTES (256 * 1024)

/* Matches COMP_TOO_LARGE / ET_ERR_TOO_LARGE on the device, so the two UIs can
   say the same thing. */
#define TW_ERR_TOO_LARGE (-4)

#define TW_LABEL_MAX   64
#define TW_DETAIL_MAX  160
#define TW_DOC_MAX     256
#define TW_MESSAGE_MAX 256

/* Field selectors, mirrored in js/ti.js. */
enum {
    TW_FIELD_LABEL = 0,
    TW_FIELD_DETAIL = 1,
    TW_FIELD_DOC = 2
};

enum {
    TW_HOVER_NAME = 0,
    TW_HOVER_TYPE = 1,
    TW_HOVER_SIGNATURE = 2,
    TW_HOVER_DOC = 3
};

enum {
    TW_DIAG_START_Y = 0,
    TW_DIAG_START_X = 1,
    TW_DIAG_END_Y = 2,
    TW_DIAG_END_X = 3
};

enum {
    TW_CALL_SIGNATURE = 0,
    TW_CALL_ARG_NAME = 1,
    TW_CALL_ARG_TYPE = 2
};

typedef struct {
    char label[TW_LABEL_MAX];
    char detail[TW_DETAIL_MAX];
    char doc[TW_DOC_MAX];
    int  label_len;
} tw_item_t;

typedef struct {
    int  start_y, start_x, end_y, end_x;
    char message[TW_MESSAGE_MAX];
} tw_diag_t;

typedef struct {
    char name[TI_VARIABLE_NAME_CAPACITY];
    char type[TI_TYPE_NAME_CAPACITY];
    char signature[TW_DETAIL_MAX];
    char doc[TW_DOC_MAX];
    int  is_method;
    int  found;
} tw_hover_t;

typedef struct {
    char signature[TW_DETAIL_MAX];
    char name[TI_CALL_ARGUMENT_NAME_CAPACITY];
    char type[TI_CALL_ARGUMENT_TYPE_CAPACITY];
    int  index;
    int  found;
} tw_call_t;

static tw_item_t  g_items[TI_SUGGESTION_CAPACITY];
static int        g_count;
static tw_diag_t  g_diags[TI_DIAGNOSTIC_CAPACITY];
static int        g_diag_count;
static tw_hover_t g_hover;
static tw_call_t  g_call;

/* The source the page is asking about. Owned here so JavaScript never has to
   malloc or free inside the module: it asks for a buffer, writes the UTF-8
   bytes into it, and passes the pointer straight back. */
static char  *g_source;
static size_t g_source_cap;

static void tw_copy_field(char *dst, size_t cap, const char *src, int len)
{
    if (!src || len <= 0) { dst[0] = '\0'; return; }
    size_t n = (size_t)len;
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void tw_copy_str(char *dst, size_t cap, const char *src)
{
    tw_copy_field(dst, cap, src, src ? (int)strlen(src) : 0);
}

/* Where a byte offset lands as a line and a character column -- what an editor
   can point at. Continuation bytes (10xxxxxx) are not characters. */
static void tw_pos_of_offset(const char *src, int len, int off, int *out_y, int *out_x)
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

static void tw_sources(const char *src, int len, TiSource *item, TiSourceList *list)
{
    item->source = src;
    item->source_byte_length = len;
    list->items = item;
    list->count = 1;
}

static int tw_source_ok(const char *src, int len)
{
    return src && len >= 0 && len <= TW_MAX_SOURCE_BYTES;
}

/* ---- the source buffer -------------------------------------------------- */

/* Room for `bytes` of source plus its terminator, reused between requests.
   Returns NULL when the request is above the cap or the heap is exhausted. */
EMSCRIPTEN_KEEPALIVE
char *tw_source_buffer(int bytes)
{
    if (bytes < 0 || bytes > TW_MAX_SOURCE_BYTES) return NULL;
    size_t want = (size_t)bytes + 1;
    if (want > g_source_cap) {
        char *grown = (char *)realloc(g_source, want);
        if (!grown) return NULL;
        g_source = grown;
        g_source_cap = want;
    }
    return g_source;
}

EMSCRIPTEN_KEEPALIVE
int tw_max_source_bytes(void)
{
    return TW_MAX_SOURCE_BYTES;
}

/* ---- completion --------------------------------------------------------- */

/* Copy the answer out of the arena. A name already taken is dropped: overriding
   a method lists it twice, once from the subclass and once from the parent, and
   a list of names has no use for the second (same rule as the device). */
static void tw_take(const TiSuggestionList *out)
{
    g_count = 0;
    for (int i = 0; i < out->count && g_count < TI_SUGGESTION_CAPACITY; i++) {
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

        tw_item_t *it = &g_items[g_count];
        tw_copy_field(it->label, TW_LABEL_MAX, s->contents, s->contents_length);
        it->label_len = (int)strlen(it->label);
        tw_copy_str(it->detail, TW_DETAIL_MAX, s->detail);
        tw_copy_str(it->doc, TW_DOC_MAX, s->document);
        g_count++;
    }
}

EMSCRIPTEN_KEEPALIVE
int tw_suggest(const char *src, int len, int cursor)
{
    g_count = 0;
    if (!src || len < 0) return 0;
    if (len > TW_MAX_SOURCE_BYTES) return TW_ERR_TOO_LARGE;

    TiSource item;
    TiSourceList list;
    TiSuggestionList out;
    tw_sources(src, len, &item, &list);
    ti_fill_suggestions_at_cursor(&list, cursor, &out);
    tw_take(&out);
    return g_count;
}

EMSCRIPTEN_KEEPALIVE
const char *tw_suggestion(int i, int field)
{
    if (i < 0 || i >= g_count) return "";
    switch (field) {
        case TW_FIELD_LABEL:  return g_items[i].label;
        case TW_FIELD_DETAIL: return g_items[i].detail;
        case TW_FIELD_DOC:    return g_items[i].doc;
        default: return "";
    }
}

/* ---- hover -------------------------------------------------------------- */

EMSCRIPTEN_KEEPALIVE
int tw_hover(const char *src, int len, int cursor)
{
    memset(&g_hover, 0, sizeof(g_hover));
    if (!tw_source_ok(src, len)) return 0;

    TiSource item;
    TiSourceList list;
    TiHoverInfo out;
    tw_sources(src, len, &item, &list);
    ti_find_hover_at_cursor(&list, cursor, &out);

    if (out.found) {
        tw_copy_str(g_hover.name, sizeof(g_hover.name), out.variable_name);
        tw_copy_str(g_hover.type, sizeof(g_hover.type), out.type_name);
        tw_copy_str(g_hover.signature, sizeof(g_hover.signature), out.method_signature);
        tw_copy_str(g_hover.doc, sizeof(g_hover.doc), out.method_document);
        g_hover.is_method = out.is_method;
        g_hover.found = 1;
    }
    return g_hover.found;
}

EMSCRIPTEN_KEEPALIVE
const char *tw_hover_field(int field)
{
    if (!g_hover.found) return "";
    switch (field) {
        case TW_HOVER_NAME:      return g_hover.name;
        case TW_HOVER_TYPE:      return g_hover.type;
        case TW_HOVER_SIGNATURE: return g_hover.signature;
        case TW_HOVER_DOC:       return g_hover.doc;
        default: return "";
    }
}

EMSCRIPTEN_KEEPALIVE
int tw_hover_is_method(void)
{
    return g_hover.found ? g_hover.is_method : 0;
}

/* ---- signature help ----------------------------------------------------- */

EMSCRIPTEN_KEEPALIVE
int tw_call_context(const char *src, int len, int cursor)
{
    memset(&g_call, 0, sizeof(g_call));
    g_call.index = -1;
    if (!tw_source_ok(src, len)) return 0;

    TiSource item;
    TiSourceList list;
    TiCallContext out;
    tw_sources(src, len, &item, &list);
    ti_find_call_context(&list, cursor, &out);

    if (out.found) {
        tw_copy_str(g_call.signature, sizeof(g_call.signature), out.method_signature);
        tw_copy_str(g_call.name, sizeof(g_call.name), out.argument_name);
        tw_copy_str(g_call.type, sizeof(g_call.type), out.argument_type);
        g_call.index = out.argument_index;
        g_call.found = 1;
    }
    return g_call.found;
}

EMSCRIPTEN_KEEPALIVE
const char *tw_call_field(int field)
{
    if (!g_call.found) return "";
    switch (field) {
        case TW_CALL_SIGNATURE: return g_call.signature;
        case TW_CALL_ARG_NAME:  return g_call.name;
        case TW_CALL_ARG_TYPE:  return g_call.type;
        default: return "";
    }
}

EMSCRIPTEN_KEEPALIVE
int tw_call_argument_index(void)
{
    return g_call.found ? g_call.index : -1;
}

/* ---- diagnostics -------------------------------------------------------- */

EMSCRIPTEN_KEEPALIVE
int tw_diagnose(const char *src, int len)
{
    g_diag_count = 0;
    if (!src || len < 0) return 0;
    if (len > TW_MAX_SOURCE_BYTES) return TW_ERR_TOO_LARGE;

    TiSource item;
    TiSourceList list;
    TiDiagnosticList out;
    tw_sources(src, len, &item, &list);
    ti_fill_diagnostics(&list, &out);

    for (int i = 0; i < out.count && g_diag_count < TI_DIAGNOSTIC_CAPACITY; i++) {
        const TiDiagnostic *d = &out.items[i];
        tw_diag_t *slot = &g_diags[g_diag_count];
        /* Byte offsets only mean something against the buffer they were
           produced from, so convert while it is still alive. */
        tw_pos_of_offset(src, len, d->start_byte_offset, &slot->start_y, &slot->start_x);
        tw_pos_of_offset(src, len, d->end_byte_offset, &slot->end_y, &slot->end_x);
        tw_copy_str(slot->message, TW_MESSAGE_MAX, d->message);
        g_diag_count++;
    }
    return g_diag_count;
}

EMSCRIPTEN_KEEPALIVE
int tw_diagnostic_pos(int i, int field)
{
    if (i < 0 || i >= g_diag_count) return -1;
    switch (field) {
        case TW_DIAG_START_Y: return g_diags[i].start_y;
        case TW_DIAG_START_X: return g_diags[i].start_x;
        case TW_DIAG_END_Y:   return g_diags[i].end_y;
        case TW_DIAG_END_X:   return g_diags[i].end_x;
        default: return -1;
    }
}

EMSCRIPTEN_KEEPALIVE
const char *tw_diagnostic_message(int i)
{
    if (i < 0 || i >= g_diag_count) return "";
    return g_diags[i].message;
}
