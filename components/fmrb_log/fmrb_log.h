#pragma once

// Logging front-end for all targets. The FMRB_LOG* macros are the project
// convention; shared code includes THIS header (or fmrb_log_port.h for the
// rare ESP_LOG-only user), never esp_log.h directly (doc/idf_seam/). The
// platform backend lives in fmrb_log_port.h (fmrb_common).

#include "fmrb_log_port.h"
#include "fmrb_log_buffer.h"

// Re-export ESP log macros with FMRB_ prefix + log buffer hook
#define FMRB_LOGE(tag, format, ...) do { \
    ESP_LOGE(tag, format, ##__VA_ARGS__); \
    fmrb_log_buffer_printf(tag, 'E', format, ##__VA_ARGS__); \
} while(0)

#define FMRB_LOGW(tag, format, ...) do { \
    ESP_LOGW(tag, format, ##__VA_ARGS__); \
    fmrb_log_buffer_printf(tag, 'W', format, ##__VA_ARGS__); \
} while(0)

#define FMRB_LOGI(tag, format, ...) do { \
    ESP_LOGI(tag, format, ##__VA_ARGS__); \
    fmrb_log_buffer_printf(tag, 'I', format, ##__VA_ARGS__); \
} while(0)

#define FMRB_LOGD(tag, format, ...) do { \
    ESP_LOGD(tag, format, ##__VA_ARGS__); \
    if (fmrb_log_buffer_level_enabled('D')) { \
        fmrb_log_buffer_printf(tag, 'D', format, ##__VA_ARGS__); \
    } \
} while(0)

#define FMRB_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

// Log level constants (matching ESP-IDF)
#define FMRB_LOG_NONE    ESP_LOG_NONE
#define FMRB_LOG_ERROR   ESP_LOG_ERROR
#define FMRB_LOG_WARN    ESP_LOG_WARN
#define FMRB_LOG_INFO    ESP_LOG_INFO
#define FMRB_LOG_DEBUG   ESP_LOG_DEBUG
#define FMRB_LOG_VERBOSE ESP_LOG_VERBOSE

// Log level management functions
#define fmrb_log_level_set(tag, level) esp_log_level_set(tag, level)
#define fmrb_disable_log()   esp_log_level_set("*", ESP_LOG_NONE)
#define fmrb_set_log_level_info()   esp_log_level_set("*", ESP_LOG_INFO)
#define fmrb_set_log_level_debug()   esp_log_level_set("*", ESP_LOG_DEBUG)
