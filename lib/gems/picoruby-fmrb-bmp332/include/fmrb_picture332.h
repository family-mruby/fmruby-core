#ifndef FMRB_PICTURE332_H
#define FMRB_PICTURE332_H

// A picture file (a PNG or an 8-bit RGB332 BMP) looked at without decoding
// it, and rewritten as the small palette PNG, all in C so an app holds no
// pixels in its VM (doc/pictures P2): BMP332.info / BMP332.to_png and the
// app boundary's FmrbGfx#picture_info use these.
//
// fmrb_picture332_head and fmrb_picture332_to_png are pure C over buffers
// (the host test builds them, test/png332); the file functions read through
// the file HAL (picture332_file.c). Every buffer comes from the allocator
// passed in, so nothing here holds memory between calls.

#include <stdint.h>
#include <stddef.h>
#include "fmrb_png332.h"

#ifdef __cplusplus
extern "C" {
#endif

// The kinds of picture (the app boundary's picture_info format).
enum {
    FMRB_PICTURE332_PNG = 1,
    FMRB_PICTURE332_BMP = 2,
};

// What fmrb_picture332_to_png answers (and the file read).
enum {
    FMRB_PICTURE332_OK = 0,
    FMRB_PICTURE332_E_UNREADABLE = -1,  // not a picture, broken, not there
    FMRB_PICTURE332_E_TOO_BIG = -2,     // wider or taller than asked
    FMRB_PICTURE332_E_TOO_HEAVY = -3,   // the PNG written is more bytes than asked
    FMRB_PICTURE332_E_NOMEM = -4,
    FMRB_PICTURE332_E_FILE_TOO_LARGE = -5,  // the file is more bytes than asked
};

// The head of a file the size and kind are read from: a PNG's signature and
// IHDR (33 bytes) or a BMP's two headers (54 bytes).
#define FMRB_PICTURE332_HEAD_BYTES 54

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t format;     // FMRB_PICTURE332_PNG or _BMP
} fmrb_picture332_head_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint32_t bytes;     // the file's size
    uint32_t mtime;     // the file's modification time (0 where the file system has none)
} fmrb_picture332_file_info_t;

// The size and kind from the first bytes of a picture. 0, or -1 when they
// are not a PNG with its IHDR or an 8-bit uncompressed BMP (the pictures
// BMP332 reads), or a side is 0 or past 65535. The size is the file's own:
// one larger than FMRB_PNG332_MAX_DIM is reported as it is (the caller
// refuses it with its size).
int fmrb_picture332_head(const uint8_t *buf, size_t len, fmrb_picture332_head_t *out);

// The picture in buf decoded (as BMP332.parse) and written again as the
// palette PNG (as BMP332.encode_png): the same bytes those two give. A side
// past max_w / max_h is refused before anything is decoded (E_TOO_BIG, with
// *width and *height set), a PNG past max_bytes (when not 0) after it is
// written (E_TOO_HEAVY, with *out_len set and nothing returned). On success
// *out is *out_len bytes from the allocator; the caller frees it. buf is not
// changed (a BMP is decoded from a copy).
int fmrb_picture332_to_png(const uint8_t *buf, size_t len, uint16_t max_w, uint16_t max_h, size_t max_bytes,
                           const fmrb_png332_allocator_t *a, uint8_t **out, size_t *out_len,
                           uint16_t *width, uint16_t *height);

// The size, kind, bytes and time of a picture file, reading only its head.
// 0, or -1 when the file is not there or is not a picture.
int fmrb_picture332_file_info(const char *path, fmrb_picture332_file_info_t *out);

// The whole file into a buffer from the allocator. FMRB_PICTURE332_OK,
// E_FILE_TOO_LARGE when the file is more than max_bytes (nothing read;
// *len is its size), E_UNREADABLE or E_NOMEM.
int fmrb_picture332_file_read(const char *path, size_t max_bytes, const fmrb_png332_allocator_t *a,
                              uint8_t **buf, size_t *len);

#ifdef __cplusplus
}
#endif

#endif // FMRB_PICTURE332_H
