#pragma once

// The logging backend seam (doc/idf_seam/). This is the ONE header that knows
// where log lines go; it lives in fmrb_common, the bottom layer, so both
// fmrb_log (the FMRB_LOG* convention macros) and fmrb_common's own sources
// can use it without a dependency cycle.
//
//   ESP-IDF targets (esp32* and the linux simulation): esp_log.h, as always.
//   wasm: the same ESP_LOG-compatible surface over printf, implemented in
//         platform/wasm/fmrb_log_port_wasm.c.
//
// The ESP_LOG* names stay visible on every platform on purpose: several
// shared files and the posix HAL log through them without the log-buffer
// copy FMRB_LOG* adds, and changing that routing would be a behavior change.

#ifndef FMRB_PLATFORM_WASM

#include "esp_log.h"

#else /* FMRB_PLATFORM_WASM */

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

void esp_log_level_set(const char *tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char *tag);
uint32_t esp_log_timestamp(void);
void esp_log_write(esp_log_level_t level, const char *tag,
                   const char *format, ...) __attribute__((format(printf, 3, 4)));

#define ESP_LOG_LEVEL(level, tag, format, ...) \
    esp_log_write(level, tag, format, ##__VA_ARGS__)

#define ESP_LOGE(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

/* Buffer-dump variants some drivers use; reduced to a one-line notice. */
#define ESP_LOG_BUFFER_HEX(tag, buffer, buff_len) \
    ESP_LOGI(tag, "[hex buffer %u bytes]", (unsigned)(buff_len))
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buffer, buff_len, level) \
    ESP_LOG_LEVEL(level, tag, "[hex buffer %u bytes]", (unsigned)(buff_len))

#ifdef __cplusplus
}
#endif

#endif /* FMRB_PLATFORM_WASM */
