/*
 * Size baseline for the WAMR proof of concept: reads a file, prints with
 * printf and allocates like host.c does, but has no WAMR. The difference
 * between poc_host and this program is what the runtime adds.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int
main(int argc, char **argv)
{
    struct timespec ts;
    FILE *fp;
    long n = 0;
    char *buf;
    if (argc < 2)
        return 2;
    fp = fopen(argv[1], "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        n = ftell(fp);
        fclose(fp);
    }
    buf = malloc((size_t)n + 1);
    memset(buf, 0, (size_t)n + 1);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("RESULT baseline size=%ld t=%ld\n", n, (long)ts.tv_sec);
    free(buf);
    return 0;
}
