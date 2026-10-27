// Host tests of the machine's clipboard (doc/linux_native/, L12a):
//
//   kernel  main/kernel/fmrb_clipboard.c: set, read in pieces, length, the
//           generation, the maximum (a larger text is refused and the
//           clipboard left as it was), borrowing the text in place
//   join    native/backend/native_clip.c installed as the host: what an app
//           sets goes out to the main thread's side (take_out, once, with
//           the wake), what Linux offers comes in at the next read (line
//           ends made "\n", NUL bytes dropped, one generation per text), a
//           Linux text over the maximum empties the clipboard, a set after
//           a pending offer wins over it, and offers from another thread
//           while the kernel reads never give a torn text
//
// test/native/stub stands in for the RTOS (one mutex that always succeeds:
// the kernel's lock is the RTOS's) and the log.
//
// Usage: native_clip_test

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fmrb_clipboard.h"
#include "native_clip.h"

static int g_fail = 0;
static int g_count = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_count++;                                                    \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: ", __func__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
            g_fail++;                                                 \
        }                                                             \
    } while (0)

static char g_buf[FMRB_CLIPBOARD_MAX + 16];

static const char *read_all(size_t *len)
{
    size_t n = fmrb_clipboard_read(0, g_buf, sizeof(g_buf) - 1, NULL);
    g_buf[n] = '\0';
    if (len) *len = n;
    return g_buf;
}

// ---------------------------------------------------------------- kernel

static void test_kernel(void)
{
    CHECK(fmrb_clipboard_init() == FMRB_OK, "init");
    CHECK(fmrb_clipboard_length() == 0 && fmrb_clipboard_generation() == 0, "empty at the start");
    uint32_t g0 = fmrb_clipboard_generation();
    const char *t = "copy \xe3\x81\x93\xe3\x82\x93\nline 2";
    CHECK(fmrb_clipboard_set(t, strlen(t)) == FMRB_OK, "set");
    CHECK(fmrb_clipboard_generation() == g0 + 1, "one generation per set");
    CHECK(fmrb_clipboard_length() == strlen(t), "length %zu", fmrb_clipboard_length());
    CHECK(strcmp(read_all(NULL), t) == 0, "read back: %s", g_buf);
    char part[8];
    uint32_t gen = 0;
    size_t n = fmrb_clipboard_read(5, part, 3, &gen);
    CHECK(n == 3 && memcmp(part, "\xe3\x81\x93", 3) == 0 && gen == g0 + 1, "a piece from an offset");
    CHECK(fmrb_clipboard_read(strlen(t), part, 3, NULL) == 0, "nothing past the end");

    // The maximum: a larger text is refused, the clipboard unchanged.
    static char big[FMRB_CLIPBOARD_MAX + 1];
    memset(big, 'x', sizeof(big));
    CHECK(fmrb_clipboard_set(big, FMRB_CLIPBOARD_MAX + 1) == FMRB_ERR_INVALID_PARAM, "over the maximum refused");
    CHECK(fmrb_clipboard_generation() == g0 + 1 && strcmp(read_all(NULL), t) == 0, "...and the old text stays");
    CHECK(fmrb_clipboard_set(big, FMRB_CLIPBOARD_MAX) == FMRB_OK && fmrb_clipboard_length() == FMRB_CLIPBOARD_MAX,
          "exactly the maximum fits");

    // Borrowing in place.
    fmrb_clipboard_set("abc", 3);
    size_t len = 0;
    const char *p = fmrb_clipboard_lock(&len);
    CHECK(p != NULL && len == 3 && memcmp(p, "abc", 3) == 0, "borrowed in place");
    fmrb_clipboard_unlock();
    CHECK(fmrb_clipboard_set("", 0) == FMRB_OK && fmrb_clipboard_length() == 0, "an empty text");
}

// ------------------------------------------------------------------ join

static int g_wakes = 0;
static void wake(void) { g_wakes++; }

static void test_join(void)
{
    native_clip_install(wake);
    CHECK(native_clip_installed(), "installed");
    char *out = NULL;
    size_t olen = 0;
    CHECK(!native_clip_take_out(&out, &olen), "nothing to take before a set");

    // Family mruby -> Linux.
    fmrb_clipboard_set("from fmrb", 9);
    CHECK(g_wakes == 1, "the set woke the main thread (%d)", g_wakes);
    CHECK(native_clip_take_out(&out, &olen) && olen == 9 && strcmp(out, "from fmrb") == 0, "taken out");
    free(out);
    CHECK(!native_clip_take_out(&out, &olen), "taken once");

    // Linux -> Family mruby: at the next read, normalised, one generation.
    uint32_t g = fmrb_clipboard_generation();
    const char in[] = "a\r\nb\rc\0d";
    native_clip_offer(in, sizeof(in) - 1);
    size_t len = 0;
    CHECK(strcmp(read_all(&len), "a\nb\ncd") == 0 && len == 6, "Linux's text, line ends and NUL: [%s]", g_buf);
    CHECK(fmrb_clipboard_generation() == g + 1, "one generation for it");
    CHECK(!native_clip_take_out(&out, &olen), "a text from Linux does not go back out");

    // Over the maximum from Linux: refused, the clipboard empty.
    native_clip_offer(NULL, FMRB_CLIPBOARD_MAX + 100);
    CHECK(fmrb_clipboard_length() == 0, "a Linux text over the maximum empties the clipboard");

    // A set after a pending offer wins over it.
    native_clip_offer("older", 5);
    fmrb_clipboard_set("newer", 5);
    CHECK(strcmp(read_all(NULL), "newer") == 0, "the set wins: [%s]", g_buf);
    CHECK(native_clip_take_out(&out, &olen) && strcmp(out, "newer") == 0, "...and goes out");
    free(out);

    native_clip_stats_t st;
    native_clip_stats(&st);
    CHECK(st.out == 2 && st.in == 3 && st.in_big == 1 && st.taken == 3, "counts out=%u in=%u big=%u taken=%u",
          st.out, st.in, st.in_big, st.taken);
}

// Offers from the main thread's side while the kernel reads: every text read
// is one whole offer.
static volatile int g_stop = 0;

static void *offerer(void *arg)
{
    (void)arg;
    static char a[4096], b[4096];
    memset(a, 'A', sizeof(a));
    memset(b, 'B', sizeof(b));
    for (int i = 0; !g_stop; i++) native_clip_offer((i & 1) ? a : b, sizeof(a));
    return NULL;
}

static void test_threads(void)
{
    pthread_t th;
    pthread_create(&th, NULL, offerer, NULL);
    int torn = 0, reads = 0;
    for (int i = 0; i < 20000; i++) {
        size_t len = 0;
        const char *t = read_all(&len);
        if (len == 0 || (t[0] != 'A' && t[0] != 'B')) continue;   /* before the first offer */
        reads++;
        for (size_t k = 1; k < len; k++) {
            if (t[k] != t[0]) {
                torn++;
                break;
            }
        }
        if (len != 4096) torn++;
    }
    g_stop = 1;
    pthread_join(th, NULL);
    CHECK(torn == 0 && reads > 0, "%d torn of %d reads", torn, reads);
}

int main(void)
{
    test_kernel();
    test_join();
    test_threads();
    if (g_fail) {
        printf("native clip: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("native clip: %d checks passed (the kernel's clipboard, the join with Linux, offers from another thread)\n",
           g_count);
    return 0;
}
