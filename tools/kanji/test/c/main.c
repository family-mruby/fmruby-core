// Host driver for components/fmrb_kanji (tools/kanji/test/c_lookup.rb).
// Reads one command per line on stdin and prints one answer line each:
//
//   open PATH      -> open<TAB>rc
//   info           -> info<TAB>file_size char_count block_count reading_count table_bytes
//   E READING      -> E<TAB>rc[<TAB>cand...]            exact reading
//   P READING      -> P<TAB>rc[<TAB>matched<TAB>cand...]  longest leading part
//   R READING      -> R<TAB>rc[<TAB>cand...]            exact, candidates read last to first
//   S READING      -> S<TAB>rc                          first candidate into a 4-byte buffer
//   verify         -> verify<TAB>rc
//   failreads N    -> (no answer) the file HAL fails reads after N more
//   stats          -> stats<TAB>block_reads<TAB>microseconds since the last stats
//   close          -> close<TAB>live bytes and blocks left in the pool

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fmrb_kanji.h"

extern long kd_live_bytes;
extern long kd_live_blocks;
extern long kd_fail_reads_after;

static uint8_t s_block[FMRB_KANJI_BLOCK_SIZE];
static char s_line[8192];
static char s_cand[FMRB_KANJI_CAND_UTF8_MAX];

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void print_cands(fmrb_kanji_result_t *r, int reverse)
{
    for (int k = 0; k < r->count; k++) {
        int i = reverse ? r->count - 1 - k : k;
        size_t len = 0;
        fmrb_err_t err = fmrb_kanji_candidate(r, (uint16_t)i, s_cand, sizeof(s_cand), &len);
        if (err != FMRB_OK || strlen(s_cand) != len) {
            printf("\t<error %d>", err);
            return;
        }
        printf("\t%s", s_cand);
    }
}

int main(void)
{
    fmrb_kanji_dict_t *dict = NULL;
    double t0 = now_us();
    while (fgets(s_line, sizeof(s_line), stdin)) {
        size_t n = strlen(s_line);
        while (n > 0 && (s_line[n - 1] == '\n' || s_line[n - 1] == '\r')) {
            s_line[--n] = '\0';
        }
        char *arg = strchr(s_line, ' ');
        if (arg) {
            *arg++ = '\0';
        } else {
            arg = s_line + n;
        }
        fmrb_kanji_result_t r;
        if (strcmp(s_line, "open") == 0) {
            fmrb_kanji_close(dict);
            dict = NULL;
            printf("open\t%d\n", fmrb_kanji_open(arg, &dict));
        } else if (strcmp(s_line, "info") == 0) {
            fmrb_kanji_info_t info;
            memset(&info, 0, sizeof(info));
            fmrb_kanji_get_info(dict, &info);
            printf("info\t%u %u %u %u %u\n", info.file_size, info.char_count, info.block_count,
                   info.reading_count, info.table_bytes);
        } else if (strcmp(s_line, "E") == 0 || strcmp(s_line, "R") == 0) {
            fmrb_err_t err = fmrb_kanji_lookup(dict, arg, strlen(arg), s_block, &r);
            printf("%s\t%d", s_line, err);
            if (err == FMRB_OK) {
                print_cands(&r, s_line[0] == 'R');
            }
            printf("\n");
        } else if (strcmp(s_line, "P") == 0) {
            fmrb_err_t err = fmrb_kanji_longest_prefix(dict, arg, strlen(arg), s_block, &r);
            printf("P\t%d", err);
            if (err == FMRB_OK) {
                printf("\t%u", r.matched_bytes);
                print_cands(&r, 0);
            }
            printf("\n");
        } else if (strcmp(s_line, "S") == 0) {
            fmrb_err_t err = fmrb_kanji_lookup(dict, arg, strlen(arg), s_block, &r);
            if (err == FMRB_OK) {
                char small[4];
                err = fmrb_kanji_candidate(&r, 0, small, sizeof(small), NULL);
            }
            printf("S\t%d\n", err);
        } else if (strcmp(s_line, "verify") == 0) {
            printf("verify\t%d\n", fmrb_kanji_verify(dict, s_block));
        } else if (strcmp(s_line, "failreads") == 0) {
            kd_fail_reads_after = atol(arg);
        } else if (strcmp(s_line, "stats") == 0) {
            double t = now_us();
            printf("stats\t%u\t%.0f\n", fmrb_kanji_block_reads(dict), t - t0);
            t0 = t;
        } else if (strcmp(s_line, "close") == 0) {
            fmrb_kanji_close(dict);
            dict = NULL;
            kd_fail_reads_after = -1;
            printf("close\t%ld %ld\n", kd_live_bytes, kd_live_blocks);
        } else {
            printf("? %s\n", s_line);
        }
    }
    fmrb_kanji_close(dict);
    return 0;
}
