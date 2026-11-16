#pragma once

// The Japanese font image (doc/flash_layout F4): the efontJA fonts in one
// blob with a version and a directory, written by tools/font_ja/font_ja_image.rb
// into the ESP32-P4's font partition. This reader is plain C over a buffer,
// so the host test (rake font_ja:test) runs the same code the board does.

#include <stddef.h>
#include <stdint.h>

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_JA_IMAGE_MAGIC       "FMRBFONT"
#define FONT_JA_IMAGE_VERSION     1
#define FONT_JA_IMAGE_HEADER_SIZE 24
#define FONT_JA_IMAGE_ENTRY_SIZE  32
#define FONT_JA_IMAGE_NAME_MAX    24

/** What the header of a font image says. */
typedef struct {
    uint16_t version;  /**< format version (FONT_JA_IMAGE_VERSION) */
    uint16_t count;    /**< number of fonts in the directory */
    uint32_t size;     /**< bytes from the header to the end of the last font */
    uint32_t crc;      /**< CRC-32 of the bytes after the header (not checked here) */
} font_ja_image_info_t;

/**
 * @brief Check a font image's header and directory.
 *
 * Checks the magic, the version, that the image fits in @p avail bytes and
 * that every entry lies inside it. The CRC is not walked (a megabyte through
 * the flash cache at every boot); it is read so a tool can compare it.
 *
 * @param base  the start of the image (the mapped partition)
 * @param avail bytes readable from @p base (the partition size)
 * @param info  filled on FMRB_OK; on FMRB_ERR_NOT_SUPPORTED its version is the one found
 * @param why   set to a short English reason when the result is not FMRB_OK
 * @return FMRB_OK, FMRB_ERR_NOT_FOUND (no image: no magic, e.g. an erased
 *         partition), FMRB_ERR_NOT_SUPPORTED (another format version),
 *         FMRB_ERR_FAILED (a malformed header or directory)
 */
fmrb_err_t font_ja_image_open(const uint8_t *base, size_t avail,
                              font_ja_image_info_t *info, const char **why);

/**
 * @brief Find a font by its name in an image font_ja_image_open accepted.
 *
 * @param size set to the font's size in bytes when found (may be NULL)
 * @return the font's first byte, or NULL when the image has no such font
 */
const uint8_t *font_ja_image_find(const uint8_t *base, const font_ja_image_info_t *info,
                                  const char *name, uint32_t *size);

#ifdef __cplusplus
}
#endif
