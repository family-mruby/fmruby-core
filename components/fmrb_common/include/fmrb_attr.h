#pragma once

// Memory-placement attributes for shared code (doc/idf_seam/). On the ESP32
// targets they forward to ESP-IDF's esp_attr.h; everywhere else (the linux
// simulation, wasm) placement means nothing and they expand to nothing --
// which is exactly what IDF's own esp_attr.h resolves them to on the linux
// target, so behavior is unchanged there.
//
// Shared code uses these FMRB_* names; esp_attr.h itself is only included
// from esp32 platform implementations and device-only drivers.

#include "sdkconfig.h"

#if !defined(CONFIG_IDF_TARGET_LINUX) && !defined(FMRB_PLATFORM_WASM)

#include "esp_attr.h"

#define FMRB_EXT_RAM_BSS_ATTR EXT_RAM_BSS_ATTR
#define FMRB_IRAM_ATTR        IRAM_ATTR
#define FMRB_DRAM_ATTR        DRAM_ATTR

#else

#define FMRB_EXT_RAM_BSS_ATTR
#define FMRB_IRAM_ATTR
#define FMRB_DRAM_ATTR

#endif
