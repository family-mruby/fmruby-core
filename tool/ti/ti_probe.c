/*
 * What one completion request costs (picoruby-ti), measured on the host.
 *
 * Build and run with `rake ti:probe` (it links the engine from the pinned
 * checkout and the libprism that rake ti:test builds from our own prism).
 *
 * Two numbers matter for the editor:
 *   - the peak prism holds while a document is parsed, because on the device
 *     that comes out of the editor task's memory pool, and
 *   - the wall time of one request, because it happens on a Tab press.
 *
 * The generated source is a plausible app repeated until it reaches the
 * requested size, ending at a completion site the type database can answer
 * (`s = "abc"` then `s.up`). The engine parses the source three times per
 * request (two evaluation rounds plus the suggestion pass), so the time is
 * three parses while the peak is one tree.
 *
 * usage: ti_probe [reps [size_kb ...]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>

#include "picoruby_ti_suggest.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static const char *UNIT =
    "class Block%d\n"
    "  def initialize\n"
    "    @x = 10\n"
    "    @y = 20\n"
    "    @label = \"block\"\n"
    "  end\n"
    "  def draw(gfx)\n"
    "    gfx.fill_rect(@x, @y, 8, 8, 0xE0)\n"
    "    gfx.draw_text(@x, @y, @label, 0xFF)\n"
    "  end\n"
    "  def move(dx, dy)\n"
    "    @x = @x + dx\n"
    "    @y = @y + dy\n"
    "  end\n"
    "end\n";

/* The completion site: the cursor sits right after "s.up". */
static const char *TAIL =
    "s = \"abc\"\n"
    "s.up";

static char *build_source(size_t want, size_t *out_len, int *out_cursor)
{
    size_t cap = want + 4096;
    char *buf = (char *)malloc(cap);
    size_t len = 0;
    int n = 0;
    while (len < want) {
        len += (size_t)snprintf(buf + len, cap - len, UNIT, n++);
    }
    len += (size_t)snprintf(buf + len, cap - len, "%s", TAIL);
    *out_len = len;
    *out_cursor = (int)len;
    return buf;
}

int main(int argc, char **argv)
{
    static const size_t DEFAULT_SIZES[] = { 0, 2 * 1024, 8 * 1024, 32 * 1024, 200 * 1024 };
    size_t sizes_buf[64];
    const size_t *sizes = DEFAULT_SIZES;
    size_t size_count = sizeof(DEFAULT_SIZES) / sizeof(DEFAULT_SIZES[0]);
    int reps = (argc > 1) ? atoi(argv[1]) : 5;

    if (argc > 2) {
        size_count = 0;
        for (int a = 2; a < argc && size_count < 64; a++) {
            sizes_buf[size_count++] = (size_t)atol(argv[a]) * 1024;
        }
        sizes = sizes_buf;
    }

    printf("%10s %10s %12s %12s %10s\n",
           "src(B)", "cands", "peak(KB)", "residue(B)", "time(ms)");

    for (size_t i = 0; i < size_count; i++) {
        size_t len = 0;
        int cursor = 0;
        char *src = build_source(sizes[i], &len, &cursor);

        TiSource item = {
            .source = src,
            .source_byte_length = (int)len,
        };
        TiSourceList list = { .items = &item, .count = 1 };
        TiSuggestionList out;

        /* Warm up so the measured runs do not pay for first-touch heap growth. */
        ti_fill_suggestions_at_cursor(&list, cursor, &out);

        struct mallinfo2 before = mallinfo2();
        double best = 1e9;
        int count = 0;
        for (int r = 0; r < reps; r++) {
            double t0 = now_ms();
            count = ti_fill_suggestions_at_cursor(&list, cursor, &out);
            double dt = now_ms() - t0;
            if (dt < best) best = dt;
        }
        struct mallinfo2 after = mallinfo2();

        /* The engine frees the tree before returning, so the peak is measured
           with a separate parse that stops while the tree is still alive. */
        struct mallinfo2 m0 = mallinfo2();
        pm_parser_t parser;
        pm_parser_init(&parser, (const uint8_t *)src, len, NULL);
        pm_node_t *root = pm_parse(&parser);
        struct mallinfo2 m1 = mallinfo2();
        pm_node_destroy(&parser, root);
        pm_parser_free(&parser);

        /* residue is heap in use after the repeats minus before them. It grows
           logarithmically with the repeat count (allocator bins), not linearly,
           which is how we know a request does not leak. */
        printf("%10zu %10d %12.1f %12ld %10.2f\n",
               len, count,
               (double)(m1.uordblks - m0.uordblks) / 1024.0,
               (long)(after.uordblks - before.uordblks),
               best);

        free(src);
    }
    return 0;
}
