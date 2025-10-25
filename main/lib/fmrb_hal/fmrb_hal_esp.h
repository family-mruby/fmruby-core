#pragma once

// ESP32-specific API abstraction layer
// This header provides platform-independent wrappers for ESP32 functionality

#ifdef __cplusplus
extern "C" {
#endif

// ESP32 includes
#include "esp_system.h"
#include "esp_log.h"
#include "esp_attr.h"

// Re-export ESP log macros with FMRB_ prefix
#define FMRB_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define FMRB_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define FMRB_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define FMRB_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define FMRB_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

// Memory attributes
#define FMRB_IRAM_ATTR IRAM_ATTR
#define FMRB_DRAM_ATTR DRAM_ATTR

#ifdef __cplusplus
}
#endif
