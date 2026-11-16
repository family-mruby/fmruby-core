// Host runner for the font image reader (main/drivers/display_local/fonts/
// font_ja/font_ja_image.c): opens an image file the way the board opens its
// mapped partition and prints what it found, for test/font_ja/run.rb.
//
//   font_ja_test IMAGE AVAIL NAME...
//
// AVAIL is the partition size to pretend (0: the file's size). Prints
//   open <code> <why>
//   info <version> <count> <size> <crc hex>       (when open succeeded)
//   font <name> <size> <first byte> <last byte> <sum of bytes mod 2^32>
//   font <name> missing

#include <stdio.h>
#include <stdlib.h>

#include "font_ja_image.h"

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s IMAGE AVAIL NAME...\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len > 0 ? (size_t)len : 1);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    fclose(f);
    size_t avail = (size_t)strtoul(argv[2], NULL, 10);
    if (avail == 0 || avail > (size_t)len) avail = (size_t)len;

    font_ja_image_info_t info;
    const char *why = NULL;
    fmrb_err_t err = font_ja_image_open(buf, avail, &info, &why);
    printf("open %d %s\n", (int)err, why ? why : "");
    if (err == FMRB_OK) {
        printf("info %u %u %u %08x\n", info.version, info.count, (unsigned)info.size,
               (unsigned)info.crc);
        for (int i = 3; i < argc; i++) {
            uint32_t size = 0;
            const uint8_t *p = font_ja_image_find(buf, &info, argv[i], &size);
            if (!p) {
                printf("font %s missing\n", argv[i]);
                continue;
            }
            uint32_t sum = 0;
            for (uint32_t k = 0; k < size; k++) sum += p[k];
            printf("font %s %u %u %u %u\n", argv[i], (unsigned)size,
                   size ? p[0] : 0, size ? p[size - 1] : 0, (unsigned)sum);
        }
    }
    free(buf);
    return 0;
}
