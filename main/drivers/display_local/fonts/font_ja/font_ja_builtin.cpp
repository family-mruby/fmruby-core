// The efontJA fonts linked into the program from M5GFX: the native program,
// the browser build and the S3 boards that draw on their own panel (font_ja.h).
// The ESP32-P4 reads them from its font partition instead (font_ja_partition.cpp).

#include "font_ja.h"

fmrb_err_t font_ja_init(void)
{
#if defined(FMRB_NO_FONT_JA)
    return FMRB_ERR_NOT_SUPPORTED;
#else
    return FMRB_OK;
#endif
}

size_t font_ja_flash_status(char *out, size_t cap)
{
    if (out && cap) out[0] = '\0';
    return 0;
}

const lgfx::IFont *font_ja_get(font_ja_id_t id)
{
#if defined(FMRB_NO_FONT_JA)
    // Built with FMRB_FONT_JA=0: nothing here may name the fonts, or the
    // linker keeps them.
    (void)id;
    return nullptr;
#else
    switch (id) {
    case FONT_JA_16:      return &lgfx::fonts::efontJA_16;
    case FONT_JA_12:      return &lgfx::fonts::efontJA_12;
    case FONT_JA_12_BOLD: return &lgfx::fonts::efontJA_12_b;
    default:              return nullptr;
    }
#endif
}
