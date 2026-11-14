// An app's metadata from its .app.toml sidecar and its "#---fmrb" comment
// fence, merged key by key with the sidecar winning (fmrb_app_meta.h).

#include <string.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_toml.h"
#include "fmrb_app_meta.h"

static const char *TAG = "app_meta";

/*
 * The comment fence (doc/multivm_app/plan.md 3.2):
 *
 *     #---fmrb
 *     # default_window_mode = "background"
 *     # task_stack_kb = 32
 *     #---
 *
 * The fenced body has its leading "# " stripped and is handed to the ordinary
 * toml parser, so there is no second syntax to keep in step. Only the first
 * 512 bytes are read and the fence must come before the first non-comment
 * line, which keeps the cost to one short read.
 */
#define FENCE_SCAN_BYTES 512
#define FENCE_OPEN "#---fmrb"
#define FENCE_CLOSE "#---"
#define VALUE_TEXT_MAX 256

// True when the rest of a line is nothing but whitespace.
static bool rest_blank(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') {
            return false;
        }
    }
    return true;
}

static bool line_is(const char *p, size_t len, const char *mark)
{
    size_t m = strlen(mark);
    return len >= m && strncmp(p, mark, m) == 0 && rest_blank(p + m, len - m);
}

// The fence of a .rb as a parsed table. *found tells whether the file opens a
// fence at all (a broken one still makes the file an app).
static toml_table_t *load_comment(const char *app_path, bool *found)
{
    *found = false;
    const char *ext = strrchr(app_path, '.');
    if (!ext || strcmp(ext, ".rb") != 0) {
        // Ruby only: .bas / .lua / .py comment with different characters.
        return NULL;
    }

    fmrb_file_t file = NULL;
    if (fmrb_hal_file_open(app_path, FMRB_O_RDONLY, &file) != FMRB_OK) {
        return NULL;
    }
    char *head = (char *)fmrb_sys_malloc(FENCE_SCAN_BYTES + 1);
    char *body = (char *)fmrb_sys_malloc(FENCE_SCAN_BYTES + 1);
    if (!head || !body) {
        if (head) fmrb_sys_free(head);
        if (body) fmrb_sys_free(body);
        fmrb_hal_file_close(file);
        return NULL;
    }
    size_t n = 0;
    fmrb_err_t ret = fmrb_hal_file_read(file, head, FENCE_SCAN_BYTES, &n);
    fmrb_hal_file_close(file);
    if (ret != FMRB_OK || n == 0) {
        fmrb_sys_free(head);
        fmrb_sys_free(body);
        return NULL;
    }
    head[n] = '\0';

    bool in_fence = false;
    bool closed = false;
    bool malformed = false;
    size_t out = 0;
    const char *p = head;
    const char *end = head + n;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        if (!nl) {
            // The 512-byte window cut this line in half; do not guess at it.
            break;
        }
        size_t len = (size_t)(nl - p);
        if (!in_fence) {
            if (line_is(p, len, FENCE_OPEN)) {
                in_fence = true;
                *found = true;
            } else if (len > 0 && p[0] != '#' && !rest_blank(p, len)) {
                // First real line of code: a fence below this point is not ours.
                break;
            }
        } else if (line_is(p, len, FENCE_CLOSE)) {
            closed = true;
            break;
        } else if (len > 0 && p[0] == '#') {
            // Strip the comment marker and one following space.
            size_t skip = (len > 1 && p[1] == ' ') ? 2 : 1;
            size_t copy = len - skip;
            if (out + copy + 1 > FENCE_SCAN_BYTES) {
                malformed = true;
                break;
            }
            memcpy(body + out, p + skip, copy);
            out += copy;
            body[out++] = '\n';
        } else {
            // Anything but a comment inside the fence: it was never closed.
            malformed = true;
            break;
        }
        p = nl + 1;
    }
    fmrb_sys_free(head);

    if (!*found) {
        fmrb_sys_free(body);
        return NULL;
    }
    if (!closed || malformed) {
        FMRB_LOGW(TAG, "Unterminated %s fence in %s: its attributes are not used", FENCE_OPEN, app_path);
        fmrb_sys_free(body);
        return NULL;
    }
    body[out] = '\0';
    char errbuf[96];
    toml_table_t *tab = toml_parse(body, errbuf, sizeof(errbuf));
    fmrb_sys_free(body);
    if (!tab) {
        FMRB_LOGW(TAG, "Comment toml in %s failed to parse (%s): its attributes are not used",
                  app_path, errbuf);
    }
    return tab;
}

bool fmrb_app_meta_sidecar_path(const char *app_path, char *out, size_t n)
{
    if (!app_path || !out || n == 0) {
        return false;
    }
    const char *dot = strrchr(app_path, '.');
    const char *slash = strrchr(app_path, '/');
    if (!dot || (slash && dot < slash)) {
        return false;
    }
    size_t base = (size_t)(dot - app_path);
    if (base + sizeof(".toml") > n) {
        return false;
    }
    memcpy(out, app_path, base);
    memcpy(out + base, ".toml", sizeof(".toml"));
    return true;
}

bool fmrb_app_meta_load(const char *app_path, fmrb_app_meta_t *m)
{
    memset(m, 0, sizeof(*m));
    if (!app_path) {
        return false;
    }
    // On the heap: the spawner calls this deep in the kernel's call stack,
    // and a file info alone is ~300 bytes.
    typedef struct {
        char path[FMRB_MAX_PATH_LEN];
        char errbuf[96];
        fmrb_file_info_t info;
    } work_t;
    work_t *w = (work_t *)fmrb_sys_malloc(sizeof(work_t));
    if (w && fmrb_app_meta_sidecar_path(app_path, w->path, sizeof(w->path)) &&
        fmrb_hal_file_stat(w->path, &w->info) == FMRB_OK) {
        m->has_sidecar = true;
        m->sidecar = fmrb_toml_load_file(w->path, w->errbuf, sizeof(w->errbuf));
        if (!m->sidecar && w->info.size > 0) {
            FMRB_LOGW(TAG, "%s failed to parse (%s)", w->path, w->errbuf);
        }
    }
    if (w) {
        fmrb_sys_free(w);
    }
    m->comment = load_comment(app_path, &m->has_comment);
    return m->has_sidecar || m->has_comment;
}

void fmrb_app_meta_free(fmrb_app_meta_t *m)
{
    if (m->sidecar) toml_free(m->sidecar);
    if (m->comment) toml_free(m->comment);
    memset(m, 0, sizeof(*m));
}

const toml_table_t *fmrb_app_meta_table(const fmrb_app_meta_t *m, const char *key)
{
    if (m->sidecar && toml_key_exists(m->sidecar, key)) {
        return m->sidecar;
    }
    if (m->comment && toml_key_exists(m->comment, key)) {
        return m->comment;
    }
    return NULL;
}

// One scalar as text, appended at out[*len]; false when it did not fit.
static bool append_raw(toml_raw_t raw, char *out, size_t cap, size_t *len)
{
    char *s = NULL;
    const char *src = raw;
    if (toml_rtos(raw, &s) == 0) {
        src = s;
    }
    size_t l = strlen(src);
    bool ok = *len + l < cap;
    if (ok) {
        memcpy(out + *len, src, l);
        *len += l;
        out[*len] = '\0';
    }
    if (s) {
        fmrb_toml_xfree(s);
    }
    return ok;
}

static bool value_text(const toml_table_t *tab, const char *key, char *out, size_t cap, size_t *len)
{
    *len = 0;
    out[0] = '\0';
    toml_raw_t raw = toml_raw_in(tab, key);
    if (raw) {
        return append_raw(raw, out, cap, len);
    }
    toml_array_t *arr = toml_array_in(tab, key);
    if (!arr) {
        return false;  // a table: not a value
    }
    int n = toml_array_nelem(arr);
    for (int i = 0; i < n; i++) {
        toml_raw_t item = toml_raw_at(arr, i);
        if (!item) {
            continue;  // nested arrays and tables are not flattened
        }
        if (*len > 0) {
            if (*len + 1 >= cap) return false;
            out[(*len)++] = ' ';
            out[*len] = '\0';
        }
        if (!append_raw(item, out, cap, len)) {
            return false;
        }
    }
    return true;
}

static void visit_table(const toml_table_t *tab, const toml_table_t *shadow, char *buf,
                        fmrb_app_meta_visit_t visit, void *ud)
{
    const char *key;
    for (int i = 0; (key = toml_key_in(tab, i)) != NULL; i++) {
        if (shadow && toml_key_exists(shadow, key)) {
            continue;  // the sidecar already gave this key
        }
        size_t len = 0;
        if (value_text(tab, key, buf, VALUE_TEXT_MAX, &len)) {
            visit(ud, key, buf, len);
        }
    }
}

void fmrb_app_meta_each(const fmrb_app_meta_t *m, fmrb_app_meta_visit_t visit, void *ud)
{
    char *buf = (char *)fmrb_sys_malloc(VALUE_TEXT_MAX);
    if (!buf) {
        return;
    }
    if (m->sidecar) {
        visit_table(m->sidecar, NULL, buf, visit, ud);
    }
    if (m->comment) {
        visit_table(m->comment, m->sidecar, buf, visit, ud);
    }
    fmrb_sys_free(buf);
}

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t off;
    int count;
} pack_t;

static void pack_one(void *ud, const char *key, const char *val, size_t len)
{
    pack_t *p = (pack_t *)ud;
    size_t klen = strlen(key);
    if (p->count >= 255 || klen > 255 || len > 0xFFFF || p->off + 1 + klen + 2 + len > p->cap) {
        return;
    }
    p->buf[p->off++] = (uint8_t)klen;
    memcpy(p->buf + p->off, key, klen);
    p->off += klen;
    p->buf[p->off++] = (uint8_t)(len & 0xFF);
    p->buf[p->off++] = (uint8_t)(len >> 8);
    memcpy(p->buf + p->off, val, len);
    p->off += len;
    p->count++;
}

int fmrb_app_meta_pack(const char *app_path, uint8_t *buf, size_t cap)
{
    if (!buf || cap < 1) {
        return -1;
    }
    fmrb_app_meta_t m;
    if (!fmrb_app_meta_load(app_path, &m)) {
        fmrb_app_meta_free(&m);
        return -1;
    }
    pack_t p = { buf, cap, 1, 0 };
    fmrb_app_meta_each(&m, pack_one, &p);
    fmrb_app_meta_free(&m);
    buf[0] = (uint8_t)p.count;
    return (int)p.off;
}
