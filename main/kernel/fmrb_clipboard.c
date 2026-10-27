/*
 * fmrb_clipboard.c - see fmrb_clipboard.h.
 */

#include "fmrb_clipboard.h"

#include <string.h>

#include "fmrb_attr.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"

static const char *TAG = "clipboard";

/* Everything, the lock's block included, in one PSRAM object: the
 * clipboard adds nothing to internal RAM. Only tasks touch it (no ISR, no
 * flash write from it). */
typedef struct {
    fmrb_semaphore_t lock;
    fmrb_static_semaphore_t lock_block;
    const fmrb_clipboard_host_t *host;
    uint32_t gen;
    uint32_t host_seq;      /* the host's sequence last taken */
    size_t len;
    char text[FMRB_CLIPBOARD_MAX + 1];
} clipboard_t;

FMRB_EXT_RAM_BSS_ATTR static clipboard_t s_clip;

fmrb_err_t fmrb_clipboard_init(void)
{
    if (s_clip.lock != NULL) return FMRB_OK;
    s_clip.lock = fmrb_semaphore_create_mutex_static(&s_clip.lock_block);
    if (s_clip.lock == NULL) {
        FMRB_LOGE(TAG, "cannot create the lock");
        return FMRB_ERR_NO_MEMORY;
    }
    return FMRB_OK;
}

void fmrb_clipboard_host_install(const fmrb_clipboard_host_t *host)
{
    s_clip.host = host;
}

static bool lock(void)
{
    if (s_clip.lock == NULL) return false;
    return fmrb_semaphore_take(s_clip.lock, FMRB_TICK_MAX) == FMRB_TRUE;
}

static void unlock(void)
{
    fmrb_semaphore_give(s_clip.lock);
}

/* Line ends to "\n", NUL bytes out, in place; returns the new length. */
static size_t normalize(char *s, size_t n)
{
    size_t w = 0;
    for (size_t r = 0; r < n; r++) {
        char c = s[r];
        if (c == '\0') continue;
        if (c == '\r') {
            s[w++] = '\n';
            if (r + 1 < n && s[r + 1] == '\n') r++;
            continue;
        }
        s[w++] = c;
    }
    return w;
}

/* With the lock held: the host's newer text, if it has one. */
static void sync_from_host(void)
{
    const fmrb_clipboard_host_t *h = s_clip.host;
    if (h == NULL || h->take == NULL) return;
    uint32_t seq = s_clip.host_seq;
    size_t full = 0;
    if (!h->take(&seq, s_clip.text, FMRB_CLIPBOARD_MAX, &full)) return;
    s_clip.host_seq = seq;
    s_clip.gen++;
    if (full > FMRB_CLIPBOARD_MAX) {
        s_clip.len = 0;
        s_clip.text[0] = '\0';
        FMRB_LOGW(TAG, "the host's text (%u bytes) is over %u bytes: not taken, the clipboard is empty",
                  (unsigned)full, (unsigned)FMRB_CLIPBOARD_MAX);
        return;
    }
    s_clip.len = normalize(s_clip.text, full);
    s_clip.text[s_clip.len] = '\0';
    FMRB_LOGI(TAG, "took %u bytes from the host (generation %u)", (unsigned)s_clip.len,
              (unsigned)s_clip.gen);
}

fmrb_err_t fmrb_clipboard_set(const char *text, size_t len)
{
    if (len > FMRB_CLIPBOARD_MAX || (len > 0 && text == NULL)) return FMRB_ERR_INVALID_PARAM;
    if (!lock()) return FMRB_ERR_INVALID_STATE;
    /* Whatever the host had before is older than this. */
    sync_from_host();
    if (len > 0) memmove(s_clip.text, text, len);
    s_clip.len = len;
    s_clip.text[len] = '\0';
    s_clip.gen++;
    const fmrb_clipboard_host_t *h = s_clip.host;
    if (h != NULL && h->publish != NULL) h->publish(s_clip.text, s_clip.len);
    uint32_t gen = s_clip.gen;
    unlock();
    FMRB_LOGI(TAG, "set %u bytes (generation %u)", (unsigned)len, (unsigned)gen);
    (void)gen;
    return FMRB_OK;
}

size_t fmrb_clipboard_read(size_t off, char *buf, size_t cap, uint32_t *gen)
{
    if (!lock()) {
        if (gen) *gen = 0;
        return 0;
    }
    sync_from_host();
    size_t n = 0;
    if (off < s_clip.len && buf != NULL) {
        n = s_clip.len - off;
        if (n > cap) n = cap;
        memcpy(buf, s_clip.text + off, n);
    }
    if (gen) *gen = s_clip.gen;
    unlock();
    return n;
}

size_t fmrb_clipboard_length(void)
{
    if (!lock()) return 0;
    sync_from_host();
    size_t n = s_clip.len;
    unlock();
    return n;
}

uint32_t fmrb_clipboard_generation(void)
{
    if (!lock()) return 0;
    sync_from_host();
    uint32_t g = s_clip.gen;
    unlock();
    return g;
}

const char *fmrb_clipboard_lock(size_t *len)
{
    if (!lock()) {
        if (len) *len = 0;
        return NULL;
    }
    sync_from_host();
    if (len) *len = s_clip.len;
    return s_clip.text;
}

void fmrb_clipboard_unlock(void)
{
    unlock();
}
