/*
 * Test host for the FFT ext library (P1 prototype, T4; not part of any build).
 *
 * Case file, one command per line:
 *   run <n> <iters> <hex samples>   FftKernel.run     (double core)
 *   q15 <n> <iters> <hex samples>   FftKernel.run_q15 (Q15 core)
 *   level <i> <floor_db>            FftKernel.level_db (Float in, Float out)
 *
 * Magnitude bytes of run/q15 are appended to OUT; stdout gets one event line
 * per command:
 *   run <byte length> | q15 <byte length> | level <%.17g>
 *   <cmd> raise <class>: <message>
 *
 * usage: fft_host CASES OUT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fft_kernel.h"

typedef struct {
    int q15;
    const char *samples;
    sp_int n, iters;
    const char *out;
} run_ctx;

typedef struct {
    sp_int i;
    sp_float floor_db;
    sp_float ret;
} level_ctx;

static void do_run(void *p)
{
    run_ctx *c = (run_ctx *)p;
    c->out = c->q15 ? sp_FftKernel_s_run_q15(c->samples, c->n, c->iters)
                    : sp_FftKernel_s_run(c->samples, c->n, c->iters);
}

static void do_level(void *p)
{
    level_ctx *c = (level_ctx *)p;
    c->ret = sp_FftKernel_s_level_db(c->i, c->floor_db);
}

static int hexval(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
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

    Init_fft();

    char *line = NULL;
    size_t cap = 0;
    const char *cls = NULL, *msg = NULL;
    while (getline(&line, &cap, in) > 0) {
        int q15 = !strncmp(line, "q15 ", 4);
        if (q15 || !strncmp(line, "run ", 4)) {
            long n = 0, iters = 0;
            int pos = 0;
            if (sscanf(line + 4, "%ld %ld %n", &n, &iters, &pos) < 2) {
                fprintf(stderr, "bad run line\n");
                return 2;
            }
            const char *hex = line + 4 + pos;
            size_t nb = strcspn(hex, "\r\n") / 2;
            char *tmp = malloc(nb ? nb : 1);
            for (size_t i = 0; i < nb; i++) {
                tmp[i] = (char)(hexval(hex[2 * i]) * 16 + hexval(hex[2 * i + 1]));
            }
            /* A Spinel heap string, as the contract's `const char *` expects. */
            run_ctx c = { q15, sp_str_from_bytes(tmp, nb), (sp_int)n, (sp_int)iters, NULL };
            free(tmp);
            const char *tag = q15 ? "q15" : "run";
            if (Init_fft_try(do_run, &c, &cls, &msg)) {
                printf("%s raise %s: %s\n", tag, cls, msg);
            } else {
                size_t len = sp_str_byte_len(c.out);
                fwrite(c.out, 1, len, out);
                printf("%s %zu\n", tag, len);
            }
        } else if (!strncmp(line, "level ", 6)) {
            long i = 0;
            double fl = 0;
            if (sscanf(line + 6, "%ld %lf", &i, &fl) != 2) {
                fprintf(stderr, "bad level line\n");
                return 2;
            }
            level_ctx c = { (sp_int)i, (sp_float)fl, 0 };
            if (Init_fft_try(do_level, &c, &cls, &msg)) {
                printf("level raise %s: %s\n", cls, msg);
            } else {
                printf("level %.17g\n", (double)c.ret);
            }
        }
    }
    free(line);
    fclose(in);
    fclose(out);
    return 0;
}
