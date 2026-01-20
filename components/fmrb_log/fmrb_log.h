#pragma once

// ESP-IDF includes (available on both ESP32 and Linux simulation)
#include "esp_log.h"

// Re-export ESP log macros with FMRB_ prefix
#define FMRB_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define FMRB_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define FMRB_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define FMRB_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
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


