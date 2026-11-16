#include "font_ja_image.h"

#include <string.h>

// Little endian, read byte by byte: the image is mapped flash and nothing
// here may assume the compiler's view of alignment.
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static const uint8_t *entry_at(const uint8_t *base, uint32_t i)
{
    return base + FONT_JA_IMAGE_HEADER_SIZE + (size_t)FONT_JA_IMAGE_ENTRY_SIZE * i;
}

fmrb_err_t font_ja_image_open(const uint8_t *base, size_t avail,
                              font_ja_image_info_t *info, const char **why)
{
    const char *dummy;
    if (!why) why = &dummy;
    if (!base || !info) {
        *why = "no image";
        return FMRB_ERR_INVALID_PARAM;
    }
    if (avail < FONT_JA_IMAGE_HEADER_SIZE ||
        memcmp(base, FONT_JA_IMAGE_MAGIC, 8) != 0) {
        *why = "no font image in the partition";
        return FMRB_ERR_NOT_FOUND;
    }
    info->version = rd16(base + 8);
    info->count = rd16(base + 10);
    info->size = rd32(base + 12);
    info->crc = rd32(base + 16);
    if (info->version != FONT_JA_IMAGE_VERSION) {
        *why = "the font image is another format version";
        return FMRB_ERR_NOT_SUPPORTED;
    }
    if (info->size > avail) {
        *why = "the font image is larger than the partition";
        return FMRB_ERR_FAILED;
    }
    uint32_t dir_end = FONT_JA_IMAGE_HEADER_SIZE +
                       (uint32_t)FONT_JA_IMAGE_ENTRY_SIZE * info->count;
    if (info->count == 0 || dir_end > info->size) {
        *why = "the font image's directory is broken";
        return FMRB_ERR_FAILED;
    }
    for (uint32_t i = 0; i < info->count; i++) {
        const uint8_t *e = entry_at(base, i);
        uint32_t off = rd32(e + FONT_JA_IMAGE_NAME_MAX);
        uint32_t len = rd32(e + FONT_JA_IMAGE_NAME_MAX + 4);
        if (e[FONT_JA_IMAGE_NAME_MAX - 1] != '\0' || off < dir_end ||
            off > info->size || len > info->size - off) {
            *why = "the font image's directory is broken";
            return FMRB_ERR_FAILED;
        }
    }
    *why = "";
    return FMRB_OK;
}

const uint8_t *font_ja_image_find(const uint8_t *base, const font_ja_image_info_t *info,
                                  const char *name, uint32_t *size)
{
    if (!base || !info || !name) return NULL;
    for (uint32_t i = 0; i < info->count; i++) {
        const uint8_t *e = entry_at(base, i);
        if (strncmp((const char *)e, name, FONT_JA_IMAGE_NAME_MAX) != 0) continue;
        if (size) *size = rd32(e + FONT_JA_IMAGE_NAME_MAX + 4);
        return base + rd32(e + FONT_JA_IMAGE_NAME_MAX);
    }
    return NULL;
}
