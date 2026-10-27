/* clip_wasm.c - the machine's clipboard joined to the browser's
 * (doc/linux_native, L12a).
 *
 * The page (web/main.js) runs on the browser's main thread and reaches the
 * module's shared memory, like the input ring and the frame. Two slots,
 * each a buffer, a length and a sequence that is odd while its writer is
 * writing (a sequence lock), so a reader never takes half a text:
 *
 *   out  Family mruby -> page: the kernel's publish (fmrb_clipboard.h)
 *        writes it from the task that copied; the page polls the sequence
 *        each animation frame and hands the text to
 *        navigator.clipboard.writeText (in the few milliseconds after the
 *        key that copied, which counts as the user's doing).
 *   in   page -> Family mruby: the page writes the browser's text when it
 *        is given one (the paste event of Ctrl+V / Cmd+V, which needs no
 *        permission); the kernel takes it the next time an app reads the
 *        clipboard. The page holds the paste key back until the text is in
 *        (or a short while has passed), so the app reads the new text.
 *
 * When the browser refuses either way (no paste event, writeText rejected:
 * the page's rules), the machine keeps its own clipboard and the page says
 * so on the console.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <emscripten/emscripten.h>

#include "fmrb_clipboard.h"

static char s_out[FMRB_CLIPBOARD_MAX];
static volatile uint32_t s_out_len;
static volatile uint32_t s_out_seq;   /* odd while being written */

static char s_in[FMRB_CLIPBOARD_MAX];
static volatile uint32_t s_in_len;
static volatile uint32_t s_in_seq;    /* odd while the page writes */

EMSCRIPTEN_KEEPALIVE char *fmrb_wasm_clip_out_buf(void) { return s_out; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_clip_out_len_ptr(void) { return &s_out_len; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_clip_out_seq_ptr(void) { return &s_out_seq; }
EMSCRIPTEN_KEEPALIVE char *fmrb_wasm_clip_in_buf(void) { return s_in; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_clip_in_cap(void) { return FMRB_CLIPBOARD_MAX; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_clip_in_len_ptr(void) { return &s_in_len; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_clip_in_seq_ptr(void) { return &s_in_seq; }

/* Under the kernel clipboard's lock: one writer at a time. */
static void host_publish(const char *text, size_t len)
{
    if (len > FMRB_CLIPBOARD_MAX) return;
    __atomic_add_fetch(&s_out_seq, 1, __ATOMIC_SEQ_CST);   /* odd: writing */
    if (len > 0) memcpy(s_out, text, len);
    __atomic_store_n(&s_out_len, (uint32_t)len, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&s_out_seq, 1, __ATOMIC_SEQ_CST);   /* even: done */
}

static bool host_take(uint32_t *seq, char *buf, size_t cap, size_t *len)
{
    uint32_t s0 = __atomic_load_n(&s_in_seq, __ATOMIC_SEQ_CST);
    if (s0 == *seq || (s0 & 1u)) return false;   /* nothing new, or mid-write */
    uint32_t n = __atomic_load_n(&s_in_len, __ATOMIC_SEQ_CST);
    if (n <= cap && n <= FMRB_CLIPBOARD_MAX) memcpy(buf, s_in, n);
    if (__atomic_load_n(&s_in_seq, __ATOMIC_SEQ_CST) != s0) return false;   /* rewritten meanwhile */
    *len = n;
    *seq = s0;
    return true;
}

static const fmrb_clipboard_host_t s_host = {
    .publish = host_publish,
    .take = host_take,
};

void fmrb_wasm_clip_install(void)
{
    fmrb_clipboard_host_install(&s_host);
}
