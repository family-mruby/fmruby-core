/* Minimal esp_log.h for the wasm target: the ESP_LOGx macros over printf with
 * the same "L (millis) tag: message" shape the serial log has, and a global
 * runtime level so fmrb_set_log_level_* keep working. Per-tag levels are not
 * kept -- esp_log_level_set("*", ...) sets the one global level, any other tag
 * is ignored. Implementation in wasm/stub/esp_stub.c. */
#pragma once

#include "esp_attr.h"
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
