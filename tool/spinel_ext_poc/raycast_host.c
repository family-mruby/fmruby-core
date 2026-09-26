/*
 * Test host for the raycast ext library (P1 prototype, not part of any build).
 *
 * Plays the part fmrb's receiver (lib/add/picoruby-fmrb-raycast/native/
 * raycast_native.c) would play after the move to upstream's ext mechanism:
 * call Init_raycast() once, then the typed entries from the generated
 * contract header. Everything is driven by a case file so that the Ruby
 * driver can feed CRuby the very same sequence and compare the results.
 *
 * Case file, one command per line:
 *   map <w> <h> <hex bytes>     load_map (may raise; reported, not fatal)
 *   cast <px> <py> <pa>         cast; the packed rays are appended to OUT
 *   bench <n> <px> <py> <pa>    n casts of one pose, timed with CLOCK_MONOTONIC
 *   benchtry <n> <px> <py> <pa> the same, each call wrapped in Init_raycast_try
 *
 * stdout gets one event line per map/cast/bench command:
 *   map ok <return value>
 *   cast <byte length>
 *   <map|cast> raise <class>: <message>
 *   bench <n> <total ns>       (benchtry: benchtry <n> <total ns>)
 *
 * usage: raycast_host CASES OUT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "raycast_kernel.h"

/* Entry arguments and results travel through a context struct because
   Init_raycast_try only takes a void (*)(void *). */
typedef struct {
    const char *map;
    sp_int w, h;
    sp_int ret;
} load_ctx;

typedef struct {
    sp_int px, py, pa;
    const char *buf;
} cast_ctx;

static void do_load(void *p)
{
    load_ctx *c = (load_ctx *)p;
    c->ret = sp_RaycastKernel_s_load_map(c->map, c->w, c->h);
}

static void do_cast(void *p)
{
    cast_ctx *c = (cast_ctx *)p;
    c->buf = sp_RaycastKernel_s_cast(c->px, c->py, c->pa);
}

static int hexval(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s CASES OUT\n", argv[0]);
        return 2;
    }
    FILE *in = fopen(argv[1], "r");
    FILE *out = fopen(argv[2], "wb");
    if (!in || !out) {
        perror("open");
        return 2;
    }

    Init_raycast();

    char *line = NULL;
    size_t cap = 0;
    const char *cls = NULL, *msg = NULL;
    while (getline(&line, &cap, in) > 0) {
        if (!strncmp(line, "map ", 4)) {
            long w = 0, h = 0;
            int pos = 0;
            if (sscanf(line + 4, "%ld %ld %n", &w, &h, &pos) < 2) {
                fprintf(stderr, "bad map line\n");
                return 2;
            }
            const char *hex = line + 4 + pos;
            size_t hl = strcspn(hex, "\r\n");
            size_t n = hl / 2;
            /* Hand the kernel a Spinel heap string: the ext contract's
               `const char *` for a Ruby String is a marked runtime string
               (sp_str_byte_len reads the header in front of it), not a bare
               C buffer. sp_str_from_bytes keeps embedded NULs. */
            char *tmp = malloc(n ? n : 1);
            for (size_t i = 0; i < n; i++) {
                tmp[i] = (char)(hexval(hex[2 * i]) * 16 + hexval(hex[2 * i + 1]));
            }
            load_ctx c = { sp_str_from_bytes(tmp, n), (sp_int)w, (sp_int)h, 0 };
            free(tmp);
            if (Init_raycast_try(do_load, &c, &cls, &msg)) {
                printf("map raise %s: %s\n", cls, msg);
            } else {
                printf("map ok %lld\n", (long long)c.ret);
            }
        } else if (!strncmp(line, "cast ", 5)) {
            long px, py, pa;
            if (sscanf(line + 5, "%ld %ld %ld", &px, &py, &pa) != 3) {
                fprintf(stderr, "bad cast line\n");
                return 2;
            }
            cast_ctx c = { (sp_int)px, (sp_int)py, (sp_int)pa, NULL };
            if (Init_raycast_try(do_cast, &c, &cls, &msg)) {
                printf("cast raise %s: %s\n", cls, msg);
            } else {
                size_t len = sp_str_byte_len(c.buf);
                fwrite(c.buf, 1, len, out);
                printf("cast %zu\n", len);
            }
        } else if (!strncmp(line, "bench ", 6)) {
            long n, px, py, pa;
            if (sscanf(line + 6, "%ld %ld %ld %ld", &n, &px, &py, &pa) != 4) {
                fprintf(stderr, "bad bench line\n");
                return 2;
            }
            /* Called directly, without the try wrapper, as a frame loop would
               once the map is known to be good. */
            long long t0 = now_ns();
            size_t sink = 0;
            for (long i = 0; i < n; i++) {
                const char *b = sp_RaycastKernel_s_cast((sp_int)px, (sp_int)py, (sp_int)pa);
                sink += (unsigned char)b[0];
            }
            long long dt = now_ns() - t0;
            printf("bench %ld %lld\n", n, dt);
            if (sink == (size_t)-1) puts("");
        } else if (!strncmp(line, "benchtry ", 9)) {
            long n, px, py, pa;
            if (sscanf(line + 9, "%ld %ld %ld %ld", &n, &px, &py, &pa) != 4) {
                fprintf(stderr, "bad benchtry line\n");
                return 2;
            }
            /* What a host that must never let a raise escape pays per call:
               an uncaught raise ends the process (exit status 1). */
            cast_ctx c = { (sp_int)px, (sp_int)py, (sp_int)pa, NULL };
            long long t0 = now_ns();
            for (long i = 0; i < n; i++) {
                if (Init_raycast_try(do_cast, &c, &cls, &msg)) {
                    printf("benchtry raise %s: %s\n", cls, msg);
                    break;
                }
            }
            printf("benchtry %ld %lld\n", n, now_ns() - t0);
        }
    }
    free(line);
    fclose(in);
    fclose(out);
    return 0;
}
