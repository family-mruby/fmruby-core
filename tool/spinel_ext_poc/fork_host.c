/*
 * Timing host for the fork's current raycast gem (P1 comparison only).
 *
 * Links the fork-generated raycast_entry.c (--no-main --entry raycast_entry
 * --persistent-statics) against the fork runtime and provides the FFI surface
 * that spinel/raycast_ffi.rb declares, the way native/raycast_native.c does in
 * fmrb, minus the instance pool. One raycast_entry() call is one frame, so the
 * figure includes the per-call cost of the FFI getters, the output copy and
 * the entry prologue: that is what the gem pays today.
 *
 * Reads the same bench file as raycast_host (map + bench lines; other lines
 * are ignored) and prints "bench <n> <total ns>" per bench line. It also
 * writes the rays of the last call to OUT so the result can be spot-checked.
 *
 * usage: fork_host BENCH OUT
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int raycast_entry(void);
extern int sp_net_bin_len;

static char s_map[4096];
static int s_w, s_h, s_gen;
static int s_px, s_py, s_pa;
static char s_out[512];
static int s_out_len;

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

const char *raycast_spx_map(void)
{
    sp_net_bin_len = s_w * s_h;
    return s_map;
}
int raycast_spx_map_w(void) { return s_w; }
int raycast_spx_map_h(void) { return s_h; }
int raycast_spx_map_gen(void) { return s_gen; }
int raycast_spx_px(void) { return s_px; }
int raycast_spx_py(void) { return s_py; }
int raycast_spx_pa(void) { return s_pa; }
int raycast_spx_micros(void) { return (int)(now_ns() / 1000); }

void raycast_spx_output(const char *buf, int len, int us)
{
    (void)us;
    if (len > (int)sizeof(s_out)) len = (int)sizeof(s_out);
    memcpy(s_out, buf, (size_t)len);
    s_out_len = len;
}

void raycast_spx_log(const char *msg, int len)
{
    fprintf(stderr, "log: %.*s\n", len, msg);
}

static int hexval(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s BENCH OUT\n", argv[0]);
        return 2;
    }
    FILE *in = fopen(argv[1], "r");
    if (!in) {
        perror("open");
        return 2;
    }
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, in) > 0) {
        if (!strncmp(line, "map ", 4)) {
            int pos = 0;
            sscanf(line + 4, "%d %d %n", &s_w, &s_h, &pos);
            const char *hex = line + 4 + pos;
            for (int i = 0; i < s_w * s_h && i < (int)sizeof(s_map); i++) {
                s_map[i] = (char)(hexval(hex[2 * i]) * 16 + hexval(hex[2 * i + 1]));
            }
            s_gen++;
        } else if (!strncmp(line, "bench ", 6)) {
            long n;
            sscanf(line + 6, "%ld %d %d %d", &n, &s_px, &s_py, &s_pa);
            long long t0 = now_ns();
            for (long i = 0; i < n; i++) {
                raycast_entry();
            }
            printf("bench %ld %lld\n", n, now_ns() - t0);
        }
    }
    free(line);
    fclose(in);
    FILE *out = fopen(argv[2], "wb");
    if (out) {
        fwrite(s_out, 1, (size_t)s_out_len, out);
        fclose(out);
    }
    return 0;
}
