/*
 * native_ime.c - the PC's IME for the Family mruby apps: the hand-overs
 * (native_ime.h).
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "fmrb_kernel.h"
#include "host_task.h"
#include "linux_fcitx.h"
#include "native_ime.h"
#include "native_input.h"

static atomic_bool s_present;

/* What the IME types: Japanese (1) or letters (0), and a number moved on
 * every change (the host task follows it). While fcitx5 answers on the
 * bus (s_bus_says), its answers decide; else the keys are guessed from. */
static atomic_int s_ja;
static atomic_uint s_ja_seq;
static atomic_bool s_bus_says;
static atomic_bool s_bus_absent_said;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    bool on;
    int fx, fy, h;
    unsigned seq;
} s_cursor;

static atomic_uint s_commits, s_commit_bytes;

static void set_japanese(bool ja, const char *why)
{
    if (atomic_exchange(&s_ja, ja ? 1 : 0) == (ja ? 1 : 0)) return;
    atomic_fetch_add(&s_ja_seq, 1);
    fprintf(stderr, "native: IME: %s input (%s)\n", ja ? "Japanese" : "letters", why);
}

/* The fcitx5 thread's answers. */
static void on_fcitx(linux_fcitx_answer_t answer, const char *im)
{
    char why[96];
    switch (answer) {
    case LINUX_FCITX_ABSENT:
        atomic_store(&s_bus_says, false);
        if (!atomic_exchange(&s_bus_absent_said, true)) {
            fprintf(stderr, "native: IME: fcitx5 is not on the session bus: the kana indicator guesses from the keys\n");
        }
        break;
    case LINUX_FCITX_NO_CONTEXT:
        break;
    case LINUX_FCITX_LETTERS:
    case LINUX_FCITX_JAPANESE:
        if (!atomic_exchange(&s_bus_says, true)) {
            fprintf(stderr, "native: IME: fcitx5 answers on the session bus: the kana indicator follows it\n");
        }
        atomic_store(&s_bus_absent_said, false);
        snprintf(why, sizeof(why), "fcitx5: %s", im[0] ? im : "no input method name");
        set_japanese(answer == LINUX_FCITX_JAPANESE, why);
        break;
    }
}

void native_ime_set_present(bool on, const char *what)
{
    bool was = atomic_exchange(&s_present, on);
    if (was == on) return;
    if (on) {
        linux_fcitx_start(on_fcitx);
        linux_fcitx_watch(true);
        linux_fcitx_ask();
        fprintf(stderr, "native: IME: %s types for the Family mruby apps; their own kana input steps aside\n",
                what ? what : "an IME");
    } else {
        linux_fcitx_watch(false);
        /* A next IME starts afresh: letters, its answers to be learned. */
        atomic_store(&s_bus_says, false);
        set_japanese(false, "the IME went");
        fprintf(stderr, "native: IME: %s gone; the Family mruby apps' own kana input is back\n",
                what ? what : "the IME");
    }
}

void native_ime_recheck(void)
{
    if (atomic_load(&s_present)) linux_fcitx_ask();
}

void native_ime_guess(bool japanese, const char *sign)
{
    if (!atomic_load(&s_present) || atomic_load(&s_bus_says)) return;
    set_japanese(japanese, sign);
}

bool fmrb_native_ime_japanese(unsigned *seq)
{
    if (seq) *seq = atomic_load(&s_ja_seq);
    return atomic_load(&s_ja) == 1;
}

bool fmrb_native_ime_present(void)
{
    return atomic_load(&s_present);
}

void fmrb_native_ime_publish_cursor(bool on, int fx, int fy, int h)
{
    pthread_mutex_lock(&s_lock);
    if (on != s_cursor.on || (on && (fx != s_cursor.fx || fy != s_cursor.fy || h != s_cursor.h))) {
        s_cursor.on = on;
        s_cursor.fx = fx;
        s_cursor.fy = fy;
        s_cursor.h = h;
        s_cursor.seq++;
    }
    pthread_mutex_unlock(&s_lock);
}

bool native_ime_cursor(int *fx, int *fy, int *h, unsigned *seq)
{
    pthread_mutex_lock(&s_lock);
    bool on = s_cursor.on;
    if (fx) *fx = s_cursor.fx;
    if (fy) *fy = s_cursor.fy;
    if (h) *h = s_cursor.h;
    if (seq) *seq = s_cursor.seq;
    pthread_mutex_unlock(&s_lock);
    return on;
}

/* Twelve bytes to an event: b, c and d carry them (little end first), a the
 * count. */
void native_ime_commit(const char *utf8)
{
    if (utf8 == NULL) return;
    size_t len = strlen(utf8);
    if (len == 0) return;
    atomic_fetch_add(&s_commits, 1);
    atomic_fetch_add(&s_commit_bytes, (unsigned)len);
    for (size_t off = 0; off < len; off += 12) {
        uint32_t w[3] = { 0, 0, 0 };
        size_t n = len - off > 12 ? 12 : len - off;
        for (size_t i = 0; i < n; i++) w[i / 4] |= (uint32_t)(uint8_t)utf8[off + i] << (8 * (i % 4));
        native_input_push(NATIVE_INPUT_TEXT, (uint32_t)n, w[0], w[1], w[2]);
    }
}

void native_ime_deliver(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    char bytes[12];
    uint32_t w[3] = { b, c, d };
    int n = a > 12 ? 12 : (int)a;
    for (int i = 0; i < n; i++) bytes[i] = (char)((w[i / 4] >> (8 * (i % 4))) & 0xFF);
    fmrb_host_send_text(bytes, n);
}

void native_ime_stats(native_ime_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->present = atomic_load(&s_present);
    out->japanese = atomic_load(&s_ja) == 1;
    out->from_fcitx = atomic_load(&s_bus_says);
    out->commits = atomic_load(&s_commits);
    out->commit_bytes = atomic_load(&s_commit_bytes);
    pthread_mutex_lock(&s_lock);
    out->cursor_changes = s_cursor.seq;
    pthread_mutex_unlock(&s_lock);
}
