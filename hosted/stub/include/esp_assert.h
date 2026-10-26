/* Stub of ESP-IDF's esp_assert.h: only the static-assert spellings the
 * vendored freertos_tasks_c_additions.h uses. */
#pragma once

#define ESP_STATIC_ASSERT( cond, ... )    _Static_assert( cond, "" #__VA_ARGS__ )
