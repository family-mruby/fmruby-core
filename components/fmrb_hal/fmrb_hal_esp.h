#pragma once

// ESP32-specific API abstraction layer
// This header provides platform-independent wrappers for ESP32 functionality

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FMRB_PLATFORM_LINUX
// Linux simulation - memory attributes not needed
#define FMRB_IRAM_ATTR
#define FMRB_DRAM_ATTR
#else
// ESP32 hardware - use memory attributes
#include "esp_system.h"
#include "esp_attr.h"
#define FMRB_IRAM_ATTR IRAM_ATTR
#define FMRB_DRAM_ATTR DRAM_ATTR
#endif

#ifdef __cplusplus
}
#endif
