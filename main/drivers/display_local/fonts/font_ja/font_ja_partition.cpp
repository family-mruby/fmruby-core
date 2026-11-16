// The efontJA fonts from the ESP32-P4's font partition (font_ja.h,
// doc/flash_layout F4). The partition holds the image tools/font_ja/
// font_ja_image.rb writes; it is mapped once at boot and each font's bytes are
// handed to LovyanGFX where they are, the way the linked arrays were (both
// are read through the flash cache).
//
// Every static here is in PSRAM: moving the fonts out of the firmware adds
// nothing to the internal RAM.

#include <new>
#include <stdio.h>
#include <string.h>

#include "esp_partition.h"

#include "fmrb_attr.h"
#include "fmrb_log.h"
#include "font_ja.h"
#include "font_ja_image.h"

static const char *TAG = "font_ja";

// The partition (config/partitions_p4_common.csv): data, this subtype, this label.
#define FONT_JA_PARTITION_LABEL   "font_ja"
#define FONT_JA_PARTITION_SUBTYPE 0x40

FMRB_EXT_RAM_BSS_ATTR static const lgfx::IFont *s_fonts[FONT_JA_COUNT];
FMRB_EXT_RAM_BSS_ATTR static font_ja_image_info_t s_info;
FMRB_EXT_RAM_BSS_ATTR static const char *s_why;   // NULL: not looked yet / fine
FMRB_EXT_RAM_BSS_ATTR static bool s_ok;

#if defined(FMRB_NO_FONT_JA)

fmrb_err_t font_ja_init(void)
{
    // Built with FMRB_FONT_JA=0: misaki 8 only, and nothing to look for.
    s_why = "built without the Japanese fonts (FMRB_FONT_JA=0)";
    return FMRB_ERR_NOT_SUPPORTED;
}

#else

// The image's names for font_ja_id_t, in its order (FONT_JA_16, 12, 12 bold).
static const char *const k_names[FONT_JA_COUNT] = {
    "efont_ja_16", "efont_ja_12", "efont_ja_12_b",
};

alignas(lgfx::U8g2font) FMRB_EXT_RAM_BSS_ATTR
static unsigned char s_store[FONT_JA_COUNT][sizeof(lgfx::U8g2font)];

fmrb_err_t font_ja_init(void)
{
    if (s_ok) return FMRB_OK;
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)FONT_JA_PARTITION_SUBTYPE,
        FONT_JA_PARTITION_LABEL);
    if (!part) {
        s_why = "no font_ja partition (an older partition table)";
        FMRB_LOGE(TAG, "%s: Japanese is drawn in misaki 8 only. "
                  "Flash the whole image once (rake flash:keep_home).", s_why);
        return FMRB_ERR_NOT_FOUND;
    }
    const void *base = NULL;
    esp_partition_mmap_handle_t handle;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                                       &base, &handle);
    if (err != ESP_OK) {
        s_why = "the font partition could not be mapped";
        FMRB_LOGE(TAG, "%s (%s): Japanese is drawn in misaki 8 only.", s_why,
                  esp_err_to_name(err));
        return FMRB_ERR_FAILED;
    }
    const uint8_t *img = (const uint8_t *)base;
    const char *why = NULL;
    fmrb_err_t ferr = font_ja_image_open(img, part->size, &s_info, &why);
    if (ferr != FMRB_OK) {
        s_why = why;
        if (ferr == FMRB_ERR_NOT_SUPPORTED) {
            FMRB_LOGE(TAG, "%s (v%u, this firmware reads v%d): Japanese is drawn in "
                      "misaki 8 only. Flash the whole image (rake flash:keep_home).",
                      why, s_info.version, FONT_JA_IMAGE_VERSION);
        } else {
            FMRB_LOGE(TAG, "%s: Japanese is drawn in misaki 8 only. "
                      "Flash the whole image (rake flash:keep_home).", why);
        }
        esp_partition_munmap(handle);
        return ferr;
    }
    const lgfx::IFont *found[FONT_JA_COUNT] = {};
    for (int i = 0; i < FONT_JA_COUNT; i++) {
        const uint8_t *data = font_ja_image_find(img, &s_info, k_names[i], NULL);
        if (!data) {
            FMRB_LOGE(TAG, "the font image has no %s: that size is drawn in misaki 8.",
                      k_names[i]);
            continue;
        }
        found[i] = new (s_store[i]) lgfx::U8g2font(data);
    }
    memcpy(s_fonts, found, sizeof(s_fonts));
    s_ok = true;
    s_why = NULL;
    FMRB_LOGI(TAG, "partition at 0x%06x (%u KB): image v%u, %u fonts, %u bytes, crc %08x",
              (unsigned)part->address, (unsigned)(part->size / 1024), s_info.version,
              s_info.count, (unsigned)s_info.size, (unsigned)s_info.crc);
    return FMRB_OK;
}

#endif // FMRB_NO_FONT_JA

const lgfx::IFont *font_ja_get(font_ja_id_t id)
{
    if ((int)id < 0 || (int)id >= (int)FONT_JA_COUNT) return nullptr;
    return s_fonts[id];
}

// "storage@0x800000+0x6b0000,font_ja@0xeb0000+0x150000": each partition the
// app-only flash relies on, as this firmware's table has it.
static size_t layout_of(char *out, size_t cap, const char *label)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_ANY,
                                                        ESP_PARTITION_SUBTYPE_ANY, label);
    int n = p ? snprintf(out, cap, "%s@0x%x+0x%x", label, (unsigned)p->address,
                         (unsigned)p->size)
              : snprintf(out, cap, "%s@none", label);
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}

size_t font_ja_flash_status(char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    char storage[48], font[48];
    layout_of(storage, sizeof(storage), "storage");
    layout_of(font, sizeof(font), FONT_JA_PARTITION_LABEL);
    char state[80];
    if (s_ok) {
        snprintf(state, sizeof(state), "v%u crc=%08x", s_info.version, (unsigned)s_info.crc);
    } else {
        snprintf(state, sizeof(state), "none: %s", s_why ? s_why : "not looked for");
    }
    int n = snprintf(out, cap, ",\"layout\":\"%s,%s\",\"font_ja\":\"%s\"", storage, font, state);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1;
}
